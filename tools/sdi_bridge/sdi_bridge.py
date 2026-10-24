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
# D64 reading uses the `d64` PyPI package (pip install d64) instead of a
# hand-rolled track/sector walker -- Pawel's own call: "dobrze korzystać z
# bibliotek... będzie to bardziej spójne niż własna procedura". Confirmed
# more correct too, not just more consistent: the library's
# bam.total_free() gives the right "blocks free" figure (148, matching
# Pawel's own reference listing for this exact disk) where this file's
# earlier hand-rolled BAM-sum got 164 -- a real bug the library sidesteps.
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
#   pip install d64
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

from d64 import DiskImage

VERSION = "0.2"

disk_image = None  # open d64.DiskImage, or None in loose-.prg-dir mode
charcode_mode = 'ASCII'  # 'ASCII' (default) or 'PETASCII' -- see build_catalog()


# ---------------------------------------------------------------------------
# Catalog source abstraction -- same shape (name/blocks/raw_name16/read())
# whether backed by a real .d64 (via the `d64` library) or a loose directory
# of .prg files, so build_catalog()/handle_open() don't care which.
# ---------------------------------------------------------------------------

class CatalogEntry:
    __slots__ = ('name', 'blocks', 'raw_name16', '_read')

    def __init__(self, name: str, blocks: int, raw_name16: bytes, read_fn):
        self.name = name
        self.blocks = blocks
        self.raw_name16 = raw_name16
        self._read = read_fn

    def read(self) -> bytes:
        return self._read()


