#include "FddStation.h"
#include <cstdio>
#include <cstring>

FddStation::FddStation()
	: traceStart_(std::chrono::steady_clock::now())
	, stationOps_(*this)
{
	// Placeholder station bootstrap: DI ; JR $ (idle forever), rest
	// erased-EPROM 0xFF (same convention as SDI_8255's unmapped ROM
	// bank). See loadBootRom() for supplying the real 2048-byte dump
	// instead.
	memset(bootRom_, 0xFF, sizeof(bootRom_));
	bootRom_[0] = 0xF3; // DI
	bootRom_[1] = 0x18; // JR
	bootRom_[2] = 0xFE; // -2 (jump to self)

	stationCpu_ = new Z80(&stationOps_);
	stationCpu_->reset();
}

FddStation::~FddStation()
{
	delete stationCpu_;
}

bool FddStation::loadBootRom(const std::string& path)
{
	FILE* pf = fopen(path.c_str(), "rb");
	if (pf == NULL) {
		fprintf(stderr, "FddStation::loadBootRom: cannot open %s\n", path.c_str());
		return false;
	}
	uint8_t buf[kStationBootRomSize];
	size_t n = fread(buf, 1, sizeof(buf), pf);
	fclose(pf);
	if (n != sizeof(buf)) {
		fprintf(stderr, "FddStation::loadBootRom: %s is %zu bytes, expected exactly %d\n",
		        path.c_str(), n, kStationBootRomSize);
		return false;
	}
	memcpy(bootRom_, buf, sizeof(buf));
	stationRomPath_ = path;
	fprintf(stderr, "FddStation::loadBootRom: loaded station boot ROM from %s\n", path.c_str());
	return true;
}

bool FddStation::insertDisk(int driveIndex, const std::string& path)
{
	if (!wd_.insertDisk(driveIndex, path)) return false;
	if (driveIndex >= 0 && driveIndex < kNumDrives) diskPaths_[driveIndex] = path;
	return true;
}

const std::string& FddStation::diskPath(int driveIndex) const
{
	static const std::string empty;
	return (driveIndex >= 0 && driveIndex < kNumDrives) ? diskPaths_[driveIndex] : empty;
}

void FddStation::reset()
{
	// Confirmed against the real Fuse source (menu.c's menu_machine_fdd3000reset(),
	// which calls exactly wd_fdc_master_reset()+fddReset()): the station-reset
	// action resets the FDC and the station's own CPU/paging state, but
	// does NOT touch the link latches (ti2fdd/fdd2ti) at all -- user's own
	// suggestion, 2026-11-04. Clearing them here (as this code used to) is a
	// confirmed, real discrepancy from real hardware/Fuse behavior.
	stationCpu_->reset();
	stationPaged_ = true;
	wd_.reset();
}

uint32_t FddStation::step()
{
	// z80cpp is pull-based: StationOps::isActiveINT() is queried
	// internally by execute() itself (reading wd_.intrq() live), so no
	// explicit per-step IRQ-line sync call is needed here.
	uint32_t before = stationTstates_;
	stationCpu_->execute();
	uint32_t spent = stationTstates_ - before;
	if (spent == 0) spent = 4; // defensive floor -- a real opcode always costs T-states
	wd_.tick(spent); // Wd1772Fsm::tick() wants real T-states consumed
	return spent;
}

// Station block 7 -- see Fdd3000.h's bit-layout table (a hardware fact,
// not copied logic). All five fields are active-LOW on the wire, so the
// decode below inverts each bit's sense explicitly rather than reading
// as if 1 meant "selected".
void FddStation::writeConfigRegister(uint8_t value)
{
	int drive = -1;
	for (int bit = 0; bit < 4; bit++) {
		if (!((value >> bit) & 1)) { drive = bit; break; }
	}
	wd_.selectDrive(drive);
	wd_.setSide((value & 0x10) == 0);      // bit4=0 -> side B
	wd_.setDensity((value & 0x20) == 0);   // bit5=0 -> double density
	stationPaged_ = (value & 0x40) == 0;   // bit6=0 -> boot-ROM view visible
	// bit7 (/HEAD) not modeled -- see Wd1772Fsm's simplification list.
}

void FddStation::logLinkTrace(const char* direction, uint8_t value) const
{
	if (!linkTraceEnabled_) return;
	double ts = std::chrono::duration<double>(std::chrono::steady_clock::now() - traceStart_).count();
	char bin[9];
	for (int b = 0; b < 8; b++) bin[b] = ((value >> (7 - b)) & 1) ? '1' : '0';
	bin[8] = '\0';
	fprintf(stderr, "FDD3000LINK ts=%.6f stnPC=%04x side=STN  dir=%s val=%02x bin=%s\n",
	        ts, stationCpu_->getRegPC(), direction, value, bin);
}

