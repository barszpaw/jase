#include "AyChipCore.h"

void AyChipCore::writeRegister(int reg, uint8_t value)
{
	if (reg < 0 || reg > 13)
		return;
	registers_[reg] = value;

	switch (reg) {
		case 0: case 1:
			channels_[0].tonePeriod = registers_[0] | ((registers_[1] & 0x0F) << 8);
			if (channels_[0].tonePeriod == 0) channels_[0].tonePeriod = 1;
			break;
		case 2: case 3:
			channels_[1].tonePeriod = registers_[2] | ((registers_[3] & 0x0F) << 8);
			if (channels_[1].tonePeriod == 0) channels_[1].tonePeriod = 1;
			break;
		case 4: case 5:
			channels_[2].tonePeriod = registers_[4] | ((registers_[5] & 0x0F) << 8);
			if (channels_[2].tonePeriod == 0) channels_[2].tonePeriod = 1;
			break;
		case 6:
			noisePeriod_ = value & 0x1F;
			if (noisePeriod_ == 0) noisePeriod_ = 1;
			break;
		case 7:
			for (int c = 0; c < 3; c++) {
				channels_[c].toneEnabled = !((value >> c) & 1);
				channels_[c].noiseEnabled = !((value >> (c + 3)) & 1);
			}
			break;
		case 8: case 9: case 10: {
			int c = reg - 8;
			channels_[c].volume = value & 0x0F;
			channels_[c].useEnvelope = (value & 0x10) != 0;
			break;
		}
		case 11: case 12:
			envelopePeriod_ = registers_[11] | (registers_[12] << 8);
			if (envelopePeriod_ == 0) envelopePeriod_ = 1;
			break;
		case 13:
			envelopeShape_ = value & 0x0F;
			envelopeLevel_ = (envelopeShape_ & 0x04) ? 0 : 15; // ATTACK: start low, else start high
			envelopeRampPos_ = 0;
			envelopeFirstTick_ = true;
			envelopeReversed_ = false;
			break;
	}
}

// Envelope shape semantics per the AY-3-8912 datasheet's register 13
// definition: bit3 CONT (continue past one ramp), bit2 ATTACK (ramp up
// instead of down), bit1 ALT (alternate direction each ramp), bit0 HOLD
// (freeze at the end of the first ramp instead of restarting). Four bits
// select one of a small number of distinct waveforms; this directly
// implements that state machine rather than a precomputed table.
void AyChipCore::stepEnvelope()
{
	bool cont   = (envelopeShape_ & 0x08) != 0;
	bool attack = (envelopeShape_ & 0x04) != 0;
	bool alt    = (envelopeShape_ & 0x02) != 0;
	bool hold   = (envelopeShape_ & 0x01) != 0;

	bool advancing = envelopeFirstTick_ || (cont && !hold);
	if (advancing) {
		int step = envelopeReversed_ ? (attack ? -1 : 1) : (attack ? 1 : -1);
		envelopeLevel_ += step;
		if (envelopeLevel_ < 0) envelopeLevel_ = 0;
		if (envelopeLevel_ > 15) envelopeLevel_ = 15;
	}

	if (++envelopeRampPos_ >= 16) {
		envelopeRampPos_ = 0;
		if (!cont) {
			envelopeLevel_ = 0; // one-shot ramps end low and stay there
		} else if (hold) {
			// First ramp completion with ALT set flips to the opposite
			// extreme and holds forever (envelopeFirstTick_ only true once).
			if (envelopeFirstTick_ && alt)
				envelopeLevel_ = envelopeLevel_ ? 0 : 15;
		} else if (alt) {
			envelopeReversed_ = !envelopeReversed_;
		} else {
			envelopeLevel_ = attack ? 0 : 15; // repeating sawtooth: restart
		}
		envelopeFirstTick_ = false;
	}
}

void AyChipCore::stepOneClock()
{
	for (auto& ch : channels_) {
		ch.toneCounter++;
		// Full square-wave period = 16*tonePeriod AY-clock cycles -> toggle
		// every 8*tonePeriod cycles (same divider the noise generator uses).
		if (ch.toneCounter >= (uint32_t)ch.tonePeriod * 8) {
			ch.toneCounter = 0;
			ch.toneOutput = !ch.toneOutput;
		}
	}

	noiseCounter_++;
	if (noiseCounter_ >= (uint32_t)noisePeriod_ * 8) {
		noiseCounter_ = 0;
		// 17-bit LFSR matching the AY's noise polynomial (datasheet fact,
		// same shift/tap structure independently confirmed across every
		// AY emulator checked while researching this).
		bool bit = ((noiseLfsr_ ^ (noiseLfsr_ >> 3)) & 1) != 0;
		noiseLfsr_ = (noiseLfsr_ >> 1) | ((uint32_t)bit << 16);
		noiseOutput_ = (noiseLfsr_ & 1) != 0;
	}

	envelopeCounter_++;
	// Envelope frequency = AY_clock/(256*period) per the datasheet, i.e.
	// 16 AY-clock-ticks per 1/16th-ramp-step * 16 steps per full ramp —
	// the extra *16 vs the tone/noise dividers above is NOT a typo.
	if (envelopeCounter_ >= (uint32_t)envelopePeriod_ * 16) {
		envelopeCounter_ = 0;
		stepEnvelope();
	}
}

int AyChipCore::mixedLevel() const
{
	int mixed = 0;
	for (auto& ch : channels_) {
		// Real AY gate logic: a channel outputs its tone/noise AND
		// combination, or is forced high (silent/DC) if neither tone
		// nor noise is enabled for it.
		if (!ch.toneEnabled && !ch.noiseEnabled)
			continue; // both off: real hardware holds high (silent), contributes nothing
		bool toneGate = !ch.toneEnabled || ch.toneOutput;
		bool noiseGate = !ch.noiseEnabled || noiseOutput_;
		if (toneGate && noiseGate) {
			int vol = ch.useEnvelope ? envelopeLevel_ : ch.volume;
			mixed += vol; // 0-15 per channel, three channels -> up to 45
		}
	}
	return mixed;
}
