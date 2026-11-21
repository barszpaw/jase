#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A loaded CPCEMU "DSK"/"Extended DSK" disk image -- the real, public,
// openly-documented container format used by the FDD3000/TOS/CP/M disk
// images in this project. The basic container walk was originally written
// from the on-disk format; sector-size and Extended DSK protection handling
// now follow MAME's dsk_format::load semantics (attribution/license in
// DskImage.cpp). Track timing/flux is passed through the station's MFM
// adapter for MAME's WD1772, rather than using MAME's full floppy-image
// framework.
//
// Format, as verified: a 256-byte disk-info header (track count, side
// count, and -- for Extended DSK -- a per-track size byte table, each
// entry = actual track size / 256), followed by one variable-length
// track block per (track,side), each starting with its own 256-byte
// "Track-Info" header (sector size code N, sector count, then an 8-byte
// descriptor per sector: C,H,R,N,ST1,ST2,len-lo,len-hi) and the sector
// data itself immediately after, in the SAME order as the descriptor
// list (which real images don't always store in ascending R order --
// confirmed the real physical interleave the "TOS bez tajemnic" article
// documents, E=0,7,14,5,12,3,10,1,8,15,6,13,4,11,2,9 (skew=7, TOS's own
// `#27` "Wczytanie sektora" OS call's documented argument sequence for
// reading a whole track at full speed), is exactly why a sector must be
// found by matching R in this list, never assumed to sit at a fixed
// offset. NOTE: this is the PHYSICAL media interleave, a property of the
// formatted disk itself -- a genuinely different, independent fact from
// src/CpmFs.h's own skew=5 BDOS logical-to-physical translate table
// (that CBIOS's own choice for how it maps ITS logical sector numbering
// onto this same physical media -- confirmed not to match the physical
// interleave above, and there's no reason it would have to).
class DskImage
{
public:
	// Sector metadata retained by MAME's dsk_format loader and consumed
	// later when synthesizing the MFM track for the WD1772.
	struct SectorInfo {
		uint8_t c = 0, h = 0, r = 0, n = 0;
		uint8_t st1 = 0, st2 = 0;
		int length = 0;
		bool hasData = true;
	};

	bool load(const std::string& path);

	// Loads a "raw" image: a headerless, plain sequential dump with no
	// container format at all -- track 0/side 0's sectors, then track
	// 0/side 1 (if 2-sided), then track 1/side 0, etc., each sector
	// exactly `sectorSize` bytes with no gaps, sector IDs implicitly
	// 0..sectorsPerTrack-1 in that stored order (there's nothing in a raw
	// file to record a physical interleave at all -- same convention
	// ~/src/TOStools's own RawImageAsDSK adapter uses for its own `--raw`
	// mode, which is where this request came from). Internally
	// synthesizes an Extended-DSK-shaped in-memory buffer so every other
	// method (findSector()/sectorData()/...) works completely unchanged;
	// flush()/saveAs() detect this and write back out in the ORIGINAL
	// raw shape, not the synthesized one -- a raw image must stay a raw
	// image on disk, or writeFile()/deleteFile() (which just call
	// flush()) would silently corrupt it into a different format at the
	// same path. Fails if the file size doesn't exactly match
	// numTracks*numSides*sectorsPerTrack*sectorSize.
	bool loadRaw(const std::string& path, int numTracks, int numSides, int sectorsPerTrack, int sectorSize);

	// Creates a new, already-formatted, blank FDD3000 disk image at
	// `path` and writes it to disk -- see DskImage.cpp's own comment for
	// the exact byte layout, reverse-engineered from real, working FDD3000
	// disk images (2026-11-14). Does NOT load the result into this
	// instance -- call load(path) afterward to use it. numSides must be
	// 1 or 2; the real reference geometries are (numTracks=40,
	// numSides=1) for a 170KB disk and (numTracks=80, numSides=2) for a
	// 620KB one, but any plausible geometry is accepted.
	static bool createBlank(const std::string& path, int numTracks, int numSides);

	bool loaded() const { return !data_.empty(); }
	int numTracks() const { return numTracks_; }
	int numSides() const { return numSides_; }

	// Finds sector `sectorId` on (track, side). Returns a pointer directly
	// into the loaded image (writes via writable() land straight back in
	// the in-memory copy -- see class comment on persistence below) and
	// fills `len`. nullptr if the track doesn't exist or no sector with
	// that id is present on it (real hardware's RNF case).
	// `indexOut`, if non-null, gets the matched descriptor's ordinal
	// position within the track's STORED (physical interleave) order --
	// Wd1772 uses this as a proxy for "how far around the track" a real
	// head would have to travel to reach it, to give Type II/III commands
	// a non-instant, position-dependent BUSY delay instead of completing
	// on the same instruction they're issued.
	uint8_t* findSector(int track, int side, int sectorId, int& len, int* indexOut = nullptr);

	// How many sectors are listed on (track, side), and the raw 8-byte
	// descriptor for the i-th one in STORED (not necessarily ascending
	// R) order -- id/len, used by Wd1772's Read Address.
	int sectorCount(int track, int side) const;
	bool sectorDescriptor(int track, int side, int index, uint8_t& id, int& len) const;
	bool sectorInfo(int track, int side, int index, SectorInfo& info) const;
	uint8_t* sectorData(int track, int side, int index, int& len);
	int gap3Length(int track, int side) const;

	// Persists the WHOLE in-memory image back to the file it was load()ed
	// from (2026-11-14 -- see MEMORY.md/CLAUDE.md: writes via sectorData()/
	// findSector() land in data_ immediately, same as before, but data_
	// was never written back to disk until this existed). Wd1772Fsm calls
	// this once a full Write Sector command completes, not per byte.
	bool flush();
	const std::string& path() const { return path_; }

	// "Save As" (2026-11-14): re-points this image at a NEW file and
	// writes the current in-memory content there -- the real workflow is
	// "Insert New" creates a disk at a throwaway temp path first (same
	// createBlank()+load() path as any other disk, nothing special about
	// it -- the user's own call: "może być w pamięci lub plik
	// tymczasowy... pomaga w unifikowaniu operacji"), then the user's
	// explicit Save As picks the REAL destination. Future flush() calls
	// (every completed Write Sector) go to the new path from here on.
	bool saveAs(const std::string& newPath)
	{
		path_ = newPath;
		return flush();
	}

	// Writes the in-memory image out as a genuine CPCEMU/Extended DSK
	// container file, regardless of how THIS instance was loaded --
	// unlike saveAs()/flush(), which deliberately preserve a raw image's
	// original flat shape on write-back (see loadRaw()'s own comment),
	// this is for the opposite, one-shot case: explicitly converting a
	// raw image into a real container file (see tools/raw2dsk.cpp).
	// Does not touch this instance's own path_/rawFormat_. Safe to call
	// right after loadRaw() because that already leaves data_ holding a
	// genuine synthesized Extended DSK buffer -- this just dumps it.
	bool exportAsContainer(const std::string& path) const;

private:
	struct TrackInfo {
		long dataOffset = -1; // offset of this track's 256-byte header in data_, -1 = not present
		std::vector<int> sectorLengths;
		std::vector<long> sectorDataOffsets;
	};

	std::vector<uint8_t> data_;
	std::string path_; // set by load(), used by flush()
	int numTracks_ = 0;
	int numSides_ = 0;
	std::vector<TrackInfo> tracks_; // index = track*numSides_ + side
	bool rawFormat_ = false; // see loadRaw()'s own comment -- changes how flush() serializes

	const TrackInfo* trackInfo(int track, int side) const;
	bool flushRaw();
};
