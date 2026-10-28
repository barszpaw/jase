#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# Multiply bridge -- companion process for JASE's Multiply peripheral
# (src/Multiply.cpp), the Arduino+microSD addon for the ZX Dandanator! Mini
# (github.com/mad3001/Multiply). See docs/Dandanator/dandanator_multiply_notes.md
# for the full hardware/protocol research this is built from.
#
# Same split as tools/sdi_bridge/sdi_bridge.py: JASE's C++ side (Multiply.cpp)
# is a dumb byte-pipe -- it doesn't know what CMD_ZX2SD_* commands MEAN, only
# how to shuttle bytes to/from here over TCP. All the real logic (SD
# directory browsing, file-type classification, screen-preview streaming)
# lives in THIS process, written in Python rather than C++ for the same
# reason sdi_bridge.py exists: file-format-heavy logic doesn't belong in the
# hot Z80-emulation path.
#
# Wire protocol: UNFRAMED raw bytes, both directions, on one TCP connection
# -- a direct reflection of the real hardware's own wire protocol: the
# Dandanator's command-52 channel delivers one byte at a time with no
# message delimiters, and the real Arduino firmware tracks command/payload
# boundaries itself (via getBuffer_0's null-terminated reads and
# getBuffer_N's fixed-length reads -- see
# docs/Dandanator/reference_src/libSerialCmds.ino). This bridge does the
# same: recv_exact()/recv_cstring() below, and the per-command payload
# shapes in handle_client()'s dispatch, were reverse-engineered directly
# from the real firmware's own command loop (Multiply_Arduino.ino) and
# directory-listing code (libFileList.ino/libSDFiles.ino) -- not guessed.
# Those sources aren't vendored in this repo (CC BY-SA, github.com/
# mad3001/Multiply) but were read in full during development; see the
# per-function comments below for what each one is based on.
#
# v2 scope: identification, hardware-type notification, directory
# navigation (CD_ROOT/CD/CD_IX/GETDIR), directory listing (LS_RELATIVE,
# single chapter only -- see MAX_FILES), opening a file by name or index
# (OFREAD/OFREAD_IX), and screen-preview streaming (CMD_ZX2SD_SCR, plain
# .SCR files only). Deliberately NOT implemented yet: the real
# CMD_ZX2SD_Z80_*/SNA_*/TAP/ROMSET content-streaming commands (actually
# loading a game through Multiply), SCR-extracted-from-Z80/SNA/TAP,
# GETINFO's per-type extended data, multi-chapter pagination for huge
# directories, and the PC-tunnel romset-upload commands. Each of those is
# payload-framed correctly below even where unimplemented (consuming the
# right number of bytes is NOT optional -- see "_consume_unhandled" --
# getting this wrong desyncs every command after it for the rest of the
# connection, same risk the real firmware's own fixed dispatch table
# avoids by construction).
# -----------------------------------------------------------------------------

import argparse
import os
import socket
import struct

VERSION = "0.2"

# Commands the real Arduino firmware's main dispatch loop recognizes (see
# Multiply_Arduino.ino's loop() and incCmdsFileTypes.h). Only a subset is
# handled below -- see module docstring.
CMD_ZX2INO_REQ_ID      = 0x01
CMD_ZX2SD_SETZXTYPE    = 0x03
CMD_ZX2SD_OFREAD       = 0x08
CMD_ZX2SD_OFREAD_IX    = 0x09
CMD_ZX2SD_CD_ROOT      = 0x0A
CMD_ZX2SD_CD           = 0x0B
CMD_ZX2SD_CD_IX        = 0x0C
CMD_ZX2SD_GETDIR       = 0x0D
CMD_ZX2SD_LS_RELATIVE  = 0x0E
CMD_ZX2SD_LS_ABSOLUTE  = 0x0F
CMD_ZX2SD_SCR          = 0x10
CMD_ZX2SD_Z80_16K      = 0x11
CMD_ZX2SD_Z80_48K      = 0x12
CMD_ZX2SD_Z80_128K     = 0x13
CMD_ZX2SD_SCR_FROM_Z80 = 0x14
CMD_ZX2SD_SNA_48K      = 0x16
CMD_ZX2SD_SNA_128K     = 0x17
CMD_ZX2SD_SCR_FROM_SNA = 0x18
CMD_ZX2SD_GETINFO      = 0x1E
CMD_ZX2SD_TAP          = 0x20
CMD_ZX2SD_SCRTAP       = 0x23
CMD_ZX2SD_ROMSETBLK4B  = 0x30

