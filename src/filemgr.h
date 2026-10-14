#ifndef _FILEMGR_H_
#define _FILEMGR_H_

class MinZX;

class FileMgr
{
public:
	bool loadSNA(const char* filename, MinZX* targetEmulator);

	// Parses a .TAP file into raw blocks and hands them to targetEmulator
	// for fast-loading (see MinZX::loadTapeBlocks). Doesn't touch Z80
	// state directly — unlike loadSNA, this doesn't "run" anything; the
	// Spectrum still needs BASIC's LOAD "" to actually pull the blocks in.
	bool loadTAP(const char* filename, MinZX* targetEmulator);
};

#endif