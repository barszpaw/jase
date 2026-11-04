#include "Fdd3000.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <sstream>
#include <vector>
#include <SDL2/SDL.h>

Fdd3000::Fdd3000()
	: stationOps_(*this)
{
	memset(interfaceRom_.data, 0xFF, MemoryMap::PAGE_SIZE);
	interfaceRom_.isRom = true;
	memset(interfaceRam_.data, 0x00, MemoryMap::PAGE_SIZE);
	interfaceRam_.isRom = false;

	// Placeholder station bootstrap: DI ; JR $ (idle forever), rest
	// erased-EPROM 0xFF (same convention as SDI_8255's unmapped ROM
	// bank). See loadStationBootRom() for supplying the real 2048-byte
	// dump instead.
	memset(bootRom_, 0xFF, sizeof(bootRom_));
	bootRom_[0] = 0xF3; // DI
	bootRom_[1] = 0x18; // JR
	bootRom_[2] = 0xFE; // -2 (jump to self)

	stationCpu_ = new Z80(&stationOps_);
	stationCpu_->reset();
}

Fdd3000::~Fdd3000()
{
	delete stationCpu_;
}

// Accepts three real file shapes, all confirmed against docs/FDD3000/M397
// (a modern, documented TTL clone of the interface -- real schematic +
// EPROM jumper table + a real 32768-byte dump, `Ti_of_TTL_128k.rom`):
//   - 8192 bytes: the real, FULL ROM content for the host's 8K ROMCS
//     window (page 0, 0x0000-0x1FFF) -- used as-is.
//   - 32768 bytes: a full 27C256 EPROM dump (the M397 schematic's BOM
//     lists exactly this chip). The schematic's own address-line jumper
//     table (A13/A14, see EPROM_jumper.jpg) shows the REAL populated
//     config for a 27256 ties BOTH A13 and A14 to a fixed 0 -- i.e. no
//     bank switching at all; only the chip's OWN first 8192 bytes are
//     ever hardware-addressable. That first 8K is what gets used here;
//     the remaining 24K of the file is simply never wired up.
//   - 4096 bytes (e.g. the older fdd3stavi.rom/fdd3timex.rom dumps,
//     confirmed only 14 bytes different from the real 8K dump's own
//     first 4K): kept for backward compatibility, but THIS WAS THE REAL
//     BUG (found 2026-11-02, see project memory) -- a 4K file was being
//     MIRRORED into both halves of the 8K window, on the unverified
//     assumption the real chip's second 4K duplicates the first. It
//     doesn't: a live hash compare of `Ti_of_TTL_128k.rom`'s two 4K
//     halves shows them completely different. Any real code living in
//     0x1000-0x1FFF (which the trace confirms execution DOES reach --
//     the ROM's own documented banner/startup procedure at
//     #0EE1-#0FFF) was silently running a wrong, repeated copy of the
//     FIRST 4K instead. Still mirrors for this 4K case (better than
//     refusing to load at all), but a real dump should be at least 8K.
bool Fdd3000::loadInterfaceRom(const char* path)
{
	FILE* pf = fopen(path, "rb");
	if (pf == NULL) {
		fprintf(stderr, "Fdd3000::loadInterfaceRom: cannot open %s\n", path);
		return false;
	}
	static const size_t kHalfRomSize = 4096;
	static const size_t kFullRomSize = 8192;
	static const size_t kEpromDumpSize = 32768;
	std::vector<uint8_t> buf(kEpromDumpSize);
	size_t n = fread(buf.data(), 1, buf.size(), pf);
	fclose(pf);

	if (n == kFullRomSize || n == kEpromDumpSize) {
		// Both cases: the real content is simply the first 8K (an exact
		// dump, or a 27256's full capacity with only its low 8K actually
		// wired up per the real jumper table -- see comment above).
		memcpy(interfaceRom_.data, buf.data(), kFullRomSize);
	} else if (n == kHalfRomSize) {
		fprintf(stderr, "Fdd3000::loadInterfaceRom: %s is 4096 bytes -- mirroring into both "
		        "halves of the 8K window, but this is KNOWN WRONG for real hardware (the real "
		        "second 4K is NOT a mirror, see Fdd3000.cpp's own comment). Use an 8192 or "
		        "32768-byte dump if you have one.\n", path);
		memcpy(interfaceRom_.data, buf.data(), kHalfRomSize);
		memcpy(interfaceRom_.data + kHalfRomSize, buf.data(), kHalfRomSize);
	} else {
		fprintf(stderr, "Fdd3000::loadInterfaceRom: %s is %zu bytes, expected 4096, 8192, or 32768\n",
		        path, n);
		return false;
	}
	romPath_ = path;
	printf("Fdd3000::loadInterfaceRom: loaded interface ROM from %s (%zu bytes)\n", path, n);
	return true;
}

