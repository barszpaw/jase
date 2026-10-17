#include "TurboSound.h"
#include <cstdio>

bool TurboSound::in(uint16_t port, uint8_t& value)
{
	if ((port & 0xC002) != 0xC000)
		return false;
	value = chips_[selectedChip_].readRegister(selectedRegister_);
	return true;
}

bool TurboSound::out(uint16_t port, uint8_t value)
{
	if ((port & 0xC002) == 0xC000) {
		// NedoPC way: 0xFF/0xFE written to the SELECT port pick the active
		// chip instead of a register (see TurboSound.h) -- real AY chips
		// only have registers 0-13, so these two values are never
		// ambiguous with an actual register-select write.
		if (value == 0xFF) { selectedChip_ = 0; return true; }
		if (value == 0xFE) { selectedChip_ = 1; return true; }
		selectedRegister_ = value & 0x0F;
		return true;
	}
	if ((port & 0xC002) == 0x8000) {
		chips_[selectedChip_].writeRegister(selectedRegister_, value);
		return true;
	}
	return false;
}

std::string TurboSound::debugCommand(const std::vector<std::string>& toks)
{
	if (toks.empty() || toks[0] != "TURBOSOUND")
		return std::string();

	char out[256];
	snprintf(out, sizeof(out),
	         "OK selectedChip=%d chipA(toneA=%u toneB=%u toneC=%u) chipB(toneA=%u toneB=%u toneC=%u)",
	         selectedChip_,
	         (unsigned)chips_[0].tonePeriod(0), (unsigned)chips_[0].tonePeriod(1), (unsigned)chips_[0].tonePeriod(2),
	         (unsigned)chips_[1].tonePeriod(0), (unsigned)chips_[1].tonePeriod(1), (unsigned)chips_[1].tonePeriod(2));
	return out;
}

void TurboSound::generate(uint8_t* buffer, int count, int sampleRate)
{
	const double clockHz = 1750000.0; // same AY clock as single-chip AyPsg
	const double cyclesPerSample = clockHz / sampleRate;

	for (int i = 0; i < count; i++) {
		clockAccum_ += cyclesPerSample;
		int cycles = (int)clockAccum_;
		clockAccum_ -= cycles;
		for (int c = 0; c < cycles; c++) {
			chips_[0].stepOneClock();
			chips_[1].stepOneClock();
		}

		int mixed = chips_[0].mixedLevel() + chips_[1].mixedLevel(); // 0-90 (two chips, 0-45 each)

		// Same crude DC-removal approach as AyPsg::generate(), rescaled
		// for twice the headroom (0-90 instead of 0-45) so a single chip
		// active alone still sounds the same loudness as plain AyPsg.
		int contribution = (mixed * 20 / 90) - 10; // roughly -10..+10
		int sample = (int)buffer[i] - 128 + contribution;
		if (sample < -127) sample = -127;
		if (sample > 127) sample = 127;
		buffer[i] = (uint8_t)(sample + 128);
	}
}
