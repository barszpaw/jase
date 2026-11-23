# -----------------------------------------------------------------------------
# ZXDB online browsing provider -- lets virtual_sdcard.py's VirtualSDCard show
# a special "ZXDB" directory whose content comes from the real ZXDB REST API
# (https://api.zxinfo.dk, the database behind spectrumcomputing.co.uk) instead
# of the local filesystem, downloading+caching files on demand. Deliberately
# pure network/parsing logic, no socket I/O of its own -- same split as every
# other module in this package (see virtual_sdcard.py's own docstring) -- so
# this can be unit-tested against recorded JSON fixtures without a live
# network call.
#
# API version note (2026-11-15, corrected same day it was first written):
# the FIRST version of this module targeted /v3, reverse-engineered from
# zxdbfs (github.com/hermitretro/zxdbfs, GPLv3 -- a FUSE filesystem for
# ZXDB, read for FACTS about the API only, same clean-room policy as every
# other GPL reference this project has used, see src/Dandanator.h's class
# comment -- no code copied, an independent implementation in a different
# language against the same public API). /v3 turned out to be a DEAD,
# decommissioned API version (every /v3/* request returns a real 502, even
# /v3/docs -- confirmed it's not a transient outage, the backend behind
# that whole path prefix is simply gone). The REAL, current, live API is
# /v5 -- confirmed straight from the live frontend's own compiled JS
# (https://zxinfo.dk/js/app.*.js, grepped for `$api_base_url` and the
# literal endpoint template strings built from it), NOT from any
# third-party docs or an assumption that bumping the version number in the
# URL would just work. Two real path renames alongside the version bump:
# "games" -> "entries" (`/v3/games/byletter/X` -> `/v5/entries/byletter/X`,
# `/v3/games/<id>` -> `/v5/entries/<id>`), and the single-entry detail
# fetch no longer takes (or needs) `?mode=compact` -- the plain `/entries/
# <id>` already returns the full record, `releases[].files[].path/size/
# type/format` included, byte-for-byte the same shape the old /v3 response
# had for that part. `byletter` also dropped the `contenttype=SOFTWARE`
# query param the old API needed -- confirmed empirically (not assumed)
# that /v5/entries/byletter/<letter> returns ONLY contentType=SOFTWARE
# results even without it, so it's simply omitted now rather than kept out
# of caution.
#
# zxdbfs itself credits a THIRD, older approach -- ZXDS's own online-access
# feature (http://zxds.raxoft.cz/, closed-source) -- which this project
# also researched first but didn't end up using: ZXDS's own protocol is a
# hand-rolled gzipped-text-listing format specific to a handful of mirrors
# ZXDS's author pre-arranged, a worse fit for a general-purpose online
# directory than ZXDB's own real REST API.
# -----------------------------------------------------------------------------

import os
import re
import zipfile

import requests

from protocol import FT_DIRECTORY, FT_SCR, FT_SNA_48K, FT_TAP, FT_Z80_48K

API_ROOT = "https://api.zxinfo.dk/v5"
LETTERS = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"

# Real archive mirrors a downloaded file's `path` (from ZXDB's own metadata,
# always WoS-style, e.g. "/pub/sinclair/games/x/Xevious.tap.zip") resolves
# against -- confirmed from zxdbfs's getRootDownloadURL()/fixupWoSPath():
# "/zxdb/sinclair/..." paths (ZXDB's own newer, non-WoS-archive assets) are
# served directly from spectrumcomputing.co.uk, while "/pub/sinclair/..."
# paths (the original World of Spectrum archive layout) are served from a
# frozen archive.org mirror instead of a live WoS server -- WoS itself has
# gone up and down over the years, archive.org's copy is far more reliably
# available. The mirror's own internal layout drops the "/pub/sinclair"
# prefix, hence the fixup.
_ZXDB_DIRECT_HOST = "https://spectrumcomputing.co.uk"
_WOS_MIRROR_ROOT = (
    "https://archive.org/download/World_of_Spectrum_June_2017_Mirror/"
    "World%20of%20Spectrum%20June%202017%20Mirror.zip/"
    "World%20of%20Spectrum%20June%202017%20Mirror/sinclair"
)

# Extension (after stripping a trailing .zip, if any -- the real archive
# always wraps individual files this way) -> FT_* byte this bridge already
# knows how to classify/serve. Same deliberately simplified, extension-only
# approach virtual_sdcard.py's own classify() uses for local files -- a
# downloaded .Z80 always gets FT_Z80_48K for DISPLAY purposes regardless of
# its real 16K/48K/128K-ness (the actual load path inspects the real
# header, same as a local file would); .SNA always gets FT_SNA_48K since
# ZXDB's metadata gives only the (zipped) file size, which isn't reliable
# enough to distinguish 48K/128K SNA before downloading -- a documented
# simplification, not a hang (SNA is rare in these listings; TAP/Z80
# dominate). .TZX is deliberately NOT included -- there is no FT_TZX at
# all in protocol.py, matching classify()'s own local-file scope: this
# bridge's CMD_ZX2SD_TAP handler (tap.py) only implements the real
# firmware's plain-TAP block protocol, not TZX's pulse-level format, so a
# ZXDB game whose ONLY file is a .tzx (no .tap alongside it) simply won't
# show up here -- same gap as a local .tzx file would hit, not new.
_EXT_TO_FT = {
    ".TAP": FT_TAP,
    ".Z80": FT_Z80_48K,
    ".SNA": FT_SNA_48K,
    ".SCR": FT_SCR,
}


