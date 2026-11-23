# -----------------------------------------------------------------------------
# VirtualSDCard -- the directory-navigation half of the virtual SD card
# Multiply's firmware expects: current directory, listing, classification.
# Deliberately pure (no socket I/O, no wire encoding) -- session.py owns the
# conn.sendall() calls and uses protocol.encode_entry() on whatever this
# returns. Keeping that split is what makes VirtualSDCard independently
# testable (see the bridge's own development notes: it was unit-tested
# directly against synthetic fixtures, not through the slow TCP+debug-
# protocol round trip, specifically because the parsing/logic layers don't
# touch sockets at all).
# -----------------------------------------------------------------------------

import collections
import os

import zxds
from protocol import FT_DIRECTORY, FT_SCR, FT_SNA_48K, FT_SNA_128K, FT_TAP, \
    FT_Z80_48K, SCR_SIZE, SNA_48K_SIZE, SNA_128K_SIZE, SNA_128K_SIZE2, MAX_FILES

Entry = collections.namedtuple('Entry', ['name', 'is_dir', 'ft', 'path'])

# Sentinel cwd/path scheme for the "ONLINE" virtual directory (see zxds.py)
# -- chosen to never collide with a real absolute filesystem path. Shape:
#   "online:/"                                 -- server list (servers.cfg)
#   "online:/<Server>"                         -- a server's root listing
#   "online:/<Server>/<token>"                 -- a real ZXDS dir token
#   "online:/<Server>/<token>/<prefix...>"     -- N levels of synthetic
#                                                  file-grouping within
#                                                  that token's own page
#                                                  (prefix segments joined
#                                                  by \x1f, see zxds.py's
#                                                  own list_dir() docstring
#                                                  for why this can be
#                                                  deeper than one level)
# ONLINE_FILE_PREFIX marks a FILE entry's `.path` as "not downloaded yet,
# fetch-on-open, and which server it came from" -- see local_path_for().
ONLINE_ROOT = "online:/"
ONLINE_FILE_PREFIX = "online:file:"
_PREFIX_SEP = "\x1f"

# The real Multiply ROM re-sorts the listing it receives alphabetically
# (confirmed live, 2026-11-17 -- it does NOT preserve wire order, so
# putting this entry right after ".." in list_entries() below only
# controls OUR OWN data structure, not what actually renders first). A
# leading "!" (0x21) sorts before every digit (0x30+) and letter
# (0x41+/0x61+) in a plain byte comparison -- a leading digit like
# "0NLINE" would lose that comparison to a real local folder starting
# with a lower digit (e.g. this project's own "007 Disassembler").
ONLINE_DISPLAY_NAME = "!OnLine"


def _online_encode(server, token, prefix):
    parts = [server]
    if token is not None:
        parts.append(token)
        if prefix:
            parts.append(_PREFIX_SEP.join(prefix))
    return ONLINE_ROOT + '/'.join(parts)


def _online_decode(cwd):
    """"online:/ZX AAA Demos/author_year_a/A and B Soft" ->
    ('ZX AAA Demos', 'author_year_a', ('A and B Soft',)); "online:/" ->
    (None, None, ())."""
    rest = cwd[len(ONLINE_ROOT):]
    if not rest:
        return (None, None, ())
    bits = rest.split('/', 2)
    server = bits[0]
    token = bits[1] if len(bits) > 1 else None
    prefix = tuple(bits[2].split(_PREFIX_SEP)) if len(bits) > 2 and bits[2] else ()
    return (server, token, prefix)


def _online_up_path(server, token, prefix):
    """Where ".." should go from this (server, token, prefix) -- None
    means "exit ONLINE entirely" (caller substitutes the real root)."""
    if server is None:
        return None
    if token is None:
        return ONLINE_ROOT
    if not prefix:
        return _online_encode(server, None, ())
    return _online_encode(server, token, prefix[:-1])


def classify(path, is_dir):
    """FT_* byte for this entry, or None to hide it -- a deliberately
    simplified stand-in for SetFileType() in libSDFiles.ino (which
    inspects file headers/CRCs/sizes in detail; this only looks at
    extension + size)."""
    name = os.path.basename(path)
    if name.startswith('.'):
        return None
    if is_dir:
        return FT_DIRECTORY
    ext = os.path.splitext(name)[1].upper()
    try:
        size = os.path.getsize(path)
    except OSError:
        return None
    if ext == '.SCR' and size == SCR_SIZE:
        return FT_SCR
    if ext == '.SNA':
        if size == SNA_48K_SIZE:
            return FT_SNA_48K
        if size in (SNA_128K_SIZE, SNA_128K_SIZE2):
            return FT_SNA_128K
        return None
    if ext == '.Z80':
        # Real SetFileType() inspects the header to pick 16K/48K/128K and
        # compressed-vs-not; this bridge only needs it to DISPLAY a type in
        # the listing, so it just always shows 48K -- the actual load
        # (snapshot.py) inspects the real header itself.
        return FT_Z80_48K
    if ext == '.TAP':
        return FT_TAP
    return None