// ---- Station memory/port access -- the station's own, entirely private
// 64K address space. Core-agnostic (no T-state accounting here -- that's
// StationOps' job below, mirroring how MinZX itself separates memory
// access from T-state bookkeeping). ----

uint8_t FddStation::stationPeek8(uint16_t address)
{
	if (stationPaged_ && address < sizeof(bootRom_))
		return bootRom_[address];
	return stationRam_[address];
}

void FddStation::stationPoke8(uint16_t address, uint8_t value)
{
	// Writes always land in the underlying RAM, even while the boot-ROM
	// view is paged in for reads -- the ROM is a real, separate chip
	// shadowing low RAM for reads only.
	stationRam_[address] = value;
}

uint8_t FddStation::stationInPort(uint16_t port)
{
	int ioport = (port & 0xFF) >> 5;
	switch (ioport) {
	case 1: {
		// Bit 7 mirrors WD1772 DRQ, matching the real boot ROM's own
		// polling convention (confirmed via disassembly).
		uint8_t v = linkToStation_ | (wd_.drq() ? 0x80 : 0x00);
		logLinkTrace("IN ", v);
		return v;
	}
	case 6: // WD1772 register window: C0=status/cmd, C1=track, C2=sector, C3=data
		switch (port & 0x03) {
		case 0: return wd_.readStatus();
		case 1: return wd_.readTrackReg();
		case 2: return wd_.readSectorReg();
		default: return wd_.readDataReg();
		}
	// Block 7 (config register) is write-only on real hardware. Idle-bus
	// default for everything else (serial, block 0), matching the real
	// hardware's own undriven-port convention.
	default: return 0xFF;
	}
}

void FddStation::stationOutPort(uint16_t port, uint8_t value)
{
	int ioport = (port & 0xFF) >> 5;
	switch (ioport) {
	case 1:
		linkFromStation_ = value;
		linkDirty_ = true; // unconditional -- see stationByteDirty()'s own comment
		logLinkTrace("OUT", value);
		break;
	case 6:
		switch (port & 0x03) {
		case 0: wd_.writeCommand(value); break;
		case 1: wd_.writeTrackReg(value); break;
		case 2: wd_.writeSectorReg(value); break;
		default: wd_.writeDataReg(value); break;
		}
		break;
	case 7: writeConfigRegister(value); break;
	// Everything else (serial, block 0) ignored.
	default: break;
	}
}

// ---- StationOps: the station CPU's own Z80operations, a thin T-state-
// counting wrapper around the core-agnostic station access methods above.
// T-state costs match MinZX's own Z80operations implementation (minzx.cpp)
// exactly -- same base costs, no contention (the station has none) -- so
// stationTstates_ is a real, comparable running counter.

uint8_t FddStation::StationOps::fetchOpcode(uint16_t address)
{
	uint8_t v = peek8(address); // peek8 already accounts the shared 3 T-states
	owner_.stationTstates_ += 1; // M1 cycle's extra T-state over a plain memory read
	return v;
}

uint8_t FddStation::StationOps::peek8(uint16_t address)
{
	owner_.stationTstates_ += 3;
	return owner_.stationPeek8(address);
}

void FddStation::StationOps::poke8(uint16_t address, uint8_t value)
{
	owner_.stationTstates_ += 3;
	owner_.stationPoke8(address, value);
}

uint16_t FddStation::StationOps::peek16(uint16_t address)
{
	uint8_t lsb = peek8(address);
	uint8_t msb = peek8(address + 1);
	return (msb << 8) | lsb;
}

void FddStation::StationOps::poke16(uint16_t address, RegisterPair word)
{
	poke8(address, word.byte8.lo);
	poke8(address + 1, word.byte8.hi);
}

uint8_t FddStation::StationOps::inPort(uint16_t port)
{
	// Real Z80 IN timing is 4 T-states for the I/O cycle itself.
	owner_.stationTstates_ += 4;
	return owner_.stationInPort(port);
}

void FddStation::StationOps::outPort(uint16_t port, uint8_t value)
{
	owner_.stationTstates_ += 4; // matches MinZX::outPort's own base cost
	owner_.stationOutPort(port, value);
}

void FddStation::StationOps::addressOnBus(uint16_t address, int32_t wstates)
{
	// The station's own bus has no ULA-style memory contention -- just the
	// plain T-state cost, same as MinZX::addressOnBus()'s own
	// non-contended path.
	(void)address;
	owner_.stationTstates_ += (uint32_t)wstates;
}

void FddStation::StationOps::interruptHandlingTime(int32_t wstates)
{
	owner_.stationTstates_ += (uint32_t)wstates;
}

bool FddStation::StationOps::isActiveINT(void)
{
	return owner_.wd_.intrq();
}