bool Fdd3000::loadStationBootRom(const char* path)
{
	FILE* pf = fopen(path, "rb");
	if (pf == NULL) {
		fprintf(stderr, "Fdd3000::loadStationBootRom: cannot open %s\n", path);
		return false;
	}
	uint8_t buf[kStationBootRomSize];
	size_t n = fread(buf, 1, sizeof(buf), pf);
	fclose(pf);
	if (n != sizeof(buf)) {
		fprintf(stderr, "Fdd3000::loadStationBootRom: %s is %zu bytes, expected exactly %d\n",
		        path, n, kStationBootRomSize);
		return false;
	}
	memcpy(bootRom_, buf, sizeof(buf));
	stationRomPath_ = path;
	printf("Fdd3000::loadStationBootRom: loaded station boot ROM from %s\n", path);
	return true;
}

bool Fdd3000::insertDisk(int driveIndex, const char* path)
{
	if (!wd_.insertDisk(driveIndex, path)) return false;
	if (driveIndex >= 0 && driveIndex < Wd1772::kNumDrives) diskPath_[driveIndex] = path;
	return true;
}

void Fdd3000::stationReset()
{
	stationCpu_->reset();
	stationPaged_ = true;
	linkToStation_ = 0;
	linkFromStation_ = 0;
	stationTstateBudget_ = 0.0; // don't carry a stale backlog across a reset
	wd_.reset();
}

// Station block 7 -- see Fdd3000.h's bit-layout table (a hardware fact,
// not copied logic). All five fields are active-LOW on the wire, so the
// decode below inverts each bit's sense explicitly rather than reading
// as if 1 meant "selected".
void Fdd3000::writeConfigRegister(uint8_t value)
{
	int drive = -1;
	for (int bit = 0; bit < 4; bit++) {
		if (!((value >> bit) & 1)) { drive = bit; break; }
	}
	wd_.selectDrive(drive);
	wd_.setSide((value & 0x10) == 0);      // bit4=0 -> side B
	wd_.setDensity((value & 0x20) == 0);   // bit5=0 -> double density
	stationPaged_ = (value & 0x40) == 0;   // bit6=0 -> boot-ROM view visible
	// bit7 (/HEAD) not modeled -- see Wd1772.h's simplification list.
}

// See Fdd3000.h's setLinkTraceEnabled() comment for why this exists and
// what it logs. Timestamped the same way IECTRACE/IOTRACE are (seconds
// since SDL init via SDL_GetPerformanceCounter) so all three trace
// streams line up on one shared clock if ever captured together.
void Fdd3000::logLinkTrace(const char* side, const char* direction, uint8_t value) const
{
	double ts = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
	char bin[9];
	for (int b = 0; b < 8; b++) bin[b] = ((value >> (7 - b)) & 1) ? '1' : '0';
	bin[8] = '\0';
	fprintf(stderr,
	        "FDD3000LINK ts=%.6f hostPC=%04x stnPC=%04x side=%s dir=%s val=%02x bin=%s\n",
	        ts, lastHostPc_, stationCpu_->getRegPC(), side, direction, value, bin);
}

