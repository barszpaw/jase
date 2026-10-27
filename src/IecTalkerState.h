#pragma once
#include "IecState.h"
#include <cstdint>

// The transmit direction: us driving CLK+DATA so the controller can
// receive FROM us, after a real TALK+OPEN selected us. The real SDI ROM
// has its own Z80-side "receive a byte FROM the talker" routine at
// $3138-$319e (mirroring a standard IEC receiveIECByte, roles reversed):
// it releases its own CLK and spins waiting for CLK to read asserted --
// that's its "go ahead, talker" signal -- then per bit waits for a
// CLK-released edge (the sample point), shifts the bit, waits for the
// re-assert (completing that bit's pair), and after 8 such pairs acks
// by asserting its own DAT.
//
// Implements the real 5-step handshake (Jim Butterfield, "How The
// VIC/64 Serial Bus Works", Compute! July 1983 -- Pawel supplied the
// original doc) PLUS the turnaround's own claim step, confirmed by a
// second, more explicit reference Pawel supplied
// (IEC_pokaz_katalog_dyskietki.md, a step-by-step ATN/CLK/DAT table for
// LOAD"$",8): right after ATN releases, the station must explicitly
// ASSERT its own CLK first ("Przejecie Zegara" -- claiming the clock)
// -- a clean baton-pass while the controller's own CLK is still
// (or about to be) released, so the resolved bus line never visibly
// drops in between -- and only THEN release it as the real Step-1
// "ready to send" signal. This is exactly the original (and later
// removed, as an unjustified guess) "ClaimBus" phase from early in this
// project's history: it WAS a real protocol requirement, just not tied
// to the right justification at the time it was cut.
//
// Idle (fetch the byte)->ClaimClk (assert CLK, the claim)->
// WaitListenerReady (release CLK = Step 1, then wait for the listener's
// own DAT release, Step 2 -- NO fixed time limit on our side)->
// BitSetup/BitValid (Step 3, per bit)->WaitAck (Step 4).
class IecTalkerState : public IecState
{
public:
	void onEnter(IecDeviceEmulator& ctx, IecBus& bus) override;
	void onEdge(IecDeviceEmulator& ctx, IecBus& bus) override;
	void onTick(IecDeviceEmulator& ctx, IecBus& bus, uint32_t deltaTstates) override;

	// For the IECBUS debug command.
	int phase() const { return (int)subState_; }
	int bitIndex() const { return bitIndex_; }
	uint8_t currentByte() const { return currentByte_; }

	// Called by the facade when ATN asserts mid-transmission -- the
	// controller takes priority immediately (Butterfield: "ATN overrides
	// everything in progress"). Releases both lines and resets to Idle
	// so a fresh TALK+OPEN starts clean.
	void abort(IecBus& bus);

	// --- New: filename send path (mirrors real ROM's $30d4 bit-bang sender) ---
	// Called by the facade when we need to queue a byte for transmission
	// (after OPEN secondary byte, or for SAVE payload). Returns true if
	// the byte was accepted into the pipeline, false if the pipeline is full.
	bool queueSendByte(uint8_t byte, bool eoi);

private:
	enum class SubState {
		Idle, ClaimClk, WaitListenerReady, EoiNotify, BitSetup, BitValid, WaitAck,
		// Filename/SAVE send path (one-shot turnaround + streaming):
		Turnaround,   // One-shot: assert CLK (claim bus), then release for Step 1
		SendWaitReady, // Wait for listener to release DAT (ready for data)
		SendBitSetup,  // Per-bit: assert CLK + set DAT
		SendBitValid,  // Per-bit: release CLK (sample point), release DAT
		SendWaitAck,   // Wait for listener to assert DAT (ack)
	};

	void enterSubState(SubState s);
	void checkTransitions(IecDeviceEmulator& ctx, IecBus& bus);
	void beginBits(IecBus& bus);

	// Existing receive path (LOAD encoder) state:
	SubState subState_ = SubState::Idle;
	uint32_t elapsed_ = 0;
	uint8_t currentByte_ = 0x00; // current byte being shifted out, from requestRead()
	int bitIndex_ = 0;
	bool eoi_ = false; // this is the last byte of the stream -- see EoiNotify
	bool byteAttempted_ = false;

	// New: send path state (for filename/SAVE payload)
	struct SendQueue {
		uint8_t byte = 0;
		bool eoi = false;
		bool valid = false;
	};
	SendQueue sendQueue_;
};
