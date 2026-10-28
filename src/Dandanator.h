#pragma once
#include "Peripheral.h"
#include "MemoryMap.h"

// ZX Dandanator! Mini stand-in -- a real, still-sold ROM/RAM cartridge
// (dandare.es, team Dandare/overCLK/mad3001), not a Romantic Robot
// Multiface clone. Facts cross-checked against TWO independent sources,
// in agreement: ZEsarUX 13.0's storage/dandanator.c (GPL, read as a
// HOWTO only -- no code copied, same clean-room policy as Multiface/
// DivIDE) and the real schematic + manual published at dandare.es (the
// Dandanator Mini team's own site explicitly lists ZEsarUX as a
// confirmed-compatible emulator for romsets produced by their own PC/Mac
// tool, which is why ZEsarUX's reverse-engineered protocol was trusted
// as ground truth here). Full research notes: docs/Dandanator/
// dandanator_multiply_notes.md.
//
// Real hardware: a PIC microcontroller watches the Spectrum bus
// (ROMCS48K, /WR, address lines) and a 512KB parallel NOR flash
// (SST39SF040) holds 32 banks of 16KB each. Two tricks make the whole
// thing work:
//
//   1) Bank switching -- which 16KB bank is mapped over 0x0000-0x3FFF
//      (banks 0-31) or whether the REAL internal Spectrum ROM shows
//      through instead (bank 32, the "passthrough" state) -- driven
//      entirely by commands, never by PC or port I/O.
//
//   2) The command channel itself: writes to Z80 address space 0x0000-
//      0x0003 are, on real hardware, a hard no-op wherever genuine ROM
//      sits (you can't bit-bang a NOR flash's write-command sequence
//      just by asserting /WR at an arbitrary read-only address) -- so
//      the Dandanator safely repurposes them as a side-channel command
//      bus, invisible to any software not deliberately using it, which
//      is exactly why this needs Peripheral::onMemoryWrite() (see that
//      method's own comment for why no existing hook could do this):
//      addr 1 = command byte, addr 2/3 = data1/data2, addr 0 = a
//      confirmation pulse. "Normal" 1-byte commands (1-39) are instead
//      confirmed by writing the SAME command byte to addr 1 repeatedly,
//      exactly as many times as the command's own numeric value --
//      self-debouncing against bus noise without needing addr 0 at all.
//      "Special" 3-byte commands (40-49) use all four addresses: cmd,
//      data1, data2, then one confirming pulse to addr 0.
//
// Simplifications vs. real hardware (documented, not hidden, same
// policy as Multiface.h/DivIde.h):
//   - No T-state confirmation delay. Real hardware needs ~35-121
//     T-states between the final pulse and the command actually taking
//     effect (ZEsarUX models this explicitly); here it's immediate.
//     Nothing observed so far depends on that gap, but it's a real gap.
//   - Command 39 ("remember current slot as the post-reset slot") is a
//     no-op, matching ZEsarUX's own admitted gap for this command.
//   - Dan-SNAP (the Multiface-like NMI freeze/snapshot feature) is NOT
//     implemented. It's a romset-level feature layered on top of this
//     same bank-switch mechanism, not core cartridge behaviour -- a
//     reasonable v2.
//   - Command 52's PAYLOAD handling (the Multiply microSD addon's own
//     protocol -- directory browsing, file streaming) is a separate
//     peripheral's job, not this cartridge's -- see DandanatorCmd52Sink
//     below for the one hook this class provides.
//   - Commands 41-43 ("unusable on emulation" per ZEsarUX itself) and
//     the onboard Kempston joystick interface (GAL22V10 on the real
//     board, port 0x1F) are not implemented here.
class Dandanator;

// Command 52 (DANCMD_MULTIPLY) is the real Dandanator PIC firmware's own
// byte-oriented side-channel addressed at the Multiply addon board --
// layered on this cartridge's existing 3-byte special-command transport
// (same addr 0/1/2/3 pulses as commands 40-49, just with cmd=52 and a
// fixed data1=1 marker/DANDAT_MULTIPLY -- confirmed from Multiply's own
// firmware source, see docs/Dandanator/dandanator_multiply_notes.md;
// ZEsarUX's dandanator.c doesn't implement this command at all). Kept as
// a tiny standalone interface (not part of Peripheral itself) since it's
// specific to Dandanator's own protocol, not a generic MinZX-level hook
// -- a Multiply peripheral implements this and registers itself via
// Dandanator::setCmd52Sink(); Dandanator never needs to know Multiply's
// class even exists.
class DandanatorCmd52Sink
{
public:
	virtual ~DandanatorCmd52Sink() {}
	virtual void onDandanatorByte(uint8_t value) = 0;
};

