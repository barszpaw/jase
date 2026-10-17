#include "AyPsg.h"
#include <cstdio>

bool AyPsg::in(uint16_t port, uint8_t& value)
{
	// SELECT register port (0xFFFD) is the only one that's readable on
	// real hardware; the DATA port (0xBFFD) has no corresponding read.
	if ((port & 0xC002) != 0xC000)
		return false;
	value = chip_.readRegister(selectedRegister_);
	return true;
}

bool AyPsg::out(uint16_t port, uint8_t value)
{
	if ((port & 0xC002) == 0xC000) {
		selectedRegister_ = value & 0x0F;
		return true;
	}
	if ((port & 0xC002) == 0x8000) {
		chip_.writeRegister(selectedRegister_, value);
		return true;
	}
	return false;
}

std::string AyPsg::debugCommand(const std::vector<std::string>& toks)
{
	if (toks.empty() || toks[0] != "AY")
		return std::string();

	char out[256];
	snprintf(out, sizeof(out),
	         "OK toneA=%u toneB=%u toneC=%u noise=%u mixer=%02x volA=%u volB=%u volC=%u envPeriod=%u envShape=%u",
	         (unsigned)chip_.tonePeriod(0), (unsigned)chip_.tonePeriod(1), (unsigned)chip_.tonePeriod(2),
	         (unsigned)chip_.noisePeriodValue(), (unsigned)chip_.mixerReg(),
	         (unsigned)chip_.volume(0), (unsigned)chip_.volume(1), (unsigned)chip_.volume(2),
	         (unsigned)chip_.envelopePeriodValue(), (unsigned)chip_.envelopeShapeValue());
	return out;
}

void AyPsg::generate(uint8_t* buffer, int count, int sampleRate)
{
	// AY clock = Z80 clock / 2 on a Spectrum (1.75 MHz at the standard
	// 3.5 MHz CPU clock) — NOT the Amstrad CPC's fixed 1 MHz AY clock;
	// confirmed against Fuse's own AY_CLOCK_RATIO constant rather than
	// assumed, since a CPC-derived AY implementation would get this wrong.
	const double clockHz = 1750000.0;
	const double cyclesPerSample = clockHz / sampleRate;

	for (int i = 0; i < count; i++) {
		clockAccum_ += cyclesPerSample;
		int cycles = (int)clockAccum_;
		clockAccum_ -= cycles;
		for (int c = 0; c < cycles; c++)
			chip_.stepOneClock();

		int mixed = chip_.mixedLevel(); // 0-45

		// mixed is 0-45 (three channels, 0-15 each). Real AY output has no
		// AC coupling built in here, so scale to a modest range and
		// subtract half-scale as a crude DC-removal approximation (a
		// proper high-pass would do better, but this is adequate for a
		// dev-loop testbed, not a hi-fi reference emulator) before adding
		// to whatever the beeper already wrote into this sample.
		int contribution = (mixed * 20 / 45) - 10; // roughly -10..+10
		int sample = (int)buffer[i] - 128 + contribution;
		if (sample < -127) sample = -127;
		if (sample > 127) sample = 127;
		buffer[i] = (uint8_t)(sample + 128);
	}
}
