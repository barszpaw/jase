#ifndef _FILEMGR_H_
#define _FILEMGR_H_

class MinZX;

class FileMgr
{
public:
	bool loadSNA(const char* filename, MinZX* targetEmulator);

	// Inverse of loadSNA: writes the current Z80+memory state. Follows the
	// same convention loadSNA expects on the way back in — .SNA doesn't
	// store PC directly, so PC is pushed onto a (temporarily adjusted) SP
	// before writing, and loadSNA's own "pop PC off the stack" on load
	// restores it exactly, along with everything genuinely beneath it on
	// the real stack (the rest of the call chain, unlike a register-only
	// capture — see project memory for why that distinction matters).
	bool saveSNA(const char* filename, MinZX* sourceEmulator);

	// Parses a .TAP file into raw blocks and hands them to targetEmulator
	// for fast-loading (see MinZX::loadTapeBlocks). Doesn't touch Z80
	// state directly — unlike loadSNA, this doesn't "run" anything; the
	// Spectrum still needs BASIC's LOAD "" to actually pull the blocks in.
	bool loadTAP(const char* filename, MinZX* targetEmulator);

	// Same idea as loadTAP but for .TZX: each block carries its own pilot/
	// sync/bit timing (see TapeBlock in minzx.h) instead of always using
	// the standard ROM constants, which is how real turbo loaders work.
	bool loadTZX(const char* filename, MinZX* targetEmulator);

	// .Z80 snapshot (v1/v2/v3, compressed or not) -- facts (header
	// layout, page-number remapping, RLE compression) cross-checked
	// against libspectrum's z80.c (GPL, facts only, no code copied).
	// Covers plain 48K and 128K only -- no Interface 1/+D/Multiface/MGT
	// page data, no Timex, no AY register state (JASE doesn't expose
	// AyPsg's current registers for read-back yet). Refuses to load a
	// snapshot whose machine type doesn't match the emulator's current
	// --machine (can't be switched post-init, see MinZX::setMachine()).
	bool loadZ80(const char* filename, MinZX* targetEmulator);
	bool saveZ80(const char* filename, MinZX* sourceEmulator);
};

#endif