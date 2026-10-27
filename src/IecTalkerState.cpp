#include "IecTalkerState.h"
#include "IecDeviceEmulator.h"
#include "IecBus.h"

// Per-bit timing -- Ts/Tv from the real, documented CBM serial-bus
// protocol (Jim Butterfield, "How The VIC/64 Serial Bus Works",
// Compute! July 1983). Ts (bit set-up, talker) typ 70us/max unspecified,
// Tv (data valid hold) min/typ 20us. Held a bit past minimum for
// margin, same as the listener side's own timings. 1us = 3.5 T-states
// at JASE's fixed 3.5MHz Z80 clock.
static constexpr uint32_t kBitSetupTstates = 280; // ~80us, Ts
static constexpr uint32_t kBitValidTstates = 245; // ~70us, Tv
// Claim-CLK hold -- not a documented protocol constant (the reference
// table doesn't give one), just long enough for the controller's own
// bounded ~20-iteration poll (see $3148-3158, ~25 T-states/iteration)
// to definitely observe the assert before we release it again.
static constexpr uint32_t kClaimClkTstates = 280;
// EOI notify hold (Step 2's "do nothing" branch, Butterfield's article
// + the reference table's section 4): withholding the CLK-assert that
// would normally start Step 3 for >200us is what tells the listener
// "this is the last byte" -- it notices the timeout, sends its own
// 60-80us DAT ack pulse, then we proceed with the byte normally. No
// edge-detection of that ack pulse here (same fixed-timer style as the
// listener side's own EoiAckPulse) -- just a hold comfortably past
// 200us + the ack pulse width.
static constexpr uint32_t kEoiNotifyTstates = 1050; // ~300us

// Send path timing (mirrors receive path but as talker driving the bus)
static constexpr uint32_t kSendBitSetupTstates = 280;  // ~80us, Ts
static constexpr uint32_t kSendBitValidTstates = 245;  // ~70us, Tv
static constexpr uint32_t kSendWaitAckTstates = 1050;  // ~300us max wait for ack

void IecTalkerState::enterSubState(SubState s)
{
	subState_ = s;
	elapsed_ = 0;
}

void IecTalkerState::onEnter(IecDeviceEmulator& ctx, IecBus& bus)
{
	(void)ctx;
	(void)bus;
	enterSubState(SubState::Idle);
	byteAttempted_ = false;
	sendQueue_.valid = false;
}

void IecTalkerState::abort(IecBus& bus)
{
	bus.setDeviceClk(false);
	bus.setDeviceDat(false);
	byteAttempted_ = false;
	sendQueue_.valid = false;
	enterSubState(SubState::Idle);
}

bool IecTalkerState::queueSendByte(uint8_t byte, bool eoi)
{
	if (sendQueue_.valid) {
		return false; // pipeline full
	}
	sendQueue_.byte = byte;
	sendQueue_.eoi = eoi;
	sendQueue_.valid = true;
	return true;
}

void IecTalkerState::onEdge(IecDeviceEmulator& ctx, IecBus& bus)
{
	checkTransitions(ctx, bus);
}

void IecTalkerState::onTick(IecDeviceEmulator& ctx, IecBus& bus, uint32_t deltaTstates)
{
	elapsed_ += deltaTstates;
	checkTransitions(ctx, bus);
}

void IecTalkerState::beginBits(IecBus& bus)
{
	bus.setDeviceClk(true); // Step 3 begins
	bitIndex_ = 0;
	bus.setDeviceDat((currentByte_ & 1) == 0); // bit 0, LSB-first
	enterSubState(SubState::BitSetup);
}