def _api_get(path, params):
    resp = requests.get(API_ROOT + path, params=params, timeout=15)
    resp.raise_for_status()
    return resp.json()


def list_root():
    """The ZXDB root: one directory per letter/digit, matching zxdbfs's own
    /by-letter scope (ZXDB's REST API has no "by year"/"by genre" listing
    endpoint of its own to mirror the old ZXDS text-config's categories --
    only byletter and search, and this bridge has no way to let the user
    TYPE a search term through Multiply's cursor-driven browser UI, so
    search is left for a future session rather than guessed at)."""
    return [(ch, True, FT_DIRECTORY, ch) for ch in LETTERS]


def list_letter(letter):
    """Entries whose title starts with `letter` -- GET /entries/byletter/
    <letter>, mode=tiny (just enough fields to build a listing, not the
    full detail record). No contenttype filter needed -- confirmed live
    this endpoint already returns SOFTWARE-only results on its own. Each
    game becomes its own virtual directory (it may have several files
    across different releases -- see list_game())."""
    data = _api_get(f"/entries/byletter/{letter}", {
        "mode": "tiny", "size": 5000, "offset": 0,
    })
    out = []
    for hit in data.get("hits", {}).get("hits", []):
        gameid = hit["_id"]
        title = (hit.get("_source") or {}).get("title") or gameid
        out.append((f"{title} [{gameid}]", True, FT_DIRECTORY, gameid))
    return out


def list_game(gameid):
    """The loadable files for one game -- GET /entries/<id> (the plain,
    no-query-params detail fetch already returns the full record,
    releases[].files[] included -- no mode=compact needed/supported here,
    unlike the old /v3/games/<id>?mode=compact). Flattens every release's
    files[] into one list (the same filename can legitimately appear in
    more than one release, e.g. an original and a re-release sharing an
    identical dump -- de-duplicated by name), keeping only the extensions
    this bridge actually knows how to serve (TAP/Z80/SNA/SCR) -- pictures,
    pokes, and documents are real ZXDB download entries too, but this
    bridge has no FT_* for them and no use for them once downloaded, so
    they're silently skipped, same as classify() skips unrecognized local
    extensions."""
    data = _api_get(f"/entries/{gameid}", {})
    src = data.get("_source") or {}
    out = []
    seen = set()
    for release in src.get("releases") or []:
        for f in release.get("files") or []:
            path = f.get("path")
            if not path:
                continue
            name = os.path.basename(path)
            base, ext = os.path.splitext(name)
            if ext.lower() == ".zip":
                name = base
                ext = os.path.splitext(base)[1]
            ft = _EXT_TO_FT.get(ext.upper())
            if ft is None or name in seen:
                continue
            seen.add(name)
            out.append((name, False, ft, path))
    return out


def _download_url(remote_path):
    if remote_path.startswith("/zxdb/sinclair"):
        return _ZXDB_DIRECT_HOST + remote_path
    stripped = remote_path
    if stripped.startswith("/pub/sinclair"):
        stripped = stripped[len("/pub/sinclair"):]
    return _WOS_MIRROR_ROOT + stripped


def fetch_file(remote_path, cache_dir):
    """Downloads (if not already cached) the file ZXDB lists at
    `remote_path`, unzips it if it came zipped (the real archive always
    wraps individual files this way -- detected by content, not just the
    .zip extension, since list_game() already strips that from the display
    name), and returns the LOCAL filesystem path to the raw TAP/Z80/SNA/SCR
    bytes -- from here on indistinguishable from any locally-served file to
    the rest of this package (session.py's open_file(), GETINFO, etc. never
    need to know a file came from ZXDB at all).

    Caching is unconditional and unbounded (no size cap or pruning, unlike
    ZXDS's own 32MB-with-startup-pruning scheme) -- a deliberate v1
    simplification: delete files from `cache_dir` by hand if it grows too
    large. The cache key is the sanitized basename, so two different games
    that happen to share an identical filename would collide -- acceptable
    for a v1 personal-use cache, not attempted to be made collision-proof.
    """
    os.makedirs(cache_dir, exist_ok=True)
    safe_name = re.sub(r'[^A-Za-z0-9._-]', '_', os.path.basename(remote_path))
    final_name = safe_name[:-4] if safe_name.lower().endswith('.zip') else safe_name
    final_path = os.path.join(cache_dir, final_name)
    if os.path.isfile(final_path):
        return final_path

    resp = requests.get(_download_url(remote_path), timeout=60)
    resp.raise_for_status()
    data = resp.content

    if data[:4] == b'PK\x03\x04':
        tmp_zip = final_path + '.zip.tmp'
        with open(tmp_zip, 'wb') as f:
            f.write(data)
        try:
            with zipfile.ZipFile(tmp_zip) as zf:
                names = zf.namelist()
                member = next(
                    (n for n in names if os.path.basename(n).lower() == final_name.lower()),
                    names[0],
                )
                data = zf.read(member)
        finally:
            os.remove(tmp_zip)

    with open(final_path, 'wb') as f:
        f.write(data)
    return final_path
