#!/usr/bin/env python3
"""Dump JASE's ZX Spectrum pixel memory ($4000-$57FF) over the debug
protocol and render it as monochrome PNG, deliberately ignoring the
attribute memory ($5800-$5AFF) entirely.

Why: SDI debugging kept producing a screen that *looked* black (border=0,
attribute bytes all 0x00 -- real INK 0/PAPER 0, not a rendering bug), which
made SCREENSHOT useless for checking whether text was actually drawn. Pixel
bits are set/cleared independently of color -- reading them directly and
rendering as plain black-on-white sidesteps the whole INK/PAPER question,
and does it as a pure read with zero side effects on emulator state (won't
perturb a timing-sensitive test, unlike POKEing attributes to force a
visible color).

Usage:
    python3 screen_raw_dump.py [--port 9800] [--out /path/to/out.png]
"""
import argparse
import socket
import time

from PIL import Image


def dbg(sock: socket.socket, cmd: str, wait: float = 0.3) -> str:
    sock.sendall((cmd + "\n").encode())
    time.sleep(wait)
    buf = b""
    sock.settimeout(5.0)
    try:
        while not buf.endswith(b"\n"):
            chunk = sock.recv(8192)
            if not chunk:
                break
            buf += chunk
    except socket.timeout:
        pass
    return buf.decode(errors="replace").strip()


def mem_read(sock: socket.socket, addr: int, length: int) -> bytes:
    """MEM caps at 4096 bytes/call -- chunk transparently."""
    out = bytearray()
    off = 0
    while off < length:
        n = min(4096, length - off)
        resp = dbg(sock, f"MEM {addr + off:04x} {n}")
        if not resp.startswith("OK "):
            raise RuntimeError(f"MEM read failed at {addr+off:04x}: {resp}")
        out += bytes.fromhex(resp[3:])
        off += n
    return bytes(out)


def decode_spectrum_bitmap(data: bytes) -> Image.Image:
    """Standard ZX Spectrum non-linear 256x192 1bpp layout -> PIL Image.
    Set bit = ink (rendered black), clear bit = paper (rendered white) --
    i.e. this ignores color entirely, just "was this pixel drawn or not"."""
    img = Image.new("1", (256, 192), color=1)  # 1 = white
    px = img.load()
    for third in range(3):
        for line7 in range(8):
            for row in range(8):
                y = third * 64 + row * 8 + line7
                row_base = third * 2048 + row * 32 + line7 * 256
                for col in range(32):
                    byte = data[row_base + col]
                    for bit in range(8):
                        x = col * 8 + bit
                        set_ = (byte >> (7 - bit)) & 1
                        px[x, y] = 0 if set_ else 1  # set bit -> black
    return img


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=9800)
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--out", default="/tmp/screen_raw.png")
    args = ap.parse_args()

    s = socket.create_connection((args.host, args.port))
    data = mem_read(s, 0x4000, 6144)
    s.close()

    img = decode_spectrum_bitmap(data)
    img = img.resize((512, 384), Image.NEAREST)  # 2x for easier viewing
    img.save(args.out)
    print(f"Saved {args.out} ({sum(data)} nonzero-byte-sum of {len(data)} bitmap bytes -- 0 means truly blank screen)")


if __name__ == "__main__":
    main()
