#!/usr/bin/env python3
"""Decode raw --debug-instrtrace / "INSTRTRACE ON" output into readable Z80
disassembly + register lines.

Why this exists as a separate, offline step rather than decoding inside
JASE itself: the emulator already knows the real, correctly-aligned byte
boundaries for every executed instruction (that's the whole point of a
runtime trace over static disassembly -- no guessing which byte a CALL/JP
target actually lands on, the problem that repeatedly bit static analysis
of the real SDI ROM, see docs/VC1541/VC1541.md). Decoding those bytes to a
mnemonic is a separate, already-solved problem: this project already
validated z80dis for exactly that job while building the SDI ROM writeup.
Reusing it here avoids trusting a second, hand-rolled Z80 disassembler
inside the emulator itself.

Usage:
    pip install z80dis
    ./jase ... --debug-iotrace --debug-instrtrace 2>trace.log
    python3 tools/instrtrace_decode.py trace.log
    # or stream it live:
    ./jase ... --debug-instrtrace 2>&1 | python3 tools/instrtrace_decode.py -
"""
import re
import sys

try:
    import z80dis.z80 as z80
except ImportError:
    sys.exit("z80dis not installed -- pip install z80dis")

LINE_RE = re.compile(
    r"INSTRTRACE (?:ts=(?P<ts>[0-9.]+) )?pc=(?P<pc>[0-9a-f]{4}) bytes=(?P<bytes>[0-9a-f]{8}) "
    r"af=(?P<af>[0-9a-f]{4}) bc=(?P<bc>[0-9a-f]{4}) de=(?P<de>[0-9a-f]{4}) hl=(?P<hl>[0-9a-f]{4}) "
    r"ix=(?P<ix>[0-9a-f]{4}) iy=(?P<iy>[0-9a-f]{4}) sp=(?P<sp>[0-9a-f]{4}) "
    r"afx=(?P<afx>[0-9a-f]{4}) bcx=(?P<bcx>[0-9a-f]{4}) dex=(?P<dex>[0-9a-f]{4}) hlx=(?P<hlx>[0-9a-f]{4})"
)


def decode_line(line: str) -> str:
    m = LINE_RE.search(line)
    if not m:
        return line.rstrip("\n")

    pc = int(m.group("pc"), 16)
    raw = bytes.fromhex(m.group("bytes"))
    af, bc, de, hl, ix, iy, sp, afx, bcx, dex, hlx = (
        int(m.group(g), 16) for g in ("af", "bc", "de", "hl", "ix", "iy", "sp", "afx", "bcx", "dex", "hlx")
    )

    dec = z80.decode(raw, pc)
    mnemonic = z80.disasm(dec)
    used = raw[: max(dec.len, 1)]

    ts_prefix = f"[{float(m.group('ts')):12.6f}] " if m.group("ts") else ""
    return (
        f"{ts_prefix}{pc:04x}: {used.hex():<10} {mnemonic:<22} "
        f"AF={af:04x} BC={bc:04x} DE={de:04x} HL={hl:04x} "
        f"IX={ix:04x} IY={iy:04x} SP={sp:04x}  "
        f"AF'={afx:04x} BC'={bcx:04x} DE'={dex:04x} HL'={hlx:04x}"
    )


def main() -> None:
    path = sys.argv[1] if len(sys.argv) > 1 else "-"
    stream = sys.stdin if path == "-" else open(path, "r", errors="replace")
    try:
        for line in stream:
            if "INSTRTRACE" in line:
                print(decode_line(line))
    finally:
        if stream is not sys.stdin:
            stream.close()


if __name__ == "__main__":
    main()
