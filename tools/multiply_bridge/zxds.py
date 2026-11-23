# -----------------------------------------------------------------------------
# Generic ZXDS protocol client -- drives ALL servers listed in servers.cfg
# (http://zxds.raxoft.cz/servers.cfg, bundled locally as servers.cfg) via
# ZXDS's own "online access" directory-tree protocol, researched live
# 2026-11-16 (ZXDS itself is closed-source homebrew with no public
# repository -- this is a clean-room reimplementation from observed HTTP
# traffic, not ported code).
#
# Protocol, confirmed empirically against the live service:
#   - servers.cfg: repeated "server = <name>" / "config = <url>" pairs.
#   - each server's own *.cfg has up to 4 keys: an optional "base" (prefix
#     for any relative root/dir/file value below), "root" (URL/path of the
#     top-level directory listing), "dir" (URL/path template with a single
#     "%n" placeholder for any OTHER directory level), "file" (URL/path
#     template with a single "%f" placeholder for a downloadable file).
#   - root/dir listings are gzip-compressed plain text, one entry per line:
#       "<display>//:<token>"       -- a subdirectory; <token> substitutes
#                                      "%n" in the "dir" template to fetch it
#       "<display>//<size>=<path>"  -- a file; <size> is the real byte
#                                      count, <path> substitutes "%f" in
#                                      the "file" template to download it
#   - "%n"/"%f" substitution is always a single flat placeholder -- there is
#     no path-nesting across levels, each token/path is already globally
#     unique within its server (confirmed: fetching a deep .dat directly by
#     its own token, bypassing every parent level, works identically).
#   - a file-type <display> is often "<group>/<filename>" (e.g. one ZXDB
#     "letter" page lists every file for every game starting with that
#     letter, flattened into one page) -- grouped here into a synthetic
#     "g:<group>" pseudo-directory per distinct prefix, same shape as a
#     real game subdirectory, so browsing feels identical to zxdb.py's.
#   - resolving a "file" template is NOT server-specific in practice: for
#     every server tried (ZXDB, demotopia, zxaaa), the resulting URL -- even
#     when it points back at zxds.raxoft.cz itself (e.g. ZXDB's "file = %f"
#     with base=".../servers/zxdb/") -- turns out to be a REDIRECTOR to the
#     file's real, CURRENT host (confirmed live: a "pub/sinclair/..." ZXDB
#     path 302s through worldofspectrum.org, a "zxdb/sinclair/..." path
#     302s through spectrumcomputing.co.uk -- both verified by downloading
#     real content). This is more robust than hardcoding any one archive's
#     address ourselves (zxdb.py's own _download_url() froze on a 2017
#     archive.org WoS mirror because WoS itself used to be down -- it is
#     not, right now). No per-server download special-casing is needed.
# -----------------------------------------------------------------------------

import gzip
import os
import re
import urllib.parse
import zipfile

import requests

from protocol import FT_DIRECTORY, FT_SCR, FT_SNA_48K, FT_TAP, FT_Z80_48K

_HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SERVERS_CFG = os.path.join(_HERE, "servers.cfg")

# Same deliberately-simplified extension-only typing virtual_sdcard.py's own
# classify() and zxdb.py use -- see zxdb.py's own comment for why .TZX and
# anything else (pictures/pokes/text) are left out.
_EXT_TO_FT = {
    ".TAP": FT_TAP,
    ".Z80": FT_Z80_48K,
    ".SNA": FT_SNA_48K,
    ".SCR": FT_SCR,
}

_ABS_URL_RE = re.compile(r'^[a-zA-Z][a-zA-Z0-9+.\-]*://')


def _parse_kv(text):
    """Parses ZXDS's "# comment" / "key = value" config syntax into an
    ORDERED list of (key, value) pairs -- servers.cfg repeats "server"/
    "config" for each entry, so a plain dict would lose all but the last."""
    pairs = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith('#') or '=' not in line:
            continue
        key, _, value = line.partition('=')
        pairs.append((key.strip(), value.strip()))
    return pairs


