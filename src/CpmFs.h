#pragma once
#include "DskImage.h"
#include <cstdint>
#include <string>
#include <vector>

// Read/write access to the CP/M 2.2 filesystem living inside a DskImage --
// built directly on DskImage's own sector-level API (findSector()), not a
// second, independent disk-image parser.
//
// 2026-11-21 CORRECTION, found the hard way: an earlier version of this
// file assumed a skew=7 FORMULA-computed translate table and dirEntries=128
// (from the "whole directory track" eyeball guess below), and "confirmed"
// it only by checking that list()/readFile() reproduced real `DIR` output
// correctly -- which turned out to be a WORTHLESS check, since visiting
// all 16 physical sectors of the directory track in ANY order (any
// permutation at all) finds the same set of entries; it can never expose a
// wrong permutation. The actual bug surfaced when a new directory entry
// written by writeFile() (HELLO.TXT) was byte-perfect and placed at a
// "logical" slot CpmFs believed was early/valid, yet real CP/M's `DIR`/
// `TYPE` never saw it. Root-caused by pausing the real, running station
// CPU (fdd3000_station's new STEP-pauses-the-main-loop mechanism, added
// for exactly this) and dumping its live RAM: the real DPB at that moment
// (BSH=3,BLM=7,EXM=0,DSM=151,DRM=63,AL0=0xF0,AL1=0,CKS=16,OFF=2) sits right
// next to a 2-byte pointer (0xfc6b) to the REAL translate table -- a
// literal 16-byte permutation of 1..16 in RAM, found by searching for
// exactly that signature. It is NOT the skew=7 formula's output:
//   real:   [0,5,10,15,4,9,14,3,8,13,2,7,12,1,6,11]  (0-based)
//   wrong:  [0,7,14,5,12,3,10,1,8,15,6,13,4,11,2,9]  (the old skew=7 guess)
// They agree at logical index 0 (trivial for any permutation) and
// COINCIDENTALLY at a couple of others, which is exactly why reading
// pre-existing files never exposed the mismatch -- those files all
// happened to live within the real table's valid range anyway.
// DRM=63 is the other half of the bug: only 64 directory entries (logical
// sectors 0-7) are real/scanned, not 128 -- AL0=0xF0/AL1=0x00 reserve a
// full 4 blocks (16 sectors) of physical space for the directory, but only
// the first 2 of those blocks are ever actually used as entries; the rest
// is reserved-but-dead space, kept out of both the directory scan AND
// file allocation. HELLO.TXT's old wrong-skew write landed on physical
// sector 7, which the REAL table maps to logical entry 88 -- past DRM=63,
// so BDOS never requested that sector as directory space at all. Hence:
// byte-perfect entry, completely invisible, not a display bug, not a
// MonitorLink artifact (reconfirmed after that separate bug was fixed).
//
// Geometry facts that WERE already correct (now doubly confirmed by the
// same live DPB dump, e.g. BSH/BLM/EXM matching BLS=1024, OFF matching
// reservedTracks=2):
//   - 16 sectors/track, 256 bytes/sector.
//   - 2 reserved tracks.
//   - Block size (BLS) = 1024 bytes = 4 physical sectors/block.
//
// NOT independently confirmed: the 340KB (40 tracks x 2 sides) and 620KB
// (80 tracks x 2 sides) FDD3000 CP/M densities -- no real sample of either
// was available to check against, and given the above, do NOT assume this
// disk's translate table or DRM generalizes to those densities either.
// writeFile() refuses them outright (see Dpb.blocks16bit).
class CpmFs
{
public:
	struct DirEntry {
		std::string name;   // "FILENAME.EXT", normalized uppercase, attribute bits stripped
		size_t sizeBytes;    // exact byte count (records * 128), no trailing-0x1A trimming
		int records;         // total 128-byte CP/M records across all extents
		bool readOnly;        // T1 attribute bit (ext byte 0, high bit)
		bool system;          // T2 attribute bit (ext byte 1, high bit)
	};

	// Binds to an already-loaded DskImage and derives block-size/directory
	// parameters from its own geometry (numTracks()/numSides()) -- see
	// class comment on which densities are confirmed vs. best-effort.
	bool open(DskImage& dsk);

	std::vector<DirEntry> list() const;

	// Exact byte-for-byte extraction (no CP/M text-mode 0x1A trimming --
	// that's a convention PIP applies, not a property of the file itself).
	bool readFile(const std::string& name, std::vector<uint8_t>& out) const;

	// Allocates free blocks, writes however many directory extents the
	// file needs (one per 16 blocks = 16KB with 8-bit block pointers, half
	// that once a disk's block count pushes pointers to 16-bit), then
	// calls DskImage::flush(). Fails (no partial write left in the
	// directory) if the name already exists, is malformed, or there isn't
	// enough free space.
	bool writeFile(const std::string& name, const std::vector<uint8_t>& data);

	// Marks every extent's user byte as 0xE5 (CP/M's own "deleted" marker)
	// and flushes. Does NOT zero the freed blocks' data -- same as real
	// CP/M, which only ever marks directory entries, never scrubs data.
	bool deleteFile(const std::string& name);

private:
	struct Dpb {
		int reservedTracks = 2;
		int sectorsPerTrack = 16;
		int sectorSize = 256;
		int blockSize = 1024;
		int dirEntries = 64;      // REAL DRM=63 (see class comment) -- NOT 128
		int dirReservedBlocks = 4; // REAL AL0=0xF0/AL1=0x00 -- NOT derived from dirEntries
		int totalBlocks = 0;      // derived from disk geometry minus reserved tracks
		bool blocks16bit = false; // true once totalBlocks > 256 (standard CP/M 2.2 rule)
		int sectorsPerBlock = 0;  // blockSize / sectorSize
	};

	DskImage* dsk_ = nullptr;
	Dpb dpb_;

	// Logical (deskewed) sector index -> physical track/sector-id, across
	// the WHOLE disk (track 0 = reserved, not part of the logical data
	// space at all -- logical sector 0 is reserved-tracks-end).
	uint8_t* logicalSector(int logicalIndex, int& len) const;

	// Raw 32-byte directory entries, in on-disk order, INCLUDING 0xE5
	// (deleted) ones -- callers filter.
	std::vector<std::vector<uint8_t>> readRawDirectory() const;
	bool writeRawDirectoryEntry(int entryIndex, const uint8_t bytes[32]);

	// Every block number currently referenced by any non-deleted
	// directory entry -- used both to list "free" blocks for writeFile()
	// and to decide blocks16bit's actual pointer width when reading.
	std::vector<int> usedBlocks(const std::vector<std::vector<uint8_t>>& rawDir) const;

	static std::string normalizeName(const std::string& name);
	static std::string decodeEntryName(const uint8_t entry[32]);
	// True if entry's name+ext (attribute bits masked off) equals the
	// already-normalized (11-byte, no dot) form normalizeName() produces.
	static bool matchesName(const uint8_t entry[32], const std::string& normalized11);
};
