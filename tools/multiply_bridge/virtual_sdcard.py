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

from protocol import FT_DIRECTORY, FT_SCR, FT_SNA_48K, FT_SNA_128K, FT_TAP, \
    FT_Z80_48K, SCR_SIZE, SNA_48K_SIZE, SNA_128K_SIZE, SNA_128K_SIZE2, MAX_FILES

Entry = collections.namedtuple('Entry', ['name', 'is_dir', 'ft', 'path'])


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
    def __init__(self, root):
        self.root = os.path.abspath(root)
        self.cwd = self.root
        self.entries = []  # last list_entries() result -- what CD_IX/OFREAD_IX/GETINFO index into

    def cd_root(self):
        self.cwd = self.root

    def cd(self, name):
        """Returns True if `name` was a valid subdirectory (or ".." when not
        already at root) and self.cwd was updated."""
        target = os.path.dirname(self.cwd) if name == '..' else os.path.join(self.cwd, name)
        target = os.path.abspath(target)
        if target.startswith(self.root) and os.path.isdir(target):
            self.cwd = target
            return True
        return False

    def cd_ix(self, ix):
        """Returns True if `ix` (into the last list_entries() result) named
        a directory and self.cwd was updated."""
        if ix >= len(self.entries) or not self.entries[ix].is_dir:
            return False
        self.cwd = os.path.abspath(self.entries[ix].path)
        return True

    def current_name(self):
        return os.path.basename(self.cwd) or '/'

    def resolve(self, path_or_name):
        """Absolute path for a name/relative-path under cwd, or None if it
        doesn't exist, isn't a file, or would escape the SD card root."""
        target = os.path.abspath(os.path.join(self.cwd, path_or_name))
        if not target.startswith(self.root) or not os.path.isfile(target):
            return None
        return target

    def list_entries(self):
        """Rebuilds and returns self.entries for self.cwd: ".." first
        (unless at root), then directories, then files -- matching
        ListChapter()'s own directories-then-files ordering. The index is
        just the position in this list -- OUR OWN invention, not the real
        SD card's FAT32 directory index (SdFat's dirIndex()), fine since
        nothing outside this process ever interprets it (it only round-
        trips back to us via OFREAD_IX/CD_IX/GETINFO)."""
        entries = []
        if self.cwd != self.root:
            entries.append(Entry("..", True, FT_DIRECTORY, os.path.dirname(self.cwd)))
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
