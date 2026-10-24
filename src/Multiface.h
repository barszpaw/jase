#pragma once
#include "Peripheral.h"
#include "MemoryMap.h"

// Multiface ONE stand-in -- not a real Multiface (no menu, no snapshot/
// POKE UI; our own tiny clean-room test program unless a real dumped ROM
// is supplied via loadRom(), same reasoning as the AY core: facts cross-
// checked against fusex's peripherals/multiface.c, no code copied from
// it). Off by default (see setEnabled()/--multiface1 in main.cpp) --
// with no card "installed", /NMI still reaches the CPU (that's a generic
// Z80 signal, not something Multiface owns) but onNMI() does nothing,
// and ports 0x9F/0x1F are inert.
//
// Real Multiface ONE hardware pages in 16K over 0x0000-0x3FFF while
// active: an 8K ROM at 0x0000-0x1FFF plus the interface's own 8K RAM at
// 0x2000-0x3FFF (used to freeze/restore the Z80's state and hold the
// menu). Two independent ways to activate it, both real:
//   - The red button -- onNMI() sets pending_; onInstructionFetch() then
//     catches PC==0x0066 the moment it appears (before the opcode there
//     is fetched) and pages in right then. z80cpp's nmi() (see
//     z80cpp/z80.cpp) sets PC=0x0066 at the end of the execute() call
//     that services it, so this is the first real opportunity to
//     intercept -- confirmed this is also exactly how fusex's
//     z80_ops.c does it (CHECK(multiface,...) if (PC==0x0066)
//     multiface_setic8()), same hook position.
//   - Direct port access -- IN A,(0x9F) pages in, IN A,(0x1F) pages out,
//     independent of NMI entirely (see in()).
// Entered via NMI (our own placeholder ROM, no real dumped image)
// additionally pages back out the moment PC leaves the 0x0000-0x3FFF
// window -- belt-and-braces for RETN, since that ROM doesn't bother with
// the IN A,(0x1F) trick (an OUT/port-triggered unpage from the SECOND-
// to-last instruction would race the CPU's own fetch of RETN's bytes).
// Deliberately NOT applied when page-in came from a direct IN A,(0x9F)
// instead: that path is real software calling in from OUTSIDE the
// window and expecting to keep running from wherever it already was
// while the window stays mapped, exactly until it explicitly reads port
// 0x1F -- the window-exit check would immediately undo it on the very
// next instruction otherwise, since PC was never going to enter
// 0x0000-0x3FFF at all.
class Multiface : public Peripheral
{
public:
	Multiface();

	const char* name() const override { return "MULTIFACE1"; }
	bool in(uint16_t port, uint8_t& value) override;
	void onInstructionFetch(uint16_t pc, MemoryMap& memory, uint32_t tstates) override;
	void onNMI() override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

	// --multiface1 on the command line -- off by default, matching
	// fusex's own settings_current.multiface1 default.
	void setEnabled(bool on) { enabled_ = on; }
	bool isEnabled() const { return enabled_; }

	// Optional real Multiface ONE ROM dump (exactly 8192 bytes, same
	// convention as --rom for the system ROM -- copyrighted, never
	// bundled, user supplies their own local file). Without this, the
	// built-in placeholder (border flash + RAM marker) stays active,
	// which proves the paging/NMI mechanism works but has no actual
	// menu. Returns false (placeholder stays active) if the file can't
	// be read or isn't exactly 8192 bytes.
	bool loadRom(const char* path);

private:
	void initPlaceholderRom();
	void pageIn(MemoryMap& memory);
	void pageOut(MemoryMap& memory);

	MemoryMap::Bank rom_{};
	MemoryMap::Bank ram_{};
	bool active_ = false;    // currently has pages 0/1 + ROMCS, see pageIn/pageOut
	bool enabled_ = false;
	bool pending_ = false;    // NMI fired, waiting for PC==0x0066
	bool autoUnpage_ = false; // only armed for the NMI path, see class comment

	// in() has no MemoryMap of its own to page with (Peripheral::in()'s
	// signature is shared by every port-only device, and widening it
	// just for this one ROMCS case would leak that concern onto
	// Kempston/AY/ZiFi too) -- so a port-triggered page in/out is
	// deferred exactly one step, to the very next onInstructionFetch(),
	// which does have a MemoryMap& to act on. Timing-equivalent to doing
	// it synchronously inside in(): no instruction can run in between.
	bool pageInPending_ = false;
	bool pageOutPending_ = false;
};
