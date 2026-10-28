#include "Multiply.h"
#include <cstdio>

namespace {
// multiply_commands_v1.asm: CMD_ZX2SD_REQ_ID EQU 0x01 -- "Ask for
// Multiply Identification".
const uint8_t CMD_REQ_ID = 0x01;

// Placeholder reply payload -- the real firmware's actual identification
// response FORMAT (length, fields) wasn't captured in the sources read
// for this (see docs/Dandanator/dandanator_multiply_notes.md); this
// single byte only proves the TX+RX round trip works end-to-end, it is
// not a faithful reproduction of a real reply.
const uint8_t PLACEHOLDER_ID_BYTE = 0x01;
}

// Called synchronously from Dandanator::executeCommand() the moment a
// command-52 byte arrives -- see DandanatorCmd52Sink.
void Multiply::onDandanatorByte(uint8_t value)
{
	if (!enabled_) return;
	lastByteReceived_ = value;
	if (value == CMD_REQ_ID) {
		replyByte_ = PLACEHOLDER_ID_BYTE;
		rxPhase_ = RxPhase::HighReady;
	}
	// Any other command byte: v1 has nothing further implemented, so it's
	// silently acknowledged-but-ignored, same as Dandanator's own
	// no-op commands.
}

// See class comment for the real 2-edge nibble handshake this
// approximates. `port` is checked the same way Kempston/Multiface check
// it (low byte only, matching real hardware's partial address decode).
bool Multiply::in(uint16_t port, uint8_t& value)
{
	if (!enabled_ || (port & 0xFF) != 0x1F) return false;
	if (rxPhase_ == RxPhase::Idle) return false; // nothing queued -- let Kempston/default answer this port

	// Real port layout (same bit assignment as Kempston's joystick
	// byte): bits 4-1 = 4-bit nibble, bit 0 = the strobe the real
	// firmware's Load4bitBlk edge-detects on (RRA puts bit0 in carry,
	// shifting bits 4-1 down into 3-0).
	bool highPhase = (rxPhase_ == RxPhase::HighReady);
	uint8_t nibble = highPhase ? (replyByte_ >> 4) : (replyByte_ & 0x0F);
	uint8_t strobe = highPhase ? 1 : 0;
	value = (uint8_t)((nibble << 1) | strobe);

	rxPhase_ = highPhase ? RxPhase::LowReady : RxPhase::Idle;
	return true;
}

std::string Multiply::debugCommand(const std::vector<std::string>& toks)
{
	if (toks.empty() || toks[0] != "MULTIPLY")
		return std::string();

	char out[96];
	snprintf(out, sizeof(out), "OK enabled=%d lastByteReceived=%d rxPhase=%d",
	         enabled_ ? 1 : 0, lastByteReceived_, (int)rxPhase_);
	return out;
}
