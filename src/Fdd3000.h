#pragma once
#include "Peripheral.h"
#include "MemoryMap.h"
#include "mame_wd1772/mame_wd1772_wrapper.h"
#include "mame_z80/z80mame_wrapper.h"
#include <cstdint>
#include <string>

// Forward-declared here (not pulled in via mame_z80/mame_stub.h, which is
// internal plumbing for building z80.cpp itself, not a clean public API)
// purely so the `friend` declarations below can see these as having C
// linkage -- the actual declarations/definitions these must match live in
// mame_stub.h/Fdd3000.cpp respectively.
extern "C" {
	uint8_t mamez80_mem_read(uint16_t addr);
	void    mamez80_mem_write(uint16_t addr, uint8_t value);
	uint8_t mamez80_io_read(uint16_t port);
	void    mamez80_io_write(uint16_t port, uint8_t value);
}

// Timex FDD 3000 disk station stand-in -- a real, standalone Polish
// peripheral: a separate little computer (Z80A + 64K RAM + WD1772 FDC)
// connected to the Spectrum through a thin "interface" (4K ROM + 1K RAM +
// address-decode glue) that talks to the station over a single byte-wide
// link port. Facts below cross-checked from TWO independent, non-code
// sources (same clean-room policy as Multiface/DivIDE/SDI -- see
// project memory's "GPL firewall practice"): (1) `z80dasm` disassembly of
// the REAL interface ROM (`fdd3stavi.rom`/`fdd3timex.rom`, confirmed port
// 0xEF used repeatedly for IN/OUT, confirmed address #0604 is the
// documented "reactivate standard Spectrum ROM" exit point); (2) the
// 5-part "TOS bez tajemnic" magazine article (Bajtek, Wojciech Jabłoński),
// which documents the interface ROM's memory map, the command protocol,
// and the on-disk filesystem layout byte-for-byte. No code was copied
// from Fuse's GPL FDD3000 patch (`~/src/Fuse_FDD3000/`) even though it
// was read for orientation -- only protocol/hardware facts, same as
// everywhere else in this codebase.
//
// GOAL (see project memory, project_jase_fdd3000.md): NOT TOS/game
// compatibility -- running real CP/M 2.2 (confirmed real: a Polbrit
// "BIOS 3.1, 1987" boots on real `CPM 525.dsk`) on the station's OWN Z80,
// with the Spectrum reduced to a dumb terminal. This is why the station
// gets a REAL second Z80 instance here rather than a protocol-level
// command interpreter (that approach, considered and rejected, only
// covers TOS's own command set -- CP/M doesn't speak it at all once it's
// taken over the station).
//
// Station I/O block 7 (drive/config register) bit layout -- confirmed
// from a hardware-documentation comment found while reading Fuse's GPL
// patch for orientation (reproduced here as a bit-meaning table, a
// hardware fact, not as copied code -- same policy as everywhere else):
// bits 0-3 = /select drive 0-3 (active LOW -- first zero bit wins), bit
// 4 = side select (0 = side B, 1 = side A -- i.e. the signal is
// literally "/SIDE_B"), bit 5 = /DDEN (0 = double density), bit 6 =
// /BOOTROM (0 = station's low-memory boot-ROM view visible), bit 7 =
// /HEAD (0 = head loaded/active). Not modeled: bit 7's effect on real
// head-load timing (see Wd1772.h's own simplification list).
//
// Architecture (two independent Z80 address spaces, two independent
// Z80 CPUs):
//
//   HOST side (the Spectrum's own Z80, already emulated by MinZX):
//     - port 0xEF: the single link byte, read = what the station last
//       wrote, write = latches a byte for the station to read.
//     - ROMCS paging of the interface ROM/RAM over pages 0/1
//       (0x0000-0x3FFF): page in on PC==0x0000 or PC==0x0008, page out
//       on PC==0x0604 -- IMMEDIATE/same-cycle, NOT deferred by one
//       onInstructionFetch() call like DivIDE's trapAfter_/unmapAfter_.
//       Confirmed from Fuse's real, ACTIVE paging hooks (z80_ops.c's
//       CHECK(fdd3000_early,...)/CHECK(fdd3000_late,...), which run
//       before the trap address's own opcode is fetched -- the
//       superficially similar CHECK(fdd3000,...) block is dead code,
//       fully commented out). Using DivIDE's deferred pattern here was
//       a real, confirmed bug (host PC stuck looping the boot handshake
//       forever instead of completing it).
//
//   STATION side (a second, independent Z80 + 64K RAM, owned entirely by
//   this class -- MinZX never sees it): stepped from onInstructionFetch()
//   so it advances in lockstep with the host's own virtual time (matters
//   for JASE's pausable/single-steppable debug protocol -- see project
//   memory for why an async, wall-clock-paced second process was
//   rejected in favor of this). I/O is coarse-decoded the same way the
//   real hardware's address-decode glue does: ioport = (port&0xFF)>>5,
//   giving 8 blocks of 32 ports (block 1 = the link byte, with read bit
//   7 mirroring the WD1772's own DRQ line -- confirmed by disassembling
//   the real station boot ROM's own polling loop; blocks 2-5 = RS232C,
//   not implemented; block 6 = WD1772 FDC registers (status/cmd, track,
//   sector, data -- see Wd1772.h); block 7 = drive/config register, see
//   the bit-layout table above).
//
//   Station CPU core: MAME's real `z80_device` (src/mame_z80/, BSD-3-
//   Clause, Juergen Buchmueller, via a thin `z80mame_*` C wrapper -- same
//   shape the earlier `fdd3000-mame` branch already built and verified
//   standalone: a real behavioral test (DJNZ/HALT timing) plus a direct
//   replay-comparison against real captured Fuse traffic, byte-for-byte
//   identical to Fuse's own Z80free core on everything exercised).
//   Swapped in here (branch fdd3000-inprocess-mame, 2026-11-09) in place
//   of the Z80free core this file used before (itself a real port of
//   Fuse's own station CPU, GPL-3 -- see CLAUDE.md's "GPL firewall
//   LIFTED for FDD3000" note, 2026-11-04) -- NOT because Z80free was
//   found incorrect (it wasn't, twice confirmed), but to go back to
//   BSD-3-Clause-only licensing for this file plus reuse the same,
//   already-verified-correct WD1772 port (see below) behind one
//   consistent core family. The wrapper's own API is a single global
//   instance (no per-call context pointer for mem/io read/write) --
//   `s_activeForMameZ80` in the .cpp dispatches those global callbacks to
//   whichever Fdd3000 instance is current (JASE only ever has one FDD3000
//   peripheral active at a time, same constraint Z80free's own API
//   already had).
//
//   Station FDC core: MAME's real `wd1772_device` (src/mame_wd1772/,
//   BSD-3-Clause, Olivier Galibert) via `MameWd1772`, wrapped behind the
//   EXACT SAME method-name API the clean-room `Wd1772` class (src/
//   Wd1772.h, used by this file before) already used -- a near-drop-in
//   swap, confirmed correct standalone (byte-exact real sector reads vs.
//   DskImage's own ground truth) on the earlier `fdd3000-mame` branch
//   before being carried over here. One real API difference: `tick()`
//   now takes the real T-states the just-executed station instruction
//   consumed (MAME's own `z80mame_step()` return value) instead of being
//   called with no argument once per station step -- see
//   stepStationForOneHostInstruction()'s own comment.
//
// Simplifications vs real hardware, documented not hidden (see
// project memory for the full phased plan):
//   - The station's internal bootstrap (real hardware: a real,
//     copyrighted 2048-byte boot ROM, confirmed by disassembling --
//     NOT copying -- Fuse's own `fdd3boot.h`: polls the link port's DRQ
//     mirror bit and streams the real OS off disk via the FDC's data
//     register using a Force-Interrupt/Restore/Read-Sector sequence
//     that matches the real WD1772's command encoding exactly) is, by
//     default, a trivial built-in placeholder (DI; JR $) -- see
//     loadStationBootRom() for supplying the real dump instead. With
//     only the placeholder, the station can't boot anything real off
//     disk, but can still be driven by hand over the debug protocol
//     (write a loader into station RAM, set station PC) for testing.
//   - The real WD1772 port DOES model step/settle/spin-up timing and
//     digital-PLL bit-level MFM decode (a strict upgrade over the
//     clean-room `Wd1772` this file used before, which had none of
//     that) -- see mame_wd1772/mame_wd1772_wrapper.h's own comment for
//     what's still simplified (no Read/Write Track, writes not
//     persisted to the backing DskImage).
//   - Station-CPU-to-host-CPU clock ratio: a REAL clock-domain crossing
//     as of 2026-11-09 (user-directed: "does a host T-state tick
//     actually get passed to the station's own clock?" -- it didn't
//     before this, hostTstates was received and silently discarded).
//     Host Spectrum = 3.5MHz, station = 4MHz (M397/FDD35 schematics) --
//     an exact 8:7 ratio, accumulated in real T-state units (see
//     stationTstateBudget7_/stepStationForOneHostInstruction()), NOT an
//     instruction-count heuristic. Replaces the earlier EXPERIMENTAL 5:2
//     (2.5x) BY INSTRUCTION COUNT scheme (first confirmed success this
//     whole investigation, 2026-11-05: hit the host dispatcher's
//     success-return address 0x04c3 exactly once in 60M steps with the
//     OLD Z80free+clean-room-Wd1772 pairing, see project memory) -- that
//     scheme is not known WRONG, just superseded by a more principled
//     real-clock-ratio approach now that both cores are genuinely
//     T-state-aware (z80mame_step()'s own return value, MameWd1772::
//     tick()'s own required argument) rather than a guessed ratio tuned
//     by trial and error against cores that weren't. See project memory
//     for the full tuning history
//     (1x/4x/16x/6:5-by-count/T-state-proportional/single-instruction/
//     reactive-burst/bridge-process/5:2-by-count/8:7-by-T-state) and
//     next-session leads.
class Fdd3000 : public Peripheral
{
public:
	Fdd3000();
	~Fdd3000() override;

