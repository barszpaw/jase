#pragma once
#include "Peripheral.h"
#include "Dandanator.h"
#include <vector>
#include <string>
#include <cstdint>

// "Multiply" stand-in -- an Arduino Nano + microSD card addon board for
// the ZX Dandanator! Mini (github.com/mad3001/Multiply, CC BY-SA; real
// KiCad schematic + firmware source read as a HOWTO only, no code
// copied, same clean-room policy as Multiface/DivIDE/Dandanator). Full
// research notes: docs/Dandanator/dandanator_multiply_notes.md. Rides
// entirely on the host Dandanator's own command channel -- it is not a
// card in its own right and cannot exist without one.
//
// Implemented as a TCP bridge client, same architecture as ZiFi.h/.cpp
// (non-blocking socket, TX/RX ring buffers, pump() once per frame) --
// NOT an in-process reimplementation. Reasoning: the real CMD_ZX2SD_*
// command set (SD directory browsing, Z80/SNA/TAP parsing and
// generation, ZX7-compressed listings) is exactly the kind of file-
// format-heavy logic this project already keeps OUT of the hot Z80-
// emulation path and off-loads to a companion Python process instead
// (see tools/sdi_bridge/sdi_bridge.py for SDI's equivalent split, and
// IecBridgeClient.h's own reasoning). This class is deliberately a
// thin byte-pipe: it knows nothing about what CMD_ZX2SD_* commands
// MEAN, only how to get bytes to and from tools/multiply_bridge/multiply_bridge.py.
//
// Two channels, both confirmed from the real firmware source (not
// guessed -- see reference_src/ under the docs path above):
//
//   - Z80 -> Multiply (TX): one byte per Dandanator command 52
//     (DANCMD_MULTIPLY) with data1 fixed at 1 (DANDAT_MULTIPLY) -- see
//     Dandanator::setCmd52Sink()/DandanatorCmd52Sink. onDandanatorByte()
//     pushes straight into txRing_; pump() drains it to the bridge
//     socket, unframed (the real wire protocol has no message
//     delimiters either -- the bridge has to track command/payload
//     boundaries itself, exactly as the real Arduino firmware does).
//
//   - Multiply -> Z80 (RX): a custom 4-bit-per-edge protocol read
//     through IN A,(0x1F) -- the SAME port the Dandanator's own onboard
//     Kempston interface (and a real Multiface ONE) also use, a
//     documented conflict (see Peripheral::in()'s own comment, and the
//     Multiply readme's own compatibility warning about machines with a
//     built-in Kempston). Real firmware's Load4bitBlk busy-polls bit 0
//     of the port until it sees a 0->1 transition (captures the high
//     nibble from bits 4-1 of THAT exact read), then busy-polls again
//     for a 1->0 transition (captures the low nibble) -- a pure
//     asynchronous ready-flag handshake, no shared clock involved at
//     all. This stand-in exploits that directly: since nothing
//     observable depends on how many iterations real software spins
//     before seeing each edge, the edge is simply presented on the very
//     first poll of each phase, sourcing each byte from the front of
//     rxRing_ (filled by pump() from the bridge) rather than modeling
//     the PIC's actual ~9us-per-nibble pacing (Dly4NIBBLEus in the real
//     Arduino firmware) -- same "immediate response" simplification
//     this codebase already uses for other asynchronous ready-flag
//     protocols.
class Multiply : public Peripheral, public DandanatorCmd52Sink
{
public:
	~Multiply() override;

	const char* name() const override { return "MULTIPLY"; }
	bool in(uint16_t port, uint8_t& value) override;
	void onDandanatorByte(uint8_t value) override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

	// --multiply on the command line -- off by default, same convention
	// as every other optional peripheral. Like the real hardware,
	// Multiply only does anything with a Dandanator present: MinZX wires
	// dandanator->setCmd52Sink(this) unconditionally at construction
	// (see minzx.cpp) regardless of this flag -- harmless while
	// disabled, since onDandanatorByte() and in() both ignore everything
	// until setEnabled(true).
	void setEnabled(bool on) { enabled_ = on; }
	bool isEnabled() const { return enabled_; }

	// Connects (non-blocking) to tools/multiply_bridge/multiply_bridge.py or an
	// equivalent companion process. Safe to call even if never
	// configured -- just does nothing elsewhere. Remembers host/port on
	// success, for reconnect().
	bool connectTo(const char* host, int port);

	// Re-dials whatever connectTo() last succeeded with -- lets a
	// running JASE recover from killing/restarting the bridge without
	// restarting JASE itself. False if connectTo() was never called
	// successfully. Also exposed as the debug protocol's "MULTIPLY
	// RECONNECT" (and "MULTIPLY CONNECT host port" to dial a fresh
	// target without needing --multiply-bridge at startup).
	bool reconnect();

	// Call once per emulated frame (50Hz): drains the TX ring to the
	// bridge socket and pulls any available reply bytes into the RX
	// ring. Non-blocking either way, like ZiFi::pump()/DebugServer::poll().
	void pump();

private:
	enum class RxPhase { Idle, HighReady, LowReady };

	bool enabled_ = false;

	int sockFd_ = -1;
	std::vector<uint8_t> rxRing_;
	std::vector<uint8_t> txRing_;
	static const size_t kRingCap = 4096;
	std::string lastHost_;
	int lastPort_ = 0;

	// RX side (us -> Z80): which half of the CURRENT reply byte (popped
	// from the front of rxRing_ the moment serving starts) port 0x1F
	// should currently present -- see class comment.
	RxPhase rxPhase_ = RxPhase::Idle;
	uint8_t replyByte_ = 0;
};
