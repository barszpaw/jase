#pragma once
#include "IecBus.h"
#include "IIecDevice.h"
#include <cstdint>
#include <string>

// The "device" side of the IEC bus -- symmetric counterpart to
// SDI_8255/the Z80 controller side, both attached to a shared IecBus.
// Replaces IecProtocolDecoder (see IecBus.h's class comment for why):
// that class tangled "us receiving" and "us sending" into one
// Port-C-byte-reading design, which had no clean place for a real
// talker implementation and needed a periodic-pulse stub instead.
//
// RECEIVE direction (us receiving bytes the controller sends, under
// ATN or in the WeAreListener/SAVE data phase): byte-level algorithm
// and timing (200us CLK-wait before declaring EOI, 80us EOI-ack pulse)
// read directly from fox20io/c64-fdemu's IECBusHandler.cpp,
// receiveIECByte() (GPLv3, not copied -- see IecBridgeClient.h's
// provenance note for the same reasoning). Converted to Z80 T-states
// at JASE's fixed 3.5MHz clock (1us = 3.5 T-states) per Pawel's own
// direction to use the Z80's own clock as the time base.
//
// TRANSMIT direction (us driving CLK+DATA so the controller can
// receive FROM us, when WeAreTalker after a real TALK+OPEN): the real
// SDI ROM has its OWN Z80-side "receive a byte FROM the talker" routine
// at $3138-$319e (mirroring receiveIECByte, just in Z80 asm, roles
// reversed), live-traced against the real ROM (see VC1541.md and
// project memory): it releases its own CLK and spins ($3365-$3372, no
// timeout) waiting for CLK to read asserted -- that IS its "go ahead,
// talker" signal, nothing else needed. Per bit it then waits for a
// CLK-released edge (the sample point, same convention as receive:
// asserted DAT = bit 0, released = bit 1, LSB-first) and shifts the
// result into place. After 8 bits it acks by asserting its own DAT and
// moves on to read the next byte the same way. checkTxTimeouts()'s
// Idle case starts a byte (role_==WeAreTalker && bus CLK released) by
// calling device_->requestRead() for real data -- no more placeholder,
// no more periodic retry of an unaddressed probe (that was a different,
// earlier-traced thing, see VC1541.md's own history of this).
class IecDeviceEmulator
{
public:
	void setDevice(IIecDevice* device) { device_ = device; }

	// Called whenever the controller's own drive of the bus changes
	// (SDI_8255's I8255::outC, after updating bus_) -- edge-precise,
	// synchronized with the Z80's actual OUT instruction.
	void onControllerChanged(IecBus& bus, uint32_t tstates);

	// Called once per Z80 instruction (SDI_8255::onInstructionFetch) so
	// time-based transitions (EOI timeout, ack release, bit hold times,
	// starting/advancing a talker byte) fire even with no bus change to
	// react to.
	void tick(IecBus& bus, uint32_t tstates);

	// For the IECBUS debug command.
	int rxPhase() const { return (int)rxPhase_; }
	int txPhase() const { return (int)txPhase_; }
	int role() const { return (int)role_; }
	int bitCount() const { return bitCount_; }
	uint8_t shiftByte() const { return shiftByte_; }
	uint8_t lastByte() const { return lastByte_; }
	int secondaryCmd() const { return (int)secondaryCmd_; }
	const std::string& nameBuf() const { return nameBuf_; }
	int activeChannel() const { return activeChannel_; }

private:
	enum class RxPhase {
		Idle, AtnPresenceAck, WaitReadyToSend, EoiAckPulse, WaitReadyToSendNoEoi, Receiving, AckHold,
	};
	enum class TxPhase {
		Idle, ClaimBus, BitSetup, BitValid,
	};
	enum class IecRole { None, WeAreTalker, WeAreListener };
	enum class SecondaryCmd { None, Open, Close, PlainSelect };

	void advanceTime(uint32_t tstates);
	void enterRxPhase(RxPhase p);
	void enterTxPhase(TxPhase p);
	void enterAtnPresenceAck(IecBus& bus);
	void checkRxTimeouts(IecBus& bus);
	void checkTxTimeouts(IecBus& bus);
	void onByteReceived(uint8_t byte, bool eoi);
	void onAtnByteReceived(uint8_t byte);
	void onSecondaryByte(uint8_t b);
	void onDataByte(uint8_t b);
	void finalizeSession();

	IIecDevice* device_ = nullptr;

	bool lastAtn_ = false;
	bool lastCtrlClk_ = false;

	uint32_t lastTstates_ = 0;
	bool haveLastTstates_ = false;

	// --- receive state ---
	RxPhase rxPhase_ = RxPhase::Idle;
	uint32_t rxElapsed_ = 0;
	int bitCount_ = 0;
	uint8_t shiftByte_ = 0;
	bool byteEoiPending_ = false;
	bool datWeDrive_ = false; // our ack/ready-for-data drive, true=asserted

	IecRole role_ = IecRole::None;
	uint8_t lastByte_ = 0;

	SecondaryCmd secondaryCmd_ = SecondaryCmd::None;
	int activeChannel_ = 0;
	std::string nameBuf_;

	// --- transmit state (see class comment) ---
	TxPhase txPhase_ = TxPhase::Idle;
	uint32_t txElapsed_ = 0;
	uint8_t txByte_ = 0x00; // current byte being shifted out, from requestRead()
	int txBitIndex_ = 0;
	// requestRead() is a blocking network round-trip -- without this,
	// checkTxTimeouts() would retry it on every single Z80 instruction
	// while idle+WeAreTalker+CLK-released, hammering the bridge and
	// stalling emulation. Reset wherever a fresh read attempt makes
	// sense (new TALK, new secondary/channel).
	bool txAttempted_ = false;
};
