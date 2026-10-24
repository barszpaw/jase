#pragma once
#include <cstdint>

class IecBus;
class IecDeviceEmulator;

// Base of the State pattern for the two mutually-exclusive IEC bus
// roles (Listener/Receiver and Talker/Transmitter) -- architecture
// worked out by Pawel with Gemini for a parallel C64-side project (see
// project memory), ported here to replace IecDeviceEmulator's old dual
// checkRxTimeouts()/checkTxTimeouts() design.
//
// Exactly one concrete state is ever "current" in IecDeviceEmulator.
// This is what structurally prevents the RX/TX bus-arbitration bug this
// project hit before (a stale RX timer firing mid-TX-bit and stomping
// the line being held stable): there is no second state's onTick()/
// onEdge() left running to race with -- not a guard condition bolted on
// afterward, but a property of the architecture itself.
class IecState
{
public:
	virtual ~IecState() = default;

	// Called the instant this state becomes current (replaces the old
	// per-call-site enterRxPhase(Idle)/enterTxPhase(Idle) resets).
	virtual void onEnter(IecDeviceEmulator& ctx, IecBus& bus) {}

	// Edge-driven: the controller's own drive of CLK/DAT just changed
	// (an OUT to Port C completed). ATN itself is handled centrally by
	// the facade (IecDeviceEmulator), not here -- ATN overrides whatever
	// a state is doing, which is a cross-cutting concern, not part of
	// either role's own bit machinery.
	virtual void onEdge(IecDeviceEmulator& ctx, IecBus& bus) {}

	// Time-driven: called once per Z80 instruction regardless of edges,
	// so timeout/hold-time transitions (EOI wait, ack hold, per-bit
	// setup/valid) fire even with no bus change to react to.
	virtual void onTick(IecDeviceEmulator& ctx, IecBus& bus, uint32_t deltaTstates) {}
};
