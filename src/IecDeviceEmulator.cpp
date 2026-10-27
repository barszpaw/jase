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
		// Channel 15 is the CBM error/status channel -- handled here
		// defensively (auto-open under TALK, same shape as channel 0's
		// catalog case) in case some future ROM path addresses it this
		// way. NOT what SDI's own *L actually does, though: live-traced
		// with a full atn/role transition dump, its device-presence
		// probe addresses channel 15 under LISTEN (role_==WeAreListener),
		// not TALK -- see the PlainSelect branch below, which is the
		// shape that probe actually uses, and whose own comment explains
		// why this WeAreTalker branch never fires for it in practice.
		//
		// NOTE: device_->open() below is a synchronous, blocking TCP
		// call (see IecBridgeClient::open()'s own warning) made straight
		// from this hot dispatch path. Fine against a fast local bridge;
		// would stall the whole emulated CPU against a slow/remote one.
		if (role_ == IecRole::WeAreTalker && device_) {
			if (activeChannel_ == 0)
				device_->open(activeChannel_, "$");
			else if (activeChannel_ == 15)
				device_->open(activeChannel_, "$STATUS");
		}
		// For WeAreListener role, the OPEN secondary means the controller
		// (SDI) is about to send us a filename (LOAD/SAVE/CAT). We don't
		// need to do anything here -- the data bytes will come in onDataByte
		// and we already have the LOAD encoder ready.
	} else if (isClose) {
		secondaryCmd_ = SecondaryCmd::Close;
		activeChannel_ = b & 0x0f;
		if (device_) device_->close(activeChannel_);
	} else if (b >= 0xf0) {
		secondaryCmd_ = SecondaryCmd::PlainSelect;
		activeChannel_ = b & 0x0f;
		// This IS the shape SDI's own *L actually uses to probe channel
		// 15: LISTEN + plain-select 0xFF, role_==WeAreListener -- so the
		// WeAreTalker guard below never fires for it (kept for protocol
		// completeness/future-proofing, not because SDI needs it).
		// Confirmed by a full atn/role/secCmd/chan transition dump
		// (IECTRACE) around a real *L run: the probe is a sub-millisecond
		// ATN pulse (ATN drops for ~20 trace samples then reasserts)
		// with NO byte ever completing a full 8-bit shift under it --
		// i.e. SDI isn't actually writing or reading data on channel 15
		// here at all, just pulsing ATN, structurally identical to the
		// ATN-presence-ack handshake IecListenerState::enterAtnPresenceAck
		// already answers (DAT asserted locally, no device_ call, no
		// network I/O -- see that function). So there was never a real
		// "channel 15 blocks forever" bug in this flow: the earlier
		// appearance of one came from an incompletely-submitted test
		// command (ENTER not actually registering), not from this
		// probe. The code below is honest protocol support for a
		// hypothetical TALK-side use of channel 15, not a fix for
		// anything SDI's own ROM was observed to need.
		if (role_ == IecRole::WeAreTalker && activeChannel_ == 15 && device_)
			device_->open(activeChannel_, "$STATUS");
	}
	// Kick off the send path for WeAreTalker when we have an OPEN or
	// PlainSelect secondary. The real ROM uses both forms:
	// - OPEN ($60-$6F): explicit OPEN command (e.g., TALK+OPEN channel 15 for status)
	// - PlainSelect ($F0-$FF): KERNAL LOAD/SAVE convention without separate OPEN
	// Both should trigger our talker to start sending (filename for OPEN,
	// or data for SAVE/PlainSelect).
	if (role_ == IecRole::WeAreTalker && (secondaryCmd_ == SecondaryCmd::Open || secondaryCmd_ == SecondaryCmd::PlainSelect)) {
		// The talker will pick this up in its Idle state via queueSendByte
		// when it has data to send. For now, we just note that a send
		// session is active.
	}
}

void IecDeviceEmulator::onDataByte(uint8_t b)
{
	// OPEN's filename arrives as data-phase bytes following the
	// secondary address, terminated by UNLISTEN (see finalizeSession).
	// SAVE's payload isn't forwarded anywhere yet.
	if (secondaryCmd_ == SecondaryCmd::Open) {
		nameBuf_ += (char)b;
		// If we're the talker (controller sending filename to device),
		// queue this byte for transmission via the IEC bus.
		if (role_ == IecRole::WeAreTalker) {
			// We don't know EOI yet for individual bytes -- the real ROM
			// sends filename bytes via $30bc/$30d4 loop, then UNLISTEN
			// terminates. The last byte before UNLISTEN is effectively
			// the EOI byte, but we don't know that until we see UNLISTEN.
			// For now, send all with eoi=false; the finalizeSession will
			// handle the last byte specially if needed.
			talker_.queueSendByte(b, false);
		}
	}
}

void IecDeviceEmulator::finalizeSession()
{
	if (secondaryCmd_ == SecondaryCmd::Open && !nameBuf_.empty() && device_)
		device_->open(activeChannel_, nameBuf_);
	secondaryCmd_ = SecondaryCmd::None;
	nameBuf_.clear();
}