	const char* name() const override { return "FDD3000"; }
	bool in(uint16_t port, uint8_t& value) override;
	bool out(uint16_t port, uint8_t value) override;
	void onInstructionFetch(uint16_t pc, MemoryMap& memory, uint32_t tstates) override;
	void onInstructionExecuted(MemoryMap& memory, uint32_t tstates) override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

	// The host-side interface's own 1K RAM (interfaceRam_ below) is a
	// real, physically separate RAM chip on the interface card -- a plain
	// Z80 RESET pulse (onReset(false), F11/Dandanator-button-triggered)
	// does not touch it on real hardware, same as it doesn't touch the
	// station's own RAM across the link. Only a genuine power-cycle
	// (onReset(true), MinZX::hardReset()) clears it back to its true
	// power-on content -- matches the constructor's own
	// `memset(interfaceRam_.data, 0x00, ...)`. User-observed, 2026-11-08:
	// the real STAVI/TI-of-TTL boot banner appears on real hardware only
	// intermittently after a plain Z80 reset, suspected to be exactly
	// this kind of leftover-interface-RAM-state nondeterminism (also
	// mirrors a documented Fuse convention: emulated ports default 0x00
	// if used, 0xFF if not, which is itself a power-on-state concept
	// distinct from a mid-session RESET).
	void onReset(bool hard) override;

