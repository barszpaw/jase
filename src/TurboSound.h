#pragma once
#include "Peripheral.h"
#include "AyChipCore.h"
#include "IAySound.h"
#include <array>
#include <cstdint>

// TurboSound: two independent AY-3-8912 chips on the same two ports as
// the single-chip AyPsg (0xFFFD select / 0xBFFD data), with chip
// selection done through the "NedoPC way" -- the one mechanism actually
// confirmed live (not dead code) in pico-spec's own Ports.cpp (checked
// on Pawel's request, since he asked whether the port spec was known):
//
//   Writing 0xFF or 0xFE as the "register number" to the SELECT port
//   (0xFFFD) doesn't select a register at all -- real AY chips only have
//   14 registers (0-13), so those two values are never valid register
//   numbers and are safe to repurpose as a side-channel. 0xFF selects
//   chip A as the active chip, 0xFE selects chip B. Every OTHER value
//   written to the SELECT port picks a real register (0-13) on whichever
//   chip is currently active, exactly like plain single-AY AyPsg.
//   Register reads/writes to the DATA port (0xBFFD) always go to the
//   currently active chip.
//
// (pico-spec's Ports.cpp also has a second, address-high-byte-based
// "Old TS way" branch guarded by the same outer port decode -- confirmed
// by exhaustively checking every 16-bit address that it can never
// actually fire under that decode, i.e. it's dead code there. Not
// replicated here; the NedoPC convention above is the one real clones
// use and the one every address this decode accepts can actually reach.)
//
// Two independent AyChipCore engines, mixed together in generate() --
// JASE's audio path is mono (one shared uint8_t* buffer with the
// beeper), so this sums both chips' levels rather than panning them
// left/right the way real hardware's two physical outputs would.
class TurboSound : public Peripheral, public IAySound
{
public:
	TurboSound() {}

	const char* name() const override { return "TURBOSOUND"; }
	bool in(uint16_t port, uint8_t& value) override;
	bool out(uint16_t port, uint8_t value) override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

	void generate(uint8_t* buffer, int count, int sampleRate) override;

private:
	std::array<AyChipCore, 2> chips_;
	int selectedChip_ = 0;
	uint8_t selectedRegister_ = 0;
	double clockAccum_ = 0.0;
};
