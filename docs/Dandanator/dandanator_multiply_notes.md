# Dandanator Mini + Multiply — research notes (2026-10-28)

Prepared before writing any plugin code, per request: "przygotuj wiedzę" —
gather hardware facts and cross-check them against a known-good reference
implementation before touching JASE's `Peripheral` plugin architecture.

## Sources (primary, all fetched this session)

- **Dandanator! Mini official site** — http://www.dandare.es/Proyectos_Dandare/ZX_Dandanator%21_Mini_EN.html
  (team: Dandare/AntonioVillena-era design, overCLK, mad3001). Manual:
  `Manual_ZXDDTRMini_EN.html`. Schematics page:
  `Esquemas_ZXDDTRMini.html` → real KiCad-exported schematic
  `Schematic 2.1b.jpg`, saved here as
  [`dandanator_mini_schematic_v2.1b.jpg`](dandanator_mini_schematic_v2.1b.jpg).
- **Multiply** — https://github.com/mad3001/Multiply (CC BY-SA 4.0),
  official "Addon for ZX Dandanator! Mini". Cloned locally for research;
  the protocol-relevant source excerpts are vendored under
  [`reference_src/`](reference_src/) (same license, kept small and
  attributed — not a full repo mirror). Schematic saved as
  [`multiply_schematic_v1.1b.png`](multiply_schematic_v1.1b.png).
