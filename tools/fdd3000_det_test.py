#!/usr/bin/env python3
"""Deterministic FDD3000 test harness.

JASE's normal main loop (main.cpp's update()) paces itself to real
wall-clock time (SDL_Delay targeting 50Hz) -- so a test that drives the
emulator by sleeping N real seconds and then sampling state is NOT
reproducible: the number of host/station instructions actually executed
in that window depends on real machine load at the moment. This was
found live on 2026-11-04 (see project memory project_jase_fdd3000.md):
three identical wall-clock-timed runs of the same build produced three
different outcomes.

STEP <n> over the debug protocol (DebugServer, -> MinZX::step()) is NOT
wall-clock paced -- it's a plain instruction-count loop with no
SDL_Delay, calling every peripheral's onInstructionFetch() exactly the
same way the real-time loop does (see minzx.cpp). Driving tests through
STEP instead of time.sleep() makes them fully reproducible: same STEP
count -> byte-identical final state, every time (verified: 3 reps at
10,000,000 steps gave identical FDD3000 status/REGS/link-histogram).

Usage:
    python3 tools/fdd3000_det_test.py [steps] [reps] [base_port] [--rom PATH]

Run from the JASE repo root (or anywhere -- it cd's into the repo via
JASE_DIR below). Each rep launches its own jase process headless
(--debug-port only, no need for a visible window) and tears it down
after reading state, so reps never interfere with each other.
"""
import socket, subprocess, sys, time, os, signal, re, argparse

JASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# NOT under tools/out/ -- that's a stray pre-existing FILE in this repo, not
# a directory. Per-rep .log/.err go to a tmp dir by default instead.
DEFAULT_OUTDIR = "/tmp/jase_fdd3000_det_test"


def cmd(s, c, timeout=20.0):
    s.sendall((c + "\n").encode())
    data = b""
    s.settimeout(timeout)
    try:
        while True:
            chunk = s.recv(65536)
            if not chunk:
                break
            data += chunk
            if b"\n" in data:
                break
    except socket.timeout:
        pass
    return data.decode(errors="replace").strip()


def run_once(port, steps, rom, station_rom, disk, tag, outdir, extra_args=None):
    os.makedirs(outdir, exist_ok=True)
    log_path = os.path.join(outdir, f"{tag}.log")
    err_path = os.path.join(outdir, f"{tag}.err")
    for p in (log_path, err_path):
        if os.path.exists(p):
            os.remove(p)
    args = [
        "./jase", "--machine", "48k", "--fdd3000",
        "--fdd3000-rom", rom,
        "--fdd3000-station-rom", station_rom,
        "--fdd3000-disk-a", disk,
        "--debug-port", str(port),
        "--debug-fdd3000-linktrace",
    ]
    if extra_args:
        args += extra_args
    with open(log_path, "wb") as lf, open(err_path, "wb") as ef:
        proc = subprocess.Popen(args, cwd=JASE_DIR, stdout=lf, stderr=ef)
    try:
        for _ in range(50):
            try:
                s = socket.create_connection(("127.0.0.1", port), timeout=1)
                break
            except OSError:
                time.sleep(0.1)
        else:
            raise RuntimeError("debug port never came up")

        cmd(s, "FDD3000 RESET")
        cmd(s, "RESET")
        t0 = time.time()
        cmd(s, f"STEP {steps}", timeout=120.0)
        elapsed = time.time() - t0
        fdd3000_status = cmd(s, "FDD3000")
        regs = cmd(s, "REGS")
        s.close()
    finally:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()

    time.sleep(0.3)  # let stdio buffers flush to disk after SIGTERM
    histo_stn_out, histo_host_in = {}, {}
    try:
        with open(err_path, "rb") as f:
            for line in f:
                line = line.decode(errors="replace")
                m = re.search(r"side=(STN |HOST) dir=(IN |OUT) val=([0-9a-f]{2})", line)
                if not m:
                    continue
                side, direction, val = m.group(1), m.group(2), m.group(3)
                if side == "STN " and direction == "OUT":
                    histo_stn_out[val] = histo_stn_out.get(val, 0) + 1
                elif side == "HOST" and direction == "IN ":
                    histo_host_in[val] = histo_host_in.get(val, 0) + 1
    except FileNotFoundError:
        pass

    return {
        "elapsed_wall_s": round(elapsed, 2),
        "fdd3000_status": fdd3000_status,
        "regs": regs,
        "stn_out_histo": dict(sorted(histo_stn_out.items(), key=lambda kv: -kv[1])),
        "host_in_histo": dict(sorted(histo_host_in.items(), key=lambda kv: -kv[1])),
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("steps", nargs="?", type=int, default=10_000_000, help="host instructions per rep (default 10M)")
    ap.add_argument("reps", nargs="?", type=int, default=3, help="number of repetitions (default 3)")
    ap.add_argument("base_port", nargs="?", type=int, default=9970, help="first debug port to use (default 9970)")
    ap.add_argument("--rom", default="/usr/share/fuse/fdd3stavi.rom", help="interface ROM dump")
    ap.add_argument("--station-rom", default="fdd3boot.rom", help="station boot ROM dump")
    ap.add_argument("--disk", default="/home/pawelb/src/Fuse_FDD3000/disks/TDu/TDU-01.dsk", help="disk image for drive A")
    ap.add_argument("--outdir", default=DEFAULT_OUTDIR, help="where to write per-rep .log/.err files")
    args = ap.parse_args()

    results = []
    for i in range(args.reps):
        r = run_once(args.base_port + i, args.steps, args.rom, args.station_rom, args.disk,
                      tag=f"det_rep{i+1}", outdir=args.outdir)
        results.append(r)
        print(f"--- REP {i+1} (STEP {args.steps}) ---")
        print("  wall time to execute STEP:", r["elapsed_wall_s"], "s")
        print("  FDD3000:", r["fdd3000_status"])
        print("  REGS:", r["regs"])
        print("  STN OUT histo:", r["stn_out_histo"])
        print("  HOST IN histo:", r["host_in_histo"])

    statuses = set(r["fdd3000_status"] for r in results)
    regs_set = set(r["regs"] for r in results)
    print()
    print("=== DETERMINISM CHECK ===")
    print(f"distinct FDD3000 status strings across {args.reps} reps: {len(statuses)}")
    print(f"distinct REGS strings across {args.reps} reps: {len(regs_set)}")
    if len(statuses) == 1 and len(regs_set) == 1:
        print("DETERMINISTIC: all reps produced identical final state.")
    else:
        print("NON-DETERMINISTIC: reps differ despite identical STEP count! "
              "(if this ever happens, something genuinely non-deterministic "
              "is happening inside the emulated machine itself, e.g. an "
              "uninitialized-read dependency -- not just test-harness timing.)")


if __name__ == "__main__":
    main()
