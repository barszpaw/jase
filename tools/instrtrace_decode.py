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
    # or stream it live, straight from JASE's stderr, with no intermediate
    # raw-trace file ever touching disk (every print() below is flush=True
    # specifically so this stays genuinely live -- piping through plain
    # print() with no flush left stdout fully block-buffered once it's not
    # a tty, so a long hang showed nothing at all until the pipe closed;
    # confirmed via a synthetic slow-feed test before fixing):
    ./jase ... --debug-instrtrace 2>&1 | python3 tools/instrtrace_decode.py --dedup -

    # A long trace revisits the same handful of addresses thousands of
    # times (tight delay/poll loops like a DJNZ spin or an IN/OUT retry) --
    # --dedup collapses consecutive lines that land on the SAME pc into one,
    # with a trailing "[xN]" run count, keeping the first line's (freshest
    # entry-to-the-loop) register snapshot. Confirmed on a real 67k-line
    # SDI trace: 66904 decoded lines -> 24107 with --dedup, no information
    # lost about WHICH addresses ran or in what order, only about exactly
    # how many times a register changed mid-spin. This replaces the earlier
    # ad-hoc tools/dedup_instrtrace_decode.py (same idea, but that one keyed
    # on the full formatted line including opcode bytes/mnemonic text parsed
    # back out of already-decoded output, so it silently dropped register
    # state entirely instead of just collapsing runs of it -- keying on pc
    # alone, before formatting, is simpler and strictly more informative):
    python3 tools/instrtrace_decode.py --dedup trace.log
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


def decode_line(line: str):
    """Returns (pc, formatted) for a matched INSTRTRACE line, or (None,
    formatted) for anything else (e.g. IOTRACE/IECTRACE lines interleaved
    in the same stream) -- None as a dedup key never matches itself across
    lines, so these always pass through --dedup unchanged."""
    m = LINE_RE.search(line)
    if not m:
        return None, line.rstrip("\n")

    pc = int(m.group("pc"), 16)
    raw = bytes.fromhex(m.group("bytes"))
    af, bc, de, hl, ix, iy, sp, afx, bcx, dex, hlx = (
        int(m.group(g), 16) for g in ("af", "bc", "de", "hl", "ix", "iy", "sp", "afx", "bcx", "dex", "hlx")
    )

    dec = z80.decode(raw, pc)
    mnemonic = z80.disasm(dec)
    used = raw[: max(dec.len, 1)]

    ts_prefix = f"[{float(m.group('ts')):12.6f}] " if m.group("ts") else ""
    formatted = (
        f"{ts_prefix}{pc:04x}: {used.hex():<10} {mnemonic:<22} "
        f"AF={af:04x} BC={bc:04x} DE={de:04x} HL={hl:04x} "
        f"IX={ix:04x} IY={iy:04x} SP={sp:04x}  "
        f"AF'={afx:04x} BC'={bcx:04x} DE'={dex:04x} HL'={hlx:04x}"
    )
    return pc, formatted


def main() -> None:
    args = sys.argv[1:]
    dedup = "--dedup" in args
    args = [a for a in args if a != "--dedup"]
    path = args[0] if args else "-"
    stream = sys.stdin if path == "-" else open(path, "r", errors="replace")

    pending_pc = None
    pending_line = None
    run_len = 0

    def flush():
        if pending_line is None:
            return
        suffix = f"  [x{run_len}]" if run_len > 1 else ""
        print(f"{pending_line}{suffix}", flush=True)

    try:
        for line in stream:
            if "INSTRTRACE" not in line:
                continue
            pc, formatted = decode_line(line)
            if not dedup:
                print(formatted, flush=True)
                continue
            if pc is not None and pc == pending_pc:
                run_len += 1
                continue
            flush()
            pending_pc, pending_line, run_len = pc, formatted, 1
        if dedup:
            flush()
    finally:
        if stream is not sys.stdin:
            stream.close()


if __name__ == "__main__":
    main()