bool Fdd3000::in(uint16_t port, uint8_t& value)
{
	if (!enabled_) return false;
	if ((port & 0xFF) != 0xEF) return false;
	value = linkFromStation_;
	if (linkTraceEnabled_) logLinkTrace("HOST", "IN ", value);
	return true;
}

bool Fdd3000::out(uint16_t port, uint8_t value)
{
	if (!enabled_) return false;
	if ((port & 0xFF) != 0xEF) return false;
	linkToStation_ = value;
	if (linkTraceEnabled_) logLinkTrace("HOST", "OUT", value);
	return true;
}

void Fdd3000::applyHostPaging(MemoryMap& memory)
{
	if (active_) {
		memory.mapPage(0, &interfaceRom_);
		memory.mapPage(1, &interfaceRam_);
		memory.setRomcsAsserted(true);
	} else {
		memory.setRomcsAsserted(false);
	}
}

void Fdd3000::onInstructionFetch(uint16_t pc, MemoryMap& memory, uint32_t tstates)
{
	if (!enabled_) return;
	lastHostPc_ = pc; // stashed for logLinkTrace()'s own hostPC column

	// Host-side ROMCS automap -- IMMEDIATE/same-cycle, NOT deferred by one
	// instruction (unlike DivIde's trapAfter_/unmapAfter_). Confirmed from
	// the real, active Fuse code (z80/z80_ops.c's CHECK(fdd3000_early,...)
	// / CHECK(fdd3000_late,...) -- the fully commented-out CHECK(fdd3000,...)
	// block is dead code): both hooks run BEFORE the opcode at pc is
	// actually fetched, so the trap address's own opcode already comes
	// from the POST-trap mapping. Using DivIDE's deferred pattern here
	// was a real bug (traced live: host PC stuck looping 0x0000-0x8000
	// forever, re-running the boot handshake instead of completing it).
	if (!active_ && (pc == 0x0000 || pc == 0x0008)) {
		active_ = true;
	} else if (active_ && pc == 0x0604) {
		active_ = false;
	}
	applyHostPaging(memory);

	stepStationForOneHostInstruction(tstates);
}

// T-state-proportional stepping -- see Fdd3000.h's class comment for why
// this replaced both the earlier flat multipliers and Fuse's own
// instruction-COUNT ratio (6:5), neither of which actually tracks real
// elapsed time between JASE's and Fuse's differently-costed Z80 cores.
void Fdd3000::stepStationForOneHostInstruction(uint32_t hostTstates)
{
	// hostTstates is the host's running tstates counter, sampled once per
	// host instruction (at fetch time) -- the delta since the last call is
	// exactly how much real host time elapsed while the station was last
	// left to catch up. MinZX wraps tstates to 0 at every frame boundary
	// (see minzx.cpp), so a decrease means a wrap just happened; treat the
	// new (small) value itself as the delta in that case rather than
	// underflowing.
	uint32_t hostDelta = (hostTstates >= lastHostTstates_)
		? (hostTstates - lastHostTstates_)
		: hostTstates;
	lastHostTstates_ = hostTstates;

	stationTstateBudget_ += hostDelta * kStationToHostClockRatio;

	// At most ONE station instruction per host instruction -- deliberately
	// NOT a catch-up loop. Both sides run the same narrow, bounded-retry
	// (DJNZ ~256) polling loop waiting for each other's ACK nibble; a
	// batched multi-instruction catch-up here lets the station race ahead
	// in a single burst and then stall, so the two CPUs' polling loops
	// drift out of step with each other even though the LONG-RUN ratio is
	// correct on average -- see project memory / docs/FDD3000 for the live
	// evidence (host sees the expected ACK byte only ~6 times in 48M
	// polls). Stepping one instruction at a time, carrying any leftover
	// budget to the next call, keeps the two CPUs interleaved as finely as
	// the host's own per-instruction granularity allows.
	if (stationTstateBudget_ > 0.0) {
		uint32_t before = stationTstates_;
		stationCpu_->execute();
		uint32_t spent = stationTstates_ - before;
		if (spent == 0) spent = 4; // defensive floor -- a real opcode always costs T-states
		stationTstateBudget_ -= (double)spent;
	}
}

