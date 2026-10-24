#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# C64 IEC File Server -- TCP transport variant
#
# Adapted from fox20io/c64-fdemu's src/fileserver/c64iec_fs.py (Copyright (C)
# 2026 Norbert Laszlo), https://github.com/fox20io/c64-fdemu -- see LICENSE
# in this directory (GNU GPLv3, same as upstream). This file, and this
# directory, are GPLv3 -- a DIFFERENT license from the rest of the JASE
# repository (MIT). Nothing in this directory is compiled into, linked with,
# or distributed as part of the `jase` binary; it only ever runs as its own
# separate companion process, talking to JASE's SDI peripheral over a plain
# TCP socket using the exact same line-based ASCII wire protocol upstream
# already uses over USB-serial to a real Arduino. See docs/VC1541/VC1541.md
# for the full writeup of why that boundary matters and what it allows.
#
# What changed from upstream, specifically: every serial.Serial() read/write
# (read_line/send_line/read_exact, the DTR/RTS/open/reconnect dance, and the
# "<<<...>>>" boot-banner sniff used to detect a physical adapter resetting)
# is replaced with the TCP socket equivalent (read_line/send_line/read_exact
# against a socket, and a plain accept() loop where each new connection IS
# the reset event -- there's no separate physical-reset signal to sniff for
# over TCP, a fresh connection already means "the other end just started").
# All of the loose-file-serving logic below (build_directory_listing,
# handle_open/close/read/write/list, the CBM directory format, the
# wildcard/virtual-file handling) is unchanged from upstream -- same
# license, same author's original work, this file does not claim authorship
# of any of that.
#
# ALSO ADDED (not from upstream, fresh code): --basedir can point at a
# single .d64 disk image instead of a directory of loose .prg files --
# load_d64_image() parses it once at startup (standard, long-public D64
# layout: 35 tracks of 256-byte sectors, directory chain starting at
# track 18/sector 1, each entry's data following its own track/sector
# link chain) and the rest of the server treats its contents exactly
# like a loose-file directory, in on-disk order rather than
# alphabetical. This is what lets real SDI disk images (e.g. the ones
# under docs/VC1541/web/.../disk/*.d64) be served directly without a
# separate extraction step -- real SDI firmware documentation (see
# docs/VC1541/web/.../rom/SDI/SDIv103J.TXT) describes exactly this
# workflow against a real SD2IEC ("*COM CD/directory/d64image.d64
# mounts a D64"), so this isn't a guess at how it's meant to be used.
#
# Usage:
#   python c64iec_fs_tcp.py [--tcp-port 9101] [--basedir /path] [--devid 8]
#   python c64iec_fs_tcp.py --basedir /path/to/disk.d64
# -----------------------------------------------------------------------------
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software Foundation,
# Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
# -----------------------------------------------------------------------------

import argparse
import io
import os
import socket
import sys
import time

VERSION = "1.0-tcp"

CHUNK_SIZE = 256

DIR_LIST_HEADER = f"\x00\x00\x12\" C64-FDEMU V{VERSION} \" 00 2A\x00".encode('ascii')
DIR_LIST_FOOTER = b"\x00\x00BLOCK FREE.                \x00\x00\x00"
DIR_LIST_SPC27 = b" " * 27

# None = serving loose .prg files from a real directory (upstream's own
# mode, untouched). A list of (name, bytes) tuples = serving a mounted
# .d64 image instead, in on-disk directory order -- see load_d64_image().
d64_image = None


# ---------------------------------------------------------------------------
# D64 disk image support (fresh code, not from upstream -- see the top-of-
# file comment for why this was added and what it's for).
#
# Standard D64 layout facts (long-public Commodore format, documented in
# many independent places -- not specific to any one codebase):
#   - 35 tracks of 256-byte sectors; sectors/track: 21 (tracks 1-17),
#     19 (18-24), 18 (25-30), 17 (31-35). No error-info bytes appended
#     (a plain 174848-byte image, which is what both real SDI disk
#     images under docs/VC1541/web/.../disk/ are).
#   - The directory is a linked chain of sectors starting at track 18,
#     sector 1 (the BAM itself lives at track 18, sector 0, not read
#     here -- block-free counts aren't needed to just list/serve files).
#   - Each directory sector: byte 0-1 = track/sector of the next
#     directory sector (track 0 = end of chain), then 8 entries of 32
#     bytes each. Per entry: byte 0 = file type (low nibble 2 = PRG,
#     bit 7 = closed), bytes 1-2 = track/sector of the file's first
#     data block, bytes 3-18 = filename (16 bytes, $A0-padded), bytes
#     28-29 = size in blocks (not used here, just the actual chain).
#   - File data: each data block is also track/sector-linked (bytes 0-1
#     of each 256-byte block); track 0 on the LAST block means byte 1
#     is the index of the last valid byte in THIS block instead of a
#     sector number, not a trailing link.
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
    """Parse a .d64 image, returning [(name, bytes), ...] in on-disk order."""
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
                continue  # deleted/unused slot
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
            break  # malformed chain -- stop rather than loop forever
        visited.add((track, sector))
        off = _d64_ts_offset(track, sector)
        block = data[off: off + 256]
        next_track, next_sector = block[0], block[1]
        if next_track == 0:
            last_used = next_sector  # index of the last valid byte, per the format
            out += block[2:2 + max(0, last_used - 1)]
        else:
            out += block[2:256]
        track, sector = next_track, next_sector
    return bytes(out)


