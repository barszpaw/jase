# Converts bitluni/ESP32Lib's Font6x8.h (one byte per pixel, 0/255) into
# the bit-packed table JASE's OsdMenu actually compiles in. Input file is
# the verbatim copy kept alongside this script (Font6x8_bitluni.h) -- re-
# fetch it from https://github.com/bitluni/ESP32Lib/blob/master/src/
# Ressources/Font6x8.h if it's ever missing.
import os
import re

script_dir = os.path.dirname(__file__)
text = open(os.path.join(script_dir, "Font6x8_bitluni.h")).read()
# Pull just the Font6x8Pixels[] array body
m = re.search(r'Font6x8Pixels\[\]\s*=\s*\{(.*?)\};', text, re.S)
body = m.group(1)
nums = [int(x) for x in re.findall(r'-?\d+', body)]
assert len(nums) == 96*6*8, len(nums)

CHAR_W, CHAR_H, FIRST, COUNT = 6, 8, 32, 96

lines = []
lines.append("// Auto-generated 6x8 bitmap font, printable ASCII 0x20-0x7F.")
lines.append("//")
lines.append("// Pixel data is bitluni's Font6x8Pixels from bitluni/ESP32Lib")
lines.append("// (https://github.com/bitluni/ESP32Lib/blob/master/src/Ressources/Font6x8.h),")
lines.append("// the same font pico-spec's own OSD (OSDMenu.cpp/OSDMain.cpp,")
lines.append("// `Font6x8`) draws its menu with -- used here on Pawel's request to")
lines.append("// match that look. Converted from bitluni's one-byte-per-pixel (0/255)")
lines.append("// layout into a bit-packed [glyph][row] table (bits 5..0 = pixels")
lines.append("// left-to-right, bit5 = leftmost) via tools/font/conv_bitluni_font.py --")
lines.append("// no pixel data changed, just repacked.")
lines.append("//")
lines.append("// LICENSE NOTE: unlike the rest of JASE (MIT, see LICENSE), this")
lines.append("// pixel data is bitluni's own work, published under Creative Commons")
lines.append("// Attribution-ShareAlike 4.0 (https://creativecommons.org/licenses/by-sa/4.0/).")
lines.append("// Attribution: font by bitluni (https://github.com/bitluni), 2019.")
lines.append("// CC BY-SA's ShareAlike clause means THIS FILE (and only this file --")
lines.append("// a font table, not the code that reads it) carries that license,")
lines.append("// not JASE's MIT one; redistributing it has to keep this notice.")
lines.append("#pragma once")
lines.append("#include <cstdint>")
lines.append(f"static const int kOsdFontFirstChar = {FIRST};")
lines.append(f"static const int kOsdFontCharCount = {COUNT};")
lines.append(f"static const uint8_t kOsdFont6x8[{COUNT}][{CHAR_H}] = {{")

idx = 0
for g in range(COUNT):
    rows = []
    for row in range(CHAR_H):
        bits = 0
        for col in range(CHAR_W):
            v = nums[idx]; idx += 1
            if v > 128:
                bits |= 1 << (CHAR_W - 1 - col)
        rows.append(bits)
    code = FIRST + g
    ch = chr(code) if 32 <= code < 127 else '?'
    lines.append("    { " + ", ".join(f"0x{b:02X}" for b in rows) + " }, // 0x%02X '%s'" % (code, ch))
lines.append("};")

out_path = os.path.join(script_dir, "..", "..", "src", "OsdFont.h")
with open(out_path, "w") as f:
    f.write("\n".join(lines) + "\n")
print("wrote", COUNT, "glyphs to", out_path)
