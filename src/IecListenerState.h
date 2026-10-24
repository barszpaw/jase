#pragma once
#include "IecState.h"
#include <cstdint>

// The receive direction: us as IEC listener, whether that's because the
// controller is addressing everyone under ATN (every device must listen
// during an ATN cycle, see Butterfield's article) or because a LISTEN
// command actually selected us for a data phase afterward. Byte-level
// algorithm and timing (200us CLK-wait before declaring EOI, 80us
// EOI-ack pulse) read from fox20io/c64-fdemu's IECBusHandler.cpp,
// receiveIECByte() (GPLv3, not copied -- facts only, re-implemented
// clean-room, same as this project's other wire-protocol reuses).
class IecListenerState : public IecState
{
public:
	void onEnter(IecDeviceEmulator& ctx, IecBus& bus) override;
	void onEdge(IecDeviceEmulator& ctx, IecBus& bus) override;
	void onTick(IecDeviceEmulator& ctx, IecBus& bus, uint32_t deltaTstates) override;

	// Entered on ATN assert (and re-entered before every byte the
	// controller sends, per the real ROM's $30d8-$30e1 dance) --
	// asserts our presence (DAT true) if a device is actually behind
	// us, or honestly stays silent ("device not present") if not.
	void enterAtnPresenceAck(IecDeviceEmulator& ctx, IecBus& bus);

	// Re-enter the baseline "waiting for the next byte" phase without
	// the presence-ack step -- used when ATN releases and we're not
	// about to receive a fresh addressing byte (role_ already settled).
	void enterWaitReadyToSend();

	// For the IECBUS debug command -- unchanged field names/values from
	// the old IecDeviceEmulator so the debug protocol surface doesn't
	// change.
	int phase() const { return (int)subState_; }
	int bitCount() const { return bitCount_; }
	uint8_t shiftByte() const { return shiftByte_; }
	bool isInPresenceAck() const { return subState_ == SubState::AtnPresenceAck; }

	// True in the "between bytes, nothing in flight" sub-phases -- safe
	// points for the facade to hand off to the talker once ATN has
	// released and role_ says we should (see IecDeviceEmulator's post-
	// tick check). Deliberately excludes AtnPresenceAck/Receiving/
	// AckHold/EoiAckPulse -- those are active holds or in-progress bit
	// reception with their own timing-dependent completion, and cutting
	// them short reintroduces the exact premature-edge bug documented
	// on AtnPresenceAck's own handling elsewhere in this project.
	bool isIdleWaiting() const {
		return subState_ == SubState::WaitReadyToSend || subState_ == SubState::WaitReadyToSendNoEoi;
	}

private:
	enum class SubState {
		Idle, AtnPresenceAck, WaitReadyToSend, EoiAckPulse, WaitReadyToSendNoEoi, Receiving, AckHold,
	};

	void enterSubState(SubState s);

	SubState subState_ = SubState::Idle;
	uint32_t elapsed_ = 0;
	int bitCount_ = 0;
	uint8_t shiftByte_ = 0;
	bool byteEoiPending_ = false;
	bool datWeDrive_ = false; // our ack/ready-for-data drive, true=asserted

	bool lastCtrlClk_ = false;
};
