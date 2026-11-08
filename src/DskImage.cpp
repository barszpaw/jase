#include "DskImage.h"
#include <cstdio>
#include <cstring>

bool DskImage::load(const std::string& path)
{
	data_.clear();
	tracks_.clear();
	numTracks_ = numSides_ = 0;

	FILE* pf = fopen(path.c_str(), "rb");
	if (!pf) {
		fprintf(stderr, "DskImage::load: cannot open %s\n", path.c_str());
		return false;
	}
	fseek(pf, 0, SEEK_END);
	long size = ftell(pf);
	fseek(pf, 0, SEEK_SET);
	if (size < 256) {
		fprintf(stderr, "DskImage::load: %s is too small to be a DSK image\n", path.c_str());
		fclose(pf);
		return false;
	}
	data_.resize((size_t)size);
	size_t n = fread(data_.data(), 1, (size_t)size, pf);
	fclose(pf);
	if (n != (size_t)size) {
		fprintf(stderr, "DskImage::load: short read on %s\n", path.c_str());
		data_.clear();
		return false;
	}

	bool extended;
	if (memcmp(data_.data(), "EXTENDED CPC DSK File", 21) == 0) {
		extended = true;
	} else if (memcmp(data_.data(), "MV - CPCEMU Disk-File", 21) == 0) {
		extended = false;
	} else {
		fprintf(stderr, "DskImage::load: %s is not a CPCEMU/Extended DSK image (bad signature)\n", path.c_str());
		data_.clear();
		return false;
	}

	numTracks_ = data_[0x30];
	numSides_ = data_[0x31];
	if (numTracks_ <= 0 || numSides_ <= 0 || numSides_ > 2) {
		fprintf(stderr, "DskImage::load: %s has implausible geometry (%d tracks, %d sides)\n",
		        path.c_str(), numTracks_, numSides_);
		data_.clear();
		return false;
	}

	tracks_.resize((size_t)(numTracks_ * numSides_));
	long offset = 256;
	for (int i = 0; i < numTracks_ * numSides_; i++) {
		int trackSize;
		if (extended) {
			// Track-size table lives right after the fixed header fields,
			// one byte per (track,side) in the same storage order as the
			// track blocks themselves; actual size = byte * 256. A zero
			// entry means that track wasn't formatted/present at all.
			if ((size_t)(0x34 + i) >= data_.size()) break;
			trackSize = data_[0x34 + i] * 256;
		} else {
			// Standard (non-extended) DSK: one fixed size for every track,
			// little-endian uint16 at 0x32-0x33.
			trackSize = data_[0x32] | (data_[0x33] << 8);
		}
		if (trackSize == 0) continue; // unformatted track, leave dataOffset at -1
		if (offset + 256 > (long)data_.size()) break; // truncated file, stop here
		if (memcmp(&data_[offset], "Track-Info", 10) != 0) {
			fprintf(stderr, "DskImage::load: %s: missing Track-Info signature at track index %d\n",
			        path.c_str(), i);
			break;
		}
		tracks_[i].dataOffset = offset;
		offset += trackSize;
	}

	printf("DskImage::load: loaded %s (%d tracks x %d side%s)\n",
	       path.c_str(), numTracks_, numSides_, numSides_ == 1 ? "" : "s");
	return true;
}

const DskImage::TrackInfo* DskImage::trackInfo(int track, int side) const
{
	// A real single-sided drive has only one physical head -- its /SIDE
	// SELECT line is simply unconnected, so the drive reads the same
	// side no matter what the host commands. Mirror that here: on a
	// single-sided image, ignore whatever side the caller asked for
	// rather than reporting "no sectors" the way a true two-sided image
	// would for an out-of-range side (found live, 2026-11-07: TOS
	// writing /SIDE_B on a single-sided TOS disk made every sector on
	// every track invisible, misread by the FDD3000 emulation as the
	// disk having an unformatted track 1).
	if (numSides_ == 1) side = 0;
	if (track < 0 || track >= numTracks_ || side < 0 || side >= numSides_) return nullptr;
	const TrackInfo& t = tracks_[(size_t)(track * numSides_ + side)];
	if (t.dataOffset < 0) return nullptr;
	return &t;
}

// Shared walk of a track's sector-descriptor list (8 bytes each, right
// after the 24-byte fixed part of the 256-byte Track-Info header: 12
// bytes signature + 4 unused + track/side/unused/N + sector-count +
// GAP#3 + filler = offset 0x18). Sector data for descriptor k starts at
// (track header start + 0x100) plus the sum of the declared lengths of
// descriptors 0..k-1 -- NOT at a fixed per-sector stride, since a real
// image can mix sector sizes (confirmed in the Extended DSK spec; not
// observed in any real file here, but cheap to get right).
int DskImage::sectorCount(int track, int side) const
{
	const TrackInfo* t = trackInfo(track, side);
	if (!t) return 0;
	return data_[t->dataOffset + 0x15];
}

// The descriptor's own len-lo/len-hi (offset+6/+7) is an Extended-DSK-only
// field -- standard ("MV - CPCEMU Disk-File") images legitimately leave it
// zero and expect the reader to derive size from N (offset+3) instead, the
// same 128<<N convention every real WD17xx sector-size code uses. Found
// live, 2026-11-07: a real standard-format disk (MUSIC.DSK) has len=0 on
// every descriptor -- findSector() was returning a "found" sector with
// zero length, so Wd1772's transfer never actually delivered real data
// (station read 256 bytes of its own stale, never-advanced data register
// instead), the root cause of the station boot getting nowhere.
static int declaredOrDerivedLen(const std::vector<uint8_t>& data, long d)
{
	int len = data[d + 6] | (data[d + 7] << 8);
	if (len == 0) len = 128 << data[d + 3]; // N
	return len;
}

bool DskImage::sectorDescriptor(int track, int side, int index, uint8_t& id, int& len) const
{
	const TrackInfo* t = trackInfo(track, side);
	if (!t) return false;
	int count = data_[t->dataOffset + 0x15];
	if (index < 0 || index >= count) return false;
	long d = t->dataOffset + 0x18 + index * 8;
	id = data_[d + 2]; // R
	len = declaredOrDerivedLen(data_, d);
	return true;
}

uint8_t* DskImage::findSector(int track, int side, int sectorId, int& len, int* indexOut)
{
	const TrackInfo* t = trackInfo(track, side);
	if (!t) return nullptr;
	int count = data_[t->dataOffset + 0x15];
	long dataPos = t->dataOffset + 0x100;
	for (int i = 0; i < count; i++) {
		long d = t->dataOffset + 0x18 + i * 8;
		uint8_t r = data_[d + 2];
		int thisLen = declaredOrDerivedLen(data_, d);
		if (r == (uint8_t)sectorId) {
			len = thisLen;
			if (dataPos + thisLen > (long)data_.size()) return nullptr; // truncated file
			if (indexOut) *indexOut = i;
			return &data_[dataPos];
		}
		dataPos += thisLen;
	}
	return nullptr;
}
