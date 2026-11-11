#pragma once
#include "Wd1772Fsm.h"
#include "z80.h"
#include <cstdint>
#include <string>
#include <chrono>

// The FDD3000 station computer itself: Z80A + 64K RAM + boot-ROM overlay +
// WD1772 FDC + the one link byte each way to the host interface. Extracted
// (2026-11-11) from Fdd3000's own in-process implementation (same code,
// unchanged behavior) so BOTH the in-process driver (Fdd3000, stepping
// this from onInstructionExecuted()) and the standalone-process driver
// (fdd3000_station/main.cpp, stepping this from a real-time-paced loop)
// share ONE implementation instead of two independently-maintained copies
// -- confirmed live, 2026-11-11: two separate ports of this same logic
// already diverged once (a leftover 12MHz pacing constant on the
// standalone side caused a real, confusing behavior difference that took
// a side-by-side trace to track down). This class only knows about the
// STATION's own 64K address space; it has no opinion on how often step()
// gets called or what's driving the host side of the link.
//
// See Fdd3000.h's own class comment for the hardware facts (I/O block
// layout, config-register bits, etc.) -- those didn't change, just moved
// house.
class FddStation
{
public:
	FddStation();
	~FddStation();

	static const int kStationBootRomSize = 2048;
	static const int kNumDrives = Wd1772Fsm::kNumDrives;

	bool loadBootRom(const std::string& path);
	bool insertDisk(int driveIndex, const std::string& path);
	const std::string& diskPath(int driveIndex) const;

	// Station-reset contract (see Fdd3000.h's stationReset() comment):
	// resets the CPU/paging/FDC but NOT the link latches.
	void reset();

	// Executes exactly one station Z80 instruction + the FDC's own tick
	// for however many T-states that instruction just cost. Returns the
	// T-states spent, so the caller (host-T-state-budget loop or
	// real-time-pacing loop, see class comment) can account for it.
	uint32_t step();

	// The ONE link wire, each direction a single latched byte (real
	// hardware's ti2fdd/fdd2ti). The host interface calls setHostByte()
	// on its own OUT and reads stationByte() on its own IN.
	void setHostByte(uint8_t value) { linkToStation_ = value; }
	uint8_t stationByte() const { return linkFromStation_; }

	bool bootRomVisible() const { return stationPaged_; }
	Wd1772Fsm& fdc() { return wd_; }
	const Wd1772Fsm& fdc() const { return wd_; }

	// Register accessors for the debug protocol's FDD3000 REGS/SETREG/STEP
	// sub-commands -- forward straight to the station's own Z80 instance.
	uint16_t getRegAF() const { return stationCpu_->getRegAF(); }
	void setRegAF(uint16_t v) { stationCpu_->setRegAF(v); }
	uint16_t getRegBC() const { return stationCpu_->getRegBC(); }
	void setRegBC(uint16_t v) { stationCpu_->setRegBC(v); }
	uint16_t getRegDE() const { return stationCpu_->getRegDE(); }
	void setRegDE(uint16_t v) { stationCpu_->setRegDE(v); }
	uint16_t getRegHL() const { return stationCpu_->getRegHL(); }
	void setRegHL(uint16_t v) { stationCpu_->setRegHL(v); }
	uint16_t getRegAFx() const { return stationCpu_->getRegAFx(); }
	void setRegAFx(uint16_t v) { stationCpu_->setRegAFx(v); }
	uint16_t getRegBCx() const { return stationCpu_->getRegBCx(); }
	void setRegBCx(uint16_t v) { stationCpu_->setRegBCx(v); }
	uint16_t getRegDEx() const { return stationCpu_->getRegDEx(); }
	void setRegDEx(uint16_t v) { stationCpu_->setRegDEx(v); }
	uint16_t getRegHLx() const { return stationCpu_->getRegHLx(); }
	void setRegHLx(uint16_t v) { stationCpu_->setRegHLx(v); }
	uint16_t getRegIX() const { return stationCpu_->getRegIX(); }
	void setRegIX(uint16_t v) { stationCpu_->setRegIX(v); }
	uint16_t getRegIY() const { return stationCpu_->getRegIY(); }
	void setRegIY(uint16_t v) { stationCpu_->setRegIY(v); }
	uint16_t getRegSP() const { return stationCpu_->getRegSP(); }
	void setRegSP(uint16_t v) { stationCpu_->setRegSP(v); }
	uint16_t getRegPC() const { return stationCpu_->getRegPC(); }
	void setRegPC(uint16_t v) { stationCpu_->setRegPC(v); }
	uint8_t getRegI() const { return stationCpu_->getRegI(); }
	uint8_t getRegR() const { return stationCpu_->getRegR(); }
	bool getIFF1() const { return stationCpu_->isIFF1(); }
	bool getIFF2() const { return stationCpu_->isIFF2(); }
	int getIM() const { return (int)stationCpu_->getIM(); }

	// MEM/POKE debug sub-commands read/write through the SAME view the
	// station CPU itself sees (boot-ROM shadow while bootRomVisible()).
	uint8_t peek(uint16_t address) { return stationPeek8(address); }
	void poke(uint16_t address, uint8_t value) { stationPoke8(address, value); }

	// Same ON/OFF runtime-toggle convention as Fdd3000's own LINKTRACE --
	// logs every station-side IN/OUT on the link latch (block 1). Separate
	// from the host-side half of the same trace (Fdd3000's own
	// logLinkTrace()); this class has no SDL dependency (needed standalone
	// in fdd3000_station too), so it timestamps off its own
	// std::chrono clock rather than SDL_GetPerformanceCounter -- the two
	// halves don't share a common epoch, same as the real bridge-mode
	// trace never did either.
	void setLinkTraceEnabled(bool on) { linkTraceEnabled_ = on; }
	bool isLinkTraceEnabled() const { return linkTraceEnabled_; }

private:
	class StationOps : public Z80operations
	{
	public:
		explicit StationOps(FddStation& owner) : owner_(owner) {}
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
		FddStation& owner_;
	};

	uint8_t stationPeek8(uint16_t address);
	void stationPoke8(uint16_t address, uint8_t value);
	uint8_t stationInPort(uint16_t port);
	void stationOutPort(uint16_t port, uint8_t value);
	void writeConfigRegister(uint8_t value);
	void logLinkTrace(const char* direction, uint8_t value) const;

	uint8_t stationRam_[65536] = {};
	bool stationPaged_ = true;
	uint8_t bootRom_[kStationBootRomSize];
	std::string stationRomPath_;
	uint8_t linkToStation_ = 0;
	uint8_t linkFromStation_ = 0;
	bool linkTraceEnabled_ = false;
	std::chrono::steady_clock::time_point traceStart_;

	Wd1772Fsm wd_;
	std::string diskPaths_[kNumDrives];

	uint32_t stationTstates_ = 0;
	StationOps stationOps_;
	Z80* stationCpu_ = nullptr;
};
