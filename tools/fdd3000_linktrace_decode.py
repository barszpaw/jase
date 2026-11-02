#!/usr/bin/env python3
"""Decode raw "FDD3000 LINKTRACE ON" / --debug-fdd3000-linktrace output into a
readable transaction log of the ONE wire that connects the FDD3000 interface
(host side, port 0xEF) to the station's own Z80 (I/O block 1): two one-byte
latches, linkToStation_ (host->station, real hardware's "ti2fdd") and
linkFromStation_ (station->host, "fdd2ti"). See src/Fdd3000.cpp's
logLinkTrace() for the raw line format this reads, and Fdd3000.h's
setLinkTraceEnabled() comment for why this exists: the "why does Fuse work
and we don't" investigation (2026-11-02, see project memory) kept needing to
manually re-derive what the link bytes were doing from register snapshots
and hand-decoded DJNZ loops -- this makes that visible directly, one line
per byte actually crossing the wire, instead of inferred after the fact.

Usage:
    ./jase --fdd3000 ... --debug-fdd3000-linktrace --debug-port 9000 2>trace.log
    python3 tools/fdd3000_linktrace_decode.py trace.log

    # or live, straight off JASE's stderr (same --dedup rationale as
    # instrtrace_decode.py -- a stuck poll loop emits thousands of
    # identical lines; --dedup collapses consecutive (side,dir,value)
    # repeats into one line with a trailing "[xN]" run count):
    ./jase ... --debug-fdd3000-linktrace 2>&1 | python3 tools/fdd3000_linktrace_decode.py --dedup -

Each row shows, in order: time since the first logged byte, which side
originated it (HOST or STN) and whether it was a read or a write of ITS OWN
latch, the byte itself in hex and binary, and a short annotation -- the only
currently-DOCUMENTED bit meaning is bit 7 on a station-side IN (mirrors the
WD1772's own DRQ line, confirmed by disassembling the real station boot
ROM's polling loop, see Fdd3000.h). Everything else is left as raw bits
deliberately -- this tool decodes the WIRE, not the undocumented protocol
riding on it; don't bolt on a guessed bit-meaning table here without first
confirming it the same way DRQ-mirror was confirmed (real disassembly or a
live cross-check), per project memory's "verify before trusting" policy.
"""
import re
import sys

LINE_RE = re.compile(
    r"FDD3000LINK\s+ts=(?P<ts>[0-9.]+)\s+hostPC=(?P<hostpc>[0-9a-f]{4})\s+stnPC=(?P<stnpc>[0-9a-f]{4})\s+"
    r"side=(?P<side>\S+)\s+dir=(?P<dir>\S+)\s+val=(?P<val>[0-9a-f]{2})\s+bin=(?P<bin>[01]{8})"
)


def annotate(side: str, direction: str, val: int) -> str:
    notes = []
    # Only a station-side IN actually carries the live DRQ mirror (see
    # StationOps::inPort()'s case 1 -- it ORs wd_.drq() into bit 7 of
    # whatever the host last wrote). A station-side OUT or a host-side
    # access never touches that bit, so don't label it there.
    if side == "STN" and direction == "IN":
        notes.append(f"drq={(val >> 7) & 1}")
    if not notes:
        return ""
    return "  (" + ", ".join(notes) + ")"


def decode_line(line: str):
    """Returns a dict of parsed fields for a matched FDD3000LINK line, or
    None for anything else (e.g. INSTRTRACE/IOTRACE lines interleaved in
    the same captured stream)."""
    m = LINE_RE.search(line)
    if not m:
        return None
    return {
        "ts": float(m.group("ts")),
        "hostpc": m.group("hostpc"),
        "stnpc": m.group("stnpc"),
        "side": m.group("side").strip(),
        "dir": m.group("dir").strip(),
        "val": int(m.group("val"), 16),
        "bin": m.group("bin"),
    }


def format_row(rec: dict, t0: float) -> str:
    who = "HOST" if rec["side"] == "HOST" else "STN "
    verb = "read " if rec["dir"] == "IN" else "wrote"
    return (
        f"[{rec['ts'] - t0:9.6f}] hostPC={rec['hostpc']} stnPC={rec['stnpc']}  "
        f"{who} {verb} 0x{rec['val']:02x} ({rec['bin']})"
        f"{annotate(rec['side'], rec['dir'], rec['val'])}"
    )


def main() -> None:
    args = sys.argv[1:]
    dedup = "--dedup" in args
    args = [a for a in args if a != "--dedup"]
    path = args[0] if args else "-"
    stream = sys.stdin if path == "-" else open(path, "r", errors="replace")

    t0 = None
    pending_key = None
    pending_rec = None
    run_len = 0

    def flush():
        if pending_rec is None:
            return
        suffix = f"  [x{run_len}]" if run_len > 1 else ""
        print(f"{format_row(pending_rec, t0)}{suffix}", flush=True)

    try:
        for line in stream:
            if "FDD3000LINK" not in line:
                continue
            rec = decode_line(line)
            if rec is None:
                continue
            if t0 is None:
                t0 = rec["ts"]
            if not dedup:
                print(format_row(rec, t0), flush=True)
                continue
            key = (rec["side"], rec["dir"], rec["val"])
            if key == pending_key:
                run_len += 1
                continue
            flush()
            pending_key, pending_rec, run_len = key, rec, 1
        if dedup:
            flush()
    finally:
        if stream is not sys.stdin:
            stream.close()


if __name__ == "__main__":
    main()
