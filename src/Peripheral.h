#pragma once
#include <cstdint>
#include <string>
#include <vector>

class MemoryMap;

// Base class for an emulated EXPANSION device — Kempston, AY-PSG, ZiFi,
// Multiface, and later DivMMC/Spectranext/... — Pawel's "every device is
// its own class, controllable from the debug protocol, and as
// independent from the rest as possible" design (2026-10-15/16). The
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
	// NOTE: on real hardware, two devices can genuinely share a port
	// (Kempston and Multiface ONE both answer 0x1F — a documented
	// conflict, not a bug) — MinZX calls every peripheral's in() for a
	// given port regardless of whether an earlier one already claimed
	// it, so a device wanting a side effect (Multiface's page-out) still
	// gets to run even when it isn't the one supplying the returned
	// byte (see processInputPort()). Only the FIRST peripheral to return
	// true determines the actual value.
	virtual bool in(uint16_t port, uint8_t& value) { (void)port; (void)value; return false; }

	// Return true if this device decodes `port` for OUT (and acted on it).
	virtual bool out(uint16_t port, uint8_t value) { (void)port; (void)value; return false; }

	// Layer 3 debug command for this device (see project memory for the
	// contract): toks[0] is the command name as received. Return the full
	// "OK ..."/"ERR ..." reply, or an empty string if this device doesn't
	// recognize it — DebugServer tries the next peripheral, falling
	// through to "ERR unknown command" if none claim it.
	virtual std::string debugCommand(const std::vector<std::string>& toks) { (void)toks; return std::string(); }

	// ROMCS-style devices (Multiface, and later DivMMC/IF1/...) page
	// their own ROM/RAM in over the Z80 address space based on PC and/or
	// NMI, not just port I/O. These two hooks are how they do that
	// without MinZX knowing anything device-specific; default no-ops, so
	// plain port-mapped peripherals (Kempston, AY, ZiFi) never need to
	// touch either.

	// Called once before every instruction fetch, with the PC about to
	// execute — same hook position/timing MinZX's own tape engine and
	// 128K paging already use internally, now available to any
	// peripheral. `memory` is handed over each call rather than stored,
	// so the device only ever needs this one type from MinZX's internals,
	// never a standing reference to MinZX itself.
	virtual void onInstructionFetch(uint16_t pc, MemoryMap& memory) { (void)pc; (void)memory; }

	// Called when the machine's /NMI line is pulled (see MinZX::
	// triggerNMI()). The generic Z80 signal always fires on the CPU
	// regardless of any peripheral; this is just notice to whichever
	// device defines what /NMI *means* to it (Multiface's "red button"
	// semantics live entirely here, not in MinZX).
	virtual void onNMI() {}
};
