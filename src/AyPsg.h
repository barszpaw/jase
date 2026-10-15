#pragma once
#include "Peripheral.h"
#include <array>
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
class AyPsg : public Peripheral
{
public:
	AyPsg();

	const char* name() const override { return "AY"; }
	bool in(uint16_t port, uint8_t& value) override;
	bool out(uint16_t port, uint8_t value) override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

	// Mixes `count` samples into `buffer` (AUDIO_U8, centered at 128 — the
	// same format MinZX's beeper already queues), one sample at a time,
	// additively (so the caller can layer this on top of the beeper's own
	// contribution already in the buffer) with clamping to avoid wraparound.
	void generate(uint8_t* buffer, int count, int sampleRate);

private:
	void writeRegister(int reg, uint8_t value);
	void stepOneClock();
	void stepEnvelope();

	uint8_t selectedRegister_ = 0;
	uint8_t registers_[14] = {};

	struct Channel {
		uint16_t tonePeriod = 1;
		uint32_t toneCounter = 0;
		bool toneOutput = false;
		uint8_t volume = 0;
		bool useEnvelope = false;
		bool toneEnabled = false;
		bool noiseEnabled = false;
	};
	std::array<Channel, 3> channels_;

	uint16_t noisePeriod_ = 1;
	uint32_t noiseCounter_ = 0;
	uint32_t noiseLfsr_ = 1;
	bool noiseOutput_ = false;

	// Envelope: bitmask semantics straight off register 13's datasheet
	// definition (bit3 CONT, bit2 ATTACK, bit1 ALT, bit0 HOLD), not a
	// precomputed lookup table.
	uint16_t envelopePeriod_ = 1;
	uint32_t envelopeCounter_ = 0;
	uint8_t envelopeShape_ = 0;
	int envelopeLevel_ = 0;     // 0-15, current envelope amplitude
	int envelopeRampPos_ = 0;   // 0-15, position within the current ramp (distinct from envelopeLevel_ itself)
	bool envelopeFirstTick_ = true;
	bool envelopeReversed_ = false;

	double clockAccum_ = 0.0;
};