# File-type bytes for directory-entry records (incCmdsFileTypes.h).
FT_END_DIRECTORY = 0x00
FT_DIRECTORY     = 0x0C
FT_SCR           = 0x10
FT_Z80_16K       = 0x11
FT_Z80_48K       = 0x12
FT_Z80_128K      = 0x13
FT_SNA_48K       = 0x16
FT_SNA_128K      = 0x17
FT_TAP           = 0x20
FT_ROMSET        = 0x30

# Per-entry record layout, reverse-engineered from ListChapter()/
# FirstChapterScan() in libFileList.ino: type(1) + name(30, case-folded,
# zero-padded) + 0x00 separator(1) + index as uint16 LE(2) = 34 bytes.
NAME_LEN = 30
MSG_LEN = NAME_LEN + 4
# One "chapter" (libFileList.ino: entriesPerPage=23 * pagesPerChapter=11).
# v1 limitation: only chapter 0 is served -- see handle_ls_relative().
MAX_FILES = 253

SNA_48K_SIZE   = 49179
SNA_128K_SIZE  = 131103
SNA_128K_SIZE2 = 147487
SCR_SIZE       = 6912
SCR_BLKSIZE    = 432


class ClientState:
    def __init__(self, root):
        self.root = os.path.abspath(root)
        self.cwd = self.root
        self.hw_type = 3  # SETZXTYPE default assumption: 128K-capable
        self.entries = []        # last LS_RELATIVE/CD result: list of (name, is_dir, ft, full_path)
        self.open_path = None    # path opened by the most recent OFREAD/OFREAD_IX


def recv_exact(conn, n):
    buf = b''
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("peer closed mid-read")
        buf += chunk
    return buf


def recv_cstring(conn, maxlen=255):
    """getBuffer_0()'s wire format: bytes until a 0x00 terminator."""
    buf = bytearray()
    while True:
        b = recv_exact(conn, 1)
        if b == b'\x00':
            break
        buf += b
        if len(buf) >= maxlen:
            break
    return bytes(buf)


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
        # compressed-vs-not; not implemented here (loading isn't
        # implemented yet either, see module docstring) -- shown as 48K.
        return FT_Z80_48K
    if ext == '.TAP':
        return FT_TAP
    return None


def list_dir(state):
    """Rebuilds state.entries for state.cwd: ".." first (unless at root),
    then directories, then files -- matching ListChapter()'s own
    directories-then-files ordering. The index assigned here (just the
    position in this list) is OUR OWN invention, not the real SD card's
    FAT32 directory index (SdFat's dirIndex()) -- fine since nothing
    outside this process ever interprets it, it only round-trips back to
    us via OFREAD_IX/CD_IX/GETINFO."""
    entries = []
    if state.cwd != state.root:
        entries.append(("..", True, FT_DIRECTORY, os.path.dirname(state.cwd)))
    try:
        names = sorted(os.listdir(state.cwd), key=str.lower)
    except OSError:
        names = []
    dirs, files = [], []
    for name in names:
        full = os.path.join(state.cwd, name)
        is_dir = os.path.isdir(full)
        ft = classify(full, is_dir)
        if ft is None:
            continue
        (dirs if is_dir else files).append((name, is_dir, ft, full))
    entries.extend(dirs)
    entries.extend(files)
    state.entries = entries[:MAX_FILES]


def encode_entry(name, ft, index):
    name_field = name[:NAME_LEN]
    if name_field:
        name_field = name_field[0].upper() + name_field[1:].lower()
    name_bytes = name_field.encode('ascii', errors='replace').ljust(NAME_LEN, b'\x00')
    return bytes([ft]) + name_bytes + bytes([0x00]) + struct.pack('<H', index & 0xFFFF)


def handle_req_id(conn):
    conn.sendall(bytes([0x01]))


def handle_setzxtype(conn, state):
    state.hw_type = recv_exact(conn, 1)[0]
    print(f"[SETZXTYPE] hw_type={state.hw_type}")


def handle_cd_root(state):
    state.cwd = state.root
    print(f"[CD_ROOT] -> {state.cwd}")


