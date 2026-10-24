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

// Transmit (talker) per-bit timing -- same per-bit scale as receive. Not
// tied to a specific measured controller delay like kAtnAckTstates: the
// controller's own per-bit loop ($3177-$318c) is a tight, undelayed spin
// waiting on CLK edges, so these just need to be comfortably longer than
// its "two consecutive reads must agree" stability check in $3025 (5
// iterations, ~65 T-states) for it to reliably see a settled read.
static constexpr uint32_t kTxClaimTstates = 245;      // 70us
static constexpr uint32_t kTxBitSetupTstates = 280;   // 80us
static constexpr uint32_t kTxBitValidTstates = 245;   // 70us

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
				// The real byte-sender ($30d4) repeats its own
				// release-and-check dance ($30d8-$30e1) before EVERY
				// byte it sends, under ATN or not -- it's generic,
				// used for addressing bytes and plain data bytes
				// alike. Re-ack rather than silently waiting in
				// WaitReadyToSend for a CLK edge that might already be
				// sitting asserted from the next byte's own early
				// preamble (see $30ab). Only stop once nothing more is
				// expected (role_ reset to None, e.g. after UNLISTEN).
				if (bus.atn() || role_ == IecRole::WeAreListener)
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
	switch (txPhase_) {
		case TxPhase::Idle:
			// Real trigger, read off the ROM's own $3365-$3372 ("release
			// our CLK, then spin waiting for CLK to read asserted"): we
			// are the talker and the bus's CLK is currently released --
			// that IS the controller's "go ahead" state, no separate
			// signal needed. requestRead() is blocking (real network
			// round-trip to the bridge) but only called once per byte,
			// right here, not on every idle tick.
			if (role_ == IecRole::WeAreTalker && !bus.clk() && !txAttempted_) {
				txAttempted_ = true;
				std::string data;
				bool eoi = false;
				if (device_ && device_->requestRead(activeChannel_, 1, data, eoi) && !data.empty()) {
					txByte_ = (uint8_t)data[0];
					txBitIndex_ = 0;
					bus.setDeviceClk(true);
					enterTxPhase(TxPhase::ClaimBus);
				}
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
					txAttempted_ = false; // byte done -- let the next one be requested
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
		// Abort any in-progress talker byte -- the controller takes
		// priority the instant it asserts ATN.
		if (txPhase_ != TxPhase::Idle) {
			bus.setDeviceClk(false);
			bus.setDeviceDat(false);
			enterTxPhase(TxPhase::Idle);
		}
		role_ = IecRole::None;
		enterAtnPresenceAck(bus);
	} else if (atnJustReleased && rxPhase_ != RxPhase::AtnPresenceAck) {
		// A presence ack in progress is left alone here -- its own
		// timeout (checkRxTimeouts) already releases DAT and advances
		// to WaitReadyToSend on its own schedule, which is tied to the
		// controller's own internal delay ($30b1-$30b9), NOT to
		// whenever ATN happens to drop. Cutting the hold short here
		// just reintroduces the original premature-CLK-edge bug for
		// whichever byte is in flight when ATN releases mid-ack.
		datWeDrive_ = false;
		bus.setDeviceDat(false);
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
		role_ = IecRole::WeAreTalker;
		txAttempted_ = false; // fresh TALK -- allow a read attempt
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
		txAttempted_ = false; // new/changed channel -- allow a fresh read attempt
		// Under TALK there's no data phase to collect a filename from
		// (that only happens under LISTEN, terminated by UNLISTEN --
		// see finalizeSession()) -- live-traced against the real SDI
		// ROM's own *C (catalog) command: it addresses TALK, opens
		// channel 0, and starts clocking bits WITHOUT ever sending a
		// name at all. Channel 0 defaults to "$" (the standard CBM
		// directory pseudo-file), matching the bridge's own special
		// case for it and the SDI manual's documented SD2IEC workflow.
		if (role_ == IecRole::WeAreTalker && activeChannel_ == 0 && device_)
			device_->open(activeChannel_, "$");
	} else if (isClose) {
		secondaryCmd_ = SecondaryCmd::Close;
		activeChannel_ = b & 0x0f;
		if (device_) device_->close(activeChannel_);
	} else if (b >= 0xf0) {
		secondaryCmd_ = SecondaryCmd::PlainSelect;
		activeChannel_ = b & 0x0f;
		txAttempted_ = false;
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
