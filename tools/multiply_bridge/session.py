# -----------------------------------------------------------------------------
# Session -- per-connection state (which file is open, its parsed
# Snapshot/TapPlayer if any) plus handle_client(), the dispatch loop that
# owns all the actual socket I/O. Everything this delegates to
# (VirtualSDCard, Snapshot, TapPlayer) is pure logic with no socket
# dependency of its own -- this module is where protocol constants, wire
# I/O, and that logic meet.
# -----------------------------------------------------------------------------

import struct

import protocol as proto
from virtual_sdcard import VirtualSDCard
from snapshot import Snapshot
from tap import TapPlayer


class Session:
    def __init__(self, root):
        self.sdcard = VirtualSDCard(root)
        self.hw_type = 3  # SETZXTYPE default assumption: 128K-capable
        self.open_path = None  # path opened by the most recent OFREAD/OFREAD_IX
        self.open_data = None  # that file's full contents, read once at open time
        self.snapshot = None   # Snapshot, built lazily when a SNA/Z80 command's block 9 is requested
        self.tap = None        # TapPlayer, built lazily when CMD_ZX2SD_TAP first reads open_data

    def open_file(self, path):
        """path=None (or unreadable) clears the open file -- callers pass
        whatever VirtualSDCard.resolve()/an entry's .path gave them."""
        self.open_path = None
        self.open_data = None
        self.snapshot = None
        self.tap = None
        if not path:
            return
        try:
            with open(path, 'rb') as f:
                self.open_data = f.read()
            self.open_path = path
        except OSError:
            pass


# --- Individual command handlers -- each reads its own payload (if any)
# directly from `conn` and writes its own reply (if any). ---------------

def _handle_req_id(conn):
    conn.sendall(proto.MULTIPLY_VERSION)


def _handle_setzxtype(conn, session):
    session.hw_type = proto.recv_exact(conn, 1)[0]
    print(f"[SETZXTYPE] hw_type={session.hw_type}")


def _handle_cd_root(session):
    session.sdcard.cd_root()
    print(f"[CD_ROOT] -> {session.sdcard.cwd}")


def _handle_cd(conn, session):
    name = proto.recv_cstring(conn).decode('ascii', errors='replace')
    ok = session.sdcard.cd(name)
    print(f"[CD] {name!r} -> {session.sdcard.cwd}" if ok else f"[CD] {name!r} not a directory (or escapes root), ignored")


def _handle_cd_ix(conn, session):
    ix = struct.unpack('<H', proto.recv_exact(conn, 2))[0]
    ok = session.sdcard.cd_ix(ix)
    print(f"[CD_IX] {ix} -> {session.sdcard.cwd}" if ok else f"[CD_IX] {ix} out of range or not a directory, ignored")


def _handle_getdir(conn, session):
    name = session.sdcard.current_name().encode('ascii', errors='replace')[:proto.MAX_FLEN - 1] + b'\x00'
    conn.sendall(name.ljust(proto.MAX_FLEN, b'\x00'))


def _handle_ls_relative(conn, session):
    chapter = struct.unpack('<H', proto.recv_exact(conn, 2))[0]
    # SOLVED 2026-11-01 (was previously just worked around -- see git
    # history): the varying "garbage" chapter values the ROM sent here on
    # every run (24, 124, 7, 37...) were not uninitialized Z80 RAM at all.
    # A chapter-0 request's reply MUST carry chapter/page bookkeeping
    # (see proto.encode_chapter_terminator's own comment) that this
    # server used to omit entirely -- so the ROM's own chapter table
    # (ChapTable) was never populated, and the NEXT chapter it asked for
    # was computed from THAT garbage table, different every run. Now that
    # encode_chapter_terminator() fills it in correctly, a well-behaved
    # ROM should only ever ask for chapter 0 here (this server still has
    # no real multi-chapter support -- v2, for directories over
    # MAX_FILES entries -- so any other value is still just treated as 0,
    # but shouldn't normally occur anymore).
    if chapter not in (0, 0xFFFF):
        print(f"[LS_RELATIVE] chapter {chapter} (treating as 0 -- see comment)")
    entries = session.sdcard.list_entries()
    print(f"[LS_RELATIVE] {session.sdcard.cwd}: {len(entries)} entries")
    for index, entry in enumerate(entries):
        conn.sendall(proto.encode_entry(entry.name, entry.ft, index))
    conn.sendall(proto.encode_chapter_terminator(len(entries)))


