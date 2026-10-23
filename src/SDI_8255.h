#pragma once
#include "Peripheral.h"
#include "MemoryMap.h"
#include "I8255.h"
#include <cstdio>

// SDI_8255 -- the real SDI board's hardware-decode layer: the 8255 PPI
// (U10) plus the address-decode/ROM-paging network around it (U8 2764
// EPROM, U3A/U4A/U4B's port decode, U2A/U5A/U5B/U2B/U6x's ROMCS/CE
// decode, Q1), gate-verified against the real schematic (see
// docs/VC1541/VC1541.md's "8255 gate network" section). Deliberately
// does NOT yet know anything about the IEC bus protocol (ATN/CLK/DAT
// semantics) that SDI's firmware drives over Port C -- that's the next
// layer, built on top of this (via chip().outC/inC) once this
// foundation is confirmed solid. Working name until then, chosen so a
// later rename to "SDI" is a one-line change, not a rewrite -- the
// previous single-class SDI tangled the 8255/ROM-decode layer and the
// IEC protocol layer together, which is what led to the still-
// unresolved listenerDat_ timing bug main's VC1541.md documents.
//
// I/O decode: U3A (74LS30 8-input NAND) ANDs A0,A1,A2,A3,A4,A7 -- selected
// when all six are 1, same encoding as the real port addresses $9F/$BF/
// $DF/$FF (A5,A6 free, matching the 8255's own A0,A1 register select).
//
// ROMCS/CE decode (see the real gate network this is built from): a
// NARROW condition -- M1 opcode fetch, in memory space, address bits
// A15,A14=0 (U5A) AND A13-A10=1111 (U2A), i.e. exactly $3C00-$3FFF --
// sets a latch (U6B/U6C, confirmed as a flip-flop via Pawel's own
// schematic-derived truth tables, not inferred). Once set, a BROADER
// condition (U5B: any memory read with A15,A14=0, i.e. anywhere in
// $0000-$3FFF, no A13 check) keeps the EPROM's CE asserted. The latch's
// own RESET condition isn't visible in the schematic excerpt available
// so far -- modelled here as "M1 fetch outside $0000-$3FFF" (the CPU
// has genuinely moved on), the same convention DivIde/Multiface already
// use in this codebase, flagged as an inference, not a confirmed fact.
//
// EPROM mapping: U8 (2764, 8K) has only A0-A11 wired from the Z80 bus --
// A12 comes from Port C bit 1 (PC1) instead, confirmed from the real
// schematic. This means its content repeats every 4K across the WHOLE
// $0000-$3FFF CE-latched window, not just one 4K slice -- modelled here
// by mirroring the active 4K bank into both 8K MemoryMap pages (0 and
// 1) rather than mapping a single 4K window the way the old SDI.cpp
// did. The real SDI103J ROM dump (4096 bytes) is bank 0; bank 1 (PC1=1)
// is undumped -- the SDI manual documents it as free space reserved for
// the user's own code, not alternate firmware, and nothing in the real
// ROM's own traced execution ever toggles PC1 -- modelled as erased-
// EPROM 0xFF until something needs otherwise.
class SDI_8255 : public Peripheral
{
public:
	SDI_8255();

	const char* name() const override { return "SDI8255"; }
	bool in(uint16_t port, uint8_t& value) override;
	bool out(uint16_t port, uint8_t value) override;
	void onInstructionFetch(uint16_t pc, MemoryMap& memory) override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

	// --sdi8255 on the command line -- off by default, same convention
	// as --divide/--multiface1.
	void setEnabled(bool on) { enabled_ = on; }
	bool isEnabled() const { return enabled_; }

	// The real SDI103J ROM dump (exactly 4096 bytes -- bank 0; see class
	// comment on why bank 1 isn't loaded from a file).
	bool loadRom(const char* path);

	// For the next layer (IEC protocol) to wire up chip().outA/outB/
	// outC/inA/inB/inC once it's built on top of this.
	I8255& chip() { return chip_; }

private:
	void pageIn(MemoryMap& memory);
	void pageOut(MemoryMap& memory);
	void onPortCChanged(uint8_t value);
	void rebuildRomWindows();

	I8255 chip_;

	uint8_t eprom_[2][0x1000] = {};
	bool romLoaded_ = false;
	bool romBank1Selected_ = false; // PC1 -- live, even though nothing toggles it yet

	MemoryMap::Bank page0_{}; // mirrors the active 4K bank across $0000-$1FFF
	MemoryMap::Bank page1_{}; // ...and again across $2000-$3FFF

	bool enabled_ = false;
	bool active_ = false; // the real ROMCS/CE latch's current state
};
