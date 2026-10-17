#pragma once
#include "Peripheral.h"
#include "AyChipCore.h"
#include "IAySound.h"
#include <cstdint>

// AY-3-8912 PSG — original implementation (no GPL source copied; informed
// only by the public AY-3-8910/8912 datasheet's register map/envelope
// semantics and well-known emulator architecture patterns, independently
// re-derived after reading Fuse/ZEsarUX/pico-spec's own AY code to learn
// what the facts are — see project memory for the licensing discussion
// that led here). JASE stays MIT.
//
// Port decode matches real 128K hardware (the port numbers themselves are
// facts, not protectable expression): SELECT register at 0xFFFD
// ((port & 0xC002) == 0xC000), WRITE DATA at 0xBFFD
// ((port & 0xC002) == 0x8000).
//
// AY clock = Z80 clock / 2 (1.75 MHz on a 3.5 MHz Spectrum) — confirmed by
// checking Fuse's own AY_CLOCK_RATIO constant; NOT the Amstrad CPC's fixed
// 1 MHz AY clock some ported code (e.g. atic_atac_remake's AyChip) assumes.
//
// Single-chip only -- see TurboSound.h for the two-chip variant (same
// AyChipCore engine, a chip-select mux on top instead of this class's
// direct single-register-set decode). MinZX constructs exactly one of
// the two, never both (see MinZX::setTurboSound()).
class AyPsg : public Peripheral, public IAySound
{
public:
	AyPsg() {}

	const char* name() const override { return "AY"; }
	bool in(uint16_t port, uint8_t& value) override;
	bool out(uint16_t port, uint8_t value) override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

	// Mixes `count` samples into `buffer` (AUDIO_U8, centered at 128 — the
	// same format MinZX's beeper already queues), one sample at a time,
	// additively (so the caller can layer this on top of the beeper's own
	// contribution already in the buffer) with clamping to avoid wraparound.
	void generate(uint8_t* buffer, int count, int sampleRate) override;

private:
	AyChipCore chip_;
	uint8_t selectedRegister_ = 0;
	double clockAccum_ = 0.0;
};
