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
};

#endif