// Sub-commands below give the SAME station-side access the top-level
// debug protocol already gives the host (MEM/POKE/REGS/SETREG/STEP) --
// needed because the station is a second, entirely private Z80 + RAM
// that MinZX/DebugServer never sees (see Fdd3000.h's class comment).
// "FDD3000 <SUBCMD> ..." instead of a second top-level command name,
// same convention as every other peripheral's debugCommand() (toks[0]
// is always this device's own name; sub-dispatch is this class's own
// business, not DebugServer's).
std::string Fdd3000::debugCommand(const std::vector<std::string>& toks)
{
	if (toks.empty() || toks[0] != "FDD3000")
		return std::string();

	char out[256];

	if (toks.size() == 1) {
		snprintf(out, sizeof(out),
		         "OK enabled=%d active=%d linkToStation=%02x linkFromStation=%02x stationPaged=%d stationPC=%04x "
		         "wdStatus=%02x wdTrack=%02x wdSector=%02x wdDrq=%d wdIntrq=%d",
		         enabled_ ? 1 : 0, active_ ? 1 : 0, linkToStation_, linkFromStation_,
		         stationPaged_ ? 1 : 0, stationCpu_->getRegPC(),
		         wd_.peekStatus(), wd_.readTrackReg(), wd_.readSectorReg(), wd_.drq() ? 1 : 0, wd_.intrq() ? 1 : 0);
		return out;
	}

	std::string sub = toks[1];
	for (char& c : sub) c = (char)toupper((unsigned char)c);

	if (sub == "REGS") {
		snprintf(out, sizeof(out),
		         "OK AF=%04x BC=%04x DE=%04x HL=%04x AFx=%04x BCx=%04x DEx=%04x HLx=%04x "
		         "IX=%04x IY=%04x SP=%04x PC=%04x I=%02x R=%02x IFF1=%d IFF2=%d IM=%d",
		         stationCpu_->getRegAF(), stationCpu_->getRegBC(), stationCpu_->getRegDE(), stationCpu_->getRegHL(),
		         stationCpu_->getRegAFx(), stationCpu_->getRegBCx(), stationCpu_->getRegDEx(), stationCpu_->getRegHLx(),
		         stationCpu_->getRegIX(), stationCpu_->getRegIY(), stationCpu_->getRegSP(), stationCpu_->getRegPC(),
		         stationCpu_->getRegI(), stationCpu_->getRegR(),
		         stationCpu_->isIFF1() ? 1 : 0, stationCpu_->isIFF2() ? 1 : 0, (int)stationCpu_->getIM());
		return out;
	}

	if (sub == "SETREG" && toks.size() >= 4) {
		// Same register set/spelling as the host's own SETREG (see
		// debugserver.cpp) -- "FDD3000 SETREG PC 3f00" jumps the station
		// CPU, same shape as the top-level "SETREG PC ..." does for the
		// host.
		uint16_t val = (uint16_t)strtoul(toks[3].c_str(), nullptr, 16);
		std::string reg = toks[2];
		for (char& c : reg) c = (char)toupper((unsigned char)c);
		bool ok = true;
		if (reg == "AF") stationCpu_->setRegAF(val);
		else if (reg == "BC") stationCpu_->setRegBC(val);
		else if (reg == "DE") stationCpu_->setRegDE(val);
		else if (reg == "HL") stationCpu_->setRegHL(val);
		else if (reg == "AFX") stationCpu_->setRegAFx(val);
		else if (reg == "BCX") stationCpu_->setRegBCx(val);
		else if (reg == "DEX") stationCpu_->setRegDEx(val);
		else if (reg == "HLX") stationCpu_->setRegHLx(val);
		else if (reg == "IX") stationCpu_->setRegIX(val);
		else if (reg == "IY") stationCpu_->setRegIY(val);
		else if (reg == "SP") stationCpu_->setRegSP(val);
		else if (reg == "PC") stationCpu_->setRegPC(val);
		else ok = false;
		return ok ? "OK" : "ERR unknown register";
	}

	if (sub == "MEM" && toks.size() >= 4) {
		// Reads through StationOps::peek8() -- i.e. whatever the station
		// CPU itself currently sees (boot-ROM view while stationPaged_,
		// RAM otherwise), same as the host's own MEM reading through
		// MinZX::peek8() rather than a raw array. Same hex-string shape
		// and length cap as the host command.
		uint16_t addr = (uint16_t)strtoul(toks[2].c_str(), nullptr, 16);
		long len = strtol(toks[3].c_str(), nullptr, 10);
		if (len < 0) len = 0;
		if (len > 4096) len = 4096;
		std::string resp = "OK ";
		char hex[3];
		for (long i = 0; i < len; i++) {
			snprintf(hex, sizeof(hex), "%02x", stationOps_.peek8((uint16_t)(addr + i)));
			resp += hex;
		}
		return resp;
	}

	if (sub == "POKE" && toks.size() >= 4) {
		// Writes through StationOps::poke8() -- always lands in the
		// underlying RAM regardless of stationPaged_ (see poke8()'s own
		// comment), same real-hardware shadowing behavior Dandanator's
		// onMemoryWrite() relies on for the host side.
		uint16_t addr = (uint16_t)strtoul(toks[2].c_str(), nullptr, 16);
		std::istringstream bs(toks[3]);
		std::string b;
		int count = 0;
		while (std::getline(bs, b, ',')) {
			stationOps_.poke8((uint16_t)(addr + count), (uint8_t)strtoul(b.c_str(), nullptr, 16));
			count++;
		}
		return "OK";
	}

	if (sub == "STEP") {
		// Steps the station CPU directly, independent of the host's own
		// instruction stream -- useful for driving/inspecting the
		// station in isolation (e.g. after POKEing in a test routine and
		// SETREG PC-ing to it) without needing the host Z80 to also be
		// running. NOT the same as the normal 1:1 coupling in
		// onInstructionFetch(), which keeps advancing regardless.
		int n = toks.size() >= 3 ? atoi(toks[2].c_str()) : 1;
		if (n < 1) n = 1;
		for (int i = 0; i < n; i++) stationCpu_->execute();
		return "OK";
	}

	if (sub == "RESET") {
		stationReset();
		return "OK";
	}

	if (sub == "LINKTRACE") {
		// Same ON/OFF runtime-toggle convention as SDI_8255's IECTRACE --
		// see setLinkTraceEnabled()'s header comment for what this logs.
		if (toks.size() >= 3) {
			if (toks[2] == "ON") setLinkTraceEnabled(true);
			else if (toks[2] == "OFF") setLinkTraceEnabled(false);
		}
		snprintf(out, sizeof(out), "OK linktrace=%d", isLinkTraceEnabled() ? 1 : 0);
		return out;
	}

	return "ERR unknown FDD3000 sub-command";
}

