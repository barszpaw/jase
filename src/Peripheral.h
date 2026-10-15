#pragma once
#include <cstdint>
#include <string>
#include <vector>

// Base class for an emulated EXPANSION device — Kempston, and later
// AY-PSG/ZiFi/DivSD/Spectranext/... — Pawel's "every device is its own
// class, controllable from the debug protocol" design (2026-10-15). The
// ULA's own built-in functions (keyboard matrix, border, beeper, EAR —
// all sharing port 0xFE on real hardware, one inseparable chip) are NOT
// peripherals in this sense and stay intrinsic to MinZX itself; this
// abstraction is for pluggable expansion cards/chips only.
class Peripheral
{
public:
	virtual ~Peripheral() {}

	// Layer 3 debug-protocol command name this device answers to (e.g.
	// "KEMPSTON") — see project memory for the naming convention.
	virtual const char* name() const = 0;

	// Return true and fill `value` if this device decodes `port` for IN.
	virtual bool in(uint16_t port, uint8_t& value) { (void)port; (void)value; return false; }

	// Return true if this device decodes `port` for OUT (and acted on it).
	virtual bool out(uint16_t port, uint8_t value) { (void)port; (void)value; return false; }

	// Layer 3 debug command for this device (see project memory for the
	// contract): toks[0] is the command name as received. Return the full
	// "OK ..."/"ERR ..." reply, or an empty string if this device doesn't
	// recognize it — DebugServer tries the next peripheral, falling
	// through to "ERR unknown command" if none claim it.
	virtual std::string debugCommand(const std::vector<std::string>& toks) { (void)toks; return std::string(); }
};
