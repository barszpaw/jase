# `settings.json` — key/value reference

`settings.json` (repo root) is a JSON template holding every JASE CLI
parameter (`src/main.cpp`), grouped the same way as `./jase --help`'s own
sections. Load it with:

```
./jase --settings settings.json
```

This expands the file into the exact same flags `--help` documents
(`settingsToArgs()` in `src/main.cpp` — not a separate/parallel parsing
path, so this file can never drift out of sync with real flag behavior),
spliced in **ahead of** the real command-line arguments. Every parsing
loop in `main.cpp` is a left-to-right scan that keeps overwriting value
flags and no-ops on a repeated boolean flag, so **an explicit flag on the
real command line always wins** over the matching `settings.json` value:

```
./jase --settings settings.json --dandanator-rom games/other.rom
```

loads the settings file's `dandanator.rom` first, then immediately
overrides it with `games/other.rom`.

`null` (or a field left out entirely) means "flag omitted" — JASE falls
back to its own built-in default for that flag. `false` for a boolean
means the same thing. Top-level sections (`"machine"`, `"media"`, ...)
can be omitted entirely if you don't need anything from them.

Requires `nlohmann-json3-dev` (`sudo apt install nlohmann-json3-dev`) to
build — header-only, see the `Makefile`'s `INCLUDES` line.

## `machine`

Picked **before** anything else boots — `machine` selects which ROM
loads, so changing it mid-session isn't possible.

| Key | Type | CLI flag | Values / notes |
|---|---|---|---|
| `machine` | string | `--machine NAME` | `"48k"` (default), `"tc2048"`, or `"128k"` |
| `rom` | string path | `--rom PATH` | Overrides the ROM `machine` picked (e.g. a standalone diagnostic ROM that replaces it outright) |
| `turbosound` | bool | `--turbosound` | Two AY-3-8912 chips instead of one (NedoPC convention: write `0xFF`/`0xFE` to `0xFFFD` to pick chip A/B). Replaces the single-chip AY outright, not an addition |
| `timexHires` | bool | `--timex-hires` | Enables the Timex SCLD video mode register (port `0xFF`, modes 0-2/6) on **any** `machine`, not just `tc2048` |
| `pcKeys` | bool | `--pc-keys` | Type Spectrum punctuation directly from the matching PC key via Symbol Shift, instead of holding Symbol Shift yourself. Off by default (unmodified keyboard matrix matches real hardware) |

## `media`

Tape/snapshot/cartridge content to stage or load at startup.