// ---- StationOps: the station's own, entirely private 64K address space ----
//
// T-state accounting below mirrors MinZX's own peek8/poke8/inPort/outPort
// (minzx.cpp) exactly -- same base costs, no contention (the station has
// none) -- so stationTstates_ is a real, comparable running counter, the
// same way MinZX's own `tstates` member is. See Fdd3000::
// stepStationForOneHostInstruction() for how this drives the station's
// clock-ratio-proportional stepping.

uint8_t Fdd3000::StationOps::fetchOpcode(uint16_t address)
{
	uint8_t v = peek8(address); // peek8 already accounts the shared 3 T-states
	owner_.stationTstates_ += 1; // M1 cycle's extra T-state over a plain memory read
	return v;
}

uint8_t Fdd3000::StationOps::peek8(uint16_t address)
{
	owner_.stationTstates_ += 3;
	if (owner_.stationPaged_ && address < sizeof(owner_.bootRom_))
		return owner_.bootRom_[address];
	return owner_.stationRam_[address];
}

void Fdd3000::StationOps::poke8(uint16_t address, uint8_t value)
{
	owner_.stationTstates_ += 3;
	// Writes always land in the underlying RAM, even while the boot-ROM
	// view is paged in for reads -- the ROM is a real, separate chip
	// shadowing low RAM for reads only (same convention disassembly of
	// the real interface ROM's own RAM table implies elsewhere in this
	// codebase's ROMCS devices, e.g. Dandanator's onMemoryWrite()).
	owner_.stationRam_[address] = value;
}

