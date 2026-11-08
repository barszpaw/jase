#pragma once
#include <cstdint>
#include <string>
#include <vector>

// A loaded CPCEMU "DSK"/"Extended DSK" disk image -- the real, public,
// openly-documented container format every real FDD3000/TOS/CP/M disk
// image found for this project actually uses (confirmed, 2026-11-01:
// `~/src/Fuse_FDD3000/disks/**/*.dsk`, all either "MV - CPCEMU
// Disk-File" or "EXTENDED CPC DSK File" signed, produced by the real
// "CPCDiskXP" tool per that archive's own notes). Clean-room parser
// written directly from the format's own on-disk layout (confirmed
// byte-for-byte via `xxd` against a real file, not from any other
// project's code) -- same policy as everywhere else in this codebase.
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

	// Simplification, documented not hidden: writes (Wd1772's Write
	// Sector) land only in this in-memory copy, never flushed back to
	// the file on disk -- same "no persistence yet" gap DivIde's own
	// insertDisk() comment doesn't have (DivIde writes through) but
	// Fdd3000 does for now, since CP/M's own SAVE/PIP paths weren't
	// tested yet at the time this was written. Revisit once that's
	// verified against the real Fuse+FDD3000 reference.

private:
	struct TrackInfo {
		long dataOffset = -1; // offset of this track's 256-byte header in data_, -1 = not present
	};

	std::vector<uint8_t> data_;
	int numTracks_ = 0;
	int numSides_ = 0;
	std::vector<TrackInfo> tracks_; // index = track*numSides_ + side

	const TrackInfo* trackInfo(int track, int side) const;
};