	// --fdd3000 on the command line -- off by default, same convention
	// as --multiface1/--divide.
	void setEnabled(bool on) { enabled_ = on; }
	bool isEnabled() const { return enabled_; }

	// The real interface ROM -- accepts 8192 bytes (the real, full 8K
	// ROMCS window content, preferred), 32768 bytes (a full 27C256 EPROM
	// dump, e.g. `docs/FDD3000/M397/TIofTTLv3/Ti_of_TTL_128k.rom` -- only
	// its first 8K is actually used, see .cpp), or 4096 bytes (the older
	// `fdd3stavi.rom`/`fdd3timex.rom` dumps -- mirrored into both halves
	// of the 8K window, a confirmed-approximate fallback, NOT accurate:
	// see the .cpp's own comment for how this was found wrong).
	bool loadInterfaceRom(const char* path);
	const std::string& getRomPath() const { return romPath_; }

	// The STATION's own internal boot ROM -- a SEPARATE, real chip from
	// the interface ROM above (confirmed: Fuse's own patch keeps it as a
	// literal byte array baked into `fdd3boot.h`'s C source, not a
	// loadable `roms/*.rom` file the way `fdd3stavi.rom`/`fdd3timex.rom`
	// are -- `Addr < 2048` in that same source is why this is exactly
	// 2048 bytes, see kStationBootRomSize). That array is real,
	// copyrighted firmware content (dumped from a real FDD3000 board),
	// same category as every other ROM in this codebase -- NOT embedded
	// in JASE's source even via a "rewritten" transcription, bring your
	// own dump. Without one, the built-in placeholder (DI; JR $, see
	// constructor) keeps the station CPU idling harmlessly instead of
	// running real firmware.
	bool loadStationBootRom(const char* path);
	const std::string& getStationRomPath() const { return stationRomPath_; }