uint16_t Fdd3000::StationOps::peek16(uint16_t address)
{
	uint8_t lsb = peek8(address);
	uint8_t msb = peek8(address + 1);
	return (msb << 8) | lsb;
}

void Fdd3000::StationOps::poke16(uint16_t address, RegisterPair word)
{
	poke8(address, word.byte8.lo);
	poke8(address + 1, word.byte8.hi);
}

uint8_t Fdd3000::StationOps::inPort(uint16_t port)
{
	// Real Z80 IN timing is 4 T-states for the I/O cycle itself (confirmed
	// via a differential test against Fuse's own Z80free core -- see
	// project memory). MinZX::inPort had the same off-by-one; fixed there too.
	owner_.stationTstates_ += 4;
	int ioport = (port & 0xFF) >> 5;
	switch (ioport) {
	case 1: {
		// Bit 7 mirrors WD1772 DRQ, matching the real boot ROM's own
		// polling convention (confirmed via disassembly, see Fdd3000.h).
		uint8_t v = owner_.linkToStation_ | (owner_.wd_.drq() ? 0x80 : 0x00);
		if (owner_.linkTraceEnabled_) owner_.logLinkTrace("STN ", "IN ", v);
		return v;
	}
	case 6: // WD1772 register window: C0=status/cmd, C1=track, C2=sector, C3=data
		switch (port & 0x03) {
		case 0: return owner_.wd_.readStatus();
		case 1: return owner_.wd_.readTrackReg();
		case 2: return owner_.wd_.readSectorReg();
		default: return owner_.wd_.readDataReg();
		}
	// Block 7 (config register) is write-only on real hardware. Idle-bus
	// default for everything else (serial, block 0), matching the real
	// hardware's own undriven-port convention.
	default: return 0xFF;
	}
}

void Fdd3000::StationOps::outPort(uint16_t port, uint8_t value)
{
	owner_.stationTstates_ += 4; // matches MinZX::outPort's own base cost
	int ioport = (port & 0xFF) >> 5;
	switch (ioport) {
	case 1:
		owner_.linkFromStation_ = value;
		if (owner_.linkTraceEnabled_) owner_.logLinkTrace("STN ", "OUT", value);
		break;
	case 6:
		switch (port & 0x03) {
		case 0: owner_.wd_.writeCommand(value); break;
		case 1: owner_.wd_.writeTrackReg(value); break;
		case 2: owner_.wd_.writeSectorReg(value); break;
		default: owner_.wd_.writeDataReg(value); break;
		}
		break;
	case 7: owner_.writeConfigRegister(value); break;
	// Everything else (serial, block 0) ignored.
	default: break;
	}
}

void Fdd3000::StationOps::addressOnBus(uint16_t address, int32_t wstates)
{
	// The station's own bus has no ULA-style memory contention (that's a
	// Spectrum-specific artifact) -- just the plain T-state cost, same as
	// MinZX::addressOnBus()'s own non-contended path.
	(void)address;
	owner_.stationTstates_ += (uint32_t)wstates;
}

void Fdd3000::StationOps::interruptHandlingTime(int32_t wstates)
{
	owner_.stationTstates_ += (uint32_t)wstates;
}

bool Fdd3000::StationOps::isActiveINT(void)
{
	return owner_.wd_.intrq();
}
