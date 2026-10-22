# Spectranext — implementation plan and port/protocol reference

Research notes for a future JASE `Spectranext` peripheral. Written after
reading the real implementation in `~/src/fusex` (a Fuse fork, GPLv2) --
**facts only** (port numbers, page layout, struct sizes, command IDs are
not protectable expression), same discipline as every other peripheral
in this project (AY, DivIDE, Multiface). No fusex code is copied; JASE's
eventual implementation is a fresh clean-room rewrite.

## Terminology (easy to get wrong, confirmed directly with Pawel)

**Spectranet** and **Spectranext** are the SAME hardware lineage, not two
unrelated things:

- **Spectranet** is the original real ZX Spectrum network interface card
  (Philip Kendall / Stuart Brady design, emulated in upstream Fuse since
  2011 under GPLv2 -- `peripherals/spectranet.c`). Real hardware: a WIZnet
  W5100 Ethernet-to-parallel chip, 128K flash ROM, 128K RAM, paged in like
  DivMMC/Multiface. Real hardware's own homepage (confirmed via fusex's
  bundled help text, `fusepb/FuseHelp/_English.lproj/spectranet.md`):
  `http://spectrum.alioth.net/doc/index.php/Main_Page` -- worth checking
  directly for the real protocol spec (esp. XFS's `stat.inc` format and
  `F_fetchpage`/`F_restorepage` convention, see below) before JASE locks
  in struct layouts purely from fusex's own implementation.
- **Spectranext** ("Alioth Spectranet (Spectranext)", per the real boot
  banner) is a newer, extended revision of that same card/firmware family,
  adding a WiFi-oriented high-level command protocol and a virtual
  filesystem layer on top of the same base hardware. fusex's own
  extensions for this live in `peripherals/nic/spectranext*.c/h` and
  `peripherals/fs/xfs*.c/h`.
- **This is NOT the ZX Spectrum Next machine** (the FPGA "TBBlue" core,
  NextRegs, Layer2, sprites, copper). That confusion was resolved and
  ruled out early in this research -- this fusex checkout has zero
  TBBlue/NextReg code (confirmed: no hits for `nextreg`, `layer2`,
  `copper`, `tbblue`, or ports `0x243B`/`0x253B`).

## Why fusex specifically

fusex gates all of this behind a WiFi-aware boot banner and an on-screen
menu (Resource Index / Load Resource URL / Filesystem Mounts /
Configuration) -- it's clearly meant to be usable as a convenience layer
for loading games/resources from the host over "the network" without
real Spectranet hardware, which is exactly the kind of feature JASE's
own OsdMenu (Network -> Remote, Gopher/TNFS) already covers a different
way. Spectranext is a third option: instead of JASE's own menu driving
loads, REAL Spectrum software (a ROM, a BASIC loader, NextZXOS-alike
client code) could talk to this peripheral directly, the same way it
would on Alioth's real card.

## Base Spectranet hardware: ports

All four base ports are **full 16-bit decoded** (`periph_port_t` mask
`0xffff`, not a partial decode like the ULA's `0xfe` or the AY's
`0xc002`) -- the register's own low byte being `0x3b` is purely a
convention, every one of the 256 possible high bytes is a distinct port.