	// A real disk image (CPCEMU/Extended DSK -- see DskImage.h) for one
	// of the station's 4 drives (0=A..3=D), e.g. the real `CPM 525.dsk`/
	// `CPM tos a2 start cpm 525.dsk` found for this project.
	bool insertDisk(int driveIndex, const char* path);
	// Last successfully inserted path per drive ("" if never set) --
	// OsdMenu's own peripheral config screens read this to show/persist
	// current state.
	const std::string& getDiskPath(int driveIndex) const { return diskPath_[driveIndex]; }

	// FDD3000's own physical reset button -- separate from MinZX::
	// reset()/F11. Real workflow (confirmed live against the working
	// Fuse+FDD3000 build, see project memory): swap which disk image is
	// in "drive A", THEN press this -- the station's tiny boot ROM just
	// loads whatever is in drive A at the moment it resets, which is how
	// real hardware switches between booting TOS and booting CP/M. The
	// host Z80 (and whatever terminal/monitor program is running on it)
	// is deliberately left untouched.
	void stationReset();

	// Live decoder for the ONE wire that actually connects the interface
	// (host side) to the station (its own Z80): the link byte, port 0xEF
	// on the host / I/O block 1 on the station. Same ON/OFF runtime-toggle
	// convention as SDI_8255's IECTRACE/IOTRACE (see SDI_8255.h) -- no
	// rebuild needed, toggle via "FDD3000 LINKTRACE ON/OFF" over the debug
	// protocol or --debug-fdd3000-linktrace on the command line. Logs
	// EVERY host IN/OUT on 0xEF and EVERY station IN/OUT on block 1 (the
	// two latches, linkToStation_/linkFromStation_), each tagged with both
	// CPUs' current PC -- built so a real host<->station handshake (or a
	// stuck one, like the still-open TOS-banner gap, see project memory)
	// can be watched byte-by-byte instead of inferred from register
	// snapshots after the fact.
	void setLinkTraceEnabled(bool on) { linkTraceEnabled_ = on; }
	bool isLinkTraceEnabled() const { return linkTraceEnabled_; }

private:
	// MAME's z80_device wrapper API (mamez80_mem_read/write/io_read/write,
	// mame_z80/z80mame_wrapper.h) is global, not a per-instance method --
	// same shape Z80free's own Z80free_Rd/Wr/In/Out had, see class
	// comment. These free functions (defined in the .cpp) are the only
	// external callers of the private station-access methods below,
	// dispatched through s_activeForMameZ80.
	friend uint8_t ::mamez80_mem_read(uint16_t);
	friend void ::mamez80_mem_write(uint16_t, uint8_t);
	friend uint8_t ::mamez80_io_read(uint16_t);
	friend void ::mamez80_io_write(uint16_t, uint8_t);

	void applyHostPaging(MemoryMap& memory);
	void logLinkTrace(const char* side, const char* direction, uint8_t value) const;
	void stepStationForOneHostInstruction(uint32_t hostTstates);
	void syncStationInterrupt(); // raises/clears the MAME core's level-triggered IRQ line from wd_.intrq(), see .cpp

