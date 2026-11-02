#pragma once
#include "DskImage.h"
#include <cstdint>
#include <string>

// WD1772 floppy disk controller -- just enough of the real chip's
// command/status state machine for a known real CP/M BIOS (Polbrit's
// "BIOS 3.1, 1987" on the real `CPM 525.dsk`) and TOS A.2's own firmware
// to drive real sector-based disk images, not a cycle-accurate/flux-level
// model. Same "just enough for the real driver" scoping DivIde.h already
// uses for esxDOS's ATA command subset.
//
// Facts cross-checked from TWO independently-licensed, non-GPL sources
// (2026-11-01, user-directed) before writing a single line of this file,
// same clean-room discipline as the rest of this codebase: MAME's
// `src/devices/machine/wd_fdc.{h,cpp}` (license:BSD-3-Clause, Olivier
// Galibert -- confirms the public WD17xx status-register bit layout, and
// specifically that WD1770/WD1772 repurpose bit5 as "spin-up complete"
// and bit7 as "motor on" where other family members use HLD/NRDY) and
// `~/src/PicoZX/repos/pico-spec-drewpo/src/wd1793.{h,cpp}` (MIT,
// Juan Carlos González Amestoy / Víctor Iborra -- the sibling project
// JASE was built to test against, already a trusted reference elsewhere
// in this codebase, see DivIde.h). Command byte encoding (Type I-IV
// families, the h/v/u bit positions) is standard, publicly documented
// WD17xx datasheet knowledge, not either project's own invention.
//
// Deliberate simplifications vs. the real chip, documented not hidden:
//   - No step-rate/settling/spin-up timing at all -- every Type I
//     command (Restore/Seek/Step/Step In/Step Out) completes the SAME
//     instruction it's issued on. The one real gotcha this causes
//     (confirmed live in pico-spec-drewpo's own commit history, see its
//     `typeI_busy_oneshot` comment: a BIOS that polls "wait for BUSY=1"
//     immediately after issuing a Type I command hangs forever if BUSY
//     is already 0 by the time it checks) is specifically worked around
//     here too -- see busyOneShot_.
//   - No index-pulse, head-load-timing, or write-protect modeling (those
//     status bits always read 0).
//   - No real CRC: a sector is either found (by matching R in its
//     track's descriptor list, via DskImage::findSector) or it isn't --
//     no simulated media corruption, no "verify" beyond checking the
//     track has at least one sector.
//   - Read Track / Write Track (Type III, raw MFM stream I/O) are NOT
//     implemented -- completes immediately with no data transferred
//     rather than hanging the guest waiting for DRQ. Read Sector/Write
//     Sector (the commands the real "TOS bez tajemnic" article's own
//     command table documents TOS actually issuing, cmd 27/28) and Read
//     Address are implemented.
//   - Writes (Write Sector) land only in DskImage's in-memory copy, not
//     flushed back to the backing file -- see DskImage.h.
class Wd1772
{
public:
	void reset();

	// Register access -- Fdd3000's station I/O block 6 (see Fdd3000.cpp)
	// wires these straight to the real WD1772 register window.
	void writeCommand(uint8_t value);
	void writeTrackReg(uint8_t value) { if (!busy_) track_ = value; }
	void writeSectorReg(uint8_t value) { if (!busy_) sector_ = value; }
	void writeDataReg(uint8_t value);
	uint8_t readStatus();
	uint8_t readTrackReg() const { return track_; }
	uint8_t readSectorReg() const { return sector_; }
	uint8_t readDataReg();

	// Non-mutating status snapshot for debug/inspection -- readStatus()
	// itself clears INTRQ and consumes the busy-one-shot guard (both real
	// WD1772 register-read side effects the station CPU relies on), so a
	// debug command peeking at status must NOT go through it.
	uint8_t peekStatus() const;

	// DRQ/INTRQ output lines -- Fdd3000 wires drq() into the station's
	// block-1 link-byte bit 7 (matching the real boot ROM's own polling
	// convention, confirmed via disassembly) and intrq() into the
	// station Z80's interrupt input.
	bool drq() const { return drq_; }
	bool intrq() const { return intrq_; }

	// Drive/side/density selection is external glue on real hardware
	// (FDD3000's own config register, block 7 -- see Fdd3000.cpp), not
	// something the WD1772 command bits control here.
	void selectDrive(int index) { selectedDrive_ = (index >= 0 && index < kNumDrives) ? index : -1; }
	void setSide(bool sideB) { side_ = sideB; }
	void setDensity(bool doubleDensity) { density_ = doubleDensity; }

	static const int kNumDrives = 4;
	bool insertDisk(int driveIndex, const std::string& path);

private:
	enum class Xfer { None, Read, Write };

	struct Drive {
		DskImage image;
		bool hasImage = false;
	};

	Drive* currentDrive();
	bool currentDriveHasImage() const;

	void startTypeI(uint8_t cmd);
	void startTypeII(uint8_t cmd);
	void startTypeIII(uint8_t cmd);
	void forceInterrupt(uint8_t cmd);
	void beginSectorRead();
	void beginSectorWrite();
	void advanceAfterSector();
	void finishCommand(bool setIntrq);

	Drive drives_[kNumDrives];
	int selectedDrive_ = 0;
	bool side_ = false;
	bool density_ = true;

	uint8_t command_ = 0, track_ = 0, sector_ = 0, data_ = 0;
	bool busy_ = false, drq_ = false, intrq_ = false;
	bool crcError_ = false, recordNotFound_ = false;
	bool lastCommandWasTypeI_ = true;
	int lastDirection_ = -1; // STEP re-uses the direction of the last Seek/Step In/Step Out

	// See class comment: forces one BUSY=1 status read after a Type I
	// command that (in this simplified, instant-completion model)
	// already finished by the time anything reads status.
	bool busyOneShot_ = false;

	// Type II/III data-transfer state.
	Xfer xferMode_ = Xfer::None;
	uint8_t* xferPtr_ = nullptr; // points into the owning drive's DskImage buffer (Read/Write) or addrBuf_ (Read Address)
	int xferLen_ = 0;
	int xferPos_ = 0;
	bool multiple_ = false; // Type II "m" bit: auto-advance sector_ and keep going
	uint8_t addrBuf_[6] = {}; // Read Address's own 6-byte reply (track,side,sector,N,CRC1,CRC2)
};
