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
//
// CORRECTION 2026-11-27: the paragraph above USED to claim the directory
// track's sectors are stored in plain ascending order, "verified on both
// samples" -- that check was WORTHLESS, the exact same trap CpmFs.h's own
// class comment documents hitting for CP/M: reading all 16 physical
// sectors of a track in ANY order reproduces the same SET of 32-byte
// directory entries (list() sorts by name regardless), so it can never
// expose a wrong permutation. The real bug (reported by the user via an
// independent reference tool, ~/src/TOStools/tos-browser.py, comparing
// sha256 of a real extracted file) only showed up reading a real file's
// data content byte-for-byte PAST its first sector -- any skew table
// starting phys=0 always maps logical index 0 to physical sector 0,
// masking the bug for single-sector files/reads. TOS actually uses the
// SAME skew-factor mechanism as CP/M's own translate table
// (buildSkewTable() in CpmFs.cpp/TosFs.cpp), just a DIFFERENT skew value:
// 7 for TOS, vs. 5 for CP/M on this same physical media (see CpmFs.h's own
// class comment) -- two independent BDOS/OS choices over the same disk,
// confirmed directly by the user 2026-11-27. Fixed in TosFs.cpp's
// blockSector() (applies to BOTH the directory track and data tracks,
// since both go through the same function); verified by extracting
// R-TYPE.SCR and matching its sha256 against tos-browser.py's own output.
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
// 640KB "dense" (80-track, 2-sided) density: CONFIRMED 2026-11-22 against a
// real sample (`CPM tos a2 start cpm 525.dsk`, a 2-sided TOS disk that also
// boots CP/M) -- the article's own "napęd gęsty numeruje naprzemiennie obie
// strony dyskietki" (dense drive numbers both sides alternately) means the
// article's "4 reserved tracks" are 4 LOGICAL units, not 4 physical tracks:
// on a 2-sided disk that's only 2 PHYSICAL tracks (each holding 2 logical
// units' worth via its 2 sides), so the directory sits at PHYSICAL track 2,
// side 0 -- confirmed live (found the mandatory disk-name record exactly
// there, nowhere else checked). Allocation units cylinder-interleave the
// same way past the directory: unit 0-3 = track R/side0 (R=physicalReserved
// Tracks), unit 4-7 = track R/side1, unit 8-11 = track R+1/side0, etc. --
// i.e. side alternates every physical track, same cylinder fully used
// before stepping (same convention CpmFs's own class comment speculates
// for CP/M's still-unconfirmed 2-sided densities, now independently
// confirmed true for TOS specifically). Verified by reading real files
// through this mapping and getting plausible, non-garbage content.
//
// Found and fixed one more real bug while verifying this sample: a
// subdirectory's own K byte (which subdirectory it's nested inside) can
// have bit7 set -- this disk's real "GAMES" entry (nested inside "FUN",
// FUN's own D=4) has K=0x84, not 0x04. Bit7 here is almost certainly a
// "this subdirectory is hidden" flag, by analogy with a file's own E[0]/
// E[1] bit7 flags -- but regardless of its exact meaning, every K
// comparison MUST mask it off (`& 0x7F`) before comparing to a
// userNumber/parent filter, or entries nested inside a bit7-flagged
// subdirectory silently vanish from their own parent's listing. The two
// originally-checked 170KB samples never exercised this (no subdirectory
// there has bit7 set), which is why it went unnoticed until this one.
//
// 2026-11-22, user pointed at an independent, separately-developed tool
// (~/src/TOStools/tostools/definitions.py's DISK_DEFS table) listing FOUR
// tos-timex-* geometries. Two match what's already independently verified
// above (`TosTimex` = this file's 170KB; `TosTimex640` = the 640KB dense
// shape -- modulo one discrepancy, see Preset::TosTimex640's own comment).
// The other two (`TosTimex320`/`TosTimex320Alt`, two DIFFERENT physical
// layouts for the same 320KB nominal capacity) originally had no real
// sample in this repo. Their geometry numbers (reserved tracks, dirEntries)
// are taken from that table as a reasonable starting point.
//
// CORRECTION 2026-11-27: this paragraph used to argue sector ordering
// should use "this file's own confirmed natural-order convention", NOT
// that external table's `generate_xlt_table(16, skew=7)` -- backwards.
// The "confirmation" of natural order was never real (see the top of this
// comment's 2026-11-27 correction), and skew=7 turned out to be EXACTLY
// right, for the SAME reason that external tool's author presumably used
// it: it's the real TOS BDOS translate table, just like CP/M's own
// skew=5 on this same media. blockSector() now applies it uniformly across
// every preset (same shared function), so TosTimex320/TosTimex320Alt get
// the fix too even though neither has had its OWN data content verified
// byte-for-byte against this exact skew value yet (TosTimex320Alt's prior
// "independent verification" below was a listing-only check, same
// listing-is-permutation-invariant trap -- worth re-checking against real
// file content if this preset is ever exercised for real reads).
//
// 2026-11-23: `TosTimex320Alt` is now ALSO independently verified, against
// a real raw image (`~/src/TOStools/bajtek1_side_ab.img`, 327680 bytes,
// loaded via DskImage::loadRaw() + tools/raw2dsk) -- `openWithPreset(...,
// TosTimex320Alt)` reads a perfectly legible listing (4 real subdirectories,
// 10 real files, sane names/sizes). `TosTimex320` (80 tracks, 1 side) was
// tried against the SAME raw bytes first and produced garbage, which is
// what settled the ambiguity (both presets claim the same 327680-byte total
// size, so file size alone can't distinguish them) -- this does NOT mean
// TosTimex320 itself is wrong, only that this particular sample isn't one;
// it remains unverified, no real sample of that exact geometry found yet.
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

	// Named geometry presets, for manual override when auto-detection
	// fails or guesses wrong (see dsk_browser.cpp's own format-choice
	// dialog). TosTimex/TosTimex640/TosTimex320Alt are independently
	// verified (see class comment); TosTimex320 is NOT -- best-effort from
	// an external reference table, no real sample to check against yet.
	enum class Preset { TosTimex, TosTimex640, TosTimex320, TosTimex320Alt };
	static const char* presetName(Preset p);
	static const char* presetDescription(Preset p);
	// (tracks, sides) a preset expects -- exposed so callers building a
	// RAW-geometry UI (dsk_browser's format-choice dialog) don't have to
	// duplicate these numbers themselves.
	static void presetGeometry(Preset p, int& tracks, int& sides);

	// Binds to an already-loaded DskImage, auto-detecting between the two
	// independently-verified presets (TosTimex/TosTimex640) by numSides().
	// Returns false if the geometry doesn't match either.
	bool open(DskImage& dsk);

	// Binds using an EXPLICIT preset, bypassing auto-detection entirely --
	// for manual override (wrong auto-guess, or one of the two unverified
	// presets, which open() above never picks on its own). Only fails if
	// the disk's own numSides() genuinely can't match the preset's.
	bool openWithPreset(DskImage& dsk, Preset preset);

	// True if this disk's geometry only supports reading (640KB 2-sided
	// write-path allocation/layout isn't independently verified the way
	// the 1-sided write path is -- see CpmFs.h's own discipline on
	// unconfirmed densities). writeFile()/deleteFile() etc. check this
	// themselves; exposed so callers (e.g. the browser UI) can show an
	// accurate read-only indicator without duplicating the rule.
	bool isWritableGeometry() const { return geo_.numSides == 1; }

	// Lists entries whose K byte equals `userNumber` (0 = root) -- this IS
	// "entering a subdirectory" per the article: no page/track navigation,
	// just a different filter value over the same single directory track.
	std::vector<DirEntry> list(int userNumber = 0) const;

	bool readFile(const std::string& name, int userNumber, std::vector<uint8_t>& out) const;

	// Write support -- ONLY for the 1-sided geometry (see
	// isWritableGeometry()'s own comment); returns false immediately
	// otherwise, same discipline as CpmFs's own blocks16bit refusal.
	// Deletes a FILE (not a subdirectory -- rmdir isn't implemented, no
	// real sample to confirm an emptiness-check convention against).
	bool deleteFile(const std::string& name, int userNumber);
	// Creates a new, empty subdirectory named `name` inside `parentUserNumber`
	// (0 = root). Picks the lowest D number not already used anywhere on the
	// disk (matching how every real sample's D values are small and appear
	// densely packed from 1 up) and a free directory slot. Returns the new
	// subdirectory's own D number via `outDirNumber` on success.
	bool createDirectory(const std::string& name, int parentUserNumber, int& outDirNumber);

private:
	struct Geometry {
		int numSides = 1;
		int physicalReservedTracks = 4; // 4 for 1-sided (track 4); 2 for 2-sided (track 2, see class comment)
		int sectorsPerTrack = 16;
		int sectorSize = 256;
		int blockSize = 1024;
		int sectorsPerBlock = 4;
		int dirEntries = 128;
	};

	static Geometry geometryForPreset(Preset preset);

	DskImage* dsk_ = nullptr;
	Geometry geo_;

	// logicalIndex 0..15 = the directory track's own 16 sectors (natural
	// order, see class comment); logicalIndex 16+ = data tracks after it.
	uint8_t* logicalSector(int logicalIndex, int& len) const;
	uint8_t* blockSector(int blockNumber, int sectorInBlock, int& len) const;

	std::vector<std::vector<uint8_t>> readRawDirectory() const;
	bool writeRawDirectoryEntry(int entryIndex, const uint8_t bytes[32]);
	static std::string decodeName(const uint8_t entry[32]);
	static bool matchesName(const uint8_t entry[32], const std::string& normalized11);
	static std::string normalizeName(const std::string& name);
};