void IecTalkerState::checkTransitions(IecDeviceEmulator& ctx, IecBus& bus)
{
	switch (subState_) {
		case SubState::Idle:
			// Fetch the byte up front (IecBridgeClient::requestRead()
			// drains a local pre-fetched buffer, not a network round
			// trip -- see its own class comment -- so doing this once
			// per byte, not every tick, is about correctness/debounce,
			// not performance).
			if (!byteAttempted_) {
				byteAttempted_ = true;
				std::string data;
				bool eoi = false;
				if (ctx.device() && ctx.device()->requestRead(ctx.activeChannel_, 1, data, eoi) && !data.empty()) {
					currentByte_ = (uint8_t)data[0];
					eoi_ = eoi;
					// Claim the clock first ("Przejecie Zegara") -- see
					// class comment. Asserting here, while the
					// controller's own CLK may still be up, is the
					// clean baton-pass; only after this hold do we
					// release for the real Step-1 signal.
					bus.setDeviceClk(true);
					enterSubState(SubState::ClaimClk);
				}
			}
			// Also check if there's a queued send byte (filename/SAVE path)
			else if (sendQueue_.valid) {
				// Transition to turnaround to claim the bus
				bus.setDeviceClk(true);
				enterSubState(SubState::Turnaround);
			}
			break;
		case SubState::ClaimClk:
			if (elapsed_ >= kClaimClkTstates) {
				bus.setDeviceClk(false); // Step 1: ready-to-send
				enterSubState(SubState::WaitListenerReady);
			}
			break;
		case SubState::WaitListenerReady:
			// Step 2 ("ready for data"): wait for the listener to
			// release its DAT line. No fixed time limit on our side.
			// For the LAST byte of the stream, this is also where EOI
			// gets signalled: instead of immediately starting Step 3,
			// withhold the CLK-assert for a while (EoiNotify) -- see its
			// own comment.
			if (!bus.controllerDat()) {
				if (eoi_)
					enterSubState(SubState::EoiNotify);
				else
					beginBits(bus);
			}
			break;
		case SubState::EoiNotify:
			if (elapsed_ >= kEoiNotifyTstates)
				beginBits(bus);
			break;
		case SubState::BitSetup:
			if (elapsed_ >= kBitSetupTstates) {
				bus.setDeviceClk(false); // release -> "data ready", controller samples
				enterSubState(SubState::BitValid);
			}
			break;
		case SubState::BitValid:
			if (elapsed_ >= kBitValidTstates) {
				bitIndex_++;
				if (bitIndex_ >= 8) {
					// Step 4 ("frame handshake") starts with CLK
					// asserted, DATA released -- force DAT released here
					// regardless of what bit 7's value happened to be.
					bus.setDeviceDat(false);
					bus.setDeviceClk(true);
					enterSubState(SubState::WaitAck);
				} else {
					currentByte_ >>= 1; // LSB-first, same convention as receive
					bus.setDeviceClk(true);
					bus.setDeviceDat((currentByte_ & 1) == 0);
					enterSubState(SubState::BitSetup);
				}
			}
			break;
		case SubState::WaitAck:
			// Step 4: the listener acknowledges the byte by pulling its
			// own DAT true. Once seen, release our CLK and go back to
			// Idle -- the next byte's WaitListenerReady correctly keeps
			// waiting until the listener releases this same ack (its
			// own Step 5, "back where we started"), so no extra state
			// is needed here for that.
			if (bus.controllerDat()) {
				bus.setDeviceClk(false);
				byteAttempted_ = false; // byte done -- let the next one be requested
				enterSubState(SubState::Idle);
			}
			break;

		// --- Filename/SAVE send path (mirrors real ROM's $30d4) ---
		case SubState::Turnaround:
			// One-shot: assert CLK (claim bus), then release for Step 1
			// The real ROM's $30d4 does: release DAT, check listener DAT,
			// release CLK, then per-bit loop. Here we just need to
			// complete the claim-turnaround.
			if (elapsed_ >= kClaimClkTstates) {
				bus.setDeviceClk(false); // Step 1: ready-to-send
				// Now wait for listener to release DAT (ready for data)
				enterSubState(SubState::SendWaitReady);
			}
			break;
		case SubState::SendWaitReady:
			// Wait for listener to release DAT (ready for data)
			// For EOI, withhold CLK for >200us (same as receive path)
			if (!bus.controllerDat()) {
				if (sendQueue_.eoi)
					enterSubState(SubState::EoiNotify);
				else
					enterSubState(SubState::SendBitSetup);
			}
			break;
		case SubState::SendBitSetup:
			if (elapsed_ >= kSendBitSetupTstates) {
				// Assert CLK and set DAT to current bit
				bus.setDeviceClk(true);
				bus.setDeviceDat((sendQueue_.byte & 1) == 0); // LSB-first, asserted DAT = bit 0
				enterSubState(SubState::SendBitValid);
			}
			break;
		case SubState::SendBitValid:
			if (elapsed_ >= kSendBitValidTstates) {
				// Release CLK (sample point), release DAT
				bus.setDeviceClk(false);
				bus.setDeviceDat(false);
				bitIndex_++;
				if (bitIndex_ >= 8) {
					// Byte done, wait for ack
					bus.setDeviceClk(true); // assert CLK for ack phase
					enterSubState(SubState::SendWaitAck);
				} else {
					// Next bit
					sendQueue_.byte >>= 1;
					enterSubState(SubState::SendBitSetup);
				}
			}
			break;
		case SubState::SendWaitAck:
			// Wait for listener to assert DAT (ack)
			if (bus.controllerDat()) {
				bus.setDeviceClk(false);
				sendQueue_.valid = false; // byte done
				bitIndex_ = 0;
				enterSubState(SubState::Idle);
			} else if (elapsed_ >= kSendWaitAckTstates) {
				// Timeout - give up on this byte
				bus.setDeviceClk(false);
				sendQueue_.valid = false;
				bitIndex_ = 0;
				enterSubState(SubState::Idle);
			}
			break;
	}
}