class VirtualSDCard:
    def __init__(self, root, online_cache_dir=None):
        self.root = os.path.abspath(root)
        self.cwd = self.root
        # Always kept in sync with self.cwd -- cd()/cd_root()/cd_ix() all
        # call list_entries() themselves right after changing cwd (see
        # their own comments: the real ROM's "go to parent" sequence
        # replays CD_IX calls against the real SD card's OWN live FAT
        # index -- SdFat's File::open(Dir, index) -- with NO intervening
        # LS_RELATIVE, so CD_IX must resolve correctly without one here
        # too; a cache only refreshed by an explicit LS_RELATIVE request
        # would resolve a post-CD_ROOT replay CD_IX against a stale
        # listing from whatever directory was last explicitly listed).
        self.entries = []
        # None disables the ONLINE virtual directory entirely (it simply
        # never appears in the root listing) -- see zxds.py and
        # multiply_bridge.py's --online-cache flag.
        self.online_cache_dir = online_cache_dir

    def cd_root(self):
        self.cwd = self.root
        self.list_entries()

    def cd(self, name):
        """Returns True if `name` was a valid subdirectory (or ".." when not
        already at root) and self.cwd was updated."""
        if self.cwd.startswith(ONLINE_ROOT):
            if name != '..':
                return False  # ONLINE mode has no cd-by-name beyond ".." -- navigation is CD_IX-only
            server, token, prefix = _online_decode(self.cwd)
            up = _online_up_path(server, token, prefix)
            self.cwd = self.root if up is None else up
            self.list_entries()
            return True
        target = os.path.dirname(self.cwd) if name == '..' else os.path.join(self.cwd, name)
        target = os.path.abspath(target)
        if target.startswith(self.root) and os.path.isdir(target):
            self.cwd = target
            self.list_entries()
            return True
        return False

    def cd_ix(self, ix):
        """Returns True if `ix` (into the last list_entries() result) named
        a directory and self.cwd was updated."""
        if ix >= len(self.entries) or not self.entries[ix].is_dir:
            return False
        target = self.entries[ix].path
        # An ONLINE directory entry's .path is already a full "online:/..."
        # string (built by _online_list_entries()); an os.path.abspath() on
        # that would mangle it (it doesn't start with "/", so abspath would
        # treat it as relative to the CWD of this PROCESS). The ".." entry
        # at ONLINE's own root instead carries self.root (a real path, no
        # "online:" prefix) as its target -- that one DOES want the normal
        # local-path branch below, which is exactly what naturally happens
        # here since it fails the startswith() check.
        if target.startswith(ONLINE_ROOT):
            self.cwd = target
        else:
            self.cwd = os.path.abspath(target)
        self.list_entries()
        return True

    def current_name(self):
        # The real ROM's UpdatePath (PathRoutines.asm) checks GETDIR's reply
        # for a literal leading "/" to mean "this is the root -- skip
        # updating the breadcrumb"; `os.path.basename(self.root)` would
        # instead return the root folder's own name (e.g. "Games"), which
        # the ROM would misread as a real subdirectory name.
        if self.cwd == self.root:
            return '/'
        if self.cwd.startswith(ONLINE_ROOT):
            server, token, prefix = _online_decode(self.cwd)
            if prefix:
                return prefix[-1]
            if server is None:
                return ONLINE_DISPLAY_NAME
            # Best-effort: a ZXDS dir token (e.g. "games_x") isn't always a
            # pretty display name the way a prefix segment or server name
            # is -- showing the raw token here is a cosmetic shortcut, not
            # a functional gap (the breadcrumb is informational only).
            return token if token is not None else server
        return os.path.basename(self.cwd) or '/'

    def resolve(self, path_or_name):
        """Path (local, or an "online:file:..." reference -- see
        local_path_for()) for a name/relative-path under cwd, or None if it
        doesn't exist, isn't a file, or would escape the SD card root."""
        if self.cwd.startswith(ONLINE_ROOT):
            for entry in self.entries:
                if entry.name == path_or_name and not entry.is_dir:
                    return entry.path
            return None
        target = os.path.abspath(os.path.join(self.cwd, path_or_name))
        if not target.startswith(self.root) or not os.path.isfile(target):
            return None
        return target

    def local_path_for(self, path):
        """Resolves an Entry.path value (as returned by resolve()/cd_ix()'s
        indexed entries) to a real, locally-openable filesystem path --
        pass-through for ordinary local files, download-and-cache (via
        zxds.fetch_file()) for an "online:file:..." reference. The two
        session.py call sites that used to open(entry.path, ...) directly
        (Session.open_file(), GETINFO) now go through this instead, so
        neither needs to know or care whether a file came from ONLINE."""
        if not path:
            return None
        if path.startswith(ONLINE_FILE_PREFIX):
            if self.online_cache_dir is None:
                return None
            server, _, raw_path = path[len(ONLINE_FILE_PREFIX):].partition(_PREFIX_SEP)
            try:
                servers = dict(zxds.load_servers())
                cfg = zxds.fetch_server_config(servers[server])
                return zxds.fetch_file(cfg, raw_path, self.online_cache_dir)
            except Exception as e:
                print(f"[ONLINE] download failed for {server}:{raw_path}: {e}")
                return None
        return path

    def list_entries(self):
        """Rebuilds and returns self.entries for self.cwd: ".." first
        (unless at root), then directories, then files -- matching
        ListChapter()'s own directories-then-files ordering. The index is
        just the position in this list -- OUR OWN invention, not the real
        SD card's FAT32 directory index (SdFat's dirIndex()), fine since
        nothing outside this process ever interprets it (it only round-
        trips back to us via OFREAD_IX/CD_IX/GETINFO)."""
        if self.cwd.startswith(ONLINE_ROOT):
            return self._online_list_entries()
        entries = []
        # A real "..", even at ROOT (which has no real parent -- its
        # target is just self.root, a harmless no-op self-loop). Found
        # live, 2026-11-15: at least one real romset (dandanator_01.rom)
        # silently drops whatever sits at ROOT's entry INDEX 0 specifically
        # -- NOT content-specific (confirmed by moving a synthetic entry to
        # the end of the list instead: whatever landed on index 0 vanished
        # the same way). Every OTHER directory already puts a real ".." at
        # index 0 and renders fine; ROOT was the one place this bridge
        # deliberately omitted any index-0 placeholder at all (root
        # logically has no parent) -- giving ROOT the same "always a
        # throwaway entry at 0" shape every other directory already has
        # fixes the drop without needing to understand that ROM's exact
        # root cause.
        entries.append(Entry("..", True, FT_DIRECTORY, self.root))
        # ONLINE comes right after -- but the real ROM re-sorts the whole
        # listing alphabetically regardless of wire order (confirmed live,
        # see ONLINE_DISPLAY_NAME's own comment above), so staying first
        # among real content depends on the "!" prefix, not this position.
        if self.cwd == self.root and self.online_cache_dir is not None:
            entries.append(Entry(ONLINE_DISPLAY_NAME, True, FT_DIRECTORY, ONLINE_ROOT))
        try:
            names = sorted(os.listdir(self.cwd), key=str.lower)
        except OSError:
            names = []
        dirs, files = [], []
        for name in names:
            full = os.path.join(self.cwd, name)
            is_dir = os.path.isdir(full)
            ft = classify(full, is_dir)
            if ft is None:
                continue
            (dirs if is_dir else files).append(Entry(name, is_dir, ft, full))
        entries.extend(dirs)
        entries.extend(files)
        self.entries = entries[:MAX_FILES]
        return self.entries

    def _online_list_entries(self):
        """ONLINE-backed listing for self.cwd: the server list (from
        servers.cfg), a server's own ZXDS directory tree, or a level of
        synthetic file-grouping within one of that tree's pages -- see
        zxds.py. Network/parsing errors (including one server simply being
        down) degrade to an empty listing rather than propagating -- a
        half-broken online directory shouldn't be able to take down local
        browsing, the connection, or any OTHER server's listing."""
        server, token, prefix = _online_decode(self.cwd)

        if server is None:
            entries = [Entry("..", True, FT_DIRECTORY, self.root)]
            try:
                servers = zxds.load_servers()
            except Exception as e:
                print(f"[ONLINE] error loading servers.cfg: {e}")
                servers = []
            for name, _config_url in servers:
                entries.append(Entry(name, True, FT_DIRECTORY, _online_encode(name, None, ())))
            self.entries = entries[:MAX_FILES]
            return self.entries

        up = _online_up_path(server, token, prefix)
        entries = [Entry("..", True, FT_DIRECTORY, self.root if up is None else up)]
        try:
            servers = dict(zxds.load_servers())
            cfg = zxds.fetch_server_config(servers[server])
            items = zxds.list_dir(cfg, token, prefix)
        except Exception as e:
            print(f"[ONLINE] error listing {self.cwd}: {e}")
            items = []
        for name, is_dir, ft, key in items:
            kind = key[0]
            if kind == 'tok':
                path = _online_encode(server, key[1], ())
            elif kind == 'grp':
                path = _online_encode(server, token, key[1])
            else:  # 'file'
                path = ONLINE_FILE_PREFIX + server + _PREFIX_SEP + key[1]
            entries.append(Entry(name, is_dir, ft, path))
        self.entries = entries[:MAX_FILES]
        return self.entries