- **Dandanator-mini Java romset tool source** — https://github.com/teiram/dandanator-mini
  (not fetched in full this session — noted for later if PIC-side details
  are needed; the Z80-side protocol below was already fully recoverable
  from Multiply's firmware).
- **ZEsarUX 13.0** emulation source (treated as a *howto*, not copied):
  `~/src/ZEsarUX/zesarux-ZEsarUX-13.0/src/storage/dandanator.c/h` (1066
  lines, read in full) and `src/copy_interfaces/multiface.c/h` (350
  lines, read in full).
- JASE's own `src/Peripheral.h`, `src/Multiface.cpp/h`, `src/DivIde.cpp/h`,
  `src/MemoryMap.h`, `src/minzx.cpp`, and the Kempston plugin POC
  (`src/plugin_api.h`, `src/plugins/kempston_plugin.cpp`, Makefile lines
  20-55) — read in full to see what pattern already exists to build on.

**Important naming correction from the original ask**: the device is
"**Multiply**", not "Multiface" — an Arduino+microSD add-on board *for*
the Dandanator Mini (file browser / snapshot loader over the Dandanator's
own command channel), unrelated to Romantic Robot's Multiface hardware.
JASE already has a from-scratch Multiface ONE implementation
(`src/Multiface.cpp`) for the *actual* Multiface, which is a different,
already-solved problem (see "What JASE already has" below). The
connection between the two: Dandanator Mini's own firmware includes a
software feature called **Dan-SNAP** that is explicitly "Multiface-like
snapshot generation" (freeze game, NMI-triggered) — that's almost
certainly why both names came up together.

## Dandanator Mini hardware (from the real schematic)

- **U1 — PIC16F1026** (later revisions: PIC16F1826): the brain. Watches
  the Spectrum edge-connector bus directly — `ROMCS48K` (ROM chip-select
  from the bus), `Rom_WR` (write strobe to ROM address space), `NMI`,
  `RESET`, address lines A16-A19 (bank select bits) and `PA14-PA18`
  (parallel bus to the flash). Drives `PT_ROMCS` (pass-through ROMCS) to
  the second edge connector so a peripheral plugged in *after* the
  Dandanator (daisy-chained) can still take over ROM space — matches the
  product page's "automatic shutdown if another peripheral wants to take
  control of the ROM space."
- **U3 — SST39SF040**: 512KB (4 Mbit) parallel NOR flash, the actual game
  storage — 32 banks × 16KB. Address lines A0-A18 (19 bits = 512K),
  data D0-D7, `EP_CE`/`Rom_RD`/`Rom_WR` control. This is
  `DANDANATOR_SIZE (512*1024)` in ZEsarUX's header, confirmed.
- **U2 — GAL22V10** (PLD): decodes the Kempston joystick port (0x1F,
  via `IOREQGGE`/`/RD`/A5) from the onboard DB9 joystick connector, and
  generates `Rom_WR`/`Rom_RD` from `/MREQ`/`/RD`/`/WR`. The Dandanator
  Mini **includes a built-in Kempston joystick interface** — this is
  what the Multiply readme means by "Any ZX Spectrum without Kempston
  Interface can use Multiply" and the incompatibility note for machines
  with an onboard Kempston (TC2048, Inves Plus 48K): port 0x1F conflict,
  the same documented-conflict pattern already called out in JASE's
  `Peripheral.h`.
- **Two ZX edge connectors (J1, J2)**: the cartridge sits *in the middle*
  of the bus — J1 from the Spectrum, J2 passed through to the next
  peripheral. Full bus signals (address, data, `/MREQ`, `/IOREQ`, `/RD`,
  `/WR`, `/NMI`, `/HALT`, `/BUSREQ`, `/BUSACK`, `/RFSH`, `/M1`, `/WAIT`,
  clock, ±12V, ±5V) pass through unmodified except `ROMCS`.
- **SW1/SW2**: the two user buttons (see manual button table below).
- **DB9 "Kempston" connector**: standard joystick pins **plus** a small
  "Serial Pins" header (P1) next to it — these are the same DB9 pins 4
  and 9 that Multiply's schematic repurposes as TX/RX (see below). On a
  bare Dandanator (no Multiply attached) these pins are unused.

## Dandanator command protocol (ROM-write-trap channel)

Cross-checked word-for-word against ZEsarUX's `dandanator.c` AND the
*official* firmware routines in
[`reference_src/dandanator_hw_90.asm`](reference_src/dandanator_hw_90.asm)
— they agree completely, which is a strong signal ZEsarUX's reverse
engineering is accurate for this part. Mechanism, in short:

> Writes to ROM address space are normally a no-op on real hardware (you
> can't write a NOR flash by just asserting `/WR` — well, you normally
> *can't* without the dedicated command sequences this interface uses).
> The Dandanator safely listens to `Rom_WR` pulses at ROM addresses
> **0, 1, 2, 3** as a side-channel command bus, totally invisible to any
> software that isn't deliberately using it.

- **addr 1** = command byte. **addr 2** = data1. **addr 3** = data2.
  **addr 0** = confirmation pulse (`LD (0),A` — any value, only the
  write event matters).
- **1-byte ("normal") commands** (values 1-39): sent by writing the same
  command byte to addr 1 repeatedly, *N* times where *N* equals the
  command's own value (`SENDNRCMD` in the official asm: `DJNZ` loop of
  `B` = command value). ZEsarUX's state machine
  (`In_Progress_Normal`, counting `dandanator_contador_command`) decodes
  this the same way. After the Nth pulse, a ~119-121 T-state delay, then
  execute.
- **3-byte ("special") commands** (values 40-49): command→addr1,
  data1→addr2, data2→addr3, then a single confirmation pulse to addr 0;
  executes after 35 T-states. This is `SENDSPCMD`/`LONGSPCONF` in the
  official firmware.
- **Command table** (from both sources, in agreement):
  | Cmd | Meaning |
  |---|---|
  | 1-33 | Switch to Dandanator bank `cmd-1` (0-32) |
  | 34 | Switch to internal Spectrum ROM (bank 32) **and lock** future commands until cmd 46 |
  | 36 | Reset the Z80 |
  | 39 | (declared, not implemented in ZEsarUX) remember current slot as the post-reset slot |
  | 40 | **Fast switch**: data1 = bank to switch to; data2 = action bitmask — bit0=Z80 reset, bit1=NMI, bit2=lock future commands, bit3=disable future commands until cold reboot |
  | 41-43 | "Unusable on emulation" per ZEsarUX (presumably analog/serial-only ops, no software-visible effect) |
  | 46 | **Lock/unlock/disable**: data1 (== data2, else ignored) — 1=lock (only cmd 46 accepted), 16=unlock (normal), 31=disable until cold reboot |
  | **52** | **Multiply extension**, data1 must be **1** (`DANDAT_MULTIPLY`) — not present in ZEsarUX at all, see gap list below |
- **Bank numbering**: 0-31 = the 32×16KB flash banks; **32 = pass-through
  to the real internal Spectrum ROM** (the command channel keeps working
  even while bank 32 is shown, since writes to ROM space are intercepted
  regardless of which bank is currently mapped for *reads*).
- **State machine** (both sources agree): `Commands_Disabled` (boot
  state, nothing accepted) → pressing the button sets `Wait_Normal`
  (slot 0, commands accepted) → `Commands_Locked` (only cmd 46 accepted)
  is a separate state entered via cmd 46/data1=1 or cmd 40 bit2. A
  command never implicitly changes slot/ROM — only an explicit bank
  command or the physical button does.

## Dan-SNAP (the "Multiface-like" feature)

From the official manual (not in ZEsarUX — ZEsarUX's dandanator.c has no
NMI/snapshot code path at all, only the CPC-specific NMI call inside its
own unrelated `cpu_core_loop_cpc_dandanator`):

- **Left button**: short click = pause game / trigger Dan-SNAP if
  enabled; long click = reset to internal ROM and disable Dandanator
  (persists across power-cycle until menu is relaunched); double click =
  load Dandanator menu (or next test ROM if one was auto-launched at
  boot) **and performs an NMI** when a game is already loaded.
- **Right button**: always loads the Dandanator menu; at boot, boots
  the internal ROM while enabling audio-update mode.
- Dan-SNAP itself is a *romset feature*, not fixed firmware — it must be
  added to the romset like any other app. It freezes the running
  machine's RAM+registers to Dandanator flash (same idea as Multiface's
  freeze), with a 128K quirk: since the Dandanator has no way to know if
  the 128K machine is showing the main or shadow screen, it asks the
  user to confirm whether they saw a flashing on-screen marker.

