#!/usr/bin/env python3

"""
generuje  z pliku SCREEN$ przegranego z TOS na png

"""
import sys
from pathlib import Path
from PIL import Image

PALETTE = [
    [(0x00, 0x00, 0x00), (0x00, 0x00, 0xCD), (0xCD, 0x00, 0x00), (0xCD, 0x00, 0xCD),
     (0x00, 0xCD, 0x00), (0x00, 0xCD, 0xCD), (0xCD, 0xCD, 0x00), (0xCD, 0xCD, 0xCD)],
    [(0x00, 0x00, 0x00), (0x00, 0x00, 0xFF), (0xFF, 0x00, 0x00), (0xFF, 0x00, 0xFF),
     (0x00, 0xFF, 0x00), (0x00, 0xFF, 0xFF), (0xFF, 0xFF, 0x00), (0xFF, 0xFF, 0xFF)],
]

src = Path(sys.argv[1])
dst = Path(sys.argv[2]) if len(sys.argv) > 2 else src.with_suffix(".png")
raw = src.read_bytes()

if len(raw) in (6917, 6919) and raw[0] == 3:
    raw = raw[-6912:]
elif len(raw) != 6912:
    sys.exit(f"Nieprawidłowy rozmiar: {len(raw)} B")

bitmap, attrs = raw[:6144], raw[6144:]
img = Image.new("RGB", (256, 192))
pixels = img.load()

for y in range(192):
    row_offset = ((y >> 6) << 11) | ((y & 0x07) << 8) | (((y >> 3) & 0x07) << 5)
    attr_offset = (y >> 3) << 5
    for col in range(32):
        byte = bitmap[row_offset + col]
        attr = attrs[attr_offset + col]
        bright = (attr >> 6) & 1
        ink = PALETTE[bright][attr & 7]
        paper = PALETTE[bright][(attr >> 3) & 7]
        for bit in range(8):
            pixels[(col << 3) + bit, y] = ink if (byte >> (7 - bit)) & 1 else paper

img.save(dst)
print(f"Zapisano 1:1: {dst} (256x192 px)")
