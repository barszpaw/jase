#include "IecDeviceEmulator.h"

// MinZX's tstates resets to 0 every frame (see SDI_8255's own comment
// on this) -- a fixed 69888 T-states/frame (minzx.cpp's cycleTstates),
// same for every machine mode JASE supports. Needed for correct
// wraparound math below: the OLD code's wraparound handling collapsed
// delta to just the new tstates value, silently discarding the
// (kFrameTstates - lastTstates_) remainder of the old frame -- a real,
// if rare, time-measurement bug, found while re-deriving this during
// the State-pattern rewrite. Whether it ever lined up with the still-
// unexplained "stuck at bit 5 of 8" symptom from the old code is
// unconfirmed, but it's a genuine correctness bug either way and is
// fixed here regardless.
static constexpr uint32_t kFrameTstates = 69888;

IecDeviceEmulator::IecDeviceEmulator()
{
	current_ = &listener_;
}

void IecDeviceEmulator::switchToListener(IecBus& bus)
{
	current_ = &listener_;
	listener_.enterAtnPresenceAck(*this, bus);
}

void IecDeviceEmulator::switchToTalker(IecBus& bus)
{
	current_ = &talker_;
	talker_.onEnter(*this, bus);
}

static uint32_t computeDelta(uint32_t tstates, uint32_t& lastTstates, bool& have)
{
	if (!have) {
		lastTstates = tstates;
		have = true;
		return 0;
	}
	uint32_t delta;
	if (tstates >= lastTstates)
		delta = tstates - lastTstates;
	else
		delta = (kFrameTstates - lastTstates) + tstates; // wraparound, see comment above
	lastTstates = tstates;
	return delta;
}

void IecDeviceEmulator::maybeHandoffToTalker(IecBus& bus)
{
	if (current_ == &listener_ && !bus.atn() && role_ == IecRole::WeAreTalker && listener_.isIdleWaiting())
		switchToTalker(bus);
}

void IecDeviceEmulator::tick(IecBus& bus, uint32_t tstates)
{
	uint32_t delta = computeDelta(tstates, lastTstates_, haveLastTstates_);
	if (current_) current_->onTick(*this, bus, delta);
	maybeHandoffToTalker(bus);
}

void IecDeviceEmulator::onControllerChanged(IecBus& bus, uint32_t tstates)
{
	uint32_t delta = computeDelta(tstates, lastTstates_, haveLastTstates_);
	if (current_) current_->onTick(*this, bus, delta);

	bool atn = bus.atn();
	bool atnJustAsserted = atn && !lastAtn_;
	bool atnJustReleased = !atn && lastAtn_;

	if (atnJustAsserted) {
		// Abort any in-progress talker byte and take over as listener --
		// the controller takes priority the instant it asserts ATN
		// (Butterfield: "ATN overrides everything in progress").
		if (current_ == &talker_)
			talker_.abort(bus);
		role_ = IecRole::None;
		switchToListener(bus);
	} else if (atnJustReleased && !(current_ == &listener_ && listener_.isInPresenceAck())) {
		// A presence ack in progress is left alone here -- its own
		// timeout already releases DAT and advances on its own
		// schedule, tied to the controller's own internal delay, NOT to
		// whenever ATN happens to drop. Cutting the hold short here
		// just reintroduces the original premature-CLK-edge bug for
		// whichever byte is in flight when ATN releases mid-ack.
		bus.setDeviceDat(false);
		if (role_ == IecRole::WeAreListener || role_ == IecRole::None) {
			current_ = &listener_;
			listener_.enterWaitReadyToSend();
		} else {
			// WeAreTalker: ATN has released after a real TALK+OPEN --
			// this is the turnaround point. Only now does the talker
			// state actually become current and start trying to fetch
			// and send a byte; while ATN was still up, listener_ stayed
			// current the whole time (even after the TALK/secondary
			// bytes decoded), so there's no window where talker_ can
			// race ahead of the controller's own addressing sequence.
			switchToTalker(bus);
		}
	}

	if (current_) current_->onEdge(*this, bus);
	maybeHandoffToTalker(bus);

	lastAtn_ = atn;
}

void IecDeviceEmulator::onByteReceived(uint8_t byte, bool eoi, bool underAtn)
{
	(void)eoi; // not yet forwarded -- SAVE payload handling isn't implemented
	lastByte_ = byte;
	if (underAtn)
		onAtnByteReceived(byte);
	else
		onDataByte(byte);
}

void IecDeviceEmulator::onAtnByteReceived(uint8_t byte)
{
	if (byte == 0x3f || byte == 0x5f) {        // UNLISTEN / UNTALK
		finalizeSession();
		role_ = IecRole::None;
	} else if (byte >= 0x20 && byte <= 0x3e) {
		role_ = IecRole::WeAreListener;        // LISTEN: we keep receiving
	} else if (byte >= 0x40 && byte <= 0x5e) {
		role_ = IecRole::WeAreTalker;          // actual switch to talker_ waits for ATN release, see onControllerChanged
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
