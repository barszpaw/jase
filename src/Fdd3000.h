#pragma once
#include "Peripheral.h"
#include "MemoryMap.h"
#include "Wd1772.h"
#include "z80.h"
#include <cstdint>
#include <string>

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
// Simplifications vs real hardware, documented not hidden (see
// project memory for the full phased plan):
//   - The station's internal bootstrap (real hardware: a real,
//     copyrighted 2048-byte boot ROM, confirmed by disassembling --
//     NOT copying -- Fuse's own `fdd3boot.h`: polls the link port's DRQ
//     mirror bit and streams the real OS off disk via the FDC's data
//     register using a Force-Interrupt/Restore/Read-Sector sequence
//     that matches Wd1772's own command encoding exactly) is, by
//     default, a trivial built-in placeholder (DI; JR $) -- see
//     loadStationBootRom() for supplying the real dump instead. With
//     only the placeholder, the station can't boot anything real off
//     disk, but can still be driven by hand over the debug protocol
//     (write a loader into station RAM, set station PC) for testing.
//   - See Wd1772.h for the FDC's own simplification list (no step
//     timing, no Read/Write Track, no real CRC, writes not persisted).
//   - Station-CPU-to-host-CPU clock ratio: stepped kStationStepsPerHostInstr
//     (currently 4) times per host instruction. Real hardware's two Z80s
//     don't share a clock at all (see project memory) -- this ratio is
//     still an unverified placeholder, NOT re-derived from real crystal
//     frequencies. Raised from an original 1:1 after LINKTRACE (see
//     setLinkTraceEnabled()) measured the real boot handshake's station
//     response arriving ~2.5 virtual seconds too late at 1:1 -- 4x DOES
//     measurably help (the host actually completes its low-level poll
//     and conducts a real multi-byte command exchange with the station,
//     which never happened at all at 1:1). BUT raising it further (tried
//     16x) does NOT help more -- it only reaches the SAME terminal stuck
//     state faster (station settles into an unproductive 3-address loop,
//     PC cycling 0x1abb/0x1acd/0x1ad2, writing/reading the link byte
//     without the host ever responding further), proving this is NOT a
//     pure speed deficit alone -- something in the protocol SEQUENCING
//     itself (not just timing) makes the station give up mid-exchange.
//     See project memory's "ratio alone doesn't close it" section before
//     trying to brute-force this further with an even higher multiplier.
class Fdd3000 : public Peripheral
{
public:
	Fdd3000();
	~Fdd3000() override;

	const char* name() const override { return "FDD3000"; }
	bool in(uint16_t port, uint8_t& value) override;
	bool out(uint16_t port, uint8_t value) override;
	void onInstructionFetch(uint16_t pc, MemoryMap& memory, uint32_t tstates) override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

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
	// The station's own Z80operations: a private, self-contained 64K
	// address space + coarse I/O decode, entirely separate from MinZX's
	// (host) one. See class comment for what's stubbed.
	class StationOps : public Z80operations
	{
	public:
		explicit StationOps(Fdd3000& owner) : owner_(owner) {}
		uint8_t fetchOpcode(uint16_t address) override;
		uint8_t peek8(uint16_t address) override;
		void poke8(uint16_t address, uint8_t value) override;
		uint16_t peek16(uint16_t address) override;
		void poke16(uint16_t address, RegisterPair word) override;
		uint8_t inPort(uint16_t port) override;
		void outPort(uint16_t port, uint8_t value) override;
		void addressOnBus(uint16_t address, int32_t wstates) override;
		void interruptHandlingTime(int32_t wstates) override;
		bool isActiveINT(void) override;
	private:
		Fdd3000& owner_;
	};

	void applyHostPaging(MemoryMap& memory);
	void logLinkTrace(const char* side, const char* direction, uint8_t value) const;

	// See onInstructionFetch()'s own comment -- unverified placeholder,
	// tuned empirically against the real boot handshake's timing (measured
	// via LINKTRACE), not derived from real crystal frequencies.
	static const int kStationStepsPerHostInstr = 4;

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

	Wd1772 wd_;
	void writeConfigRegister(uint8_t value); // station block 7 -- see the bit-layout table above

	std::string romPath_;
	std::string diskPath_[Wd1772::kNumDrives];

	StationOps stationOps_;
	Z80* stationCpu_ = nullptr;
};
