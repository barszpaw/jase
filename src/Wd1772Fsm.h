#pragma once
#include "DskImage.h"
#include <cstdint>
#include <string>

// User-provided (2026-11-10) finite-state-machine WD1772 model -- real
// per-microstep delay cycles (step pulse ~2000, settle ~30000) instead of
// the earlier clean-room Wd1772's tiny, explicitly "untuned" step counts
// (kStepsPerSectorSlot=4/kRnfGiveUpSteps=6, see Wd1772.h) -- the
// hypothesis being tested is whether these more realistic delay
// magnitudes change the host<->station handshake's own timing enough to
// matter, now that the clock-crossing is real-T-state-proportional (see
// project memory, 2026-11-09/10 sessions).
//
// The class skeleton (State/SubState/CommandType enums, writeRegister()/
// readRegister(), tick(), the Type I step-pulse/settle FSM, Force
// Interrupt) is the user's own, kept as given. What's ADDED here (not in
// the original sketch) to make it usable as FDD3000's actual WD1772:
//   - DskImage integration (findSector/sectorDescriptor), same calls the
//     clean-room Wd1772.cpp already used -- Type II/III were sketched
//     with comments ("Pobieranie bajtu z nosnika") but no actual image
//     access.
//   - Real byte-by-byte Type II read/write (TypeII_WriteByte substate
//     existed in the enum but had no switch case -- data register writes
//     fell through to a flat register-latch with no transfer effect).
//   - Type III Read Address (6-byte track/side/sector/N/CRC reply, same
//     shape as the clean-room Wd1772::beginReadAddress()); Read Track/
//     Write Track still complete immediately, same documented
//     simplification as before (not modeled anywhere in this codebase).
//   - Drive/side/density selection + kNumDrives/insertDisk (FDD3000 has 4
//     drives; the original sketch had none of this, single implicit
//     drive).
//   - Named method wrappers (writeCommand/writeTrackReg/.../readStatus/
//     drq()/intrq()/peekStatus()) matching the EXACT API the clean-room
//     Wd1772 and MameWd1772 both already exposed to Fdd3000.cpp, so this
//     is a drop-in third option, no Fdd3000.cpp changes needed beyond the
//     type name.
//   - The same WDTRACE hook (setTraceEnabled()/logTrace()) added to the
//     clean-room Wd1772 this session, for live comparison.
class Wd1772Fsm
{
public:
	Wd1772Fsm() = default;

	// --- Rejestry WD1772 (user's own names, kept) ---
	uint8_t statusRegister{0x00};
	uint8_t trackRegister{0x00};
	uint8_t sectorRegister{0x00};
	uint8_t dataRegister{0x00};

	enum class State {
		Idle,
		ExecutingTypeI,
		ExecutingTypeII,
		ExecutingTypeIII,
		ExecutingTypeIV
	};

	enum class SubState {
		None,
		TypeI_Prepare,
		TypeI_StepPulse,
		TypeI_Verify,
		TypeII_WaitData,
		TypeII_ReadByte,
		TypeII_WriteByte,
		TypeIII_WaitData,
		TypeIII_ReadByte,
		SpinUpDelay,
		SettleDelay,
		CommandComplete
	};

	enum class CommandType {
		TypeI,
		TypeII,
		TypeIII,
		TypeIV
	};

	// --- Bus interface, user's own shape -- address 0=command/status,
	// 1=track, 2=sector, 3=data (same block-6 decode Fdd3000::
	// stationInPort/OutPort already use). ---
	void writeRegister(uint8_t address, uint8_t value)
	{
		switch (address & 0x03) {
		case 0:
			if (getCommandType(value) == CommandType::TypeIV) {
				executeTypeIV(value);
			} else if (currentState == State::Idle) {
				startCommand(value);
			}
			break;
		case 1:
			if (currentState == State::Idle) trackRegister = value;
			break;
		case 2:
			if (currentState == State::Idle) sectorRegister = value;
			break;
		case 3:
			dataRegister = value;
			if (subState == SubState::TypeII_WriteByte) consumeWriteByte();
			break;
		}
	}

	uint8_t readRegister(uint8_t address)
	{
		switch (address & 0x03) {
		case 0:
			intrqFlag_ = false;
			return statusRegister;
		case 1:
			return trackRegister;
		case 2:
			return sectorRegister;
		case 3:
			if (subState == SubState::TypeII_ReadByte || subState == SubState::TypeIII_ReadByte)
				return produceReadByte();
			return dataRegister;
		}
		return 0xFF;
	}

