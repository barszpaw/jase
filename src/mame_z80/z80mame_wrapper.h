// Thin wrapper exposing MAME's z80_device (BSD-3-Clause core, see z80.cpp)
// through a small, Z80free-like C API -- same shape fdd3000_station's own
// main.cpp already drives Z80free with, so swapping cores is a drop-in
// change at the call site, not a rewrite of the station glue.
//
// Stepping granularity is NOT "exactly one Z80 instruction" the way
// Z80free's step() is -- MAME's generated core is cycle-accurate
// internally and can legitimately suspend mid-instruction (e.g. on a
// real /WAIT assertion, which this station never does). step() instead
// hands it a small fixed T-state budget (kStepTstates) each call and
// reports how many T-states were actually consumed, which may span a
// partial instruction on one call and finish it on the next. This is a
// deliberate, documented approximation, not a hidden bug: FDD3000's own
// timing model (WD1772's tick(), interrupt sync) was already built
// around "once per station step" granularity, not real T-states, and
// nothing in this station's own code depends on instruction-boundary-
// exact scheduling.
#pragma once
#include <cstdint>

extern "C" {
	void z80mame_reset(void);
	int  z80mame_step(void);        // returns T-states actually consumed this call
	void z80mame_set_irq(int state); // level-triggered, mirrors Z80free_INT/INTserved's use here

	uint16_t z80mame_get_pc(void);
	void     z80mame_set_pc(uint16_t pc);
	uint16_t z80mame_get_sp(void);
	void     z80mame_set_sp(uint16_t sp);
	uint16_t z80mame_get_af(void);
	void     z80mame_set_af(uint16_t af);
	uint16_t z80mame_get_bc(void);
	void     z80mame_set_bc(uint16_t bc);
	uint16_t z80mame_get_de(void);
	void     z80mame_set_de(uint16_t de);
	uint16_t z80mame_get_hl(void);
	void     z80mame_set_hl(uint16_t hl);
	uint16_t z80mame_get_ix(void);
	uint16_t z80mame_get_iy(void);
	uint16_t z80mame_get_af2(void);
	void     z80mame_set_af2(uint16_t v);
	uint16_t z80mame_get_bc2(void);
	void     z80mame_set_bc2(uint16_t v);
	uint16_t z80mame_get_de2(void);
	void     z80mame_set_de2(uint16_t v);
	uint16_t z80mame_get_hl2(void);
	void     z80mame_set_hl2(uint16_t v);
	void     z80mame_set_ix(uint16_t v);
	void     z80mame_set_iy(uint16_t v);
	uint8_t  z80mame_get_i(void);
	uint8_t  z80mame_get_r(void);
	int      z80mame_get_iff1(void);
	int      z80mame_get_iff2(void);
	int      z80mame_get_im(void);
}
