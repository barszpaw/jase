#pragma once
#include "Peripheral.h"
#include "MemoryMap.h"
#include "I8255.h"
#include "IecBus.h"
#include "IecDeviceEmulator.h"
#include "IecBridgeClient.h"
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
// Now owns that IEC protocol layer too: an IecBus (the shared,
// wired-AND bus abstraction -- see its own class comment for why this
// replaced the earlier single-decoder design), an IecDeviceEmulator
// (the "device" side attached to it), and a bridge connection
// (IecBridgeClient, implementing IIecDevice). chip_'s outC/inC drive
// and read the CONTROLLER side of the bus; IecDeviceEmulator drives
// and reads the DEVICE side. This class's own job is just wiring: own
// all three, connect them, expose connectBridge() for main.cpp.
//
// I/O decode: U3A (74LS30 8-input NAND) ANDs A0,A1,A2,A3,A4,A7 -- selected
// when all six are 1, same encoding as the real port addresses $9F/$BF/
// $DF/$FF (A5,A6 free, matching the 8255's own A0,A1 register select).
//
// ROMCS/CE decode: a NARROW condition -- M1 opcode fetch, in memory
// space, address bits A15,A14=0 (U5A) AND A13-A10=1111 (U2A), i.e.
// exactly $3C00-$3FFF -- sets a latch (U6B/U6C, confirmed as a
// flip-flop via Pawel's own schematic-derived truth tables). The
// schematic's own truth tables show the SUSTAIN condition (U5B) as
// broader than just $3000-$3FFF (no A13 check, so technically anywhere
// in $0000-$3FFF) -- a first implementation mirrored the EPROM across
// that whole window accordingly, but that version got stuck well before
// reaching the point the ORIGINAL single-class SDI.cpp reliably reached
// (the on-screen SDI banner). Reverted to SDI.cpp's own, narrower,
// empirically-working model instead: map ONLY page 1 ($2000-$3FFF, a
// single 4K EPROM window at $3000-$3FFF with the $2000-$2FFF half left
// as whatever was already there), release on PC leaving $3000-$3FFF.
// Page 0 ($0000-$1FFF, including the $0038 interrupt vector) is never
// touched -- this is deliberately the more conservative, narrower
// mapping again, pending understanding why the broader one regressed.
//
// EPROM mapping: U8 (2764, 8K) has only A0-A11 wired from the Z80 bus --
// A12 comes from Port C bit 1 (PC1) instead, confirmed from the real
// schematic -- so in principle content should mirror every 4K across
// whatever window is actually latched. With the window narrowed back to
// $3000-$3FFF (see above), this matters less in practice: the real
// ROM's own embedded absolute addresses are all $3xxx anyway. The real
// SDI103J ROM dump (4096 bytes) is bank 0; bank 1 (PC1=1) is undumped --
// the SDI manual documents it as free space reserved for the user's own
// code, not alternate firmware, and nothing in the real ROM's own traced
// execution ever toggles PC1 -- modelled as erased-EPROM 0xFF.
class SDI_8255 : public Peripheral
{
public:
	SDI_8255();

	const char* name() const override { return "SDI8255"; }
	bool in(uint16_t port, uint8_t& value) override;
	bool out(uint16_t port, uint8_t value) override;
	void onInstructionFetch(uint16_t pc, MemoryMap& memory, uint32_t tstates) override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

	// --sdi8255 on the command line -- off by default, same convention
	// as --divide/--multiface1.
	void setEnabled(bool on) { enabled_ = on; }
	bool isEnabled() const { return enabled_; }

	// The real SDI103J ROM dump (exactly 4096 bytes -- bank 0; see class
	// comment on why bank 1 isn't loaded from a file).
	bool loadRom(const char* path);

	// --sdi8255-bridge HOST:PORT -- see IecBridgeClient.
	bool connectBridge(const char* host, int port) { return bridge_.connect(host, port); }

	I8255& chip() { return chip_; }

	// Per-access IEC bus state logger -- what the project's earlier
	// SDI_IOTRACE (on the now-deleted single-class SDI.cpp) did for raw
	// PA/PB/PC/CONF bytes, but logging the RESOLVED IEC bus state
	// (atn/clk/dat per side, role, rxPhase/txPhase) instead -- that
	// register-level view is what you actually need to see WHY a
	// handshake edge got missed, not just that a byte was written.
	// Lost during the i8255-ppi-emulation merge (the old flag called
	// the no-longer-existing SDI class's own method) and rebuilt here,
	// Pawel's own request, alongside --debug-instrtrace.
	void setIectraceEnabled(bool on) { iectraceEnabled_ = on; }
	bool isIectraceEnabled() const { return iectraceEnabled_; }

	// Per-access 8255 register I/O logger (SDI_IOTRACE): logs every IN/OUT
	// to all four 8255 registers (PA/PB/PC/CONF) with the current PC.
	// This is the raw port-access trace (what the register sees), NOT the
	// resolved bus state (what IECTRACE logs). Runtime toggle, no rebuild.
	void setIotraceEnabled(bool on) { iotraceEnabled_ = on; }
	bool isIotraceEnabled() const { return iotraceEnabled_; }

private:
	void pageIn(MemoryMap& memory);
	void pageOut(MemoryMap& memory);
	void onPortCChanged(uint8_t value);
	void rebuildRomWindows();
	void logIectrace(const char* direction, uint8_t value);

	I8255 chip_;
	IecBus bus_;
	IecDeviceEmulator iec_;
	IecBridgeClient bridge_;

	uint8_t eprom_[2][0x1000] = {};
	bool romLoaded_ = false;
	bool romBank1Selected_ = false; // PC1 -- live, even though nothing toggles it yet

	// page1_'s lower half ($2000-$2FFF) is a one-time snapshot of
	// whatever was already mapped there (always the other half of the
	// system ROM pair, always read-only -- see pageIn()); the upper half
	// ($3000-$3FFF) is the active 4K EPROM bank. Page 0 is never mapped.
	MemoryMap::Bank page1_{};

	bool enabled_ = false;
	bool active_ = false; // the real ROMCS/CE latch's current state

	// I8255's outC/inC callbacks have no timing parameter of their own
	// (it's a generic chip, doesn't know about Z80 clocks) -- stashed
	// here from onInstructionFetch so they can hand it to iec_.
	uint32_t lastTstates_ = 0;
	uint16_t lastPc_ = 0; // same stash, for logIectrace()'s own PC column

	bool iectraceEnabled_ = false;
	bool iotraceEnabled_ = false;
};