	void tick(uint32_t clockCycles)
	{
		if (delayCycles > 0) {
			if ((uint64_t)clockCycles >= delayCycles) {
				delayCycles = 0;
			} else {
				delayCycles -= clockCycles;
				return;
			}
		}

		switch (currentState) {
		case State::Idle:
			break;
		case State::ExecutingTypeI:
			stepTypeIFSM();
			break;
		case State::ExecutingTypeII:
			stepTypeIIFSM();
			break;
		case State::ExecutingTypeIII:
			stepTypeIIIFSM();
			break;
		case State::ExecutingTypeIV:
			finishCommand();
			break;
		}
	}

	void reset()
	{
		currentState = State::Idle;
		subState = SubState::None;
		currentCommand = 0;
		delayCycles = 0;
		intrqFlag_ = false;
		drqFlag_ = false;
		stepDirection = 1;
		targetTrack = 0;
		statusRegister = trackRegister = sectorRegister = dataRegister = 0;
		multiple_ = false;
		xferPtr_ = nullptr; xferLen_ = xferPos_ = 0;
	}

	bool getINTRQ() const { return intrqFlag_; }
	bool getDRQ() const { return drqFlag_; }

	// ---- Named wrappers matching Wd1772/MameWd1772's own API, so
	// Fdd3000.cpp's station I/O decode (block 6 = command/track/sector/
	// data, same as always) needs no changes beyond the member's type. ----
	void writeCommand(uint8_t value) { writeRegister(0, value); }
	void writeTrackReg(uint8_t value) { writeRegister(1, value); }
	void writeSectorReg(uint8_t value) { writeRegister(2, value); }
	void writeDataReg(uint8_t value) { writeRegister(3, value); }
	uint8_t readStatus() { return readRegister(0); }
	uint8_t readTrackReg() const { return trackRegister; }
	uint8_t readSectorReg() const { return sectorRegister; }
	uint8_t readDataReg() { return readRegister(3); }
	// Non-mutating status snapshot (no INTRQ-clear side effect), same
	// convention as the clean-room Wd1772's own peekStatus().
	uint8_t peekStatus() const { return statusRegister; }
	bool drq() const { return getDRQ(); }
	bool intrq() const { return getINTRQ(); }

	// ---- Drive/side/density/image glue -- FDD3000 has 4 drives; the
	// original sketch had none of this (single implicit drive). Same
	// shape as the clean-room Wd1772. ----
	static const int kNumDrives = 4;
	void selectDrive(int index) { selectedDrive_ = (index >= 0 && index < kNumDrives) ? index : -1; }
	void setSide(bool sideB) { side_ = sideB; }
	void setDensity(bool doubleDensity) { density_ = doubleDensity; }
	bool insertDisk(int driveIndex, const std::string& path)
	{
		if (driveIndex < 0 || driveIndex >= kNumDrives) return false;
		// Separate "did THIS call succeed" (the return value) from "is
		// there currently a usable disk in this drive" (hasImage) -- a
		// failed load() (2026-11-23: load() itself now leaves the object
		// untouched on failure, see its own comment) must NOT eject an
		// already-inserted, still-good disk just because a later re-insert
		// attempt pointed at a bad path (e.g. the backing file got moved
		// out from under a running station). hasImage mirrors the
		// DskImage's own actual state, not this one call's outcome.
		bool ok = drives_[driveIndex].image.load(path);
		drives_[driveIndex].hasImage = drives_[driveIndex].image.loaded();
		return ok;
	}

	// "Eject" (2026-11-25) -- clears the drive back to empty (no disk).
	// Deliberately a real functional reset of Drive, not just hasImage=
	// false, so a leftover in-memory image can't be found by some later
	// code path that only checks hasImage (same discipline insertDisk()'s
	// own comment argues for in the other direction).
	bool ejectDisk(int driveIndex)
	{
		if (driveIndex < 0 || driveIndex >= kNumDrives) return false;
		drives_[driveIndex] = Drive();
		return true;
	}

	// "Save As" -- see DskImage::saveAs()'s own comment.
	bool saveDiskAs(int driveIndex, const std::string& newPath)
	{
		if (driveIndex < 0 || driveIndex >= kNumDrives || !drives_[driveIndex].hasImage) return false;
		return drives_[driveIndex].image.saveAs(newPath);
	}

