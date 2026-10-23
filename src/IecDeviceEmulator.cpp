#include "IecDeviceEmulator.h"

// Z80 clock fixed at 3.5MHz in JASE (see AyPsg.cpp) -- 1us = 3.5 T-states.
static constexpr uint32_t kEoiWaitTstates = 700;      // 200us
static constexpr uint32_t kEoiAckPulseTstates = 280;  // 80us
static constexpr uint32_t kAckHoldTstates = 245;      // 70us
// Presence ack (see onControllerChanged) -- measured directly against the
// real SDI103J ROM: it's 3761 T-states from the ATN OUT ($30a8) to its own
// presence check ($30e1), via a software delay loop ($30b1-$30b9), not a
// fixed protocol constant like the EOI/ack timings above. Held a bit past
// that measured value for margin.
static constexpr uint32_t kAtnAckTstates = 4200;

// Idle-offer transmit cycle (see class comment -- an inference, not a
// confirmed protocol fact): same per-bit timing scale as receive.
static constexpr uint32_t kTxClaimTstates = 245;      // 70us
static constexpr uint32_t kTxBitSetupTstates = 280;   // 80us
static constexpr uint32_t kTxBitValidTstates = 245;   // 70us
static constexpr uint32_t kTxIdleGapTstates = 3500;   // ~1ms between offers

void IecDeviceEmulator::advanceTime(uint32_t tstates)
{
	if (!haveLastTstates_) {
		lastTstates_ = tstates;
		haveLastTstates_ = true;
		return;
	}
	// MinZX's tstates resets to 0 every frame -- see SDI_8255's own
	// comment on this; same wraparound handling here.
	uint32_t delta = (tstates >= lastTstates_) ? (tstates - lastTstates_) : tstates;
	lastTstates_ = tstates;
	rxElapsed_ += delta;
	txElapsed_ += delta;
}

void IecDeviceEmulator::enterRxPhase(RxPhase p)
{
	rxPhase_ = p;
	rxElapsed_ = 0;
}

void IecDeviceEmulator::enterTxPhase(TxPhase p)
{
	txPhase_ = p;
	txElapsed_ = 0;
}

void IecDeviceEmulator::checkRxTimeouts(IecBus& bus)
{
	switch (rxPhase_) {
		case RxPhase::AtnPresenceAck:
			if (rxElapsed_ >= kAtnAckTstates) {
				datWeDrive_ = false;
				bus.setDeviceDat(false);
				enterRxPhase(RxPhase::WaitReadyToSend);
			}
			break;
		case RxPhase::WaitReadyToSend:
			if (rxElapsed_ >= kEoiWaitTstates) {
				byteEoiPending_ = true;
				datWeDrive_ = true;
				bus.setDeviceDat(true);
				enterRxPhase(RxPhase::EoiAckPulse);
			}
			break;
		case RxPhase::EoiAckPulse:
			if (rxElapsed_ >= kEoiAckPulseTstates) {
				datWeDrive_ = false;
				bus.setDeviceDat(false);
				enterRxPhase(RxPhase::WaitReadyToSendNoEoi);
			}
			break;
		case RxPhase::AckHold:
			if (rxElapsed_ >= kAckHoldTstates) {
				datWeDrive_ = false;
				bus.setDeviceDat(false);
				// Still under ATN -- the real ROM repeats its own
				// release-and-check dance ($30d8-$30e1) before EVERY
				// byte, not just the first, so re-ack presence rather
				// than silently waiting in WaitReadyToSend for a CLK
				// edge that might already be sitting asserted from the
				// next byte's own early preamble (see $30ab).
				if (bus.atn())
					enterAtnPresenceAck(bus);
				else
					enterRxPhase(RxPhase::WaitReadyToSend);
			}
			break;
		default:
			break;
	}
}