def handle_cd(conn, state):
    name = recv_cstring(conn).decode('ascii', errors='replace')
    target = os.path.dirname(state.cwd) if name == '..' else os.path.join(state.cwd, name)
    target = os.path.abspath(target)
    if target.startswith(state.root) and os.path.isdir(target):
        state.cwd = target
        print(f"[CD] {name!r} -> {state.cwd}")
    else:
        print(f"[CD] {name!r} not a directory (or escapes root), ignored")


def handle_cd_ix(conn, state):
    ix = struct.unpack('<H', recv_exact(conn, 2))[0]
    if ix >= len(state.entries):
        print(f"[CD_IX] index {ix} out of range, ignored")
        return
    name, is_dir, ft, full = state.entries[ix]
    if is_dir:
        state.cwd = os.path.abspath(full)
        print(f"[CD_IX] {ix} ({name!r}) -> {state.cwd}")
    else:
        print(f"[CD_IX] {ix} ({name!r}) is not a directory, ignored")


def handle_getdir(conn, state):
    name = os.path.basename(state.cwd) or '/'
    conn.sendall(name.encode('ascii', errors='replace')[:255] + b'\x00')


def handle_ls_relative(conn, state):
    chapter = struct.unpack('<H', recv_exact(conn, 2))[0]
    if chapter not in (0, 0xFFFF):
        # v1 limitation -- see MAX_FILES/module docstring. Reply with an
        # immediate end-of-directory marker rather than hang the Z80 side
        # waiting for entries that will never come.
        print(f"[LS_RELATIVE] chapter {chapter} not supported (v1: chapter 0 only)")
        conn.sendall(encode_entry("", FT_END_DIRECTORY, 0))
        return
    list_dir(state)
    print(f"[LS_RELATIVE] {state.cwd}: {len(state.entries)} entries")
    for index, (name, is_dir, ft, full) in enumerate(state.entries):
        conn.sendall(encode_entry(name, ft, index))
    conn.sendall(encode_entry("", FT_END_DIRECTORY, 0))


def _resolve_ofread(state, path_or_name):
    target = os.path.abspath(os.path.join(state.cwd, path_or_name))
    if not target.startswith(state.root) or not os.path.isfile(target):
        return None
    return target


def handle_ofread(conn, state):
    name = recv_cstring(conn).decode('ascii', errors='replace')
    target = _resolve_ofread(state, name)
    state.open_path = target
    print(f"[OFREAD] {name!r} -> {'OK ' + target if target else 'FAIL'}")
    # Real firmware's reply here (OpenFile()'s b[off]/b[off+1], consumed
    # by the Z80 side as SUCCESS/FAIL + file type) isn't replicated yet --
    # OFREAD's only observable effect currently is which file a
    # subsequent SCR/etc. command reads from.


def handle_ofread_ix(conn, state):
    ix = struct.unpack('<H', recv_exact(conn, 2))[0]
    if ix < len(state.entries) and not state.entries[ix][1]:
        state.open_path = state.entries[ix][3]
        print(f"[OFREAD_IX] {ix} -> OK {state.open_path}")
    else:
        state.open_path = None
        print(f"[OFREAD_IX] {ix} -> FAIL")


def handle_scr(conn, state):
    block = recv_exact(conn, 1)[0]
    if not state.open_path or not os.path.isfile(state.open_path):
        print("[SCR] no file open, nothing sent")
        return
    with open(state.open_path, 'rb') as f:
        if block == 0:
            conn.sendall(f.read(SCR_SIZE))
        else:
            f.seek((block - 1) * SCR_BLKSIZE)
            conn.sendall(f.read(SCR_BLKSIZE))
    print(f"[SCR] block={block} from {state.open_path}")