def list_entries(base_dir: str) -> list:
    """[CatalogEntry, ...] -- from the open .d64 if mounted (on-disk
    order), otherwise loose .prg files in base_dir (alphabetical, block
    count approximated from file size -- there's no real directory to
    read it from, raw_name16 synthesized PETSCII-style since there's no
    real on-disk field to read back)."""
    if disk_image is not None:
        out = []
        for p in disk_image.iterdir():
            name = p.name.decode('latin1') if isinstance(p.name, bytes) else p.name
            raw16 = name.upper().encode('ascii', errors='replace').ljust(16, b'\xa0')[:16]
            out.append(CatalogEntry(name, p.size_blocks, raw16, p.open().read))
        return out
    out = []
    for f in sorted(os.scandir(base_dir), key=lambda f: f.name.lower()):
        if f.is_file() and f.name.lower().endswith('.prg'):
            name = os.path.splitext(f.name)[0]
            raw16 = name.upper().encode('ascii', errors='replace').ljust(16, b'\xa0')[:16]
            size = f.stat().st_size
            blocks = max(1, (size + 253) // 254)
            out.append(CatalogEntry(name, blocks, raw16, lambda path=f.path: open(path, 'rb').read()))
    return out


def disk_header():
    """(disk_name16, disk_id2, dos_type_byte, dos_version_byte, free_blocks)
    -- raw bytes/ints as the BAM stores them, or blank defaults in
    loose-dir mode (no real BAM to read)."""
    if disk_image is not None:
        name = disk_image.name if isinstance(disk_image.name, bytes) else disk_image.name.encode('latin1')
        disk_id = disk_image.id if isinstance(disk_image.id, bytes) else disk_image.id.encode('latin1')
        return (
            name.ljust(16, b'\xa0')[:16],
            disk_id.ljust(2, b'\xa0')[:2],
            disk_image.dos_type,
            disk_image.dos_version,
            disk_image.bam.total_free(),
        )
    return b' ' * 16, b'  ', ord('2'), ord('A'), 0


def hex_preview(data: bytes, limit: int = 96) -> str:
    """Short hex+ASCII dump for logging -- truncated for anything bigger
    than a screenful so a multi-KB file open doesn't flood the log the
    way the full catalog dump intentionally still does (it's small)."""
    shown = data[:limit]
    hexs = " ".join(f"{b:02x}" for b in shown)
    asc = "".join(chr(b) if 32 <= b <= 126 else "." for b in shown)
    suffix = f" ... (+{len(data) - limit} more bytes)" if len(data) > limit else ""
    return f"{hexs} | {asc}{suffix}"


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
# Padding/charset: controlled by --charcode (module global
# charcode_mode). 'ASCII' (default) converts PETSCII shifted-space
# (0xA0) to plain ASCII space (0x20) and re-pads names with it -- this
# stream prints through the Spectrum ROM's own character printer
# (confirmed bare RST $10, zero transformation, no PETSCII awareness),
# and 0xA0 lands in the Spectrum's UDG range (144-163), which is NOT
# guaranteed blank (an uninitialized UDG table could show garbage --
# confirmed live: it shows a repeating "0" glyph, not a space).
# 'PETASCII' sends 0xA0-padded bytes unmodified -- Pawel's own
# hypothesis that SDI's ROM might handle PETSCII itself, tested and
# disproven, kept as a diagnostic option.
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

    disk_name, disk_id, dos_type, dos_version, free_blocks = disk_header()

    if charcode_mode == 'PETASCII':
        emit_line(0, b'"' + disk_name + b'" ' + disk_id + b' ' + bytes([dos_version, dos_type]))
        for e in list_entries(base_dir):
            emit_line(e.blocks, b'"' + e.raw_name16 + b'" PRG')
    else:
        disk_name = bytes(0x20 if b == 0xA0 else b for b in disk_name)
        disk_id = bytes(0x20 if b == 0xA0 else b for b in disk_id)
        emit_line(0, b'"' + disk_name + b'" ' + disk_id + b' ' + bytes([dos_version, dos_type]))
        for e in list_entries(base_dir):
            clean = ''.join(c for c in e.name.upper() if 0x20 <= ord(c) <= 0x7E)
            padded = clean.ljust(16)[:16]
            line = ('"' + padded + '" PRG').encode('ascii', errors='replace')
            emit_line(e.blocks, line)

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


def handle_open(sock: socket.socket, line: str, channels: dict, base_dir: str) -> None:
    # Split on the first two spaces only -- "OPEN <ch_hex> <name>" where
    # <name> is everything after that second space, embedded spaces and
    # all (real CBM filenames routinely contain them, e.g. "BUGGY BOY" --
    # the generic comma-or-space field split main()'s read loop uses for
    # dispatch would otherwise chop a space-containing name into several
    # fields and lose everything after the first word).
    parts = line.split(' ', 2)
    if len(parts) < 3:
        print(f"[OPEN] malformed request: {line!r}")
        send_line(sock, "ERR malformed")
        return
    try:
        channel = int(parts[1], 16)
    except ValueError:
        print(f"[OPEN] invalid channel field: {parts[1]!r}")
        send_line(sock, "ERR invalid channel")
        return
    name = parts[2]

    if name in ('$', '$0', '$1'):
        content = build_catalog(base_dir)
        print(f"[OPEN] ch={channel} name='$' -> CATALOG, {len(content)} bytes, "
              f"{len(list_entries(base_dir))} file(s)")
        print(f"       {hex_preview(content, limit=len(content))}")

    else:
        match = next((e for e in list_entries(base_dir) if e.name.upper() == name.upper()), None)
        if match is None:
            print(f"[OPEN] ch={channel} name='{name}' -> NOT FOUND")
            send_line(sock, "ERR not found")
            return
        content = match.read()
        print(f"[OPEN] ch={channel} name='{name}' -> FILE, {len(content)} bytes "
              f"({match.blocks} block(s) on disk)")
        print(f"       {hex_preview(content)}")

    channels[channel] = content
    send_line(sock, f"OK {len(content)}")
    sock.sendall(content)
    print(f"[OPEN] ch={channel} -> sent {len(content)} bytes to JASE")


def handle_close(sock: socket.socket, fields: list, channels: dict) -> None:
    if len(fields) >= 2:
        try:
            channel = int(fields[1], 16)
            had = channels.pop(channel, None)
            print(f"[CLOSE] ch={channel} ({'was open' if had is not None else 'was not open'})")
        except ValueError:
            print(f"[CLOSE] invalid channel field: {fields[1]!r}")
    else:
        print("[CLOSE] malformed request:", fields)
    send_line(sock, "OK")


def handle_write(sock: socket.socket, fields: list, channels: dict, base_dir: str) -> None:
    if len(fields) < 4:
        print("[WRITE] malformed request:", fields)
        send_line(sock, "ERR malformed")
        return
    try:
        channel = int(fields[1], 16)
        length = int(fields[2])
    except ValueError:
        print("[WRITE] malformed request:", fields)
        send_line(sock, "ERR malformed")
        return
    eoi = fields[3] if len(fields) > 3 else '?'
    data = read_exact(sock, length)
    # SAVE support is intentionally minimal for now -- not what this
    # rewrite is chasing (the catalog-format bug is). Accept and drop,
    # same honest-stub convention as leaving something unimplemented
    # elsewhere in this project rather than half-faking it.
    print(f"[WRITE] ch={channel} {length} bytes, eoi={eoi} (discarded -- SAVE not implemented yet)")
    print(f"        {hex_preview(data)}")
    send_line(sock, "OK")


def main() -> None:
    global disk_image, charcode_mode

    parser = argparse.ArgumentParser(description="SDI-native file bridge for JASE's SDI_8255 peripheral")
    parser.add_argument('--tcp-port', type=int, default=9700)
    parser.add_argument('--basedir', required=True, help="directory of loose .prg files, or a .d64 image")
    parser.add_argument('--charcode', choices=['ASCII', 'PETASCII'], default='ASCII',
                         help="catalog charset: ASCII (default, converts 0xA0 padding to a plain "
                              "space for safe display on the Spectrum charset) or PETASCII "
                              "(send real on-disk bytes unmodified, testing whether SDI's own ROM "
                              "handles the translation itself -- confirmed it doesn't, kept as a "
                              "diagnostic option)")
    args = parser.parse_args()
    charcode_mode = args.charcode

    base_dir = args.basedir
    print(f"=== SDI Bridge V{VERSION} ===")
    if os.path.isfile(base_dir) and base_dir.lower().endswith('.d64'):
        disk_image = DiskImage(base_dir).open()
        entries = list_entries(base_dir)
        name, disk_id, dos_type, dos_version, free_blocks = disk_header()
        clean_name = bytes(0x20 if b == 0xA0 else b for b in name).decode('ascii', errors='replace')
        clean_id = bytes(0x20 if b == 0xA0 else b for b in disk_id).decode('ascii', errors='replace')
        print(f"Disk: {base_dir} (mounted .d64 via the 'd64' library, charcode={charcode_mode})")
        print(f"      name=\"{clean_name}\" id=\"{clean_id}\" "
              f"dos={chr(dos_version)}{chr(dos_type)} free={free_blocks} blocks")
        print(f"      {len(entries)} file(s):")
        for e in entries:
            print(f"        {e.blocks:>3} block(s)  \"{e.name}\"")
    else:
        print(f"Dir: {base_dir} (loose .prg files, charcode={charcode_mode})")
        for e in list_entries(base_dir):
            print(f"        {e.blocks:>3} block(s)  \"{e.name}\"")

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.tcp_port))
    srv.listen(1)
    print(f"Listening on 127.0.0.1:{args.tcp_port}. Waiting for JASE's SDI peripheral... (Ctrl+C to stop)")

    while True:
        conn, addr = srv.accept()
        print(f"\n*** Client connected from {addr}.")
        channels: dict = {}
        try:
            while True:
                line = read_line(conn)
                fields = line.split(',') if ',' in line else line.split(' ')
                if not fields or not fields[0]:
                    continue
                cmd = fields[0]
                print(f"[RECV] {line!r}")
                if cmd == "OPEN":
                    handle_open(conn, line, channels, base_dir)
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