def load_servers(servers_cfg_path=DEFAULT_SERVERS_CFG):
    """[(name, config_url), ...] in servers.cfg's own file order."""
    with open(servers_cfg_path, 'r', encoding='utf-8') as f:
        text = f.read()
    servers = []
    pending_name = None
    for key, value in _parse_kv(text):
        if key == 'server':
            pending_name = value
        elif key == 'config' and pending_name is not None:
            servers.append((pending_name, value))
            pending_name = None
    return servers


class ServerConfig:
    def __init__(self, base, root, dir_tpl, file_tpl):
        self.base = base
        self.root = root
        self.dir_tpl = dir_tpl
        self.file_tpl = file_tpl

    def abs_url(self, url_or_relpath):
        if _ABS_URL_RE.match(url_or_relpath):
            return url_or_relpath
        return (self.base or '') + url_or_relpath

    def root_url(self):
        return self.abs_url(self.root)

    def dir_url(self, token):
        return self.abs_url(self.dir_tpl.replace('%n', token))

    def file_url(self, raw_path):
        quoted = urllib.parse.quote(raw_path, safe='/')
        return self.abs_url(self.file_tpl.replace('%f', quoted))


def fetch_server_config(config_url):
    resp = requests.get(config_url, timeout=15)
    resp.raise_for_status()
    kv = dict(_parse_kv(resp.text))
    return ServerConfig(kv.get('base'), kv['root'], kv['dir'], kv['file'])


def _parse_dat_line(line):
    """'<display>//:<token>' -> (display, True, None, token)
    '<display>//<size>=<path>' -> (display, False, size_or_None, path) --
    size is sometimes the empty string (confirmed on "ZX AAA Demos": every
    entry is "...//=<path>", no byte count at all), not just a positive
    int, so an empty/unparseable size means "unknown", not "drop the line".
    anything else (shouldn't happen) -> None."""
    sep = line.find('//')
    if sep == -1:
        return None
    display = line[:sep]
    rest = line[sep + 2:]
    if rest.startswith(':'):
        return (display, True, None, rest[1:])
    size_str, has_eq, path = rest.partition('=')
    if not has_eq:
        return None
    try:
        size = int(size_str)
    except ValueError:
        size = None
    return (display, False, size, path)


def _fetch_dat_entries(url):
    resp = requests.get(url, timeout=20)
    resp.raise_for_status()
    data = resp.content
    if data[:2] == b'\x1f\x8b':
        data = gzip.decompress(data)
    text = data.decode('utf-8', errors='replace')
    entries = []
    for line in text.split('\n'):
        if not line:
            continue
        parsed = _parse_dat_line(line)
        if parsed is not None:
            entries.append(parsed)
    return entries


def list_dir(cfg, token=None, prefix=()):
    """Listing for the server's root (token=None) or a directory token, at
    a given depth of synthetic file-grouping within that one page ("ZX AAA
    Demos" nests 3 deep: "Author/Year/Title/filename.zip", flattened into
    a single .dat page -- not just ZXDB's one "Game/filename" level -- so
    this walks the "/"-split display text ONE segment at a time instead of
    assuming a fixed depth).

    Returns [(name, is_dir, ft_or_None, key), ...] where key is ('tok',
    token) for a real ZXDS subdirectory (always at prefix==(), re-fetches
    a different page), or ('grp', prefix_tuple) for a synthetic grouping
    level (pass straight back into this same function), or ('file',
    raw_path) for an actual file at this depth."""
    url = cfg.root_url() if token is None else cfg.dir_url(token)
    raw = _fetch_dat_entries(url)
    prefix = tuple(prefix)

    out = []
    groups_seen = set()
    for display, is_dir, size, key in raw:
        if is_dir:
            if prefix:
                continue  # real subdirectory tokens only ever sit at a page's top
            out.append((display, True, FT_DIRECTORY, ('tok', key)))
            continue
        parts = display.split('/')
        group_parts, filename = parts[:-1], parts[-1]
        if tuple(group_parts[:len(prefix)]) != prefix:
            continue
        remaining = group_parts[len(prefix):]
        if remaining:
            next_seg = remaining[0]
            if next_seg in groups_seen:
                continue
            groups_seen.add(next_seg)
            out.append((next_seg, True, FT_DIRECTORY, ('grp', prefix + (next_seg,))))
        else:
            ft = _file_type(filename)
            if ft is not None:
                # Real archive always wraps the individual file in a .zip
                # (fetch_file() detects+unzips it on download) -- showing
                # that ".zip" suffix here breaks the real Multiply ROM's
                # own recognition of the file (it keys off the trailing
                # extension text, not just the FT_* byte), same reason
                # zxdb.py already strips it from its own display names.
                display_name = filename[:-4] if filename.lower().endswith('.zip') else filename
                out.append((display_name, False, ft, ('file', key)))
    return out