	// The station's own memory/port access, called from the global
	// mamez80_mem_read/write/io_read/write callbacks via
	// s_activeForMameZ80 -- same logic that used to live in the
	// StationOps class (removed when the station switched from z80cpp to
	// Z80free, see class comment). Also reused directly by
	// debugCommand()'s MEM/POKE sub-commands, same as before.
	uint8_t stationPeek8(uint16_t address);
	void stationPoke8(uint16_t address, uint8_t value);
	uint8_t stationInPort(uint16_t port);
	void stationOutPort(uint16_t port, uint8_t value);

	// Real clock-domain-crossing budget, in units of "1/kRatioDen-th of a
	// station T-state" (kRatioNum/kRatioDen, see
	// stepStationForOneHostInstruction()'s own comment -- currently an
	// EXPERIMENTAL 1:1, not the real 8:7). int64_t: at hostTstates up to
	// ~23 per call times kRatioNum, and potentially many calls between
	// resets, a plain 32-bit counter could in principle overflow over a
	// very long run; 64 bits has no realistic overflow concern.
	int64_t stationTstateBudget7_ = 0;

	// onInstructionExecuted()'s tstates param is MinZX::update()'s raw
	// per-FRAME cumulative counter (0..cycleTstates), not a per-instruction
	// delta -- same trap Dandanator.h's monoTstates_ comment documents.
	// Feeding the raw cumulative value straight into the station T-state
	// budget (as an earlier version of this file did) adds the ENTIRE
	// elapsed-this-frame count on every single host instruction, so the
	// budget -- and the number of station instructions stepped to drain
	// it -- grows unboundedly across a frame (roughly cycleTstates^2),
	// which is indistinguishable in practice from a true hang (confirmed
	// live, 2026-11-09: native process spinning at 100% CPU, unresponsive
	// to SDL/F1, needing kill -9, only when --fdd3000 is enabled).
	// lastRawTstates_ + the same wrap-safe delta computation Dandanator
	// uses fixes it.
	uint32_t lastRawTstates_ = 0;

	bool enabled_ = false;
	bool linkTraceEnabled_ = false;
	uint16_t lastHostPc_ = 0; // stashed in onInstructionFetch(), for logLinkTrace()'s own hostPC column

	// Host-side ROMCS paging state -- IMMEDIATE/same-cycle PC trap, see
	// class comment for why this differs from DivIDE's deferred pattern.
	bool active_ = false;

	MemoryMap::Bank interfaceRom_{}; // host page 0 (0x0000-0x1FFF): real 4K ROM, mirrored to fill 8K
	MemoryMap::Bank interfaceRam_{}; // host page 1 (0x2000-0x3FFF): real 1K RAM (article: #2000-#23FF), rest of the 8K window along for the ride

	// Station side: a second, independent Z80 + 64K RAM, entirely
	// private to this class.
	uint8_t stationRam_[65536] = {};
	bool stationPaged_ = true;   // low-address boot-ROM view (see bootRom_), real hardware's "/BOOTROM" config bit, see writeConfigRegister()
	static const int kStationBootRomSize = 2048; // confirmed from Fuse's own "Addr < 2048" paged-view check
	uint8_t bootRom_[kStationBootRomSize]; // real dump if loaded (see loadStationBootRom()), placeholder DI;JR $ otherwise
	std::string stationRomPath_;
	uint8_t linkToStation_ = 0;  // host->station latch (real hardware's "ti2fdd")
	uint8_t linkFromStation_ = 0; // station->host latch (real hardware's "fdd2ti")

	MameWd1772 wd_;
	void writeConfigRegister(uint8_t value); // station block 7 -- see the bit-layout table above

	std::string romPath_;
	std::string diskPath_[MameWd1772::kNumDrives];

	// MAME's z80_device (src/mame_z80/) is driven through a global C API,
	// same singleton shape Z80free's own Z80free_* calls had -- no
	// per-instance CPU-state member needed here (see class comment: JASE
	// only ever has one FDD3000 peripheral active at a time).
};
