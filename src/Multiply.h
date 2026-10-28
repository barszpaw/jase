#pragma once
#include "Peripheral.h"
#include "Dandanator.h"

// "Multiply" stand-in -- an Arduino Nano + microSD card addon board for
// the ZX Dandanator! Mini (github.com/mad3001/Multiply, CC BY-SA; real
// KiCad schematic + firmware source read as a HOWTO only, no code
// copied, same clean-room policy as Multiface/DivIDE/Dandanator). Full
// research notes: docs/Dandanator/dandanator_multiply_notes.md. Rides
// entirely on the host Dandanator's own command channel -- it is not a
// card in its own right and cannot exist without one.
//
// Two channels, both confirmed from the real firmware source (not
// guessed -- see reference_src/ under the docs path above):
//
//   - Z80 -> Multiply (TX): one byte per Dandanator command 52
//     (DANCMD_MULTIPLY, data1 fixed at 1/DANDAT_MULTIPLY) -- see
//     Dandanator::setCmd52Sink()/DandanatorCmd52Sink. On real hardware
//     the Dandanator's own PIC forwards this out over a genuine UART
//     wired to the Arduino (confirmed: the Arduino side reads it via
//     plain Serial.read(), never bit-bangs on receive); here it arrives
//     directly as a synchronous C++ call instead.
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
//     first poll of each phase rather than modeling the PIC's actual
//     ~9us-per-nibble pacing (Dly4NIBBLEus in the real Arduino
//     firmware) -- same "immediate response" simplification this
//     codebase already uses for other asynchronous ready-flag protocols.
//
// v1 scope -- a foundation proving the full TX+RX round trip works, NOT
// the real file browser: recognizes only CMD_ZX2SD_REQ_ID (0x01) and
// replies with one fixed identification byte. The real CMD_ZX2SD_*
// command set (SD directory browsing, Z80/SNA/TAP/romset streaming) is
// NOT implemented -- that's a virtual-filesystem-sized follow-up, not a
// one-line gap, and deliberately left for later.
class Multiply : public Peripheral, public DandanatorCmd52Sink
{
public:
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

private:
	enum class RxPhase { Idle, HighReady, LowReady };

	bool enabled_ = false;

	// TX side (Z80 -> us): a real command is [cmd byte][args...], one
	// byte per onDandanatorByte() call -- v1 only ever looks at the
	// first byte of any sequence.
	uint8_t lastByteReceived_ = 0;

	// RX side (us -> Z80): the single queued reply byte and which half
	// of it port 0x1F should currently present -- see class comment.
	RxPhase rxPhase_ = RxPhase::Idle;
	uint8_t replyByte_ = 0;
};