def _file_type(name):
    base = name[:-4] if name.lower().endswith('.zip') else name
    ext = os.path.splitext(base)[1].upper()
    return _EXT_TO_FT.get(ext)


def fetch_file(cfg, raw_path, cache_dir):
    """Downloads (if not already cached) the file at `raw_path` for this
    server, following the "file" template's redirector to wherever the
    content actually lives today, unzipping it if it came wrapped in a
    .zip (the archives this protocol points at always wrap individual
    files this way), and returns the local filesystem path to the raw
    bytes -- same cache shape as zxdb.py's own fetch_file()."""
    os.makedirs(cache_dir, exist_ok=True)
    base_name = os.path.basename(raw_path)
    safe_name = re.sub(r'[^A-Za-z0-9._-]', '_', base_name)
    final_name = safe_name[:-4] if safe_name.lower().endswith('.zip') else safe_name
    final_path = os.path.join(cache_dir, final_name)
    if os.path.isfile(final_path):
        return final_path

    resp = requests.get(cfg.file_url(raw_path), timeout=60)
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


# -----------------------------------------------------------------------------
# CLI -- manual "get"/"ls" against any server in servers.cfg, for testing
# this module (and poking at a server's tree) without a running bridge.
# -----------------------------------------------------------------------------

def _cli_servers(args):
    for name, config_url in load_servers(args.servers_cfg):
        print(f"{name}\t{config_url}")


def _cli_ls(args):
    servers = dict(load_servers(args.servers_cfg))
    if args.server not in servers:
        raise SystemExit(f"unknown server {args.server!r} (see 'zxds.py servers')")
    cfg = fetch_server_config(servers[args.server])
    prefix = tuple(args.group.split('/')) if args.group else ()
    entries = list_dir(cfg, args.token, prefix)
    for name, is_dir, ft, key in entries:
        kind = {'tok': 'DIR ', 'grp': 'GRP ', 'file': 'FILE'}[key[0]]
        shown_key = '/'.join(key[1]) if key[0] == 'grp' else key[1]
        print(f"{kind}  {name}\t{shown_key}")


def _cli_get(args):
    servers = dict(load_servers(args.servers_cfg))
    if args.server not in servers:
        raise SystemExit(f"unknown server {args.server!r} (see 'zxds.py servers')")
    cfg = fetch_server_config(servers[args.server])
    cache_dir = args.outdir or os.getcwd()
    path = fetch_file(cfg, args.raw_path, cache_dir)
    print(path)


def _main():
    import argparse
    parser = argparse.ArgumentParser(description="Manual ZXDS protocol client (see servers.cfg).")
    parser.add_argument('--servers-cfg', default=DEFAULT_SERVERS_CFG)
    sub = parser.add_subparsers(dest='cmd', required=True)

    sub.add_parser('servers', help="list servers from servers.cfg").set_defaults(func=_cli_servers)

    p_ls = sub.add_parser('ls', help="list a server's root, a dir token, or a file group")
    p_ls.add_argument('server')
    p_ls.add_argument('token', nargs='?', default=None, help="dir token (omit for server root)")
    p_ls.add_argument('--group', default=None,
                       help="descend into this grouping prefix within the page "
                            "(e.g. 'Xevious [28:7.1]' or, for deeper nesting, "
                            "'A and B Soft/1997/Music Player [A and B Soft]')")
    p_ls.set_defaults(func=_cli_ls)

    p_get = sub.add_parser('get', help="download a file by its raw ZXDS path")
    p_get.add_argument('server')
    p_get.add_argument('raw_path')
    p_get.add_argument('outdir', nargs='?', default=None)
    p_get.set_defaults(func=_cli_get)

    args = parser.parse_args()
    args.func(args)


if __name__ == '__main__':
    _main()