void IecDeviceEmulator::checkTxTimeouts(IecBus& bus)
{
	// DISABLED (see class comment and VC1541.md): the periodic idle-offer
	// was an unprincipled guess at what the controller's pre-ATN receive
	// loop ($3142-$318c in the real ROM) wants. That loop's own timeout
	// (D=0x14, bounded) is almost certainly designed to resolve FAST and
	// cleanly when nothing answers -- the normal, correct behavior for a
	// genuinely idle/quiet bus -- and the unsynchronized periodic offer
	// most likely made things WORSE (partial, misaligned CLK transitions
	// confusing that bounded wait into retrying) rather than better.
	// Reverted to "drive nothing" pending a real understanding of what
	// that probe wants, if anything, once idle alone is confirmed to
	// resolve the bounded wait cleanly.
	bool busIdle = false; (void)busIdle;

	switch (txPhase_) {
		case TxPhase::Idle:
			if (busIdle && txElapsed_ >= kTxIdleGapTstates) {
				txByte_ = 0x00; // placeholder -- see class comment
				txBitIndex_ = 0;
				bus.setDeviceClk(true);
				enterTxPhase(TxPhase::ClaimBus);
			}
			break;
		case TxPhase::ClaimBus:
			if (txElapsed_ >= kTxClaimTstates) {
				bus.setDeviceDat((txByte_ & 1) == 0); // asserted=bit0, released=bit1
				enterTxPhase(TxPhase::BitSetup);
			}
			break;
		case TxPhase::BitSetup:
			if (txElapsed_ >= kTxBitSetupTstates) {
				bus.setDeviceClk(false); // release -> valid, controller samples
				enterTxPhase(TxPhase::BitValid);
			}
			break;
		case TxPhase::BitValid:
			if (txElapsed_ >= kTxBitValidTstates) {
				txBitIndex_++;
				if (txBitIndex_ >= 8) {
					bus.setDeviceClk(false);
					bus.setDeviceDat(false);
					enterTxPhase(TxPhase::Idle);
				} else {
					txByte_ >>= 1; // LSB-first, same convention as receive
					bus.setDeviceClk(true);
					bus.setDeviceDat((txByte_ & 1) == 0);
					enterTxPhase(TxPhase::BitSetup);
				}
			}
			break;
		default:
			break;
	}
}

void IecDeviceEmulator::tick(IecBus& bus, uint32_t tstates)
{
	advanceTime(tstates);
	checkRxTimeouts(bus);
	checkTxTimeouts(bus);
}

// Presence ack (see $30e0-$30e1 in the real ROM): the controller checks
// for DAT asserted after releasing its own, and restarts if nothing
// answers. isConnected() stands in for "is there a drive on the bus at
// all" -- same fallback IIecDevice.h documents (no bridge configured =
// honest "not present"). Re-entered for EVERY byte under ATN, not just
// the first -- the real ROM repeats this exact release-and-check dance
// ($30d8-$30e1) before each byte it sends, not only at ATN's own edge.
void IecDeviceEmulator::enterAtnPresenceAck(IecBus& bus)
{
	if (device_ && device_->isConnected()) {
		datWeDrive_ = true;
		bus.setDeviceDat(true);
		enterRxPhase(RxPhase::AtnPresenceAck);
	} else {
		datWeDrive_ = false;
		bus.setDeviceDat(false);
		enterRxPhase(RxPhase::Idle);
	}
}

