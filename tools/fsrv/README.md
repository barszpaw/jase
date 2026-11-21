# FSRV for FDD3000 — CP/M file server over the real WD2123 port

A CP/M 2.2 file-transfer server for the FDD3000 station, plus a PC-side
Python client. The protocol is ported from
[tapiau/bosman-8](https://github.com/tapiau/bosman-8)'s own
`software/tools/fsrv/fsrv.c` (v2.4) and its companion PC client
`software/tools/fsrv/fsrv` — read directly from that repository, not
guessed. The **only** change from the original is the transport: bosman-8's
server talks to a real Zilog SIO-B UART; this one talks to the FDD3000
station's own WD2123 (`src/Wd2123.h`) CHA channel instead, since that's the
real serial port this project actually has. Every BDOS/FCB/CRC/directory-walk
routine is untouched standard CP/M 2.2 logic — there was nothing else
hardware-specific left to port.

User-directed design constraint: this deliberately does **not** go through
`src/MonitorLink.h` ("MonitorLink to tylko do konsoli to nie jest łącze
szeregowe" — MonitorLink is console-only, not a real serial port). It talks
to the real WD2123 ports (`#80`/`#81` CHA) exactly as real firmware would.

## Files

| File | What |
|------|------|
| `fsrv_fdd3000.asm` | CP/M-side server, hand-written Z80 (see its own header comment for why: no z88dk prebuilt Linux binary, building it from source was a disproportionate detour for one small program) |
| `fsrv_fdd3000_client.py` | PC-side client — same protocol logic as bosman-8's own `fsrv` script, but over a raw TCP socket instead of pyserial |

## Protocol (as actually implemented, not as bosman-8's own README describes)

Bosman-8's own `README.md` protocol section shows `" OK\r\n"`/`" ERR\r\n"`
text as part of what the PC receives. That's **not what the real code does**
(verified by reading `fsrv.c` and the real `fsrv` client together): that
text goes to BDOS `C_WRITESTR`, i.e. the server's *local* console, never
over the wire. The PC client only ever reads the single `'K'`/`'E'` status
byte. The actual wire protocol, command by command:

```
GET   PC -> 'G' + name 0x00
      SRV -> 'K' | 'E'                       (if 'E', done)
      SRV -> rlo rhi                          (record count, little-endian)
      for each record:
        SRV -> 128 data bytes + 1 XOR-CRC8 byte
        PC  -> 'K' (ack) | 'E' (abort)         -- strict per-record lockstep
      SRV -> 'K'                               (final ack, file closed)

PUT   PC -> 'P' + name 0x00 + rlo + rhi
      SRV -> 'K' | 'E'                       (initial ack: file created)
      for each record:
        PC  -> 128 data bytes + 1 XOR-CRC8 byte
        SRV -> 'K' (ack, written) | 'E' (CRC mismatch or write error, aborts)
      SRV -> 'K' | 'E'                         (final ack: file closed)

DIR   PC -> 'D' + mask 0x00                   ('*' wildcards allowed)
      SRV -> [11 bytes: name(8)+ext(3), attribute bits masked off] * N
      SRV -> 11 zero bytes                     (end of listing)

QUIT  PC -> 'Q'
      SRV -> 'K'                               (then the server exits)
```

No `SPEED` command: bosman-8's own `fsrv.c` v2.4 doesn't dispatch it either
(only `D`/`G`/`P`/`Q` in its main loop), and WD2123's own byte-level TCP
transport has no real baud rate to change in the first place (see
`src/Wd2123.h`'s own class comment) — nothing on either end for it to
control.

## Build

```bash
cd tools/fsrv
z80asm -o FSRV.COM fsrv_fdd3000.asm
```

Needs the `z80asm` Debian/Ubuntu package (`sudo apt install z80asm`) —
**not** z88dk's own same-named tool, different syntax. `FSRV.COM` is a
build artifact, not checked in.

## Running it

1. Put `FSRV.COM` onto a real `.dsk` image — easiest via
   [`tools/dsk_browser`](../dsk_browser.cpp)'s F5 (local → CP/M copy), or
   the `CpmFs::writeFile()` API directly.
2. Boot the station with that disk and the WD2123 CHA test TCP port open:
   ```bash
   ./fdd3000_station/fdd3000_station --boot-rom fdd3boot.rom \
       --disk-a your.dsk --monitor-port 9998 --serial1-port 9700
   ```
   (or the in-process `jase`'s own `--fdd3000-serial1-port`, same port).
3. Connect to port 9998 (e.g. telnet, or `src/MonitorLink.h`'s own protocol
   over a raw socket) and type `FSRV` at the `A>` prompt.
4. From the PC:
   ```bash
   python3 tools/fsrv/fsrv_fdd3000_client.py -P 9700 dir
   python3 tools/fsrv/fsrv_fdd3000_client.py -P 9700 get PIP.COM pip_local.com
   python3 tools/fsrv/fsrv_fdd3000_client.py -P 9700 put local.txt REMOTE.TXT
   python3 tools/fsrv/fsrv_fdd3000_client.py -P 9700 quit
   ```

## Verified (2026-11-21), live, end-to-end

- `DIR` lists real files, including masks (`*.COM` correctly excludes
  non-`.COM` files via the ported `build_fcb()`'s wildcard handling).
- `GET PIP.COM` → byte-for-byte identical to `CpmFs::readFile()`'s own
  independently-verified output (per-record CRC8 path exercised for real,
  58 records).
- `PUT` a local text file → confirmed visible and byte-correct via a
  completely independent path: real CP/M's own `TYPE` command, after `QUIT`
  returned the station to its CCP prompt.
- `QUIT` → station genuinely returns to a normal `A>` prompt (see the fixed
  bug below).

### Bug found and fixed during this verification

The first working version did `LD SP,stack_top` at entry, pointing SP at a
small local stack area instead of trusting CCP's own stack. That's harmless
*while* the program runs (its own pushes/pops stay balanced), but the
final `RET` then popped two zero bytes from that uninitialized area instead
of CCP's real return address — landing at address `0000h` (the warm-boot
vector) rather than back in CCP. Live symptom: after `QUIT`, the station
kept running — BDOS/BIOS silently re-reading tracks 0–1 to reload CCP
(a real warm boot), not a normal "returned to A>" console. Fixed by simply
not touching SP at all (standard convention for a small .COM utility with
modest stack usage) — confirmed live afterward: `QUIT` now returns a clean
`A>` prompt every time.

### Known caveat, not a bug in this port

Requesting a drive letter that isn't actually mounted (e.g. `DIR B:*.*`
when only drive A is attached) triggers a real `Bdos Err On B: Bad Sector`
on the station's own CBIOS/BDOS, and appears to leave the system's current
disk pointed at the now-broken B: afterward — subsequent commands (even
plain `A:` ones) fail the same way until a `RESET`. This reproduces with
plain CCP commands too (not FSRV-specific), so it's a property of this
station's BDOS/BIOS handling of an unmounted drive, inherited unchanged
from the same explicit-drive FCB mechanism bosman-8's own `fsrv.c` uses —
not something this port introduced or could meaningfully guard against
without deviating from the original protocol. Don't request drive letters
that aren't actually attached.
