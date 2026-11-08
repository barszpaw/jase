#include "Wd1772.h"

void Wd1772::reset()
{
	command_ = track_ = sector_ = data_ = 0;
	busy_ = drq_ = intrq_ = false;
	crcError_ = recordNotFound_ = false;
	lastCommandWasTypeI_ = true; // real chip's power-up status format
	lastDirection_ = -1;
	busyOneShot_ = false;
	xferMode_ = Xfer::None;
	xferPtr_ = nullptr; xferLen_ = xferPos_ = 0;
	multiple_ = false;
	pendingOp_ = PendingOp::None;
	pendingDelaySteps_ = 0;
	pendingXferPtr_ = nullptr; pendingXferLen_ = 0;
}

void Wd1772::tick()
{
	if (pendingOp_ == PendingOp::None) return;
	if (pendingDelaySteps_ > 0) { pendingDelaySteps_--; return; }

	PendingOp op = pendingOp_;
	pendingOp_ = PendingOp::None;
	switch (op) {
	case PendingOp::BeginRead:
		xferPtr_ = pendingXferPtr_; xferLen_ = pendingXferLen_; xferPos_ = 0;
		xferMode_ = Xfer::Read;
		drq_ = true;
		break;
	case PendingOp::BeginWrite:
		xferPtr_ = pendingXferPtr_; xferLen_ = pendingXferLen_; xferPos_ = 0;
		xferMode_ = Xfer::Write;
		drq_ = true;
		break;
	case PendingOp::ReadAddress:
		xferPtr_ = addrBuf_; xferLen_ = 6; xferPos_ = 0;
		xferMode_ = Xfer::Read;
		drq_ = true;
		break;
	case PendingOp::RecordNotFound:
		recordNotFound_ = true;
		finishCommand(true);
		break;
	case PendingOp::None:
		break;
	}
}

void Wd1772::armDelay(int sectorIndex, int sectorCount)
{
	(void)sectorCount;
	pendingDelaySteps_ = sectorIndex * kStepsPerSectorSlot;
}

bool Wd1772::insertDisk(int driveIndex, const std::string& path)
{
	if (driveIndex < 0 || driveIndex >= kNumDrives) return false;
	Drive& d = drives_[driveIndex];
	d.hasImage = d.image.load(path);
	return d.hasImage;
}

Wd1772::Drive* Wd1772::currentDrive()
{
	if (selectedDrive_ < 0 || selectedDrive_ >= kNumDrives) return nullptr;
	return &drives_[selectedDrive_];
}

bool Wd1772::currentDriveHasImage() const
{
	if (selectedDrive_ < 0 || selectedDrive_ >= kNumDrives) return false;
	return drives_[selectedDrive_].hasImage;
}

void Wd1772::writeCommand(uint8_t cmd)
{
	// Force Interrupt (0xDX) is the one command real hardware accepts
	// even while BUSY -- it's specifically how software aborts a
	// stuck/unwanted command.
	if ((cmd & 0xF0) == 0xD0) { forceInterrupt(cmd); return; }
	if (busy_) return; // real hardware ignores any other command while busy

	command_ = cmd;
	if ((cmd & 0x80) == 0) startTypeI(cmd);
	else if ((cmd & 0xC0) == 0x80) startTypeII(cmd);
	else startTypeIII(cmd);
}

void Wd1772::writeDataReg(uint8_t value)
{
	if (xferMode_ == Xfer::Write && xferPos_ < xferLen_) {
		xferPtr_[xferPos_++] = value;
		if (xferPos_ >= xferLen_) advanceAfterSector();
		// else DRQ stays set (re-armed instantly in this simplified,
		// untimed model) for the next byte of the same sector.
	} else {
		data_ = value; // plain register latch -- e.g. the Seek target track
	}
}

uint8_t Wd1772::readDataReg()
{
	if (xferMode_ != Xfer::None && xferPos_ < xferLen_) {
		uint8_t v = xferPtr_[xferPos_++];
		data_ = v;
		if (xferPos_ >= xferLen_) {
			if (xferMode_ == Xfer::Read) advanceAfterSector();
			// Read Address (xferMode_==Read too, 6-byte addrBuf_) ends the
			// same way as a single-sector read -- advanceAfterSector()
			// only continues for Type II's own multiple_ flag, which Read
			// Address never sets.
		}
		return v;
	}
	return data_;
}

uint8_t Wd1772::peekStatus() const
{
	uint8_t s = 0;
	if (busy_ || busyOneShot_) s |= 0x01;

	if (!currentDriveHasImage()) s |= 0x80; // NRDY/MON, see class comment

	if (lastCommandWasTypeI_) {
		if (track_ == 0) s |= 0x04; // TR00
		if (crcError_) s |= 0x08;
	} else {
		if (drq_) s |= 0x02;
		if (crcError_) s |= 0x08;
		if (recordNotFound_) s |= 0x10;
	}
	return s;
}

uint8_t Wd1772::readStatus()
{
	uint8_t s = peekStatus();
	busyOneShot_ = false; // real register-read side effect, see class comment
	intrq_ = false;       // real WD17xx: reading STATUS clears INTRQ
	return s;
}