	// Same ON/OFF runtime-toggle convention as the clean-room Wd1772's
	// own setTraceEnabled() (see its own comment) -- added here too for
	// direct, apples-to-apples comparison via "FDD3000 WDTRACE ON/OFF".
	void setTraceEnabled(bool on) { traceEnabled_ = on; }
	bool isTraceEnabled() const { return traceEnabled_; }

private:
	struct Drive {
		DskImage image;
		bool hasImage = false;
	};

	State currentState{State::Idle};
	SubState subState{SubState::None};
	uint8_t currentCommand{0x00};
	uint64_t delayCycles{0};

	bool intrqFlag_{false};
	bool drqFlag_{false};
	int8_t stepDirection{1};
	uint8_t targetTrack{0};

	static constexpr uint8_t STATUS_BUSY       = 0x01;
	static constexpr uint8_t STATUS_INDEX      = 0x02;
	static constexpr uint8_t STATUS_DRQ        = 0x02;
	static constexpr uint8_t STATUS_TRACK0     = 0x04;
	static constexpr uint8_t STATUS_CRC_ERROR  = 0x08;
	static constexpr uint8_t STATUS_RNF        = 0x10;
	static constexpr uint8_t STATUS_SPIN_UP    = 0x20;
	static constexpr uint8_t STATUS_WRITE_PROT = 0x40;
	static constexpr uint8_t STATUS_MOTOR_ON   = 0x80;

	Drive drives_[kNumDrives];
	int selectedDrive_ = 0;
	bool side_ = false;
	bool density_ = true;

	// Type II/III data-transfer state -- same shape as the clean-room
	// Wd1772's xferPtr_/xferLen_/xferPos_.
	bool multiple_ = false;
	uint8_t* xferPtr_ = nullptr;
	int xferLen_ = 0;
	int xferPos_ = 0;
	bool xferIsWrite_ = false;
	uint8_t addrBuf_[6] = {};

	bool traceEnabled_ = false;

	Drive* currentDrive()
	{
		if (selectedDrive_ < 0 || selectedDrive_ >= kNumDrives) return nullptr;
		return &drives_[selectedDrive_];
	}
	bool currentDriveHasImage() const
	{
		return selectedDrive_ >= 0 && selectedDrive_ < kNumDrives && drives_[selectedDrive_].hasImage;
	}

	// Real WD1772's TRACK0 status bit (0x04, Type I only) is a LIVE read
	// of the head's current position, not something latched once --
	// found missing entirely (2026-11-10 live test): the real station
	// boot ROM's own "TRK00+BUSY wait" loop (confirmed via disassembly,
	// address ~0x3A) polls this bit right after RESTORE and hangs
	// forever without it, before ever reaching a second disk command.
	void updateTrack0Status()
	{
		if (trackRegister == 0) statusRegister |= STATUS_TRACK0;
		else statusRegister &= ~STATUS_TRACK0;
	}

	CommandType getCommandType(uint8_t cmd)
	{
		if ((cmd & 0x80) == 0x00) return CommandType::TypeI;
		if ((cmd & 0xE0) == 0x80 || (cmd & 0xE0) == 0xA0) return CommandType::TypeII;
		if ((cmd & 0xF0) == 0xD0) return CommandType::TypeIV;
		return CommandType::TypeIII;
	}

	void startCommand(uint8_t cmd)
	{
		currentCommand = cmd;
		statusRegister |= STATUS_BUSY;
		intrqFlag_ = false;
		drqFlag_ = false;
		logTrace("CMD");

		CommandType type = getCommandType(cmd);
		switch (type) {
		case CommandType::TypeI:
			currentState = State::ExecutingTypeI;
			subState = SubState::TypeI_Prepare;
			parseTypeICommand();
			break;
		case CommandType::TypeII:
			// Bit 0x04 means TRACK0 for Type I but LOST DATA for Type
			// II/III (real WD17xx status bits are CONTEXTUAL by command
			// type, confirmed via a differential test against MameWd1772,
			// 2026-11-10: a stale TRACK0=1 left by a prior RESTORE was
			// being misread as this command's own status). LOST DATA
			// itself isn't modeled (would need to detect the CPU failing
			// to service DRQ in time, which this untimed-per-byte model
			// doesn't track) -- clearing it to 0 here is correct for a
			// CPU that always keeps up, which Fdd3000's own StationOps
			// always does.
			statusRegister &= ~STATUS_TRACK0;
			currentState = State::ExecutingTypeII;
			xferIsWrite_ = (cmd & 0x20) != 0;
			multiple_ = (cmd & 0x10) != 0;
			subState = SubState::TypeII_WaitData;
			break;
		case CommandType::TypeIII:
			statusRegister &= ~STATUS_TRACK0; // see Type II's own comment above
			currentState = State::ExecutingTypeIII;
			subState = SubState::TypeIII_WaitData;
			break;
		case CommandType::TypeIV:
			executeTypeIV(cmd);
			break;
		}
	}