# ---------------------------------------------------------------------------
# TCP transport helpers (replaces upstream's serial read_line/send_line/
# read_exact -- same semantics, same line-based ASCII framing, different pipe)
# ---------------------------------------------------------------------------

def read_line(sock: socket.socket) -> str:
    """Read bytes from sock until \\n, return decoded ASCII string (no \\r\\n).

    Raises ConnectionError (empty recv) when the peer closes.
    """
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
    """Send ASCII text followed by \\n."""
    sock.sendall((text + '\n').encode('ascii'))


def read_exact(sock: socket.socket, count: int) -> bytes:
    """Read exactly *count* bytes from sock, blocking until all arrive."""
    buf = bytearray()
    while len(buf) < count:
        chunk = sock.recv(count - len(buf))
        if not chunk:
            raise ConnectionError("peer closed")
        buf += chunk
    return bytes(buf)


# ---------------------------------------------------------------------------
# CBM directory listing builder (file-gathering generalized to also cover
# d64_image; the actual 32-byte-per-line CBM format below is unchanged
# from upstream)
# ---------------------------------------------------------------------------

def _list_entries(base_dir: str) -> list:
    """[(name_without_ext, size_in_bytes), ...] -- from d64_image if a disk
    is mounted (on-disk order), otherwise from loose .prg files in base_dir
    (alphabetical, upstream's own behaviour)."""
    MAX_FILES = 144
    if d64_image is not None:
        return [(name, len(content)) for name, content in d64_image[:MAX_FILES]]
    all_files = sorted(
        [
            f for f in os.scandir(base_dir)
            if f.is_file()
            and f.name.lower().endswith('.prg')
            and f.stat().st_size <= 0xFFFF
            and len(os.path.splitext(f.name)[0]) <= 16
        ],
        key=lambda f: f.name.lower()
    )[:MAX_FILES]
    return [(os.path.splitext(f.name)[0], f.stat().st_size) for f in all_files]


