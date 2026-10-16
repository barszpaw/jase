#pragma once
#include "Peripheral.h"
#include "MemoryMap.h"
#include <cstdio>

// DivIDE stand-in -- deliberately NOT a full ATA/IDE implementation, only
// the handful of commands esxDOS's own driver actually issues (facts
// cross-checked independently in fusex's peripherals/ide/{divide,divxxx}.c
// + 3rdparty/libspectrum/ide.c, pico-spec-drewpo's DivMMC.cpp IDE command
// set, and ZEsarUX's storage/ide.c -- all three agree on the same command
// codes, so these aren't guesses). No code copied from any of them (all
// three are GPL) -- this is a clean-room reimplementation of the facts.
//
// Real DivIDE pages 16K over 0x0000-0x3FFF while active: an 8K EPROM at
// 0x0000-0x1FFF (esxDOS itself) plus one of 4 banked 8K RAM pages at
// 0x2000-0x3FFF (selected by the control register's bottom 2 bits).
// Paging is driven entirely by PC, not NMI -- confirmed in fusex's
// z80_ops.c: page in on PC == 0x0000/0x0008/0x0038/0x0066/0x04C6/0x0562
// (RST vectors, the IM1 interrupt vector, NMI vector, and two specific
// 48K ROM tape entry points DivIDE intercepts for its own fast-load),
// page out on PC in 0x1FF8-0x1FFF. The control register's CONMEM bit
// (0x80) forces paging in unconditionally, overriding all of that --
// used by esxDOS itself while it has control, so software doesn't need
// to keep landing on a trap address to stay mapped in.
//
// Simplifications vs real hardware (documented, not hidden): no CHS
// addressing (esxDOS only ever uses LBA28, so that's all this supports),
// no MAPRAM-sticky/write-protect interaction (real DivIDE's automap is
// gated by those too; here automap just always works, which covers the
// overwhelming common case), single drive, no master/slave, no ATAPI.
class DivIde : public Peripheral
{
public:
	DivIde();
	~DivIde() override;

	const char* name() const override { return "DIVIDE"; }
	bool in(uint16_t port, uint8_t& value) override;
	bool out(uint16_t port, uint8_t value) override;
	void onInstructionFetch(uint16_t pc, MemoryMap& memory) override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

	// --divide on the command line -- off by default, same convention as
	// --multiface1.
	void setEnabled(bool on) { enabled_ = on; }
	bool isEnabled() const { return enabled_; }

	// esxDOS ROM for the IDE variant (ESXIDE.BIN from the esxdos release
	// zip, exactly 8192 bytes) -- copyrighted-adjacent like the system
	// ROMs even though esxDOS itself is open source, so still bring your
	// own file rather than bundling one.
	bool loadRom(const char* path);

	// The "SD card"/hard disk backing store. Accepts either a raw sector
	// image (.img/.raw -- no header, byte 0 is sector 0) or a standard
	// .hdf ("RS-IDE" signature + header, the format fusex/ZEsarUX/
	// pico-spec all produce) -- sniffed automatically from the first
	// bytes, so either of Pawel's existing esxdos.raw / esxdos.hdf just
	// works without saying which.
	bool insertDisk(const char* path);

private:
	void refreshPaging(MemoryMap& memory);
	void executeCommand(uint8_t cmd);
	uint32_t lba() const;

	MemoryMap::Bank rom_{};
	MemoryMap::Bank ram_[4]{};
	MemoryMap::Bank* savedPage0_ = nullptr;
	MemoryMap::Bank* savedPage1_ = nullptr;
	bool enabled_ = false;

	// Control register (port 0xE3).
	bool conmem_ = false;    // bit7: force page-in regardless of PC
	uint8_t ramBank_ = 0;    // bits0-1: which of ram_[4] sits at 0x2000

	// PC-trap automap state. The transition is deferred by exactly one
	// onInstructionFetch() call -- confirmed against pico-spec-drewpo's
	// DivMMC::preOpcFetch()/postOpcFetch() (same facts, no code copied,
	// GPL): a trap address's own opcode is fetched using the mapping
	// from BEFORE the trap, and the page change only takes effect
	// starting with the NEXT instruction. This matters concretely for
	// the exit trick (JP to 0x1FF8-0x1FFF): esxDOS's own ROM has "JP
	// (HL)" there (confirmed: byte 0xE9 at offset 0x1FFB in ESXIDE.BIN),
	// executed WHILE DivIDE is still mapped, landing exactly where
	// esxDOS intends via HL -- paging out one instruction too early (as
	// an earlier version of this code did) instead executes the REAL
	// system ROM's own unrelated byte at that address, which is simply
	// wrong, not an equivalent shortcut.
	bool automapActive_ = false;
	bool trapAfter_ = false;
	bool unmapAfter_ = false;

	// ATA command-block registers -- just enough of the real register
	// file for esxDOS's own driver (see class comment for what's NOT
	// modelled: CHS, multi-drive, ATAPI).
	uint8_t errorFeature_ = 0;
	uint8_t sectorCount_ = 0;
	uint8_t sectorReg_ = 0;
	uint8_t cylLow_ = 0;
	uint8_t cylHigh_ = 0;
	uint8_t driveHead_ = 0;
	uint8_t status_ = 0x40; // RDY

	uint8_t dataBuf_[512] = {};
	size_t dataPos_ = 0;
	bool writePending_ = false; // command 0x30/0x31 in progress

	FILE* disk_ = nullptr;
	long diskDataOffset_ = 0;   // 0 for a raw image, header size for .hdf
	uint32_t diskSectors_ = 0;  // total 512-byte sectors, for IDENTIFY
};