def _handle_ofread(conn, session):
    name = proto.recv_cstring(conn).decode('ascii', errors='replace')
    session.open_file(session.sdcard.resolve(name))
    print(f"[OFREAD] {name!r} -> {'OK ' + session.open_path if session.open_path else 'FAIL'}")
    # Real firmware's reply here (OpenFile()'s b[off]/b[off+1], consumed by
    # the Z80 side as SUCCESS/FAIL + file type) isn't replicated yet --
    # OFREAD's only observable effect currently is which file a subsequent
    # SCR/SNA/Z80/TAP command reads from.


def _handle_ofread_ix(conn, session):
    ix = struct.unpack('<H', proto.recv_exact(conn, 2))[0]
    entries = session.sdcard.entries
    path = entries[ix].path if ix < len(entries) and not entries[ix].is_dir else None
    session.open_file(path)
    print(f"[OFREAD_IX] {ix} -> {'OK ' + session.open_path if session.open_path else 'FAIL'}")


def _handle_scr(conn, session):
    block = proto.recv_exact(conn, 1)[0]
    if not session.open_data:
        print("[SCR] no file open, nothing sent")
        return
    if block == 0:
        conn.sendall(session.open_data[:proto.SCR_SIZE])
    else:
        start = (block - 1) * proto.SCR_BLKSIZE
        conn.sendall(session.open_data[start:start + proto.SCR_BLKSIZE])
    print(f"[SCR] block={block} from {session.open_path}")


def _handle_snapshot(conn, session, req_type, is_z80):
    kind = 'Z80' if is_z80 else 'SNA'
    c = proto.recv_exact(conn, 1)[0]
    if c == 9:
        session.snapshot = Snapshot.load(session.open_data or b'', req_type, is_z80)
        conn.sendall(session.snapshot.header)
        print(f"[{kind}] header sent ({len(session.snapshot.pages)} RAM pages parsed)")
        return
    if not session.snapshot:
        print(f"[{kind}] block {c} requested with no header sent first, ignored")
        return
    conn.sendall(session.snapshot.block(c))
    print(f"[{kind}] block={c}")


def _handle_getinfo(conn, session):
    # CMD_ZX2SD_GETINFO -- the ROM's file-browser sends this when the
    # cursor lands on a file entry (NOT a directory -- confirmed live,
    # 2026-11-01: moving the cursor over FT_DIRECTORY entries never
    # triggers this at all, matching GetMoreInfoIX()'s own OpenFileIX()
    # call, which fails for a directory and sends nothing). Payload is a
    # uint16 LE index into the CURRENT list_entries() result, same
    # indexing convention as CD_IX/OFREAD_IX.
    #
    # This was previously an unimplemented stub (payload consumed, no
    # reply) -- a real, confirmed-live hang: the ROM's receive routine
    # (Load4bitBlk) blocks forever waiting for bytes that never arrive
    # the instant the cursor touches any file (not just on open/load).
    # Byte shapes below come from libSDFiles.ino's GetMoreInfoIX(), real
    # Arduino firmware source (not guessed) -- only the FT_TAP and
    # FT_Z80_* cases are implemented; GetMoreInfoIX()'s own switch has NO
    # case at all (falls to `default: break`, i.e. sends nothing) for
    # FT_DIRECTORY/FT_SCR/FT_SNA_*, so those are never a regression vs.
    # real hardware. FT_ROMSET's case (8 +36*N bytes, game-table dump) is
    # NOT implemented -- a real gap, flagged rather than guessed.
    ix = struct.unpack('<H', proto.recv_exact(conn, 2))[0]
    entries = session.sdcard.entries
    if ix >= len(entries) or entries[ix].is_dir:
        print(f"[GETINFO] {ix} out of range or a directory -- no reply (matches real firmware)")
        return
    entry = entries[ix]
    try:
        with open(entry.path, 'rb') as f:
            data = f.read(38)
    except OSError:
        print(f"[GETINFO] {ix} ({entry.name!r}) failed to open -- no reply")
        return
    if entry.ft == proto.FT_TAP:
        # 11 bytes: [0]=SCR-embedded-in-TAP flag (SCRposTAP() on real
        # firmware -- NOT implemented here, always sent as 0/"no screen",
        # a documented simplification, not a hang), [1..10]=10-char
        # PROGRAM name from the TAP header block (offset 4-13), control
        # bytes replaced with spaces same as real firmware.
        name = bytearray(data[4:14].ljust(10, b' ')[:10])
        for i in range(len(name)):
            if name[i] < 32 or name[i] > 127:
                name[i] = 32
        conn.sendall(bytes([0]) + bytes(name))
        print(f"[GETINFO] {ix} ({entry.name!r}) FT_TAP")
    elif entry.ft in (proto.FT_Z80_16K, proto.FT_Z80_48K, proto.FT_Z80_128K):
        # 3 bytes. Real firmware distinguishes v1 (bytes 6/7 nonzero --
        # PC, absent in v2/v3) from v2/v3 by the exact same check
        # snapshot.py's loader already uses (`is_v1`); v1 sends three
        # 0xFF placeholder bytes (real firmware doesn't inspect v1
        # further here either), v2/v3 sends the additional-header-length
        # low byte, hardware-machine byte, and its bit-7 flag verbatim.
        hdr = data.ljust(38, b'\x00')
        is_v1 = hdr[6] != 0 or hdr[7] != 0
        if is_v1:
            conn.sendall(bytes([0xFF, 0xFF, 0xFF]))
        else:
            conn.sendall(bytes([hdr[30], hdr[34], hdr[37]]))
        print(f"[GETINFO] {ix} ({entry.name!r}) FT_Z80 is_v1={is_v1}")
    else:
        print(f"[GETINFO] {ix} ({entry.name!r}) ft=0x{entry.ft:02x} not implemented (e.g. FT_ROMSET) -- no reply")