void Wd1772::startTypeI(uint8_t cmd)
{
	lastCommandWasTypeI_ = true;
	busy_ = true; drq_ = false; intrq_ = false;
	crcError_ = false; recordNotFound_ = false;

	uint8_t fam = cmd & 0xF0;
	if (fam == 0x00) { // RESTORE
		track_ = 0;
		lastDirection_ = -1;
	} else if (fam == 0x10) { // SEEK -- target track was previously latched into Data via writeDataReg()
		lastDirection_ = (data_ > track_) ? +1 : -1;
		track_ = data_;
	} else if (fam == 0x20 || fam == 0x30) { // STEP (re-uses the last direction)
		int t = (int)track_ + lastDirection_;
		track_ = (uint8_t)(t < 0 ? 0 : (t > 255 ? 255 : t));
	} else if (fam == 0x40 || fam == 0x50) { // STEP IN
		lastDirection_ = +1;
		if (track_ < 255) track_++;
	} else { // 0x60/0x70: STEP OUT
		lastDirection_ = -1;
		if (track_ > 0) track_--;
	}

	if (cmd & 0x04) { // V (verify): confirm the new track actually has a formatted ID field
		Drive* d = currentDrive();
		if (!d || !d->hasImage || d->image.sectorCount(track_, side_ ? 1 : 0) == 0)
			crcError_ = true;
	}

	finishCommand(true);
}

void Wd1772::startTypeII(uint8_t cmd)
{
	lastCommandWasTypeI_ = false;
	busy_ = true; drq_ = false; intrq_ = false;
	crcError_ = false; recordNotFound_ = false;
	multiple_ = (cmd & 0x10) != 0;

	bool write = (cmd & 0x20) != 0;
	if (write) beginSectorWrite(); else beginSectorRead();
}

void Wd1772::beginSectorRead()
{
	drq_ = false; // may be re-entered from advanceAfterSector() with drq_ still set from the prior sector
	Drive* d = currentDrive();
	int len = 0, index = 0;
	uint8_t* p = d && d->hasImage ? d->image.findSector(track_, side_ ? 1 : 0, sector_, len, &index) : nullptr;
	if (!p) { pendingOp_ = PendingOp::RecordNotFound; pendingDelaySteps_ = kRnfGiveUpSteps; return; }
	pendingXferPtr_ = p; pendingXferLen_ = len;
	pendingOp_ = PendingOp::BeginRead;
	armDelay(index, d->image.sectorCount(track_, side_ ? 1 : 0));
}

void Wd1772::beginSectorWrite()
{
	drq_ = false; // see beginSectorRead()
	Drive* d = currentDrive();
	int len = 0, index = 0;
	// Can only overwrite a sector that already exists in the image's
	// track layout -- there's no way to grow/reformat a DskImage here
	// (Write Track, which is how real FORMAT creates new sector
	// descriptors, isn't implemented -- see class comment).
	uint8_t* p = d && d->hasImage ? d->image.findSector(track_, side_ ? 1 : 0, sector_, len, &index) : nullptr;
	if (!p) { pendingOp_ = PendingOp::RecordNotFound; pendingDelaySteps_ = kRnfGiveUpSteps; return; }
	pendingXferPtr_ = p; pendingXferLen_ = len;
	pendingOp_ = PendingOp::BeginWrite;
	armDelay(index, d->image.sectorCount(track_, side_ ? 1 : 0));
}

void Wd1772::advanceAfterSector()
{
	if (multiple_ && xferMode_ != Xfer::None) {
		Xfer mode = xferMode_;
		sector_++;
		if (mode == Xfer::Read) beginSectorRead(); else beginSectorWrite();
		// beginSectorRead()/beginSectorWrite() always just arm a pending
		// op now (success or RNF) -- tick() resolves it, nothing more to
		// do here.
	} else {
		finishCommand(true);
	}
}

void Wd1772::startTypeIII(uint8_t cmd)
{
	lastCommandWasTypeI_ = false;
	busy_ = true; drq_ = false; intrq_ = false;
	crcError_ = false; recordNotFound_ = false;

	uint8_t fam = cmd & 0xF0;
	if (fam == 0xC0) { // READ ADDRESS
		beginReadAddress();
	} else {
		// 0xE0 READ TRACK / 0xF0 WRITE TRACK: not implemented, see class
		// comment -- complete immediately rather than leaving the guest
		// waiting on DRQ forever.
		finishCommand(true);
	}
}

void Wd1772::beginReadAddress()
{
	Drive* d = currentDrive();
	uint8_t id = 0; int len = 0;
	if (!d || !d->hasImage || !d->image.sectorDescriptor(track_, side_ ? 1 : 0, 0, id, len)) {
		pendingOp_ = PendingOp::RecordNotFound; pendingDelaySteps_ = kRnfGiveUpSteps; return;
	}
	addrBuf_[0] = track_;
	addrBuf_[1] = side_ ? 1 : 0;
	addrBuf_[2] = id;
	addrBuf_[3] = 1; // N: sector-size code, 1 => 256 bytes (every real image found here uses 256-byte sectors)
	addrBuf_[4] = 0; addrBuf_[5] = 0; // CRC -- not modeled, always reported valid
	sector_ = id; // real WD17xx side effect: Sector register picks up the id just read
	pendingOp_ = PendingOp::ReadAddress;
	armDelay(0, d->image.sectorCount(track_, side_ ? 1 : 0)); // always descriptor index 0, see sectorDescriptor() call above
}

void Wd1772::forceInterrupt(uint8_t cmd)
{
	busy_ = false; drq_ = false;
	xferMode_ = Xfer::None;
	pendingOp_ = PendingOp::None; // real hardware: Force Interrupt aborts an in-progress ID search too
	// I0-I2 (ready-transition/index-pulse conditions) aren't modeled --
	// see class comment. I3 (bit3, immediate interrupt) is the only
	// condition that reliably fires in a non-time-simulated model.
	intrq_ = (cmd & 0x08) != 0;
}

void Wd1772::finishCommand(bool setIntrq)
{
	busy_ = false;
	drq_ = false;
	xferMode_ = Xfer::None;
	if (lastCommandWasTypeI_) busyOneShot_ = true; // see class comment
	if (setIntrq) intrq_ = true;
}
