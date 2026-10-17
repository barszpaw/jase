#pragma once
#include <array>
#include <cstdint>

// One AY-3-8912 PSG core: register state + tone/noise/envelope generation
// only -- no port decoding, no Peripheral interface. Extracted out of the
// original single-chip AyPsg so both AyPsg (one chip) and TurboSound (two
// chips + a chip-select mux) can share the exact same engine instead of
// two near-duplicate copies.
//
// Facts (register map, envelope semantics, AY clock) are the same ones
// AyPsg.h already documents -- see that file's header comment for the
// licensing note (clean-room, informed by the public datasheet).
class AyChipCore
{
public:
	AyChipCore() { registers_.fill(0); }

	void writeRegister(int reg, uint8_t value);
	uint8_t readRegister(int reg) const { return (reg >= 0 && reg < 14) ? registers_[reg] : 0xFF; }

	void stepOneClock();

	// 0-45: sum of the three channels' gated volume/envelope level (0-15
	// each). Real AY output has no AC coupling built in -- DC removal and
	// scaling to an audio sample range is the caller's job (see AyPsg::
	// generate()/TurboSound::generate(), which also combine this with
	// whatever the beeper already wrote into the shared buffer).
	int mixedLevel() const;

	// Debug-command introspection (AyPsg::debugCommand()/TurboSound's own).
	uint16_t tonePeriod(int ch) const { return channels_[ch].tonePeriod; }
	uint16_t noisePeriodValue() const { return noisePeriod_; }
	uint8_t mixerReg() const { return registers_[7]; }
	uint8_t volume(int ch) const { return channels_[ch].volume; }
	uint16_t envelopePeriodValue() const { return envelopePeriod_; }
	uint8_t envelopeShapeValue() const { return envelopeShape_; }

private:
	void stepEnvelope();

	std::array<uint8_t, 14> registers_;

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
};
