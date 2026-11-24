# fdd3000_station — standalone FDD3000 station process

Runs the Timex FDD 3000 station computer (Z80A + 64K RAM + WD1772 FDC,
see `../src/FddStation.h`) as its own, real-time-paced OS process,
without a Spectrum host or SDL at all.

Build: `make station` from the repo root (produces this directory's own
`fdd3000_station` binary).

## Quick start: standalone station + headless CP/M console over TCP

This is the fastest way to get a real CP/M prompt with zero ZX Spectrum
code and zero screen involved — the station runs CP/M on its own, real
Z80 core, and `src/MonitorLink.h` reimplements the real Monitor
Emulator's host-link protocol so a plain `telnet` session can act as its
console directly:

```bash
./fdd3000_station/fdd3000_station \
  --boot-rom roms/fdd3boot.rom \
  --disk-a disks/CPM-Boot_Disk_v2_2.dsk \
  --link-port 9600 --debug-port 9601 \
  --monitor-port 19905
```

In another terminal:

```bash
telnet 127.0.0.1 19905
```

You should land directly on a real CP/M banner and `A>` prompt — no TOS
boot, no Spectrum involved at all. Try `DIR`.

**Note**: `--monitor-port` and `--link-port` share the same link latch —
enabling `--monitor-port` disables this process's own `--link-port`
bridge handling (so a remote `jase --fdd3000-bridge` host and a local
`telnet` console never fight over the same byte). If you need the real
bridge for a remote host, drop `--monitor-port`.

## Everything else (CH1/CH2 serial ports, the in-process alternative,
## debug commands, diagnostics)

See [`docs/FDD3000/HEADLESS_CPM.md`](../docs/FDD3000/HEADLESS_CPM.md)
for the full picture — three independent ways to reach the same
`FddStation` over TCP, or run `./fdd3000_station --help` for the
complete command-channel reference.