class Dandanator : public Peripheral
{
public:
	static const int NUM_BANKS = 32;                          // 32 x 16K flash banks
	static const int PASSTHROUGH_BANK = 32;                   // "show the real Spectrum ROM" marker
	static const int ROM_SIZE = NUM_BANKS * 2 * MemoryMap::PAGE_SIZE; // 512K romset file size

	Dandanator();

	const char* name() const override { return "DANDANATOR"; }
	void onMemoryWrite(uint16_t addr, uint8_t value, MemoryMap& memory) override;
	void onInstructionFetch(uint16_t pc, MemoryMap& memory, uint32_t tstates) override;
	bool takeResetRequest() override;
	bool takeNMIRequest() override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

	// --dandanator on the command line -- off by default, same
	// convention as --multiface1/--divide.
	void setEnabled(bool on) { enabled_ = on; }
	bool isEnabled() const { return enabled_; }

	// A real 512KB romset dump (same convention as Multiface's optional
	// ROM: copyrighted game content, never bundled -- user supplies
	// their own, produced by the Dandanator PC/Mac tool). Without this,
	// a small built-in placeholder occupies bank 0 only (border flash +
	// RAM marker, same idea as Multiface::initPlaceholderRom()) so the
	// bank-switch/command-channel mechanism is exercisable without any
	// external file.
	bool loadRom(const char* path);

	// The physical right button's RUNTIME behaviour ("always loads the
	// Dandanator menu", per the real manual) -- selects bank 0 and
	// starts accepting commands (Wait_Normal state). NOT needed for the
	// initial activation: the constructor already does this (real
	// hardware auto-engages on power-on, see Dandanator.cpp), so a
	// plain `--dandanator --dandanator-rom X` boots straight into the
	// menu with no button press at all. This is for getting BACK to the
	// menu later (e.g. from a running game) -- real hardware also does
	// a full Z80 reset the instant the button is pressed (it has to --
	// the ROM just changed out from under the running program);
	// main.cpp's key binding calls MinZX::reset() right alongside this,
	// same split as the existing NMI/F10 handling (Peripheral never
	// calls back into MinZX directly except via the generic
	// takeResetRequest()/takeNMIRequest() poll).
	void pressButton();

	// Registers the Multiply addon's command-52 receive hook -- see
	// DandanatorCmd52Sink above. Not owned; pass nullptr to unregister.
	// A real Dandanator doesn't know or care whether a Multiply board is
	// physically attached, and neither does this class: with nothing
	// registered, command 52 is simply a silent no-op, same as any other
	// unrecognized command number.
	void setCmd52Sink(DandanatorCmd52Sink* sink) { cmd52Sink_ = sink; }

private:
	enum State { Disabled, Locked, WaitNormal };

	void applyPaging(MemoryMap& memory);
	void executeCommand();
	void runCommand46();
	void initPlaceholderRom();

	MemoryMap::Bank banks_[NUM_BANKS * 2]{}; // bank b = banks_[2b] (0x0000-0x1FFF) + banks_[2b+1] (0x2000-0x3FFF)

	bool enabled_ = false;
	bool switchedOn_ = false; // true once the button has been pressed at least once
	int activeBank_ = 0;      // 0..31 = flash bank, 32 (PASSTHROUGH_BANK) = real Spectrum ROM
	State state_ = Disabled;

	// Command-channel decode state (addr 1/2/3 writes, see class comment).
	uint8_t pendingCmd_ = 0;
	uint8_t pulseCount_ = 0;
	uint8_t data1_ = 0;
	uint8_t data2_ = 0;

	// Bank-change requests from onMemoryWrite()/pressButton() are NOT
	// applied immediately -- onMemoryWrite() fires mid-instruction (from
	// inside MinZX::poke8(), itself called from inside z80->execute()),
	// which is too late for MinZX's own ROMCS-release edge detection
	// (see minzx.cpp: it only compares isRomcsAsserted() across the
	// onInstructionFetch() call, *before* execute() runs). Deferring to
	// the very next onInstructionFetch() instead -- same one-instruction-
	// later pattern Multiface already uses for its port-triggered page
	// in/out (see Multiface.h's pageInPending_/pageOutPending_ comment)
	// -- lands the actual MemoryMap::mapPage()/setRomcsAsserted() call
	// exactly where MinZX's edge detection can see it.
	bool bankChangePending_ = false;
	int pendingBank_ = 0;

	bool wantsReset_ = false; // command 36, or command 40 data2 bit0
	bool wantsNMI_ = false;   // command 40 data2 bit1

	DandanatorCmd52Sink* cmd52Sink_ = nullptr; // Multiply addon, see setCmd52Sink()
};
