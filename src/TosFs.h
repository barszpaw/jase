#pragma once
#include "DskImage.h"
#include <cstdint>
#include <string>
#include <vector>

// Read-only access to the Timex TOS filesystem living inside a DskImage --
// CONFIRMED (2026-11-21) against TWO real disk samples already in this repo
// (TOSA4-Boot_Disk.dsk, TOS-CPM_Monitor_Emulator.dsk), cross-read against
// docs/FDD3000/tos_bez_tajemnic.html's own description of the format (the
// magazine article's ASCII-art byte tables got mangled by naive HTML-tag
// stripping when first extracted -- the REAL byte layout below was
// reconstructed from the raw directory-track bytes of those two disks, then
// matched back against the article's prose, not the other way around):
//
//   - 16 sectors/track, 256 bytes/sector, SAME geometry CpmFs/DskImage
//     already use elsewhere.
//   - 4 reserved tracks (0-3, "system operacyjny" per the article) --
//     DIFFERENT from CP/M's 2 on the exact same physical disk.
//   - Allocation unit = 4 sectors = 1024 bytes, SAME as CP/M's block size,
//     but TOS's own unit numbering starts at the directory track itself
//     (unit 0 = first unit of the directory track) -- confirmed: on both
//     real samples, the first file's allocation list starts at unit 4,
//     i.e. right after the directory's own 4 units (0-3).
//   - Directory = the entire 5th track (track index 4, 0-based) = 4096
//     bytes = 4 allocation units = 128 directory entries (32 bytes each).
//     Unlike CP/M on this same disk, the directory track's sectors are
//     stored in PLAIN ASCENDING order (sector id == stored index, verified
//     on both samples) -- no translate/skew table needed to read it
//     correctly. Data tracks are ASSUMED to use the same natural ordering
//     (consistent with the directory track, not independently verified
//     sector-by-sector against a known file's exact byte content).
//
// Directory entry (32 bytes), TWO shapes sharing the same first 12 bytes:
//   File record:
//     +0    K   "subdirectory" (really: user) number this file belongs to
//     +1-8  N   8-byte name, space padded
//     +9-11 E   3-byte extension; bit7 of E[0] = hidden, bit7 of E[1] =
//                write/delete-protected (CONFIRMED: every .SYS/.COD file on
//                both real samples has E[1]'s bit7 set, consistently)
//     +12   O   extent/sequence number for multi-extent files (like CP/M's
//                EX), 0-based
//     +13   L   exact byte count used in the file's LAST 256-byte sector --
//                only meaningful on a file's LAST extent; 0 on earlier
//                extents (CONFIRMED: both 2-extent sample files have L=0 on
//                extent 0, a real nonzero nonzero value on extent 1; the
//                TOS article's own editorial aside confirms CP/M has no
//                equivalent field, which is why writing a TOS file via
//                plain CP/M loses the exact trailing length)
//     +14-15 R  2-byte count of 128-byte half-sector units used in THIS
//                extent (byte14=high, byte15=low -- high byte is 0 in every
//                sample, since one extent caps at 16 allocation units = 128
//                half-units, fitting in a single byte either way)
//     +16-31 A  16 allocation-unit numbers (like CP/M's AL array)
//   Subdirectory record (ext is literally the text "DIR"):
//     +0    K   parent subdirectory number (0xFF for the disk-name record,
//                which is always the directory's very first entry)
//     +1-11 N,E same as a file record
//     +12-15    always zero (the file-record O/L/R fields, meaningless for
//                a subdirectory, which occupies no allocation units)
//     +16   D   this subdirectory's OWN number -- matched against other
//                entries' K byte to list "inside" it. CORRECTED 2026-11-22:
//                an earlier version of this file had D at +15, from
//                mis-counting the magazine article's own already-mangled
//                ASCII-art table (see top of this comment) -- found wrong
//                live against a THIRD real disk with actual nested
//                subdirectories (`TOS Demo Disk (Spectrum).DSK`, not one of
//                the two originally checked), where every real subdirectory
//                decoded to D=0 at +15 while +16 held a clean, sequential
//                1,2,3,4,5 across MATHS/FUN/UTIL/GAMES(nested in FUN)/FILING
//                -- i.e. this project's FIRST real multi-level-subdirectory
//                sample, which is also what originally exposed the bug
//                (dsk_browser couldn't enter any subdirectory at all).
//     +17-31    always zero in every sample checked
//
// Subdirectory nesting (K chains to a parent's own D, FF = root) IS now
// independently verified against a real sample with actual nesting
// (FUN, D=2, contains GAMES, K=2/D=4) -- not just the article's description.
//
// 320KB/640KB densities: NOT implemented. The article describes a
// genuinely different side-interleaving convention for the dense (640KB,
// 80-track double-sided) format that would need its own real sample to
// verify, same discipline as CpmFs's own 340/620KB caveat.
class TosFs
{
public:
	struct DirEntry {
		std::string name;    // "FILENAME.EXT", normalized uppercase
		size_t sizeBytes;     // exact byte count, from the real R/L fields
		bool isDir;           // true for a subdirectory record
		int userNumber;       // K byte (which subdirectory/user this belongs to)
		int dirNumber;        // D byte, meaningful only when isDir
		bool hidden;          // E[0] bit7
		bool protected_;       // E[1] bit7
	};

	// Binds to an already-loaded DskImage. Returns false if the geometry
	// doesn't match (currently: only the 170KB/320KB single-density
	// 40-track-1-side shape is handled -- see class comment).
	bool open(DskImage& dsk);

	// Lists entries whose K byte equals `userNumber` (0 = root) -- this IS
	// "entering a subdirectory" per the article: no page/track navigation,
	// just a different filter value over the same single directory track.
	std::vector<DirEntry> list(int userNumber = 0) const;

	bool readFile(const std::string& name, int userNumber, std::vector<uint8_t>& out) const;

private:
	struct Geometry {
		int reservedTracks = 4;
		int sectorsPerTrack = 16;
		int sectorSize = 256;
		int blockSize = 1024;
		int sectorsPerBlock = 4;
		int dirEntries = 128;
	};

	DskImage* dsk_ = nullptr;
	Geometry geo_;

	// logicalIndex 0..15 = the directory track's own 16 sectors (natural
	// order, see class comment); logicalIndex 16+ = data tracks after it.
	uint8_t* logicalSector(int logicalIndex, int& len) const;
	uint8_t* blockSector(int blockNumber, int sectorInBlock, int& len) const;

	std::vector<std::vector<uint8_t>> readRawDirectory() const;
	static std::string decodeName(const uint8_t entry[32]);
	static bool matchesName(const uint8_t entry[32], const std::string& normalized11);
	static std::string normalizeName(const std::string& name);
};