# Payload shape for every command the real firmware's dispatch table
# recognizes (Multiply_Arduino.ino), whether or not handled above --
# correct consumption is mandatory even for unimplemented commands, see
# module docstring. None = a 1-byte block-number arg like SCR's (kept
# generic since several content-streaming commands share that shape).
_FIXED_PAYLOAD = {
    CMD_ZX2INO_REQ_ID: 0,
    CMD_ZX2SD_SETZXTYPE: None,  # handled directly (reads its own 1 byte)
    CMD_ZX2SD_OFREAD: None,     # cstring, handled directly
    CMD_ZX2SD_OFREAD_IX: None,  # handled directly (2 bytes)
    CMD_ZX2SD_CD_ROOT: 0,
    CMD_ZX2SD_CD: None,         # cstring, handled directly
    CMD_ZX2SD_CD_IX: None,      # handled directly (2 bytes)
    CMD_ZX2SD_GETDIR: 0,
    CMD_ZX2SD_LS_RELATIVE: None,  # handled directly (2 bytes)
    CMD_ZX2SD_LS_ABSOLUTE: None,  # cstring -- NOT handled, consumed+ignored
    CMD_ZX2SD_SCR: None,        # handled directly (1 byte)
    CMD_ZX2SD_Z80_16K: 1,
    CMD_ZX2SD_Z80_48K: 1,
    CMD_ZX2SD_Z80_128K: 1,
    CMD_ZX2SD_SCR_FROM_Z80: 1,
    CMD_ZX2SD_SNA_48K: 1,
    CMD_ZX2SD_SNA_128K: 1,
    CMD_ZX2SD_SCR_FROM_SNA: 1,
    CMD_ZX2SD_GETINFO: 2,
    CMD_ZX2SD_TAP: 5,
    CMD_ZX2SD_SCRTAP: 1,
    CMD_ZX2SD_ROMSETBLK4B: 1,
}


def handle_client(conn, state):
    while True:
        cmd = recv_exact(conn, 1)[0]
        print(f"[RECV] cmd=0x{cmd:02x}")

        if cmd == CMD_ZX2INO_REQ_ID:
            handle_req_id(conn)
        elif cmd == CMD_ZX2SD_SETZXTYPE:
            handle_setzxtype(conn, state)
        elif cmd == CMD_ZX2SD_OFREAD:
            handle_ofread(conn, state)
        elif cmd == CMD_ZX2SD_OFREAD_IX:
            handle_ofread_ix(conn, state)
        elif cmd == CMD_ZX2SD_CD_ROOT:
            handle_cd_root(state)
        elif cmd == CMD_ZX2SD_CD:
            handle_cd(conn, state)
        elif cmd == CMD_ZX2SD_CD_IX:
            handle_cd_ix(conn, state)
        elif cmd == CMD_ZX2SD_GETDIR:
            handle_getdir(conn, state)
        elif cmd == CMD_ZX2SD_LS_RELATIVE:
            handle_ls_relative(conn, state)
        elif cmd == CMD_ZX2SD_SCR:
            handle_scr(conn, state)
        elif cmd in _FIXED_PAYLOAD and _FIXED_PAYLOAD[cmd] is not None:
            # Known command, not (yet) implemented -- still have to
            # consume its exact payload so the NEXT command byte doesn't
            # desync (see module docstring).
            n = _FIXED_PAYLOAD[cmd]
            if n:
                recv_exact(conn, n)
            print(f"[?] cmd 0x{cmd:02x} recognized but not implemented yet -- payload consumed, no reply")
        elif cmd == CMD_ZX2SD_LS_ABSOLUTE:
            recv_cstring(conn)
            print(f"[?] cmd 0x{cmd:02x} (LS_ABSOLUTE) recognized but not implemented yet -- payload consumed, no reply")
        else:
            # Truly unknown command byte -- no way to know its payload
            # shape, so there's nothing safe to do except log it. If the
            # real menu ROM ever sends one of these, every command after
            # it on this connection will likely desync.
            print(f"[!] UNKNOWN cmd 0x{cmd:02x} -- cannot determine payload length, connection may desync from here")


def main() -> None:
    parser = argparse.ArgumentParser(description="Multiply bridge for JASE's Multiply peripheral")
    parser.add_argument('--tcp-port', type=int, default=9701)
    parser.add_argument('--basedir', required=True, help="directory to serve as the virtual SD card root")
    args = parser.parse_args()

    print(f"=== Multiply Bridge v{VERSION} ===")
    print(f"SD card root: {os.path.abspath(args.basedir)}")

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.tcp_port))
    srv.listen(1)
    print(f"Listening on 127.0.0.1:{args.tcp_port}. Waiting for JASE's Multiply peripheral... (Ctrl+C to stop)")

    while True:
        conn, addr = srv.accept()
        print(f"\n*** Client connected from {addr}.")
        state = ClientState(args.basedir)
        try:
            handle_client(conn, state)
        except ConnectionError:
            pass
        finally:
            print("*** Client disconnected.")
            conn.close()


if __name__ == "__main__":
    main()
