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
    conn.sendall(session.sdcard.current_name().encode('ascii', errors='replace')[:255] + b'\x00')


def _handle_ls_relative(conn, session):
    chapter = struct.unpack('<H', proto.recv_exact(conn, 2))[0]
    if chapter not in (0, 0xFFFF):
        # v1 limitation -- see VirtualSDCard/protocol.MAX_FILES. Reply with
        # an immediate end-of-directory marker rather than hang the Z80
        # side waiting for entries that will never come.
        print(f"[LS_RELATIVE] chapter {chapter} not supported (v1: chapter 0 only)")
        conn.sendall(proto.encode_entry("", proto.FT_END_DIRECTORY, 0))
        return
    entries = session.sdcard.list_entries()
    print(f"[LS_RELATIVE] {session.sdcard.cwd}: {len(entries)} entries")
    for index, entry in enumerate(entries):
        conn.sendall(proto.encode_entry(entry.name, entry.ft, index))
    conn.sendall(proto.encode_entry("", proto.FT_END_DIRECTORY, 0))


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
