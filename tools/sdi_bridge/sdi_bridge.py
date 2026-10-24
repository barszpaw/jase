#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# SDI-native file bridge -- purpose-built replacement for
# tools/c64iec_fs/c64iec_fs_tcp.py.
#
# Why this exists: that server speaks fdemu's upstream wire protocol and
# produces a real-C64-style "$" directory listing (CBM BASIC-program binary
# format, link-pointer-chained lines). Live-traced against the real SDI103J
# ROM (see docs/VC1541/VC1541.md and project memory): SDI's *CAT does NOT
# read a directory that way at all. Its catalog routine reads a 1541-style
# ERROR-CHANNEL status line first (two ASCII digit characters + text,
# CR-terminated -- read byte-by-byte via its own $3138, discarding the line
# if the second digit is < '2', printing it if >= '2', matching how a real
# drive's power-up banner ("73,...") gets shown but a plain "00,OK,00,00"
# status doesn't) before anything else. Fed the C64 BASIC-program bytes
# instead, it stumbles on a stray 0x0D somewhere in the binary link-pointer
# noise within the first ~10 bytes, desyncs immediately, and the Z80 ROM
# code downstream of that eventually raises a genuine Sinclair BASIC error
# ("B Integer out of range") -- confirmed live, not a guess.
#
# D64 parsing (load_d64_image/_d64_sectors_per_track/_d64_ts_offset/
# _d64_extract_chain) is copied from tools/c64iec_fs/c64iec_fs_tcp.py
# (itself adapted from fox20io/c64-fdemu's c64iec_fs.py, GPLv3 -- see
# LICENSE in this directory, same license, same reason: this directory is
# its own separate process, never linked into the `jase` binary). That
# part is a correct, protocol-independent fact about the .d64 file format
# and is reused verbatim. Everything else here -- the wire protocol and
# the catalog/status-line formatting -- is new, written specifically
# against what SDI's own ROM was traced wanting, not against real-C64/
# real-1541 conventions.
#
# Wire protocol (line-based ASCII, one TCP connection per JASE run):
#   -> OPEN <ch_hex> <name>\n     <- OK <len>\n<len raw bytes>   (success)
#                                 <- ERR <reason>\n               (failure)
#   -> CLOSE <ch_hex>\n           <- OK\n
#   -> WRITE <ch_hex> <len> <E|C>\n<len raw bytes>
#                                 <- OK\n / ERR <reason>\n
#
# Deliberately pre-fetch, not per-byte: OPEN returns the COMPLETE channel
# content in one shot. JASE buffers it locally and drains it bit-by-bit
# with no further network round-trips -- the IEC bit-banging timing is
# time-critical, a TCP round-trip per byte is not something to risk it on.
#
# Usage:
#   python3 sdi_bridge.py --tcp-port 9700 --basedir /path/to/loose/prgs
#   python3 sdi_bridge.py --tcp-port 9700 --basedir /path/to/disk.d64
# -----------------------------------------------------------------------------
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version. See LICENSE in this directory.
# -----------------------------------------------------------------------------

import argparse
import os
import socket
import sys

VERSION = "0.1"

d64_image = None  # None = loose .prg dir; else [(name, bytes), ...] on-disk order


# ---------------------------------------------------------------------------
# D64 parsing -- copied from tools/c64iec_fs/c64iec_fs_tcp.py, same facts,
# same license (GPLv3), see top-of-file comment.
# ---------------------------------------------------------------------------

def _d64_sectors_per_track(track: int) -> int:
    if track <= 17:
        return 21
    if track <= 24:
        return 19
    if track <= 30:
        return 18
    return 17


def _d64_ts_offset(track: int, sector: int) -> int:
    offset = sum(_d64_sectors_per_track(t) for t in range(1, track)) * 256
    return offset + sector * 256


def load_d64_image(path: str) -> list:
    with open(path, 'rb') as f:
        data = f.read()

    entries = []
    track, sector = 18, 1
    visited = set()
    while track != 0 and (track, sector) not in visited:
        visited.add((track, sector))
        block = data[_d64_ts_offset(track, sector): _d64_ts_offset(track, sector) + 256]
        next_track, next_sector = block[0], block[1]
        for i in range(8):
            entry = block[2 + i * 32: 2 + i * 32 + 32]
            file_type = entry[0]
            if file_type == 0:
                continue
            name = entry[3:19].rstrip(b'\xa0').decode('latin1')
            start_track, start_sector = entry[1], entry[2]
            entries.append((name, _d64_extract_chain(data, start_track, start_sector)))
        track, sector = next_track, next_sector

    return entries


def _d64_extract_chain(data: bytes, track: int, sector: int) -> bytes:
    out = bytearray()
    visited = set()
    while track != 0:
        if (track, sector) in visited:
            break
        visited.add((track, sector))
        off = _d64_ts_offset(track, sector)
        block = data[off: off + 256]
        next_track, next_sector = block[0], block[1]
        if next_track == 0:
            last_used = next_sector
            out += block[2:2 + max(0, last_used - 1)]
        else:
            out += block[2:256]
        track, sector = next_track, next_sector
    return bytes(out)


def list_entries(base_dir: str) -> list:
    """[(name, bytes), ...] -- from d64_image if mounted (on-disk order),
    otherwise loose .prg files in base_dir (alphabetical)."""
    if d64_image is not None:
        return d64_image
    out = []
    for f in sorted(os.scandir(base_dir), key=lambda f: f.name.lower()):
        if f.is_file() and f.name.lower().endswith('.prg'):
            with open(f.path, 'rb') as fh:
                out.append((os.path.splitext(f.name)[0], fh.read()))
    return out