def build_directory_listing(base_dir: str) -> bytes:
    """
    Build a CBM-style directory listing as raw bytes.

    Layout of each 32-byte line:
      [0-1]  Link pointer (LE 16-bit) to the start of the next line
      [2-3]  Line number  (LE 16-bit) -- 0 for header, filesize/254 for files
      [4-30] Content (spaces + PETSCII text)
      [31]   0x00 terminator
    """
    LOAD_ADDRESS = 0x0801
    LINE_SIZE = 32

    all_files = _list_entries(base_dir)

    total_lines = 2 + len(all_files)
    buf = bytearray(total_lines * LINE_SIZE + 2)

    buf[0] = LOAD_ADDRESS & 0xFF
    buf[1] = (LOAD_ADDRESS >> 8) & 0xFF

    next_addr = LOAD_ADDRESS + LINE_SIZE + 2
    buf[2] = next_addr & 0xFF
    buf[3] = (next_addr >> 8) & 0xFF

    buf[4:4 + len(DIR_LIST_HEADER)] = DIR_LIST_HEADER

    offset = LINE_SIZE

    for file_name, file_size in all_files:
        next_off = LOAD_ADDRESS + offset + LINE_SIZE
        buf[offset + 0] = next_off & 0xFF
        buf[offset + 1] = (next_off >> 8) & 0xFF

        blocks = max(1, -(-file_size // 254))
        buf[offset + 2] = blocks & 0xFF
        buf[offset + 3] = (blocks >> 8) & 0xFF

        buf[offset + 4: offset + 4 + len(DIR_LIST_SPC27)] = DIR_LIST_SPC27

        name = file_name.upper()
        name = ''.join(c for c in name if 0x20 <= ord(c) <= 0x7E)
        name_bytes = name.encode('ascii')

        if blocks < 10:
            col = 8
        elif blocks < 100:
            col = 7
        else:
            col = 6

        buf[offset + col - 1] = 0x22
        for i, b in enumerate(name_bytes):
            buf[offset + col + i] = b
        buf[offset + col + 16] = 0x22

        buf[offset + col + 18] = ord('P')
        buf[offset + col + 19] = ord('R')
        buf[offset + col + 20] = ord('G')
        buf[offset + 31] = 0x00

        offset += LINE_SIZE

    # Footer line's link pointer stays 0x0000 (end-of-BASIC-program marker,
    # already zero from bytearray()) -- upstream's own comment here says
    # exactly this, but the code used to overwrite it with a real address
    # anyway (copy-paste from the per-file loop above, where a real link
    # pointer IS wanted). Real SDI firmware parses these link pointers
    # directly while streaming the catalog in (it has no IEC-level EOI
    # detection of its own for this read), and keeps asking for more bytes
    # past the end of the listing when it never sees the zero terminator --
    # confirmed live: that's what was producing a stray "B Integer out of
    # range" once the underlying byte stream ran out from under it.
    buf[offset + 2: offset + 2 + len(DIR_LIST_FOOTER)] = DIR_LIST_FOOTER
    return bytes(buf)


# ---------------------------------------------------------------------------
# Progress bar helper (unchanged from upstream)
# ---------------------------------------------------------------------------

def _print_progress(channel: int, total_size: int, pos: int, bytes_read: int,
                     is_last: bool, start_time: float) -> None:
    elapsed = time.monotonic() - start_time
    bps = int(pos / elapsed) if elapsed > 0 else 0

    if is_last:
        tot_kb = f"{total_size / 1024:.1f}"
        sys.stdout.write(f"\r\033[K[READ] Finished 100% {tot_kb}KB {bps}B/s\n")
    else:
        pct = int(pos / total_size * 100) if total_size > 0 else 100
        bar_width = 20
        filled = int(bar_width * pct / 100)
        bar = '█' * filled + '░' * (bar_width - filled)
        pos_kb = f"{pos / 1024:.1f}"
        tot_kb = f"{total_size / 1024:.1f}"
        sys.stdout.write(f"\r\033[K[READ] ch={channel} {bar} {pct}% {pos_kb}/{tot_kb}KB {bps}B/s")

    sys.stdout.flush()


# ---------------------------------------------------------------------------
# Command handlers (unchanged from upstream, port argument is now a socket)
# ---------------------------------------------------------------------------

def handle_open(sock: socket.socket, fields: list, channels: dict,
                 base_dir: str) -> None:
    if len(fields) < 3:
        print("[OPEN] malformed")
        send_line(sock, "E0")
        return

    try:
        channel = int(fields[1], 16)
    except ValueError:
        print("[OPEN] invalid channel")
        send_line(sock, "E0")
        return

    file_name = fields[2]
    print(f"[OPEN] ch={channel} file='{file_name}'", end='')

    if channel < 0 or channel > 15:
        print(" -> FAIL (invalid channel)")
        send_line(sock, "E1")
        return

    if channel in channels:
        try:
            channels[channel].close()
        except Exception:
            pass
        del channels[channel]

    if file_name in ('$', '$0', '$1'):
        if channel != 0:
            print(" -> FAIL ($ is read-only)")
            send_line(sock, "E2")
            return
        try:
            content = build_directory_listing(base_dir)
            channels[channel] = io.BytesIO(content)
            file_count = len(d64_image) if d64_image is not None else sum(
                1 for f in os.scandir(base_dir)
                if f.is_file() and f.name.lower().endswith('.prg')
            )
            print(f" -> OK (virtual, {file_count} file(s), {len(content)} bytes)")
            send_line(sock, "OK")
        except Exception as exc:
            print(f" -> FAIL ({exc})")
            send_line(sock, "E*")
        return

    # A mounted .d64 is inherently read-only here (this server only ever
    # parses it, never writes a modified image back) -- SAVE (channel
    # != 0) isn't something a .d64 mount supports, same as a real
    # write-protected disk.
    if d64_image is not None:
        if file_name in ('*', ':*', '0:*'):
            if not d64_image:
                print(" -> FAIL (no files on mounted disk)")
                send_line(sock, "E3")
                return
            file_name = d64_image[0][0]
            print(f" -> resolved to '{file_name}'", end='')

        match = next((c for n, c in d64_image if n.upper() == file_name.upper()), None)
        if channel != 0:
            print(" -> FAIL (mounted .d64 is read-only)")
            send_line(sock, "E2")
            return
        if match is None:
            print(" -> FAIL (not found on mounted disk)")
            send_line(sock, "E3")
            return
        channels[channel] = io.BytesIO(match)
        print(" -> OK")
        send_line(sock, "OK")
        return

    if file_name in ('*', ':*', '0:*'):
        prg_files = sorted(
            [f for f in os.scandir(base_dir)
             if f.is_file() and f.name.lower().endswith('.prg')
             and len(os.path.splitext(f.name)[0]) <= 16],
            key=lambda f: f.name.lower()
        )
        if not prg_files:
            print(" -> FAIL (no .prg files in base_dir)")
            send_line(sock, "E3")
            return
        file_name = prg_files[0].name
        print(f" -> resolved to '{file_name}'", end='')

    _, ext = os.path.splitext(file_name)
    if ext.lower() != '.prg':
        file_name += '.prg'

    file_path = os.path.join(base_dir, file_name)

    if not os.path.isfile(file_path):
        file_name_lower = file_name.lower()
        try:
            for entry in os.scandir(base_dir):
                if entry.is_file() and entry.name.lower() == file_name_lower:
                    file_path = entry.path
                    break
        except Exception:
            pass

    try:
        if channel == 0:
            if not os.path.isfile(file_path):
                print(" -> FAIL (not found)")
                send_line(sock, "E3")
                return
            channels[channel] = open(file_path, 'rb')
        else:
            channels[channel] = open(file_path, 'wb')

        print(" -> OK")
        send_line(sock, "OK")
    except PermissionError as exc:
        print(f" -> FAIL ({exc})")
        send_line(sock, "E2")
    except Exception as exc:
        print(f" -> FAIL ({exc})")
        send_line(sock, "E*")


def handle_close(sock: socket.socket, fields: list, channels: dict) -> None:
    if len(fields) < 2:
        print("[CLOSE] malformed")
        send_line(sock, "E0")
        return

    try:
        channel = int(fields[1], 16)
    except ValueError:
        print("[CLOSE] invalid channel")
        send_line(sock, "E0")
        return

    print(f"[CLOSE] ch={channel}", end='')

    if channel in channels:
        try:
            channels[channel].close()
            del channels[channel]
            print(" -> OK")
            send_line(sock, "OK")
        except Exception as exc:
            print(f" -> FAIL ({exc})")
            send_line(sock, "E*")
    else:
        print(" -> FAIL (not open)")
        send_line(sock, "E1")


def handle_read(sock: socket.socket, fields: list, channels: dict,
                 read_starts: dict) -> None:
    if len(fields) < 2:
        print("[READ] malformed")
        send_line(sock, "E0")
        return

    try:
        channel = int(fields[1], 16)
    except ValueError:
        print("[READ] invalid channel")
        send_line(sock, "E0")
        return

    read_size = CHUNK_SIZE
    if len(fields) >= 3:
        try:
            s = int(fields[2])
            if s > 0:
                read_size = s
        except ValueError:
            pass

    if channel not in channels:
        send_line(sock, "E1")
        return

    stream = channels[channel]

    if channel not in read_starts:
        read_starts[channel] = time.monotonic()

    try:
        data = stream.read(read_size)
        bytes_read = len(data)

        pos_after = stream.tell()
        try:
            total_size = os.fstat(stream.fileno()).st_size
        except Exception:
            total_size = stream.seek(0, 2)
            stream.seek(pos_after)

        remaining = total_size - pos_after
        is_last = (bytes_read < read_size) or (remaining == 0)
        flag = 'L' if is_last else 'M'

        send_line(sock, f"{flag},{bytes_read}")
        if bytes_read > 0:
            sock.sendall(data)

        if bytes_read == 0:
            return

        start_time = read_starts[channel]
        _print_progress(channel, total_size, pos_after, bytes_read, is_last, start_time)

        if is_last:
            read_starts.pop(channel, None)

    except Exception as exc:
        print(f"[READ] ERROR: {exc}")
        send_line(sock, "E*")


def handle_write(sock: socket.socket, fields: list, channels: dict) -> None:
    if len(fields) < 4:
        print("[WRITE] malformed")
        send_line(sock, "E0")
        return

    try:
        channel = int(fields[1], 16)
        byte_count = int(fields[2])
    except ValueError:
        print("[WRITE] malformed")
        send_line(sock, "E0")
        return

    eoi = (fields[3] == 'E')
    print(f"[WRITE] ch={channel} len={byte_count} eoi={eoi}", end='')

    if channel not in channels:
        print(" -> FAIL (not open)")
        if byte_count > 0:
            read_exact(sock, byte_count)
        send_line(sock, "E1")
        return

    try:
        data = read_exact(sock, byte_count)
        stream = channels[channel]
        if data:
            stream.write(data)
        if eoi:
            stream.flush()
        print(" -> OK")
        send_line(sock, "OK")
    except Exception as exc:
        print(f" -> FAIL ({exc})")
        send_line(sock, "E*")


def handle_list(sock: socket.socket, base_dir: str) -> None:
    print("[LIST]", end='')
    try:
        if d64_image is not None:
            for name, content in d64_image:
                send_line(sock, f"{name},{len(content)}")
            print(f" -> {len(d64_image)} file(s) (mounted .d64)")
            return
        files = list(os.scandir(base_dir))
        for f in files:
            if f.is_file():
                send_line(sock, f"{f.name},{f.stat().st_size}")
        count = sum(1 for f in files if f.is_file())
        print(f" -> {count} file(s)")
    except Exception as exc:
        print(f" -> FAIL ({exc})")
        send_line(sock, "E*")


def close_all_channels(channels: dict, read_starts: dict) -> None:
    for ch, stream in list(channels.items()):
        try:
            stream.close()
        except Exception:
            pass
    channels.clear()
    read_starts.clear()


# ---------------------------------------------------------------------------
# Main -- TCP accept loop (replaces upstream's serial open/reconnect loop).
# A fresh TCP connection already IS the reset event -- there's no physical
# adapter boot banner to sniff for, so this just resets state and sends the
# device-ID line immediately on accept(), where upstream only did that after
# recognizing "<<<...>>>" from the Arduino.
# ---------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description='C64 IEC File Server - TCP transport variant'
    )
    parser.add_argument('--tcp-port', default=9101, type=int, help='TCP port to listen on (default: 9101)')
    parser.add_argument('--basedir', default=os.path.expanduser('~'),
                         help='Base directory for .prg files (default: home dir)')
    parser.add_argument('--devid', default=8, type=int, help='Device ID (default: 8)')
    args = parser.parse_args()

    global d64_image
    base_dir = os.path.realpath(args.basedir)

    print(f"=== C64 IEC File Server (TCP) - Version {VERSION} ===")
    print(f"TCP Port : {args.tcp_port}")
    if os.path.isfile(base_dir) and base_dir.lower().endswith('.d64'):
        d64_image = load_d64_image(base_dir)
        print(f"Disk     : {base_dir} (mounted .d64, {len(d64_image)} file(s))")
    else:
        print(f"BaseDir  : {base_dir}")
    print(f"Device ID: {args.devid if 0 <= args.devid <= 15 else 'none'}")
    print()

    listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listen_sock.bind(("127.0.0.1", args.tcp_port))
    listen_sock.listen(1)
    print(f"Listening on 127.0.0.1:{args.tcp_port}. Waiting for JASE's SDI peripheral... (Ctrl+C to stop)")
    print()

    try:
        while True:
            sock, _ = listen_sock.accept()
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            print("*** Client connected -- treating as adapter reset.")

            channels: dict = {}
            read_starts: dict = {}
            close_all_channels(channels, read_starts)

            if 0 <= args.devid <= 15:
                send_line(sock, f"D:{args.devid:X}")
                print(f"*** Set device ID to {args.devid}")
            else:
                send_line(sock, "")
            print("*** All channels closed, buffer flushed. Ready.")
            print()

            try:
                while True:
                    line = read_line(sock)
                    if not line.strip():
                        continue

                    fields = line.split(',')
                    cmd = fields[0]

                    if cmd != 'R':
                        print(f">> {line}")

                    if cmd == 'O':
                        handle_open(sock, fields, channels, base_dir)
                    elif cmd == 'C':
                        handle_close(sock, fields, channels)
                    elif cmd == 'R':
                        handle_read(sock, fields, channels, read_starts)
                    elif cmd == 'W':
                        handle_write(sock, fields, channels)
                    elif cmd == 'L':
                        handle_list(sock, base_dir)
                    else:
                        print(f"[UNKNOWN] cmd='{cmd}' - ignoring")

            except ConnectionError:
                print("\n*** Client disconnected.\n")
            finally:
                close_all_channels(channels, read_starts)
                sock.close()

    except KeyboardInterrupt:
        print("\nShutting down...")
    finally:
        listen_sock.close()

    print("Goodbye.")


if __name__ == '__main__':
    main()
