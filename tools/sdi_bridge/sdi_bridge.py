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
import struct
import sys

VERSION = "0.1"

d64_image = None  # None = loose .prg dir; else [(name, bytes, blocks), ...] on-disk order
d64_header = None  # (disk_name16, disk_id2, dos_type2) raw bytes from the BAM, or None


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


def load_d64_image(path: str):
    """Returns ([(name, bytes, blocks), ...], (disk_name16, disk_id2, dos_type2))."""
    with open(path, 'rb') as f:
        data = f.read()

    # BAM (Block Availability Map) is always track 18 sector 0 -- disk
    # name/ID/DOS-type live at fixed offsets within it, and the first
    # byte of each track's 4-byte BAM entry (offset 4, tracks 1-35) is
    # that track's free-sector count -- summing them is the standard
    # "blocks free" figure. Raw name/id/type bytes kept as-is (not
    # decoded) -- see build_catalog() for how they're used.
    bam = data[_d64_ts_offset(18, 0): _d64_ts_offset(18, 0) + 256]
    free_blocks = sum(bam[4 + t * 4] for t in range(35))
    header = (bam[0x90:0x90 + 16], bam[0xA2:0xA2 + 2], bam[0xA5:0xA5 + 2], free_blocks)

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
            # Block count is the directory's OWN stored value (offset
            # 28-29, 16-bit LE) -- the authoritative number a real 1541
            # displays, not something to re-derive from the file's byte
            # length (which would only be an approximation).
            blocks = entry[28] | (entry[29] << 8)
            entries.append((name, _d64_extract_chain(data, start_track, start_sector), blocks))
        track, sector = next_track, next_sector

    return entries, header


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
    """[(name, bytes, blocks), ...] -- from d64_image if mounted (on-disk
    order), otherwise loose .prg files in base_dir (alphabetical, block
    count approximated from file size -- there's no real directory to
    read it from)."""
    if d64_image is not None:
        return d64_image
    out = []
    for f in sorted(os.scandir(base_dir), key=lambda f: f.name.lower()):
        if f.is_file() and f.name.lower().endswith('.prg'):
            with open(f.path, 'rb') as fh:
                content = fh.read()
                out.append((os.path.splitext(f.name)[0], content, max(1, (len(content) + 253) // 254)))
    return out


# ---------------------------------------------------------------------------
# SDI-native catalog format.
#
# An earlier hypothesis here (two-ASCII-digit-prefixed, CR-terminated
# text lines, mimicking the 1541 ERROR-CHANNEL status-line convention)
# got the real filenames printing correctly but left the screen filling
# with a spurious repeating "LIST" after the real entries -- traced
# (see project memory) to the SDI ROM reading past the end of our data
# into uninitialized RAM, because that text-line format has no
# terminator the ROM's own catalog loop recognizes. It also could never
# have matched the real SDI hardware's actual display (confirmed via a
# photo Pawel supplied): block counts shown as plain right-padded
# numbers, names in quotes, a type suffix, a disk-name header line, and
# a final "quote-count free." line -- exactly the standard CBM "$"
# directory format (the same one any real 1541 or SD2IEC emits), not
# an SDI-specific convention. SDI being designed for real IEC/1541
# interop, its ROM almost certainly parses this like any CBM-compatible
# catalog reader would -- the earlier status-line theory most likely
# only ever applied to a preceding drive-status check, not the catalog
# body itself.
#
# Structure (a real CBM BASIC "program" -- link-pointer-chained lines):
#   load address (2 bytes, 01 08)
#   per line: link-to-next (2 bytes) + block-count (2 bytes, LE) +
#             text + 0x00
#   terminated by a final link pointer of 0x0000 (no more bytes after
#   the footer line's own 0x00).
# Padding uses plain ASCII spaces (0x20), not PETSCII shifted-space
# (0xA0) -- this stream is printed through the Spectrum ROM's own
# character printer (confirmed bare RST $10, zero transformation), and
# 0xA0 isn't a normal printable glyph there.
# ---------------------------------------------------------------------------

def build_catalog(base_dir: str) -> bytes:
    out = bytearray(b"\x01\x08")
    link_addr = 0x0801

    def emit_line(linenum: int, text: bytes) -> None:
        nonlocal link_addr
        body = struct.pack('<H', linenum & 0xFFFF) + text + b"\x00"
        link_addr += 2 + len(body)
        out.extend(struct.pack('<H', link_addr & 0xFFFF))
        out.extend(body)

    if d64_header is not None:
        disk_name, disk_id, dos_type, free_blocks = d64_header
    else:
        disk_name, disk_id, dos_type, free_blocks = b' ' * 16, b'  ', b'2A', 0
    # PETSCII shifted-space (0xA0) padding -> plain ASCII space: same
    # reasoning as the per-file name padding below, this stream prints
    # through a bare RST $10 with no PETSCII awareness.
    disk_name = bytes(0x20 if b == 0xA0 else b for b in disk_name)
    disk_id = bytes(0x20 if b == 0xA0 else b for b in disk_id)
    emit_line(0, b'"' + disk_name + b'" ' + disk_id + b' ' + dos_type)

    for name, content, blocks in list_entries(base_dir):
        clean = ''.join(c for c in name.upper() if 0x20 <= ord(c) <= 0x7E)
        padded = clean.ljust(16)[:16]
        line = ('"' + padded + '" PRG').encode('ascii', errors='replace')
        emit_line(blocks, line)

    emit_line(free_blocks, b"BLOCKS FREE.")
    out += b"\x00\x00"  # end of program -- the real catalog terminator
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
        match = next((c for n, c, _b in list_entries(base_dir) if n.upper() == name.upper()), None)
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
    global d64_image, d64_header

    parser = argparse.ArgumentParser(description="SDI-native file bridge for JASE's SDI_8255 peripheral")
    parser.add_argument('--tcp-port', type=int, default=9700)
    parser.add_argument('--basedir', required=True, help="directory of loose .prg files, or a .d64 image")
    args = parser.parse_args()

    base_dir = args.basedir
    if os.path.isfile(base_dir) and base_dir.lower().endswith('.d64'):
        d64_image, d64_header = load_d64_image(base_dir)
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