	void parseTypeICommand()
	{
		uint8_t op = currentCommand & 0xF0;
		if (op == 0x00) {
			targetTrack = 0;
			trackRegister = 0xFF; // force a full sweep down to 0, same as a real unknown-position RESTORE
		} else if (op == 0x10) {
			targetTrack = dataRegister;
		} else if ((op & 0xE0) == 0x20) {
			// Step -- keeps stepDirection from the last Seek/Step In/Step Out
			targetTrack = (uint8_t)(trackRegister + stepDirection);
		} else if ((op & 0xE0) == 0x40) {
			stepDirection = 1;
			targetTrack = (uint8_t)(trackRegister < 255 ? trackRegister + 1 : 255);
		} else if ((op & 0xE0) == 0x60) {
			stepDirection = -1;
			targetTrack = (uint8_t)(trackRegister > 0 ? trackRegister - 1 : 0);
		}
	}

	void stepTypeIFSM()
	{
		switch (subState) {
		case SubState::TypeI_Prepare:
			if (trackRegister == targetTrack) {
				subState = SubState::TypeI_Verify;
			} else {
				stepDirection = (targetTrack > trackRegister) ? 1 : -1;
				subState = SubState::TypeI_StepPulse;
			}
			break;

		case SubState::TypeI_StepPulse:
			if (stepDirection > 0 && trackRegister < 255) trackRegister++;
			else if (stepDirection < 0 && trackRegister > 0) trackRegister--;
			updateTrack0Status();
			delayCycles = 2000; // step-rate delay, see class comment
			subState = (trackRegister == targetTrack) ? SubState::TypeI_Verify : SubState::TypeI_Prepare;
			break;

		case SubState::TypeI_Verify:
			if (currentCommand & 0x04) { // V bit
				Drive* d = currentDrive();
				if (!d || !d->hasImage || d->image.sectorCount(trackRegister, side_ ? 1 : 0) == 0)
					statusRegister |= STATUS_CRC_ERROR;
				else
					statusRegister &= ~STATUS_CRC_ERROR;
				// Found live, 2026-11-24: delayCycles used to be set here
				// and finishCommand() called in the SAME step, so this
				// settle time never actually gated THIS command's own
				// completion (finishCommand() clears BUSY/raises INTRQ
				// unconditionally) -- it only left a stale delayCycles
				// value behind that tick()'s own early-return (see its own
				// comment) then silently swallowed up to 30000 cycles'
				// worth of the NEXT, unrelated command instead. Harmless
				// on disks where enough real commands/time separate the
				// two (2-sided/dense images, which happened to never
				// trigger this), but on 1-sided disks the very next SEEK
				// TOS issues right after a verify-seek landed inside that
				// stolen window often enough to stall CAT* for real,
				// reproduced live (Hardware fault) and confirmed absent
				// with MAME's wd1772_device, which has no such leak.
				// Route through the previously-declared-but-unused
				// SettleDelay substate instead, so tick()'s delay gate
				// actually postpones THIS verify's own finishCommand().
				delayCycles = 30000; // settle time, see class comment
				subState = SubState::SettleDelay;
				break;
			}
			updateTrack0Status();
			finishCommand();
			break;

		case SubState::SettleDelay:
			updateTrack0Status();
			finishCommand();
			break;

		default:
			finishCommand();
			break;
		}
	}

	void beginSectorXfer()
	{
		Drive* d = currentDrive();
		int len = 0, index = 0;
		uint8_t* p = d && d->hasImage
			? d->image.findSector(trackRegister, side_ ? 1 : 0, sectorRegister, len, &index)
			: nullptr;
		if (!p) {
			statusRegister |= STATUS_RNF;
			finishCommand();
			return;
		}
		statusRegister &= ~STATUS_RNF;
		xferPtr_ = p; xferLen_ = len; xferPos_ = 0;
		drqFlag_ = true;
		statusRegister |= STATUS_DRQ;
		subState = xferIsWrite_ ? SubState::TypeII_WriteByte : SubState::TypeII_ReadByte;
		logTrace("XFER");
	}

