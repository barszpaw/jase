#pragma once
#include "IecBus.h"
#include "IIecDevice.h"
#include "IecState.h"
#include "IecListenerState.h"
#include "IecTalkerState.h"
#include <cstdint>
#include <string>

// The "device" side of the IEC bus -- symmetric counterpart to
// SDI_8255/the Z80 controller side, both attached to a shared IecBus.
//
// Rewritten (see project memory, "Gemini IEC architecture rewrite") as
// a Facade/Context over the State pattern in IecState.h: exactly one of
// listener_/talker_ is ever "current" and gets ticked/edged. This is
// what the old dual checkRxTimeouts()/checkTxTimeouts() design didn't
// have -- a stale RX timer firing mid-TX-bit and stomping the line was
// a real bug here before, fixed with a role/phase guard at the time;
// the State pattern makes that guard structurally unnecessary, not just
// patched over.
//
// This class still owns everything that's genuinely cross-cutting
// between the two roles: ATN edge detection (ATN overrides whichever
// state is current, a real protocol fact -- see Butterfield's article,
// "ATN overrides everything in progress"), and the ATN-addressing/
// secondary-byte command decode (LISTEN/TALK/UNLISTEN/UNTALK, OPEN/
// CLOSE/PlainSelect, filename collection) -- this is "what a received
// byte MEANS", not bit-level bus mechanics, same split Gemini's design
// used (parseATNCommand/onByteReceived living in the facade, not in
// ReceiverState).
class IecDeviceEmulator
{
public:
	IecDeviceEmulator();

	void setDevice(IIecDevice* device) { device_ = device; }
	IIecDevice* device() const { return device_; }

	// Called whenever the controller's own drive of the bus changes
	// (SDI_8255's I8255::outC, after updating bus_) -- edge-precise,
	// synchronized with the Z80's actual OUT instruction.
	void onControllerChanged(IecBus& bus, uint32_t tstates);

	// Called once per Z80 instruction (SDI_8255::onInstructionFetch) so
	// time-based transitions (EOI timeout, ack release, bit hold times,
	// starting/advancing a talker byte) fire even with no bus change to
	// react to.
	void tick(IecBus& bus, uint32_t tstates);

	// For the IECBUS debug command. Numeric phase values are internal
	// to IecListenerState/IecTalkerState's own SubState enums -- nothing
	// outside this codebase depends on specific magic numbers here.
	int rxPhase() const { return listener_.phase(); }
	int txPhase() const { return talker_.phase(); }
	int role() const { return (int)role_; }
	int bitCount() const { return listener_.bitCount(); }
	uint8_t shiftByte() const { return listener_.shiftByte(); }
	uint8_t lastByte() const { return lastByte_; }
	int secondaryCmd() const { return (int)secondaryCmd_; }
	const std::string& nameBuf() const { return nameBuf_; }
	int activeChannel() const { return activeChannel_; }
	int txBitIndex() const { return talker_.bitIndex(); }
	uint8_t txByte() const { return talker_.currentByte(); }

	enum class IecRole { None, WeAreTalker, WeAreListener };
	enum class SecondaryCmd { None, Open, Close, PlainSelect };

	// --- context surface used by IecListenerState/IecTalkerState ---
	IecRole role_ = IecRole::None;
	SecondaryCmd secondaryCmd_ = SecondaryCmd::None;
	int activeChannel_ = 0;
	std::string nameBuf_;

	// Called by IecListenerState once a full byte has been shifted in,
	// whether under ATN (an addressing/command byte) or plain LISTEN
	// data. Decode logic (what the byte MEANS) lives here, not in the
	// state -- see class comment.
	void onByteReceived(uint8_t byte, bool eoi, bool underAtn);

	// Switches the current state (Idle->Listener on ATN assert, etc).
	// Public so the two states can request a switch themselves (e.g.
	// Listener after UNTALK, Talker on completing turnaround).
	void switchToListener(IecBus& bus);
	void switchToTalker(IecBus& bus);

private:
	// Catches the case where ATN released while the listener was mid
	// AtnPresenceAck (its hold is deliberately left alone to finish on
	// its own schedule, see onControllerChanged) -- once that hold
	// settles into an idle-waiting sub-phase, this notices "ATN is
	// already released and role_ says WeAreTalker" and completes the
	// handoff that the one-shot ATN edge check missed. Called after
	// every dispatch to current_ (both tick() and onControllerChanged())
	// so the handoff happens within one instruction, not stalled
	// forever -- this was a real bug found live-testing the rewrite:
	// without it, listener_ cycles its between-bytes phases
	// indefinitely and the talker never engages.
	void maybeHandoffToTalker(IecBus& bus);

	void onAtnByteReceived(uint8_t byte);
	void onSecondaryByte(uint8_t b);
	void onDataByte(uint8_t b);
	void finalizeSession();

	IIecDevice* device_ = nullptr;

	bool lastAtn_ = false;
	uint32_t lastTstates_ = 0;
	bool haveLastTstates_ = false;

	uint8_t lastByte_ = 0;

	IecListenerState listener_;
	IecTalkerState talker_;
	IecState* current_ = nullptr;
};
