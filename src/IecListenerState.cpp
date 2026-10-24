#include "IecListenerState.h"
#include "IecDeviceEmulator.h"
#include "IecBus.h"

// Timing -- same facts/provenance as before (fox20io/c64-fdemu's
// receiveIECByte(), facts only, clean-room). 1us = 3.5 T-states.
static constexpr uint32_t kEoiWaitTstates = 700;      // 200us
static constexpr uint32_t kEoiAckPulseTstates = 280;  // 80us
static constexpr uint32_t kAckHoldTstates = 245;      // 70us
// Presence ack -- measured directly against the real SDI103J ROM: 3761
// T-states from the ATN OUT ($30a8) to its own presence check ($30e1),
// via a software delay loop ($30b1-$30b9), not a fixed protocol
// constant like the EOI/ack timings above. Held a bit past that
// measured value for margin.
static constexpr uint32_t kAtnAckTstates = 4200;

void IecListenerState::enterSubState(SubState s)
{
	subState_ = s;
	elapsed_ = 0;
}

void IecListenerState::onEnter(IecDeviceEmulator& ctx, IecBus& bus)
{
	(void)ctx;
	(void)bus;
	enterSubState(SubState::Idle);
}

void IecListenerState::enterWaitReadyToSend()
{
	enterSubState(SubState::WaitReadyToSend);
}

// Presence ack (see $30e0-$30e1 in the real ROM): the controller checks
// for DAT asserted after releasing its own, and restarts if nothing
// answers. isConnected() stands in for "is there a drive on the bus at
// all" -- same fallback IIecDevice.h documents (no bridge configured =
// honest "not present"). Re-entered for EVERY byte under ATN, not just
// the first -- the real ROM repeats this exact release-and-check dance
// ($30d8-$30e1) before each byte it sends, not only at ATN's own edge.
void IecListenerState::enterAtnPresenceAck(IecDeviceEmulator& ctx, IecBus& bus)
{
	if (ctx.device() && ctx.device()->isConnected()) {
		datWeDrive_ = true;
		bus.setDeviceDat(true);
		enterSubState(SubState::AtnPresenceAck);
	} else {
		datWeDrive_ = false;
		bus.setDeviceDat(false);
		enterSubState(SubState::Idle);
	}
}

void IecListenerState::onTick(IecDeviceEmulator& ctx, IecBus& bus, uint32_t deltaTstates)
{
	elapsed_ += deltaTstates;

	switch (subState_) {
		case SubState::AtnPresenceAck:
			if (elapsed_ >= kAtnAckTstates) {
				datWeDrive_ = false;
				bus.setDeviceDat(false);
				enterSubState(SubState::WaitReadyToSend);
			}
			break;
		case SubState::WaitReadyToSend:
			if (elapsed_ >= kEoiWaitTstates) {
				byteEoiPending_ = true;
				datWeDrive_ = true;
				bus.setDeviceDat(true);
				enterSubState(SubState::EoiAckPulse);
			}
			break;
		case SubState::EoiAckPulse:
			if (elapsed_ >= kEoiAckPulseTstates) {
				datWeDrive_ = false;
				bus.setDeviceDat(false);
				enterSubState(SubState::WaitReadyToSendNoEoi);
			}
			break;
		case SubState::AckHold:
			if (elapsed_ >= kAckHoldTstates) {
				datWeDrive_ = false;
				bus.setDeviceDat(false);
				// The real byte-sender ($30d4) repeats its own
				// release-and-check dance ($30d8-$30e1) before EVERY
				// byte it sends, under ATN or not -- it's generic, used
				// for addressing bytes and plain data bytes alike.
				// Re-ack rather than silently waiting in WaitReadyToSend
				// for a CLK edge that might already be sitting asserted
				// from the next byte's own early preamble (see $30ab).
				// Only stop once nothing more is expected (role_ reset
				// to None, e.g. after UNLISTEN).
				if (bus.atn() || ctx.role_ == IecDeviceEmulator::IecRole::WeAreListener)
					enterAtnPresenceAck(ctx, bus);
				else
					enterSubState(SubState::WaitReadyToSend);
			}
			break;
		default:
			break; // edge-driven phases, handled in onEdge
	}
}

void IecListenerState::onEdge(IecDeviceEmulator& ctx, IecBus& bus)
{
	bool ctrlClk = bus.controllerClk();
	bool ctrlDat = bus.controllerDat();
	bool clkReleaseEdge = !ctrlClk && lastCtrlClk_;

	switch (subState_) {
		case SubState::WaitReadyToSend:
		case SubState::WaitReadyToSendNoEoi:
			if (ctrlClk) { // controller asserted CLK = ready-to-send
				bitCount_ = 0;
				shiftByte_ = 0;
				enterSubState(SubState::Receiving);
			}
			break;

		case SubState::Receiving:
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
					enterSubState(SubState::AckHold);
					ctx.onByteReceived(byte, eoi, bus.atn());
				}
			}
			break;

		default:
			break; // time-driven phases, handled in onTick
	}

	lastCtrlClk_ = ctrlClk;
}