This is a *second*, independent ROMCS-paging state machine layered on
top of the bank-switch mechanism above — worth treating as a logically
separate concern from plain bank switching when designing the plugin,
even though both live on the same physical device.

## Multiply addon hardware

Real schematic (`multiply_schematic_v1.1b.png`, KiCad, "version 1.1b
Sept-2020", https://github.com/mad3001/Multiply):

- **Arduino Nano** — runs the file-serving firmware
  (`Src/Multiply_Arduino/Multiply_Arduino.ino` + `lib*.ino` files:
  SD/FAT, Z80 snapshot parsing, TAP/SNA handling, serial command
  dispatch).
- **microSD card** over SPI (CS/SCK/MOSI/MISO), standard `SdFat` library.
- **Two DB9 joystick pass-through ports** (Atari norm + "SJS-1" norm),
  wired to Arduino analog pins — Multiply **adds** joystick support on
  top of the Dandanator (the readme notes its own onboard Kempston
  conflicts with machines that already have one built in, e.g. the
  Dandanator Mini itself from v2.0 onward, or Inves/TC2048).
- **10-pin IDC "TO_DAN" header**, connecting to the Dandanator's "Serial
  Pins" header. Per the schematic's own pin notes: `IDC[7] = DB9[4] to
  TX`, `IDC[8] = DB9[9] to RX` — i.e. the physical joystick DB9's pins 4
  and 9 (otherwise unused/reserved on a plain Kempston DB9) double as a
  real UART between the Dandanator's PIC and the Arduino.

## Multiply wire protocol — two separate channels, confirmed from firmware source

This is the one genuinely non-obvious finding, recovered by cross-reading
[`reference_src/4bits_load_relocatable.asm`](reference_src/4bits_load_relocatable.asm),
[`reference_src/multiply_commands_v1.asm`](reference_src/multiply_commands_v1.asm),
and [`reference_src/libSerialCmds.ino`](reference_src/libSerialCmds.ino)
together — no single file states it outright:

- **Z80 → Multiply (TX)**: reuses the *existing* Dandanator command
  channel. `DANCMD_MULTIPLY = 52`, `DANDAT_MULTIPLY = 1`. To send one
  byte to Multiply: issue special command `(52, 1, byte)` through the
  normal 3-byte command sequence above. The Dandanator's PIC recognizes
  cmd 52 specially and forwards the byte out over the real UART wired to
  the Arduino's hardware `Serial` (confirmed by `Serial.read()` on the
  Arduino side in `libSerialCmds.ino` — it never bit-bangs on receive).
  Command set sent this way is `CMD_ZX2SD_*` in
  `multiply_commands_v1.asm` (identify/reset, SD directory
  browsing/`cd`, open file for read/write, and per-filetype data
  requests: SCR, Z80 16/48/128K, SNA 48/128K, TAP/TZX, raw binary,
  512K romset block).
- **Multiply → Z80 (RX)**: a **separate, custom 4-bit nibble bit-banged
  protocol read through `IN A,(0x1F)`** — i.e. the *same port* as the
  Dandanator's own built-in Kempston joystick (and Multiface ONE on real
  hardware). `Load4bitBlk` polls bit 0 of port 0x1F: a run of bit0=1
  pulses marks the high nibble, a run of bit0=0 marks the low nibble,
  assembled into a byte with `RLD`. This is why the readme warns the
  onboard Kempston can't coexist with Multiply use — they's the same
  physical wire.
- File-type enum (`FT_*`) mirrors the command set 1:1 (e.g. `FT_SCR =
  0x10` ↔ `CMD_ZX2SD_SCR = 0x10`), and directory listings are
  `FT_END_DIRECTORY`-terminated.

For a *software* JASE plugin (no real SD card, no real Arduino), the
natural design is: implement the Dandanator's own bank-switch/command
protocol faithfully, then have a "Multiply" peripheral intercept command
52 and the port-0x1F nibble reads, and serve files from a host directory
instead of a real microSD — i.e. reimplement the Arduino firmware's
*logic* (not its hardware), matching `CMD_ZX2SD_*` semantics so existing
Multiply-aware romsets/menus (like `Src/MultMenu/MultMenu.asm`) work
unmodified against the emulator.

## ZEsarUX reference coverage — what it has and doesn't

ZEsarUX's `dandanator.c` is a solid, accurate reference for the **core
bank-switch command protocol** (cross-validated above) but is missing:

- **Dan-SNAP** (no NMI/freeze code path for the Spectrum target at all —
  only the unrelated CPC `/IY`-indexed config protocol, a completely
  different command scheme for the Amstrad CPC variant of this same
  chip family, not applicable to JASE which is Spectrum-only).
- **Command 52 / Multiply** — entirely absent; ZEsarUX's command dispatch
  only handles 1-49 and silently logs "UNKNOWN command" for 52.
- **The onboard Kempston interface** on port 0x1F — ZEsarUX's
  `dandanator.c` has zero port I/O handling (it's pure memory-mapped,
  via the nested peek/poke chain); the GAL's Kempston decode isn't
  emulated there at all (ZEsarUX presumably has this in its generic
  Kempston code, unrelated to the dandanator.c file).

## What JASE already has — and the one real gap

This is the most important finding for actually planning the plugin
work: **JASE is not starting from zero.**

- `src/Multiface.cpp/h` is a complete, independent, from-scratch
  Multiface ONE implementation already wired into `minzx.cpp`'s
  `peripherals_` list and `main.cpp`'s `--multiface1` flag. It already
  demonstrates the exact ROMCS-paging + NMI pattern Dandanator/Dan-SNAP
  needs: `MemoryMap::mapPage()` to swap in a device's own ROM/RAM banks,
  `MemoryMap::setRomcsAsserted()` so the machine's own 128K paging logic
  backs off while the card is active, `onNMI()` → deferred
  `onInstructionFetch()` pickup at `PC==0x0066`, and the documented
  port-0x1F conflict with Kempston (`in()` never claims the byte value,
  only triggers the page-out side effect) — this is the *same* port
  conflict the Dandanator Mini's own onboard Kempston and Multiply's
  nibble-read channel both hit, so the precedent transfers directly.
- `src/DivIde.cpp/h` adds a second working example of PC-trap-based
  paging (RST vectors + a control-register CONMEM override), useful as a
  second data point for "how many ways does this codebase already page
  ROM/RAM in and out."
- `src/Peripheral.h`'s class comment **already explicitly anticipates**
  "Multiface, and later DivMMC/Spectranext/..." as ROMCS-style devices —
  Dandanator fits this category exactly.
- The Kempston plugin split (`src/plugin_api.h`,
  `src/plugins/kempston_plugin.cpp`, Makefile `PLUGIN_KEMPSTON` rules) is
  the template for shipping a peripheral as its own `.so` instead of
  linking into core `jase` — the same split should apply to a future
  Dandanator/Multiply plugin once the implementation is settled.

**The one missing piece**: Dandanator's command channel needs to observe
**writes to ROM address space even when the Dandanator's own banks are
NOT currently mapped there** (e.g. while bank 32 — internal ROM
pass-through — is selected, or the moment before the button is even
pressed and the real system ROM occupies pages 0/1). Multiface and
DivIDE never needed this: they only care about writes *into their own
already-mapped* RAM, which flow through `MemoryMap::poke8` normally once
`mapPage()` has installed their bank. Dandanator instead needs to snoop
writes to memory it does *not* own.

Checked `MemoryMap::poke8()` (`src/MemoryMap.h:77-81`) and
`MinZX::poke8()` (`src/minzx.cpp:1086-1094`, the sole call site reached
from the Z80 core's `Z80operations::poke8` virtual): today, a write to a
page whose `Bank::isRom == true` is silently dropped — `if (!b->isRom)
b->data[...] = value;` — with **no peripheral ever notified**. This
matches real hardware's own "ROM writes are normally no-ops" fact
exactly, but it means no existing `Peripheral` hook can see those writes
today. `onInstructionFetch()` only reports the PC *before* an
instruction executes, not what memory operation that instruction ends
up performing.

**Implication for the plugin design**: implementing Dandanator requires
one small, additive core change — a new `Peripheral` hook (something
like `onMemoryWrite(uint16_t addr, uint8_t value)`), called from
`MinZX::poke8()` for every peripheral on every write, mirroring exactly
how ZEsarUX's own `debug_nested_poke_byte_add()` chain works. This is a
genuinely new capability class (the first JASE peripheral that needs to
observe writes to memory it doesn't own), not something obtainable by
composing the existing ROMCS/NMI hooks. Everything else needed
(bank-switch paging itself, the NMI/Dan-SNAP freeze, the port 0x1F
nibble read for Multiply, the pass-through-ROM-still-watches-writes
behavior) maps cleanly onto patterns already proven in `Multiface.cpp`
and `DivIde.cpp`.

## Open questions for the implementation phase (not yet decided)

- Whether Dandanator ships as one `Peripheral` (bank-switch + Dan-SNAP)
  with Multiply as a second, separate `Peripheral` that only activates
  once it sees command 52 traffic (cleanest match to the real
  Arduino-is-a-separate-board hardware split), or as a single combined
  device.
- Whether Multiply's SD card is backed by a real host directory, a
  single romset-like container file, or left unimplemented for v1 (the
  Dandanator's own bank-switch + Dan-SNAP already covers the bulk of
  "why would someone reach for this over Multiface/DivIDE" value without
  it).
- Scope of the real vs. placeholder ROM: like `Multiface::loadRom()`,
  Dandanator needs a real 512K romset dump supplied by the user (not
  bundled — copyrighted game content lives in there), with the menu
  firmware itself (`MultMenu.asm`/`MultMenu.rom`, CC-BY-SA, small) as a
  plausible clean default/bundled ROM for a 0-games smoke test.
