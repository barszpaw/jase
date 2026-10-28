# SDI Project - Active Technical Context
(Last verified live, 2026-10-28 — see Claude's own auto-memory at
`project_jase_sdi_emulation.md` for full investigation detail/evidence;
this file is the terse pointer.)

## Verified ROM Addresses (SDI103J_ED3DC6CD.ROM)
Cross-check `docs/VC1541/SDI103J_labeled_disasm.asm` — lines marked
`>> confirmed` are live/byte-verified, `.. static only` are not. **Symbolic
names are added by hand to `SYMBOLS` in `tools/sdi_disasm.py`** (not
auto-discovered) — after confirming a new address live, add it there, with
a comment stating HOW it was confirmed and when, then the `.asm` file can
be regenerated from accumulated INSTRTRACE logs.

- **`$3515` RESOLVED (2026-10-28)**: `CATALOG_OPEN_CH0_ENTRY` = `LD A,$60
  / LD ($5c9d),A / CALL IEC_OPEN_SEND_SHARED`, confirmed by live `BREAK
  3515` + `STEP` (PC walked 3515→3517→351a→345e exactly as predicted, SP
  dropped 2 confirming the CALL). This OVERTURNS two independent
  INSTRTRACE "ground truth" captures (the old `.asm` file + a fresh
  regen from a same-day trace) that had both wrongly agreed on `23 77 23
  77 e1 c9`. Fixed in both `docs/VC1541/SDI103J_labeled_disasm.asm` and
  `tools/sdi_disasm.py`'s `SYMBOLS` comment. **Standing caution**:
  INSTRTRACE ground-truth was wrong here, consistently, for unknown
  reasons — a live `BREAK`+`STEP` spot-check beats trusting even
  repeated `>> confirmed` entries when something looks inconsistent.
- `$345e` IEC_OPEN_SEND_SHARED — shared funnel for every "open channel N,
  send string" call. Breakpointing this + reading the return address off
  the stack is the reliable way to find any given send's real caller.
- `$3996` LOAD_OPEN_SHARED_CONT — appends CR, hardcodes secondary=$6F
  (Open, channel 15), falls into $345e. Only reached from `$3f33`
  PARSE_QUOTED_FILENAME, called from exactly one place: `$3c52` (sends
  `"I"`, confirmed literal byte `0x49`).
