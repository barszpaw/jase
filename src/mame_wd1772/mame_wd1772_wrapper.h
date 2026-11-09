// Wraps MAME's real wd1772_device (wd_fdc.cpp, BSD-3-Clause) behind the
// SAME method-name API JASE's own clean-room Wd1772 class already uses
// (src/Wd1772.h) -- a drop-in swap at call sites, same shape the z80
// core swap used. Backed by DskImage (unmodified, same sector-level
// parser the rest of the project trusts) + a synthetic per-track MFM
// encode (mfm_track_encoder.h) so MAME's real, unmodified bit-level
// live_run()/digital-PLL decode engine does the actual command/status
// work, not a reimplementation of it.
#pragma once
#include <cstdint>
#include <string>

class MameWd1772
{
public:
	MameWd1772();
	~MameWd1772();

	void reset();
	// Call once per station step with the real T-states (station Z80 @
	// 4MHz) that step consumed -- advances the SAME simulated-time base
	// wd1772's own internal delays (settle time, step rate) are measured
	// against, so e.g. a 30000-cycle (3.75ms @ 8MHz) settle delay elapses
	// in roughly the right number of real station instructions instead
	// of a fixed, clock-ratio-mismatched guess.
	void tick(uint32_t stationTstatesAt4MHz);

	void writeCommand(uint8_t value);
	void writeTrackReg(uint8_t value);
	void writeSectorReg(uint8_t value);
	void writeDataReg(uint8_t value);
	uint8_t readStatus();
	uint8_t readTrackReg() const;
	uint8_t readSectorReg() const;
	uint8_t readDataReg();
	uint8_t peekStatus() const;

	bool drq() const;
	bool intrq() const;

	void selectDrive(int index);
	void setSide(bool sideB);
	void setDensity(bool doubleDensity);

	static const int kNumDrives = 4;
	bool insertDisk(int driveIndex, const std::string &path);

private:
	struct Impl;
	Impl *m_impl;
};
