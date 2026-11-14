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
// documents, E=0,7,14,5,12,3,10,1,8,15,6,13,4,11,2,9, is exactly why a
// sector must be found by matching R in this list, never assumed to sit
// at a fixed offset).
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

	const TrackInfo* trackInfo(int track, int side) const;
};