	void stepTypeIIFSM()
	{
		if (subState == SubState::TypeII_WaitData) beginSectorXfer();
		// TypeII_ReadByte/TypeII_WriteByte are driven by readRegister(3)/
		// writeRegister(3,...) directly (produceReadByte()/
		// consumeWriteByte()), not by tick() -- same as the clean-room
		// Wd1772's own DRQ-driven transfer, matching real hardware (the
		// FDC can't push/pull bytes faster than the CPU services DRQ).
	}

	uint8_t produceReadByte()
	{
		if (!xferPtr_ || xferPos_ >= xferLen_) return dataRegister;
		uint8_t v = xferPtr_[xferPos_++];
		dataRegister = v;
		if (xferPos_ >= xferLen_) advanceAfterSector();
		return v;
	}

	void consumeWriteByte()
	{
		if (!xferPtr_ || xferPos_ >= xferLen_) return;
		xferPtr_[xferPos_++] = dataRegister;
		if (xferPos_ >= xferLen_) advanceAfterSector();
	}

	void advanceAfterSector()
	{
		drqFlag_ = false;
		statusRegister &= ~STATUS_DRQ;
		// Persist Write Sector to the real .dsk file (2026-11-14 -- see
		// MEMORY.md/CLAUDE.md: writes used to land only in DskImage's
		// in-memory copy, never flushed back to disk). Once per completed
		// sector, not per byte -- xferIsWrite_ gates this out for Read
		// Sector, which shares this same completion path.
		if (xferIsWrite_) {
			Drive* d = currentDrive();
			if (d && d->hasImage) d->image.flush();
		}
		if (currentState == State::ExecutingTypeII && multiple_) {
			sectorRegister++;
			subState = SubState::TypeII_WaitData;
			// Re-enter immediately, same as the clean-room Wd1772's own
			// advanceAfterSector() -- tick()'s next call resolves it.
		} else {
			finishCommand();
		}
	}

	void stepTypeIIIFSM()
	{
		if (subState != SubState::TypeIII_WaitData) return; // ReadByte driven by readRegister(3) directly

		uint8_t fam = currentCommand & 0xF0;
		if (fam == 0xC0) { // READ ADDRESS
			Drive* d = currentDrive();
			uint8_t id = 0; int len = 0;
			if (!d || !d->hasImage || !d->image.sectorDescriptor(trackRegister, side_ ? 1 : 0, 0, id, len)) {
				statusRegister |= STATUS_RNF;
				finishCommand();
				return;
			}
			statusRegister &= ~STATUS_RNF;
			addrBuf_[0] = trackRegister;
			addrBuf_[1] = side_ ? 1 : 0;
			addrBuf_[2] = id;
			addrBuf_[3] = 1; // N: 256-byte sectors, same as the clean-room Wd1772
			addrBuf_[4] = 0; addrBuf_[5] = 0; // CRC not modeled, always reported valid
			sectorRegister = id; // real WD17xx side effect
			xferPtr_ = addrBuf_; xferLen_ = 6; xferPos_ = 0;
			drqFlag_ = true;
			statusRegister |= STATUS_DRQ;
			subState = SubState::TypeIII_ReadByte;
			logTrace("XFER");
		} else {
			// 0xE0 READ TRACK / 0xF0 WRITE TRACK: not implemented, see
			// class comment -- complete immediately rather than hanging.
			finishCommand();
		}
	}

	void executeTypeIV(uint8_t cmd)
	{
		currentState = State::Idle;
		subState = SubState::None;
		statusRegister &= ~STATUS_BUSY;
		drqFlag_ = false;
		xferPtr_ = nullptr; xferLen_ = xferPos_ = 0;

		uint8_t condition = cmd & 0x0F;
		if (condition & 0x08) intrqFlag_ = true;
		else if (condition == 0) intrqFlag_ = false;
		logTrace("FINT");
	}

	void finishCommand()
	{
		statusRegister &= ~STATUS_BUSY;
		statusRegister &= ~STATUS_DRQ;
		currentState = State::Idle;
		subState = SubState::None;
		drqFlag_ = false;
		intrqFlag_ = true;
		logTrace("DONE");
	}

	// Same shape as the clean-room Wd1772's own logTrace() (see its
	// comment) -- one line per call site (CMD/XFER/FINT/DONE) with the
	// live register/flag state at that moment.
	void logTrace(const char* what) const;
};