| Key | Type | CLI flag | Values / notes |
|---|---|---|---|
| `tape` | string path | `--tape PATH` | Stage a `.tap`/`.tzx`; `LOAD ""` loads it normally |
| `loadTape` | string path | `--load-tape PATH` | Stage a `.tap`/`.tzx` **and** resume instantly via the bundled mid-LOAD snapshot. 48K ROM only — doesn't work on `tc2048`/`128k`, use `tape` there instead |
| `loadSna` | string path | `--load-sna PATH` | Load a `.sna` snapshot directly |
| `loadZ80` | string path | `--load-z80 PATH` | Load a `.z80` snapshot directly (v1/v2/v3, compressed or not; 48K/128K only — the snapshot's machine must match `machine.machine`, since that can't change after startup) |
| `tapeFastload` | bool | `--tape-fastload` | Restore the old instant-delivery shortcut at `PC==0x0556` on top of the real tape engine. Default off — the pure pulse engine alone has to get a tape through |
| `dockCartridge` | string path | `--dock-cartridge PATH` | Load a TS2068 `.dck` cartridge into the DOCK bank pool (`tc2048` only; doesn't page it in — pair with `debugging.out` to write port `0xF4` and `debugging.pc`) |

## `peripherals`

Optional hardware add-ons, each off by default. A `*Rom`/`*Disk`/
`bridgeHost`+`bridgePort` value implies enabling its own peripheral even
if `enabled` is left `false` (matches the real CLI: e.g. `--dandanator-rom`
implies `--dandanator`).

### `zifi`

ESP-01/Wemos D1 Wi-Fi bridge (real hardware over TCP via `socat`, or
`tools/virtual_esp01`'s standalone daemon).

| Key | Type | CLI equivalent |
|---|---|---|
| `host` | string | first half of `--zifi HOST:PORT` |
| `port` | number | second half of `--zifi HOST:PORT` |

### `multiface1`

Multiface ONE stand-in. Red button = Alt+F10 window hotkey / NMI debug
command; ports `0x9F`/`0x1F` page its 8K ROM+RAM in/out over
`0x0000`-`0x3FFF`.

| Key | Type | CLI flag |
|---|---|---|
| `enabled` | bool | `--multiface1` |
| `rom` | string path | `--multiface1-rom PATH` — a real, legally-dumped 8192-byte ROM. Copyrighted like the system ROMs, bring your own, none bundled |

### `divide`

DivIDE stand-in (esxDOS over a minimal ATA/IDE register interface).
Needs both `rom` and `disk` to do anything.

| Key | Type | CLI flag |
|---|---|---|
| `enabled` | bool | `--divide` |
| `rom` | string path | `--divide-rom PATH` — esxDOS ROM, exactly 8192 bytes (e.g. `ESXIDE.BIN`) |
| `disk` | string path | `--divide-disk PATH` — raw sector image or standard `.hdf` (RS-IDE signature), sniffed automatically |

### `dandanator`

ZX Dandanator! Mini stand-in — see `docs/Dandanator/`. 32×16K banks over
`0x0000`-`0x3FFF`, button = F9 window hotkey (also resets the Z80, like
real hardware).

| Key | Type | CLI flag |
|---|---|---|
| `enabled` | bool | `--dandanator` |
| `rom` | string path | `--dandanator-rom PATH` — a real 512KB romset dump produced by the Dandanator PC/Mac tool. Copyrighted game content, bring your own, none bundled. Without it, a tiny built-in placeholder occupies bank 0 only — no actual menu |

### `multiply`

Multiply stand-in — Arduino+microSD addon for the Dandanator (see
`docs/Dandanator/`). Rides on `dandanator`'s own command 52; needs it to
do anything. Does nothing on its own without a bridge connected — the
real file-browsing/loading logic lives in
`tools/multiply_bridge/multiply_bridge.py`, not in JASE itself.

| Key | Type | CLI equivalent |
|---|---|---|
| `enabled` | bool | `--multiply` |
| `bridgeHost` | string | first half of `--multiply-bridge HOST:PORT` |
| `bridgePort` | number | second half of `--multiply-bridge HOST:PORT` |

### `sdi8255`

SDI board's 8255 PPI + ROM/ROMCS decode layer (see
`docs/VC1541/VC1541.md`) — hardware-decode layer plus the IEC bus
protocol via the bridge.

| Key | Type | CLI flag |
|---|---|---|
| `enabled` | bool | `--sdi8255` |
| `rom` | string path | `--sdi8255-rom PATH` — the real SDI103J ROM, exactly 4096 bytes |
| `bridgeHost` | string | first half of `--sdi8255-bridge HOST:PORT` (connects to `tools/sdi_bridge/sdi_bridge.py` for OPEN/CLOSE) |
| `bridgePort` | number | second half of `--sdi8255-bridge HOST:PORT` |

## `debugging`

Debug-protocol server, pre-boot memory/port pokes, and instruction/bus
tracing.

| Key | Type | CLI flag | Notes |
|---|---|---|---|
| `debugPort` | number | `--debug-port PORT` | Opens the line-based debug protocol: `REGS`, `MEM addr len`, `POKE addr b1,b2,...`, `PORT addr`, `OUT addr val`, `STATE`, `STEP [n]`, `CONT`, `STOP`, `RESET`, `NMI`, `BREAK`/`UNBREAK addr`, `BREAKS`, `BREAKIF expr`, `PRESSKEYS chord,...`, `TYPE text`, `SAVESNA path`, `LOADZ80 path`, `SAVEZ80 path`, `SCREENSHOT path`, plus one command per enabled peripheral (`KEMPSTON`, `AY`, `ZIFI`, `MULTIFACE1`, `DIVIDE`, `DANDANATOR`, `MULTIPLY`, ...) |
| `poke` | array of strings | `--poke ADDR=B1,B2,...` (repeatable) | Each string is `"ADDR=B1,B2,..."` — hex address, comma-separated hex bytes. Written before execution starts |
| `pc` | string (hex) | `--pc ADDR` | Sets the initial PC; applied after all `poke` entries |
| `out` | array of strings | `--out PORT=VALUE` (repeatable) | Each string is `"PORT=VALUE"` (hex) — writes a port before execution starts, without the Z80 executing an `OUT` |
| `tracePort` | string (hex) | `--trace-port PORT` | Logs every `IN` from this port when its value changes |
| `instrtrace.enabled` | bool | `--debug-instrtrace` | Logs every instruction (raw bytes + registers, not decoded — see `tools/instrtrace_decode.py`) while PC is in range. Toggle live via `INSTRTRACE ON`/`OFF` over the debug protocol |
| `instrtrace.rangeLow` / `instrtrace.rangeHigh` | string (hex) each | optional `LOW-HIGH` suffix on `--debug-instrtrace` | Both must be set together; defaults to SDI's own ROM window (`$3000`-`$3FFF`) if omitted |
| `iectrace` | bool | `--debug-iectrace` | Logs every access to SDI's 8255 Port C (the IEC bus lines) with resolved bus state. Toggle live via `IECTRACE ON`/`OFF` |
| `iotrace` | bool | `--debug-iotrace` | SDI 8255 port I/O trace |

## `_description` / `_examples`

Any key starting with `_` (`_description`, `_examples`) is a comment —
`settingsToArgs()` ignores unknown keys entirely, so these are safe to
leave in, edit, or strip out. `_examples` holds a couple of full,
working settings snapshots for copy/paste reference (e.g. a Dandanator
session, a Dandanator+Multiply-bridge session) — not loaded or
interpreted by JASE itself, just documentation-in-data form.

## See also

- `./jase --help` — the authoritative, always-up-to-date flag list this
  file mirrors.
- `src/main.cpp`'s `settingsToArgs()` — the actual expansion logic.