def _handle_tap(conn, session):
    payload = proto.recv_exact(conn, 5)
    zx_flag = payload[0]
    zx_length = struct.unpack('<H', payload[1:3])[0]
    zx_start = struct.unpack('<H', payload[3:5])[0]

    if session.tap is None:
        session.tap = TapPlayer(session.open_data or b'')
    status, send_length, send_start, chunk = session.tap.next_block(zx_flag, zx_length, zx_start)

    conn.sendall(bytes([status]) + struct.pack('<H', send_length & 0xFFFF) + struct.pack('<H', send_start))
    conn.sendall(chunk)
    print(f"[TAP] status={status} start={send_start:#06x} length={send_length}")


def handle_client(conn, session):
    while True:
        cmd = proto.recv_exact(conn, 1)[0]
        print(f"[RECV] cmd=0x{cmd:02x}")

        if cmd == proto.CMD_ZX2INO_REQ_ID:
            _handle_req_id(conn)
        elif cmd == proto.CMD_ZX2SD_SETZXTYPE:
            _handle_setzxtype(conn, session)
        elif cmd == proto.CMD_ZX2SD_OFREAD:
            _handle_ofread(conn, session)
        elif cmd == proto.CMD_ZX2SD_OFREAD_IX:
            _handle_ofread_ix(conn, session)
        elif cmd == proto.CMD_ZX2SD_CD_ROOT:
            _handle_cd_root(session)
        elif cmd == proto.CMD_ZX2SD_CD:
            _handle_cd(conn, session)
        elif cmd == proto.CMD_ZX2SD_CD_IX:
            _handle_cd_ix(conn, session)
        elif cmd == proto.CMD_ZX2SD_GETDIR:
            _handle_getdir(conn, session)
        elif cmd == proto.CMD_ZX2SD_LS_RELATIVE:
            _handle_ls_relative(conn, session)
        elif cmd == proto.CMD_ZX2SD_SCR:
            _handle_scr(conn, session)
        elif cmd in (proto.CMD_ZX2SD_SNA_48K, proto.CMD_ZX2SD_SNA_128K):
            _handle_snapshot(conn, session, cmd, is_z80=False)
        elif cmd in (proto.CMD_ZX2SD_Z80_16K, proto.CMD_ZX2SD_Z80_48K, proto.CMD_ZX2SD_Z80_128K):
            _handle_snapshot(conn, session, cmd, is_z80=True)
        elif cmd == proto.CMD_ZX2SD_TAP:
            _handle_tap(conn, session)
        elif cmd == proto.CMD_ZX2SD_GETINFO:
            _handle_getinfo(conn, session)
        elif cmd == proto.CMD_ZX2SD_LS_ABSOLUTE:
            proto.recv_cstring(conn)
            print(f"[?] cmd 0x{cmd:02x} (LS_ABSOLUTE) recognized but not implemented yet -- payload consumed, no reply")
        elif cmd in proto.FIXED_PAYLOAD and proto.FIXED_PAYLOAD[cmd] is not None:
            # Known command, not (yet) implemented -- still have to consume
            # its exact payload so the NEXT command byte doesn't desync.
            n = proto.FIXED_PAYLOAD[cmd]
            if n:
                proto.recv_exact(conn, n)
            print(f"[?] cmd 0x{cmd:02x} recognized but not implemented yet -- payload consumed, no reply")
        else:
            # Truly unknown command byte -- no way to know its payload
            # shape, so there's nothing safe to do except log it. If the
            # real menu ROM ever sends one of these, every command after
            # it on this connection will likely desync.
            print(f"[!] UNKNOWN cmd 0x{cmd:02x} -- cannot determine payload length, connection may desync from here")
