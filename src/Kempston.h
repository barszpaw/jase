#pragma once
#include "Peripheral.h"

// Kempston joystick interface, port 0x1F. Arrow keys for direction, left
// Alt for fire — standard bit layout: 0=right,1=left,2=down,3=up,4=fire
// (active high). Bit layout ported from pico-spec's Ports.cpp/
// ESPectrum.cpp, not guessed — see project memory.
class Kempston : public Peripheral
{
public:
	const char* name() const override { return "KEMPSTON"; }
	bool in(uint16_t port, uint8_t& value) override;
	std::string debugCommand(const std::vector<std::string>& toks) override;
};