void IecDeviceEmulator::onControllerChanged(IecBus& bus, uint32_t tstates)
{
	advanceTime(tstates);
	checkRxTimeouts(bus);
	checkTxTimeouts(bus);

	bool atn = bus.atn();
	bool ctrlClk = bus.controllerClk();
	bool ctrlDat = bus.controllerDat();

	bool clkReleaseEdge = !ctrlClk && lastCtrlClk_;
	bool atnJustAsserted = atn && !lastAtn_;
	bool atnJustReleased = !atn && lastAtn_;

	if (atnJustAsserted) {
		// Abort any in-progress idle offer -- the controller takes
		// priority the instant it asserts ATN.
		if (txPhase_ != TxPhase::Idle) {
			bus.setDeviceClk(false);
			bus.setDeviceDat(false);
			enterTxPhase(TxPhase::Idle);
		}
		role_ = IecRole::None;
		enterAtnPresenceAck(bus);
	} else if (atnJustReleased) {
		if (role_ == IecRole::WeAreListener || role_ == IecRole::None)
			enterRxPhase(RxPhase::WaitReadyToSend);
		else
			enterRxPhase(RxPhase::Idle); // WeAreTalker-via-ATN: not implemented yet
	}

	bool receivingNow = atn || role_ == IecRole::WeAreListener;
	if (receivingNow) {
		switch (rxPhase_) {
			case RxPhase::WaitReadyToSend:
			case RxPhase::WaitReadyToSendNoEoi:
				if (ctrlClk) { // controller asserted CLK = ready-to-send
					bitCount_ = 0;
					shiftByte_ = 0;
					enterRxPhase(RxPhase::Receiving);
				}
				break;

			case RxPhase::Receiving:
				if (clkReleaseEdge) {
					// Verified convention: asserted DAT = bit 0, released
					// DAT = bit 1. LSB-first.
					shiftByte_ >>= 1;
					if (!ctrlDat) shiftByte_ |= 0x80;
					bitCount_++;
					if (bitCount_ == 8) {
						uint8_t byte = shiftByte_;
						bool eoi = byteEoiPending_;
						byteEoiPending_ = false;
						datWeDrive_ = true; // ack
						bus.setDeviceDat(true);
						enterRxPhase(RxPhase::AckHold);
						onByteReceived(byte, eoi);
					}
				}
				break;

			default:
				break; // time-driven phases, handled above via checkRxTimeouts
		}
	}

	lastAtn_ = atn;
	lastCtrlClk_ = ctrlClk;
}

void IecDeviceEmulator::onByteReceived(uint8_t byte, bool eoi)
{
	(void)eoi; // not yet forwarded -- SAVE payload handling isn't implemented
	lastByte_ = byte;
	if (lastAtn_) {
		onAtnByteReceived(byte);
	} else {
		onDataByte(byte);
	}
}

void IecDeviceEmulator::onAtnByteReceived(uint8_t byte)
{
	if (byte == 0x3f || byte == 0x5f) {        // UNLISTEN / UNTALK
		finalizeSession();
		role_ = IecRole::None;
	} else if (byte >= 0x20 && byte <= 0x3e) {
		role_ = IecRole::WeAreListener;        // LISTEN: we keep receiving
	} else if (byte >= 0x40 && byte <= 0x5e) {
		role_ = IecRole::WeAreTalker;          // TALK: we should send -- not implemented yet
	} else {
		onSecondaryByte(byte);
	}
}

void IecDeviceEmulator::onSecondaryByte(uint8_t b)
{
	bool isClose = (b >= 0xe0 && b <= 0xef);
	if (b >= 0x60 && b <= 0x6f) {
		secondaryCmd_ = SecondaryCmd::Open;
		activeChannel_ = b & 0x0f;
		nameBuf_.clear();
	} else if (isClose) {
		secondaryCmd_ = SecondaryCmd::Close;
		activeChannel_ = b & 0x0f;
		if (device_) device_->close(activeChannel_);
	} else if (b >= 0xf0) {
		secondaryCmd_ = SecondaryCmd::PlainSelect;
		activeChannel_ = b & 0x0f;
	}
}

void IecDeviceEmulator::onDataByte(uint8_t b)
{
	// OPEN's filename arrives as data-phase bytes following the
	// secondary address, terminated by UNLISTEN (see finalizeSession).
	// SAVE's payload isn't forwarded anywhere yet.
	if (secondaryCmd_ == SecondaryCmd::Open)
		nameBuf_ += (char)b;
}

void IecDeviceEmulator::finalizeSession()
{
	if (secondaryCmd_ == SecondaryCmd::Open && !nameBuf_.empty() && device_)
		device_->open(activeChannel_, nameBuf_);
	secondaryCmd_ = SecondaryCmd::None;
	nameBuf_.clear();
}