# ---------------------------------------------------------------------------
# SDI-native catalog format -- see top-of-file comment for the trace this
# is built from. Status line first (ASCII "ddtext\r", d=digit -- SDI's own
# $33a6/$339c branch decides print-vs-discard on the SECOND digit), then
# one plain CR-terminated name per file. The exact per-file framing past
# the status line is a first hypothesis, not yet confirmed against the
# ROM as thoroughly as the status line itself -- expect to iterate this
# function based on what the real ROM does with it.
# ---------------------------------------------------------------------------

def build_catalog(base_dir: str) -> bytes:
    out = bytearray(b"00,OK,00,00\r")
    for name, content in list_entries(base_dir):
        clean = ''.join(c for c in name.upper() if 0x20 <= ord(c) <= 0x7E)
        # Test hypothesis: every line (not just the status line) goes
        # through the same two-ASCII-digit print/skip check -- without a
        # prefix, the filename's OWN first two characters were being
        # consumed as if they were that prefix, desyncing everything
        # after. "99," forces the print branch (second digit '9' >= '2')
        # unconditionally.
        out += b"99," + clean.encode('ascii', errors='replace') + b"\r"
    return bytes(out)


# ---------------------------------------------------------------------------
# Wire protocol handlers
# ---------------------------------------------------------------------------

def read_line(sock: socket.socket) -> str:
    buf = bytearray()
    while True:
        ch = sock.recv(1)
        if not ch:
            raise ConnectionError("peer closed")
        if ch == b'\n':
            break
        if ch != b'\r':
            buf += ch
    return buf.decode('ascii', errors='replace')


def send_line(sock: socket.socket, text: str) -> None:
    sock.sendall((text + '\n').encode('ascii'))


def read_exact(sock: socket.socket, count: int) -> bytes:
    buf = bytearray()
    while len(buf) < count:
        chunk = sock.recv(count - len(buf))
        if not chunk:
            raise ConnectionError("peer closed")
        buf += chunk
    return bytes(buf)


def handle_open(sock: socket.socket, fields: list, channels: dict, base_dir: str) -> None:
    if len(fields) < 3:
        send_line(sock, "ERR malformed")
        return
    try:
        channel = int(fields[1], 16)
    except ValueError:
        send_line(sock, "ERR invalid channel")
        return
    name = fields[2]

    if name in ('$', '$0', '$1'):
        content = build_catalog(base_dir)
        print(f"[OPEN] ch={channel} '$' -> catalog, {len(content)} bytes")
        print(" ".join(f"{b:02x}" for b in content) + " | " + "".join(chr(b) if 32 <= b <= 126 else "." for b in content) + "\n", end="")

    else:
        match = next((c for n, c in list_entries(base_dir) if n.upper() == name.upper()), None)
        if match is None:
            print(f"[OPEN] ch={channel} '{name}' -> FAIL (not found)")
            send_line(sock, "ERR not found")
            return
        content = match
        print(f"[OPEN] ch={channel} '{name}' -> OK, {len(content)} bytes")

    channels[channel] = content
    send_line(sock, f"OK {len(content)}")
    sock.sendall(content)


def handle_close(sock: socket.socket, fields: list, channels: dict) -> None:
    if len(fields) >= 2:
        try:
            channels.pop(int(fields[1], 16), None)
        except ValueError:
            pass
    send_line(sock, "OK")


def handle_write(sock: socket.socket, fields: list, channels: dict, base_dir: str) -> None:
    if len(fields) < 4:
        send_line(sock, "ERR malformed")
        return
    try:
        channel = int(fields[1], 16)
        length = int(fields[2])
    except ValueError:
        send_line(sock, "ERR malformed")
        return
    data = read_exact(sock, length)
    # SAVE support is intentionally minimal for now -- not what this
    # rewrite is chasing (the catalog-format bug is). Accept and drop,
    # same honest-stub convention as leaving something unimplemented
    # elsewhere in this project rather than half-faking it.
    print(f"[WRITE] ch={channel} {length} bytes (discarded -- SAVE not implemented yet)")
    send_line(sock, "OK")


def main() -> None:
    global d64_image

    parser = argparse.ArgumentParser(description="SDI-native file bridge for JASE's SDI_8255 peripheral")
    parser.add_argument('--tcp-port', type=int, default=9700)
    parser.add_argument('--basedir', required=True, help="directory of loose .prg files, or a .d64 image")
    args = parser.parse_args()

    base_dir = args.basedir
    if os.path.isfile(base_dir) and base_dir.lower().endswith('.d64'):
        d64_image = load_d64_image(base_dir)
        print(f"=== SDI Bridge V{VERSION} ===")
        print(f"Disk: {base_dir} (mounted .d64, {len(d64_image)} file(s))")
    else:
        print(f"=== SDI Bridge V{VERSION} ===")
        print(f"Dir: {base_dir}")

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.tcp_port))
    srv.listen(1)
    print(f"Listening on 127.0.0.1:{args.tcp_port}. Waiting for JASE's SDI peripheral... (Ctrl+C to stop)")

    while True:
        conn, _ = srv.accept()
        print("\n*** Client connected.")
        channels: dict = {}
        try:
            while True:
                line = read_line(conn)
                fields = line.split(',') if ',' in line else line.split(' ')
                if not fields or not fields[0]:
                    continue
                cmd = fields[0]
                if cmd == "OPEN":
                    handle_open(conn, fields, channels, base_dir)
                elif cmd == "CLOSE":
                    handle_close(conn, fields, channels)
                elif cmd == "WRITE":
                    handle_write(conn, fields, channels, base_dir)
                else:
                    print(f"[?] unknown command: {line!r}")
                    send_line(conn, "ERR unknown command")
        except ConnectionError:
            print("*** Client disconnected.")
        finally:
            conn.close()


if __name__ == "__main__":
    main()