| Port     | Direction | Name                    | Effect |
|----------|-----------|-------------------------|--------|
| `0x003B` | OUT       | Page A select            | Maps 4K page number `data` into Z80 `0x1000-0x1FFF` (page-pool index, see page map below) |
| `0x013B` | OUT       | Page B select            | Maps 4K page number `data` into Z80 `0x2000-0x2FFF` |
| `0x023B` | IN        | CPLD version             | Returns a fixed version byte (`0x03` in fusex) |
| `0x023B` | OUT       | Programmable trap address | Two-byte sequence: 1st write sets low byte, 2nd write sets high byte (internal flip-flop toggles which half is being written -- no way to address a specific half directly, always alternates) |
| `0x033B` | IN        | Control/status read       | bits 0-2 = ULA last-byte border bits; bit3 = programmable trap armed; bit4 = 128K screen-bank bit (only on 128K-capable machines); bit7 = **inverted** copy of the control register's bit7 (detection trick: write a bit7 value, read it back flipped, to confirm the card is present) |
| `0x033B` | OUT       | Control/status write      | bit7 stored verbatim (read back inverted, see above); bit0=1 pages the card in via I/O (`spectranet_page(1)`); bit0=0 unpages it **if it was paged in via I/O** (an NMI-paged session isn't affected by this bit); bit3 arms/disarms the programmable trap |
| `0x043B` | OUT       | Spectranext stdout        | **New in Spectranext**, not on the original card. Each byte queued into a small text buffer, flushed (and forwarded to the host's own stdout/log) on `\n` or when the buffer fills. A simple one-way debug/log channel, nothing else. |

## Paging mechanics (PC traps + NMI) -- same shape as JASE's existing DivIDE/Multiface

Confirmed in `z80/z80_ops.c`/`z80/z80.c`:

- **Page-in trap addresses**: `PC == 0x0008` OR `(PC & 0xFFF8) == 0x3FF8`
  (i.e. `0x3FF8`-`0x3FFF`) -> `spectranet_page(0)` (paged in, "not via
  I/O"). Narrower than DivIDE/DivMMC's six-address trap set.
- **Page-out trap address**: `PC == 0x007C` -> `spectranet_unpage()`. A
  single fixed address, not a range.
- **NMI**: on any Z80 NMI (regardless of source), if `spectranet_available`,
  `spectranet_nmi()` runs *in addition to* whatever else NMI does --
  sets the `nmi_flipflop` and pages the card in (`spectranet_page(0)`,
  same "not via I/O" flavour as the PC-trap path).
- **RETN**: `z80_retn()` calls `spectranet_retn()`, which only clears
  `nmi_flipflop` -- it does **not** by itself unpage. Actually leaving
  the paged window still requires reaching `PC == 0x007C` (the card's
  own ROM exit routine) or an explicit `OUT (0x033B), 0` with bit0 clear.
- **Programmable trap**: if armed (control-write bit3) and
  `PC == <the 16-bit address from port 0x023B>`, an NMI is scheduled
  (`event_add(0, z80_nmi_event)`) on the *next* instruction boundary --
  this is a **software-configurable** breakpoint-triggered NMI, letting
  a running program ask to be interrupted at an address of its own
  choosing (e.g. a hook point for background polling), independent of
  the fixed page-in trap set above.

Same two-phase "evaluate before this instruction's own fetch, apply
starting next instruction" timing discipline JASE's DivIDE already
needed (see project memory on the automap page-in-too-early bug) likely
applies here too -- not yet verified against this specific trap set, but
worth testing for exactly the same class of bug before trusting it.

## Page map (4K pages, selected via ports 0x003B/0x013B)

| Page number(s) | Contents | Access |
|---|---|---|
| `0x00`-`0x1F` | Flash ROM (128K total, AM29F010-style flash chip, writable -- real hardware lets the Z80 reflash its own firmware) | R/W, page 0 is *also* fixed at Z80 `0x0000-0x0FFF` always (never swapped out) |
| `0x40`-`0x47` | W5100 Ethernet chip registers (32K register/buffer window) | R/W, raw chip-level TCP/IP -- real low-level sockets |
| `0x48` | **Spectranext Controller** (command/status/workspace, see below) | R/W |
| `0x49` | **XFS** virtual filesystem (command/status/workspace, see below) | R/W |
| `0xC0`-`0xFF` | RAM (128K total) | R/W, page `0xC0` is *also* fixed at Z80 `0x3000-0x3FFF` always (never swapped out) |

Z80 address layout once paged in (`romcs` full 16K, 4x4K slots):
`0x0000-0x0FFF` fixed ROM page 0 · `0x1000-0x1FFF` = page-A-selected ·
`0x2000-0x2FFF` = page-B-selected · `0x3000-0x3FFF` fixed RAM page 0xC0.

## Spectranext Controller protocol (page 0x48)

One 4096-byte page. Byte 0 = `command`, byte 1 = `status`, bytes 2+ =
a big union ("workspace") whose layout depends on which command is
active. **Trigger**: writing any non-idle value to byte 0 while it was
previously idle (`0xFF`) runs the command *synchronously*, in the same
write -- not deferred to a frame boundary. Status values: `0`=success,
`1`=error, `0xFF`=idle/in-progress.

Commands (`enum spectranext_cmd_t`):

| ID | Name | In (workspace) | Out (workspace) | Notes |
|----|------|-----------------|-------------------|-------|
| 0 | GET_CONTROLLER_STATUS | - | `controller_status` (u8), `wifi_connection` (i8), `ipv4` (u32) | fusex always reports "operational" / "IP obtained" -- there's no real WiFi layer underneath, the host already has networking |
| 1 | WIFI_SCAN_ACCESS_POINTS | - | `scan_count` (u8) | fusex stub always returns exactly one fake AP named `"spectranext"` |
| 2 | WIFI_GET_ACCESS_POINT | `ap_index` (u8) | `ap_name` (char[64]) | indexes into the scan result above |
| 3 | WIFI_CONNECT_ACCESS_POINT | `ssid`/`password` (char[64] each, struct exists but fusex's stub doesn't even read them) | - | always "succeeds" |
| 4 | WIFI_DISCONNECT | - | - | |
| 5 | DNS_GETHOSTBYNAME | `host` (char[64]) | `ipv4` (u32, network byte order) | **real** host OS `getaddrinfo()` call -- the one genuinely functional network primitive in the controller itself |
| 6 | ENGINECALL | `input_file`/`output_file` (char[128] each), `operation` (char[256], space-separated argv, single/double-quote aware) | result in `status` | runs a tiny host-side JSON-path or X-path query engine (`jsonpath`/`json`/`xpath` as `argv[0]`) against `input_file`, writes the result to `output_file`. For extracting a value out of a downloaded API/JSON response without the Z80 needing its own parser. |
| 7 | GET_MESSAGE | - | `message` (char[128]), `pending` (u8) | drains a host->guest text message queue. fusex posts `"FuseX: OK\n"` once at boot (visible in the real boot banner: `FuseX: OK`) |
| `0xFF` | (idle sentinel) | - | - | writing this value never triggers anything -- it's the "nothing pending" state the command byte sits in between calls |

## XFS protocol (page 0x49) -- the bigger piece

A full POSIX-flavoured virtual filesystem, same command/status/workspace
page shape as the Controller, but much larger (`struct xfs_registers_t`,
exactly 4096 bytes, `_Static_assert`-checked in fusex's own header).
Supports **4 simultaneous mount points** (0-3) and **16 open file
handles** (`XFS_MAX_FDS`).

```
offset 0x00  command   (u8,  XFS_CMD_*)
offset 0x01  status    (u8,  XFS_STATUS_IDLE/BUSY/COMPLETE/ERROR)
offset 0x02  result    (i16, error code or success value -- LittleFS-style negative error codes, see below)
offset 0x04  file_handle (u8)
offset 0x05  saved_page_a (u8) -- "saved by F_fetchpage, restored by F_restorepage"; this phrasing
             strongly suggests XFS's wire format mirrors a real NextZXOS/esxDOS-style ROM calling
             convention -- worth hunting for that spec by name before JASE locks in its own version
offset 0x06  mount_point (u8, 0-3)
offset 0x07  reserved (1 byte, alignment)
offset 0x08  arguments (256-byte union, one struct per command -- see table below)
offset 0x108 tmp/fops scratch (248 bytes; also aliased as {remaining:u16, total:u16} during transfers)
offset 0x200 workspace (1024 bytes -- actual file data lands here for read/write)
offset 0x600 module_space (2560 bytes, purpose not yet determined -- low priority to chase)
```

Commands (`XFS_CMD_*`, values 1-17):

| ID | Name | Argument struct | Purpose |
|----|------|------------------|---------|
| 1 | MOUNT | `protocol[32]`, `hostname[64]`, `path[160]` | e.g. protocol=`"xfs"`, hostname=`"ram"` for a local/RAM-backed mount, or a real hostname for an HTTPS-backed one (fusex has a ~1500-line `xfs_https.c` engine) |
| 2 | OPEN | `path[128]`, `flags` (u16, POSIX-style O_RDONLY/O_WRONLY/O_RDWR/O_CREAT/O_EXCL/O_TRUNC/O_APPEND), `mode` (u16) | |
| 3 | READ | `size` (u16) | result lands in the workspace area |
| 4 | WRITE | `size` (u16) | data supplied via the workspace area |
| 5 | CLOSE | - | |
| 6 | OPENDIR | `path[256]` | |
| 7 | READDIR | - | one entry per call, into workspace (`xfs_stat_info`: `type` u8, `size` u32, `name[64]`) |
| 8 | CLOSEDIR | - | |
| 9 | STAT | `path[256]` | richer `xfs_stat_t` (mode/uid/gid/size/atime/mtime/ctime + trailing uid/gid strings) -- comment says "matches stat.inc format", i.e. a real documented on-disk/ROM structure worth tracking down by name |
| 10 | UNLINK | `path[256]` | |
| 11 | MKDIR | `path[256]` | |
| 12 | RMDIR | `path[256]` | |
| 13 | CHDIR | `path[256]` | |
| 14 | GETCWD | `buffer_size` (u16) | |
| 15 | RENAME | `old_path[128]`, `new_path[128]` | |
| 16 | LSEEK | `offset` (u32), `whence` (u8, SEEK_SET/CUR/END) | |
| 17 | UNMOUNT | - | |

Error codes (`enum xfs_error`) are explicitly LittleFS-compatible negative
values (`XFS_ERR_IO=-5`, `XFS_ERR_NOENT=-2`, `XFS_ERR_EXIST=-17`, etc.) --
fusex's own comment says "mapped from LittleFS error codes", which is
another concrete lead: the real card's firmware likely runs LittleFS
internally for its flash-backed local storage, and XFS's error numbering
just passes that straight through.

`xfs_engine_t` is a clean plugin-engine vtable (mount/open/read/write/
close/lseek/opendir/readdir/closedir/stat/unlink/mkdir/rmdir/chdir/
getcwd/rename/free_handle) -- fusex has at least a local-filesystem
engine (`xfs_fs.c`) and an HTTPS engine (`xfs_https.c`, ~1500 lines).
JASE's own equivalent would plug in differently (its OsdMenu local
browser is already basically this same shape for a plain-disk engine;
Gopher/TNFS clients already built this session cover a similar niche to
what the HTTPS engine is for).

## What JASE already has that's directly reusable

- **ROMCS paging coordination** (`MemoryMap::romcsAsserted_` +
  `MinZX::reapplyMachinePaging()`): built for Multiface/DivIDE, same
  exact mechanism Spectranet needs (it's also a ROMCS-style card).
- **PC-trap page-in/page-out with one-step-deferred timing**
  (`Peripheral::onInstructionFetch`): same hook DivIDE's automap uses;
  Spectranet's trap set is simpler (one page-in condition, one fixed
  page-out address, no 8-byte-aligned range dance) so this should be
  *less* fiddly than DivIDE was, not more.
- **NMI hook** (`Peripheral::onNMI`): exists already, Spectranet just
  needs to also page itself in from here (on top of whatever else NMI
  already does -- unlike Multiface, Spectranet's NMI handling doesn't
  replace anything, it layers on).
- **Local file browsing** (`OsdMenu`'s `std::filesystem`-based browser):
  a good starting shape for an XFS "local/RAM" engine.
- **Clean-room network client precedent** (`Gopher`/`Tnfs` this same
  session): same spirit an XFS "https" engine fallback would need, if
  ever implemented -- though a full HTTPS client is a much bigger lift
  than Gopher/TNFS were (TLS, not just raw sockets).

## Suggested phased implementation order

1. **Base hardware skeleton** (no network/filesystem features yet):
   ports 0x003B/0x013B/0x023B/0x033B, the 4x4K ROMCS page window, PC-trap
   + NMI + programmable-trap paging, a minimal flash ROM (can start as a
   single placeholder page, same "16-byte clean-room stub, bring-your-
   own-dump" pattern Multiface already uses) and a 128K RAM pool. No
   W5100, no Controller, no XFS yet -- just prove detection (bit7
   read-back-inverted trick) and paging work, the same incremental way
   DivIDE/Multiface were built and verified this session.
2. **Controller page (0x48)**: the smallest, most self-contained
   feature -- GET_CONTROLLER_STATUS / message queue / a real
   DNS_GETHOSTBYNAME (trivial: `getaddrinfo()`, same as fusex). WiFi
   scan/connect can be the same kind of always-succeeds stub fusex uses,
   since there's no real WiFi concept in an emulator either way.
3. **Stdout port (0x043B)**: trivial, do it alongside step 2 (useful for
   debugging everything after it).
4. **XFS, local engine only**: MOUNT (hostname="ram" or similar) +
   OPEN/READ/WRITE/CLOSE/OPENDIR/READDIR/CLOSEDIR/STAT against a real
   host directory, reusing OsdMenu's existing path-handling conventions.
   This alone would make real Spectranext client software able to load
   files from the host -- probably the single highest-value milestone.
5. **XFS, remaining path ops** (UNLINK/MKDIR/RMDIR/CHDIR/GETCWD/RENAME/
   LSEEK): mechanical once step 4's plumbing exists.
6. **ENGINECALL (JSON/XPath)**: only worth doing once something actually
   needs it (i.e. once real client software exercising it is in hand to
   test against) -- low standalone value otherwise.
7. **W5100 raw Ethernet register emulation**: likely **skip unless proven
   necessary** -- if real Spectranext-aware client software only talks
   to the Controller/XFS pages (the high-level protocol), the low-level
   chip registers may never be touched in practice. Confirm with a real
   ROM/binary before investing here; this is the single biggest, least
   certain-to-be-needed chunk of the whole feature.
8. **XFS HTTPS engine**: biggest remaining lift (real TLS), same
   "defer until something concrete needs it" judgement call as step 7.

## Open questions to resolve before/while implementing

- Is there a published spec for the real Alioth Spectranet(next) card
  (the actual hardware this models) independent of fusex's own
  implementation? The `stat.inc` and `F_fetchpage`/`F_restorepage`
  naming in XFS strongly suggests one exists (sounds like an esxDOS- or
  NextZXOS-flavoured ROM API convention) -- worth a deliberate search
  before finalizing JASE's struct layouts, since matching the REAL
  hardware spec (not just fusex's incidental implementation) is what
  actually determines compatibility with real client software.
- Does real client software for this card ever poke the W5100 registers
  directly, or does everything go through Controller/XFS? Determines
  whether step 7 above is required at all.
- Confirm the PC-trap timing (deferred-by-one-instruction, like DivIDE)
  actually matters for Spectranet's narrower trap set once implemented
  -- don't assume the bug without reproducing it first.

## License note

Everything above is extracted as **facts** (port numbers, struct sizes,
field offsets, command IDs, trap addresses) from reading fusex's GPLv2
source for research purposes, the same "read to learn what the wire
protocol actually is, then write a fresh implementation" approach as
every other borrowed-facts peripheral in this project. No fusex source
is copied into JASE. The underlying Spectranet hardware itself long
predates fusex (2011, Philip Kendall) and its port/memory layout is a
hardware fact, not fusex's expression.