- `$3f50` — literal bytes `55 49 2D 0D` = `"UI-\r"` (disassembler
  misread this as nonsense opcodes; it's data).
- `$3c52-$3c5e` — sends `"I"` then `"UI-"` as an inseparable pair, once,
  as part of the boot-banner-print activation sequence (`RANDOMIZE USR
  16000`). Does NOT explain a lone repeated `"I"` by itself.
- `$3515-$351a` — separate call site, opens channel 0 with secondary
  `$60` (the real `*C` catalog / `*LOAD` turnaround mechanism) —
  see the unresolved contradiction above before trusting this range.
- `$3025` ATN_PRESENCE_DEBOUNCE, `$3e2b` PRINT_CHAR_RST10, `$3f47`
  UIMINUS_STRING_SETUP — added to `SYMBOLS` this session.
- **Message dispatch, SOLVED**: `$31a9` PRINT_MSG_SKIP_LOOP is the real
  entry (walks the message table at `$31e3-$3338` forward exactly `B`
  messages, `B`=0-based index: `FILE NOT FOUND`=0, `DEVICE NOT
  PRESENT`=1, `SEARCHING FOR`=2, ...). `$31b5` is NOT a separate entry
  point (was wrongly labeled `DOS_Display_Error` — fixed to
  `MSG_SKIP_DJNZ`, it's just the loop's own `DJNZ`). `$3339`
  `ERR_DISPATCH` is the real generic "display error N" call site: caller
  sets `B`, `CALL`s here. Confirmed live via `BREAK 31a9`/`BREAK 3339` +
  looping until `B==1`.
- `0x3c56` `CMD_LOAD_ENTRY` (pre-existing label, not from this session)
  looks WRONG — it sits on the last byte of `$3c54`'s 3-byte `LD
  ($5c92),A` instruction, not a real instruction boundary. Not yet fixed,
  flagged only.
- `$3025-$3035` — debounce helper (stable-Port-C-bit read), only real
  confirmed caller is `$3142` inside `IEC_RECEIVE_BYTE` ($3138).
- `$3e2b-$3e30` — print-one-character helper (`RST $10` wrapper).

## Channel 15 & Payload Rules — CORRECTED 2026-10-28
- Channel 15 (`$0F`) is COMMAND/STATUS channel only. `I`=INITIALIZE,
  `UI-`=reset to standard speed. Reply `"00,OK,00,00\r"` — **no space**
  after the first comma (there WAS a stray space bug, fixed). Never
  treat these as filenames / search for them on disk.
- **File payload: raw bytes, NO header of any kind.** The "3-byte
  LSB/MSB/TypeFlag header" theory (and a separate "prepend 0xF0" theory)
  were BOTH tried and live-confirmed WRONG — either one breaks the load.
  `sdi_bridge.py`'s `handle_open()` real-file branch is just
  `content = match.read()`. Don't re-add a header without new evidence.
- Catalog (`$`) payload: real CBM BASIC "program" format, starts `01 08`
  ($0801 load addr), `0x12` (REVERSE-ON) right before the disk-name
  line's opening quote, link-chained lines, terminated `00 00`.

## MAJOR OPEN FINDING, as of 2026-10-28: the "I repeats forever" symptom is DEVICE NOT PRESENT firing repeatedly, dispatch mechanism solved, exact trigger condition still unknown
After a real `*LOAD "ace of aces"` succeeds (280 bytes, confirmed via
bridge log) and the loaded BASIC program auto-runs, it reaches line 20
(`*L"AOASC" CODE`, confirmed via live `LIST` — use `PRESSKEYS K,RETURN`
for `LIST`, never `TYPE`-spell it, same keyword-tokenization trap as
`RANDOMIZE`) and that nested load starts failing. At full emulation
speed, the screen fills with **`DEVICE NOT PRESENT`** printed over and
over (confirmed via `tools/screen_raw_dump.py` — **`SCREENSHOT` is
useless here, it renders real color attributes and the text is
black-on-black**, looks like a blank screen when it isn't). After 8
`"I"` attempts the bridge log shows the pattern switching to repeated
`CLOSE F` instead — a second, distinct, still-unexplained failure mode.

**The presence-ack timing-race theory for this is DISPROVEN**: live
IECTRACE across a full `DEVICE NOT PRESENT` flood captured 160/160 ATN
assertions with `devDat=1` (correct ack) every single time —
`IecListenerState::enterAtnPresenceAck` is not the problem, it responds
synchronously and correctly. What actually selects/triggers
`DOS_Display_Error` with the `DEVICE NOT PRESENT` message is still
unknown — a live `BREAK 31b5` session was attempted but got contaminated
by a `PRESSKEYS`-induced false `BREAK` (screen hits `scroll?`,
dismissing it with a keypress once produced a real `D BREAK - CONT
repeats` report instead) — **retry with zero keyboard interaction after
the initial `*LOAD` trigger** for a clean capture.

Running under breakpoint pacing (any `BREAK`+`CONT` loop) reliably (3/3
runs) avoids the `DEVICE NOT PRESENT` flood entirely and reaches a
*different*, deeper failure instead (`L BREAK into program` or `Out of
memory`, both at `20:2`) — pacing clearly changes which code path gets
taken, mechanism not yet understood.

**UPDATE — dispatch chain FULLY SOLVED (2026-10-28, later same day):**
`$30d4` (`DOS_Send_Byte_IEC`, generic bit/byte send) samples Port C
mid-transfer (`IN A,($DF)/OR A/JP M,$3036` — bit 7 = DAT line) → `$3036`
sets bit 7 of status byte `$5c92` → `$347e` (inside
`IEC_OPEN_SEND_SHARED`) tests `($5c92)` via `JP M,$3331` → `$3331` sets
`B=1` → falls into `ERR_DISPATCH` ($3339) → prints `DEVICE NOT PRESENT`.
**This is a DIFFERENT check than `enterAtnPresenceAck`** (already
disproven, 160/160 correct) — it fires during actual bit/byte
transmission, not the one-shot ATN presence-ack. New `SYMBOLS` added:
`$3036` `SET_DEVICE_NOT_PRESENT_FLAG`, `$3331` `MSG_B1_SET_AND_FALL`,
`$30d4` `DOS_Send_Byte_IEC`.

**ROOT CAUSE FOUND AND FIXED (2026-10-28, same day, right after)**:
`kAtnAckTstates` in `IecListenerState.cpp` gates `AtnPresenceAck`'s
fixed-timeout auto-release of DAT. It's re-entered from `AckHold` before
EVERY byte in a multi-byte exchange, not just the first — but the
constant (3761 T-states, 4200 with margin) was only ever calibrated
against the first-time case (ATN assert → check, nothing in between).
Live `BREAK 3036` + `IECTRACE` caught it directly: on re-entry, `devDat`
dropped to 0 (our own timeout firing) only 9 trace lines later —
**before** the real ROM's own re-check at `$30d8` (which needs to first
re-send a full LISTEN+devnum+secondary byte, ~9 extra instructions of
real work the original constant never accounted for). We were releasing
our presence-ack early, which the real ROM correctly read as "device
gone." **Fix**: bumped `kAtnAckTstates` 4200→20000 (generous margin, not
a second precise measurement).

**VERIFIED LIVE END-TO-END, full speed, zero breakpoints**: clean
process restart, `*L"ace of aces"` → bridge log shows zero retries:
`I`→`UI-`→real file (280B)→`CLOSE`→`I`→`UI-`→`AOASC`(6421B, the
program's own chained `*L"AOASC"CODE`)→`CLOSE`→`AOA-1`(12092B)→`CLOSE`→
(continuing). **Screen shows the real, correctly-rendered "Ace of Aces"
(1986, Artech Digital/US Gold) title screen** — first fully-successful
end-to-end `*LOAD` this entire investigation has produced. **Fix is in
the working tree, `src/IecListenerState.cpp`, NOT YET COMMITTED.**

## Methodology, hard-won this session
- `tools/screen_raw_dump.py` — reads `$4000-$57FF` pixel bitmap only,
  ignores color attributes, renders B&W PNG. Use instead of `SCREENSHOT`
  for anything SDI-related.
- `TYPE` spells letters one at a time — breaks BASIC keyword
  tokenization (`RANDOMIZE`→`PRESSKEYS T,SHIFT+ALT,L,...`, `LIST`→
  `PRESSKEYS K,RETURN`). `*L"filename"`-style SDI commands ARE safe to
  `TYPE`-spell (SDI's own raw-ASCII parser, not BASIC keyword mode).
- `TYPE`/`PRESSKEYS` share one keystroke queue — a second call before
  the first drains wipes it. One call per intended action.
- A syntax-error'd input line does NOT clear on next RETURN — stays in
  the edit buffer, more typing just appends. Full process restart only
  reliable fix.
- Always `ps aux | grep -E "jase|sdi_bridge"` (not just `pgrep -af`) and
  kill everything before a new test — stale processes silently absorb
  new debug-protocol commands.
- `scroll?` halts ALL execution waiting for a keypress — don't mistake
  a frozen `scroll?` for "the retry loop settled on its own."
- `IOTRACE ON`/`IECTRACE ON`/`--debug-instrtrace` all write to stderr.

## Current Branch & State
- Branch: `wyścig-z-ollama`
- Hardware target: i8255 replacement with CP82C55A (CMOS)

# Dandanator / Multiply - Active Technical Context
(Implemented and smoke-tested 2026-10-28 — see Claude's own auto-memory
at `project_jase_dandanator_multiply.md` for full hardware/protocol
research; this file is the terse pointer, same convention as the SDI
section above. Not committed to git yet.)

## What exists
- `src/Dandanator.h/.cpp` — ZX Dandanator! Mini cartridge stand-in: 32
  x 16K bank storage, full command protocol (commands 1-49 + 52), the
  ROM-write-trap command channel via the new `Peripheral::
  onMemoryWrite()` hook (see below). `--dandanator`/`--dandanator-rom`
  CLI flags. **Auto-engages on power-on** (constructor sets bank 0 +
  WaitNormal directly) -- matches real hardware, which needs no button
  for the default "show the menu" case (the manual's "During Boot"
  button actions are all alternatives you hold a button to get, e.g.
  boot the internal ROM instead). `pressButton()`/F9 window hotkey /
  `DANDANATOR BUTTON` debug command is the RUNTIME action (return to
  menu from a running game), not the initial activation -- fixed
  2026-10-28 after a real-romset test showed nothing happened without
  it; ZEsarUX requires a button for a different reason (hot-swapping a
  romset already loaded into its memory), not because real hardware
  does. Built-in placeholder ROM in bank 0 so it's exercisable without a
  real 512K romset too. **Verified against a real romset
  (`dandanator_10.4.3.rom`)**: menu renders correctly, keyboard works,
  commit `619737b`.
- `src/Multiply.h/.cpp` — Multiply microSD-addon stand-in, TX/RX
  **foundation only**: recognizes just `CMD_ZX2SD_REQ_ID` (0x01) and
  replies with one fixed byte, proving the plumbing works. The real
  file-browsing/streaming command set is NOT implemented. `--multiply`
  CLI flag, `MULTIPLY` debug command.
- Both build clean (no new warnings) and are round-trip tested headless
  over `--debug-port` (POKE-sequence command injection + PORT/MEM
  readback — exact transcripts in the auto-memory file).

## Hardware/protocol facts (full detail: docs/Dandanator/dandanator_multiply_notes.md)
- Real cartridge: PIC16F1826 + SST39SF040 512KB flash + GAL22V10, from
  the real schematic at dandare.es, saved as
  `docs/Dandanator/dandanator_mini_schematic_v2.1b.jpg`.
- Command channel: writes to Z80 addr 0-3 are a hard no-op on real ROM,
  safely repurposed as a side-channel bus (cmd=addr1, data1/data2=
  addr2/3, confirm pulse=addr0). 1-byte commands (1-39) self-confirm by
  repeating the SAME byte N times (N = the command's own value); 3-byte
  commands (40-49, and 52 for Multiply) confirm via one addr0 pulse.
- Multiply rides on Dandanator's cmd 52 (`DANCMD_MULTIPLY`, data1=1
  fixed marker) for Z80->Multiply bytes, and a 4-bit-per-edge nibble
  protocol on port 0x1F (the SAME port the Dandanator's own onboard
  Kempston interface, and a real Multiface ONE, also use) for
  Multiply->Z80 bytes.

## Verified against real artifacts (2026-10-28, after initial commit)
- Real romset `dandanator_10.4.3.rom` (user-supplied, not in repo):
  Dandanator initially required an explicit `pressButton()`/F9 before
  anything activated -- WRONG, real hardware auto-engages on power-on
  (manual's "During Boot" section only documents button-HELD
  alternatives). Fixed: `Dandanator::Dandanator()` now sets
  `switchedOn_`/`state_`/`bankChangePending_` directly; `pressButton()`
  kept for its real runtime role (return to menu, like the physical
  right button). Verified: real menu renders correctly, keyboard works.
  Commit `619737b`.
- Multiply rewritten from an in-process placeholder to a TCP bridge
  client -- copies `ZiFi.h/.cpp`'s architecture (non-blocking socket,
  `txRing_`/`rxRing_`, `pump()` once per frame) and talks to new
  `tools/multiply_bridge/multiply_bridge.py`, same split as SDI's own bridge
  (`sdi_bridge.py`/`IecBridgeClient`): JASE stays a dumb byte-pipe, all
  `CMD_ZX2SD_*` command semantics belong in the Python companion.
  `--multiply-bridge HOST:PORT` (implies `--multiply`). v1 bridge only
  answers `CMD_ZX2SD_REQ_ID` -- round-trip re-verified through the real
  process, works identically to the old in-process version. Commit
  `60947b6`.
- `tools/multiply_bridge/multiply_bridge.py` now implements real `CMD_ZX2SD_*` commands
  instead of REQ_ID-only: SETZXTYPE, CD_ROOT/CD/CD_IX/GETDIR,
  LS_RELATIVE (single chapter, 34-byte records: type+30char name+
  0x00+uint16 LE index), OFREAD/OFREAD_IX, CMD_ZX2SD_SCR (.SCR preview
  streaming). Protocol facts read from the real Arduino firmware in
  full (Multiply_Arduino.ino/libFileList.ino/libSDFiles.ino, not
  vendored, CC BY-SA). Unimplemented-but-recognized commands (Z80/SNA/
  TAP/ROMSET content, GETINFO, LS_ABSOLUTE) still consume their correct
  payload length (`_FIXED_PAYLOAD`) to avoid desyncing the unframed
  byte stream. Verified end-to-end against a test SD-card directory.
  Commit `4857713`. Still not done: actually loading a game.
- **Testing gotcha**: debug protocol's `STEP` sets `running_=false`
  (debugserver.cpp:322), which stops `MinZX::update()` and therefore
  `pump()` (ZiFi/Multiply) until a `CONT`. Don't use `STEP` when testing
  Multiply/ZiFi protocol round-trips -- just let real frames tick.
- **Planned, not yet done** (user's idea): replace the hardcoded
  peripheral construction/push order in `MinZX::init()` with an
  explicit `config.json` `interfaces` key defining each peripheral's
  priority -- "first wins" for port conflicts stays the same semantic,
  just externally configured instead of implicit in source order.

## Architecture gotcha — generalizes to any future peripheral
`MinZX::processInputPort()` awards a shared port's VALUE to the FIRST
peripheral in `peripherals_` whose `in()` returns true. The always-
loaded Kempston plugin claims port 0x1F UNCONDITIONALLY (0 = "no
buttons" still counts). So any peripheral that must actively SUPPLY a
value on a shared port (not just cause a side effect, unlike
Multiface's page-out trick) has to be constructed/pushed into
`peripherals_` BEFORE the Kempston plugin load in `MinZX::init()`, or
it silently loses every claim — no compile error, no crash. Dandanator
+ Multiply were moved there specifically for this reason; confirmed by
testing (wrong order read back 0x00/0x00 instead of the expected
nibble bytes).

## New core hooks added this session (reusable by future peripherals)
- `Peripheral::onMemoryWrite(addr, value, memory)` — observes every Z80
  memory write, including pages this peripheral doesn't own (needed
  because `MemoryMap::poke8()` silently drops writes to ROM pages
  before any peripheral could ever see them). Wired into
  `MinZX::poke8()`.
- `Peripheral::takeResetRequest()`/`takeNMIRequest()` — lets a
  peripheral self-trigger a Z80 reset/NMI as a protocol side effect
  (Dandanator commands 36/40); polled once per instruction in both of
  MinZX's instruction loops (`update()` and `step()`).

## Not done yet
Dan-SNAP (Multiface-like NMI freeze/snapshot), the real `CMD_ZX2SD_*`
file-browsing/streaming command set (Multiply only answers REQ_ID so
far), T-state confirmation-delay modeling (commands execute immediately
on the final pulse instead of after the real ~35-121 T-state hardware
latency), onboard-Kempston port 0x1F emulation, plugin `.so` split for
either class.
