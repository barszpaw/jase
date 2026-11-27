#include "CpmFs.h"
#include <algorithm>
#include <cstring>

namespace {

// The REAL translate (XLT) table, confirmed by pulling it directly out of
// a live, running CBIOS's own RAM (see CpmFs.h's class comment for how/
// why) -- IS a skew-factor formula after all, just skew=5, not the
// skew=7 this file originally (wrongly) assumed. Logical directory/data
// sector index (0-15) -> physical sector id (0-based).
std::vector<int> buildSkewTable(int n, int skew)
{
	std::vector<int> table(n), used(n, 0);
	int phys = 0;
	for (int logical = 0; logical < n; logical++) {
		while (used[phys]) phys = (phys + 1) % n;
		table[logical] = phys;
		used[phys] = 1;
		phys = (phys + skew) % n;
	}
	return table;
}

} // namespace

bool CpmFs::open(DskImage& dsk)
{
	if (!dsk.loaded()) return false;
	dsk_ = &dsk;

	dpb_ = Dpb();
	dpb_.sectorsPerBlock = dpb_.blockSize / dpb_.sectorSize; // 1024/256 = 4

	int sides = dsk.numSides();
	int dataTracksPerSide = dsk.numTracks() - dpb_.reservedTracks;
	if (dataTracksPerSide <= 0) return false;
	dpb_.totalBlocks = (dataTracksPerSide * sides * dpb_.sectorsPerTrack * dpb_.sectorSize) / dpb_.blockSize;
	dpb_.blocks16bit = dpb_.totalBlocks > 256;
	return true;
}

uint8_t* CpmFs::logicalSector(int logicalIndex, int& len) const
{
	int sectorsPerSide = dpb_.sectorsPerTrack;
	int dataTrackIndex = logicalIndex / sectorsPerSide;      // 0-based, within the data area
	int logicalSectorInTrack = logicalIndex % sectorsPerSide;

	int track, side;
	if (dsk_->numSides() == 2) {
		// UNCONFIRMED for 2-sided disks (no real 340/620KB sample to check
		// against, see class comment) -- cylinder-interleaved: side
		// alternates every track, same cylinder read before stepping.
		track = dpb_.reservedTracks + dataTrackIndex / 2;
		side = dataTrackIndex % 2;
	} else {
		track = dpb_.reservedTracks + dataTrackIndex;
		side = 0;
	}

	static const std::vector<int> xlt = buildSkewTable(16, 5); // see class comment -- CONFIRMED real skew value
	int physicalSectorId = xlt[logicalSectorInTrack];
	return dsk_->findSector(track, side, physicalSectorId, len);
}

std::string CpmFs::decodeEntryName(const uint8_t entry[32])
{
	std::string name;
	for (int i = 1; i <= 8; i++) {
		char c = (char)(entry[i] & 0x7F);
		if (c != ' ') name += c;
	}
	std::string ext;
	for (int i = 9; i <= 11; i++) {
		char c = (char)(entry[i] & 0x7F);
		if (c != ' ') ext += c;
	}
	if (ext.empty()) return name;
	return name + "." + ext;
}

bool CpmFs::matchesName(const uint8_t entry[32], const std::string& normalized11)
{
	for (int i = 0; i < 11; i++) {
		if ((entry[1 + i] & 0x7F) != (uint8_t)normalized11[i]) return false;
	}
	return true;
}

std::string CpmFs::normalizeName(const std::string& name)
{
	std::string base = name, ext;
	size_t dot = name.find_last_of('.');
	if (dot != std::string::npos) {
		base = name.substr(0, dot);
		ext = name.substr(dot + 1);
	}
	std::string out(11, ' ');
	for (size_t i = 0; i < base.size() && i < 8; i++) out[i] = (char)toupper((unsigned char)base[i]);
	for (size_t i = 0; i < ext.size() && i < 3; i++) out[8 + i] = (char)toupper((unsigned char)ext[i]);
	return out; // 11 raw bytes, name padded to 8 + ext padded to 3, no dot
}

std::vector<std::vector<uint8_t>> CpmFs::readRawDirectory() const
{
	std::vector<std::vector<uint8_t>> entries;
	int entriesPerSector = dpb_.sectorSize / 32; // 256/32 = 8
	int dirSectors = (dpb_.dirEntries + entriesPerSector - 1) / entriesPerSector;
	for (int s = 0; s < dirSectors; s++) {
		int len = 0;
		uint8_t* p = logicalSector(s, len);
		if (!p) break;
		for (int i = 0; i < len / 32 && (int)entries.size() < dpb_.dirEntries; i++) {
			entries.emplace_back(p + i * 32, p + i * 32 + 32);
		}
	}
	return entries;
}

bool CpmFs::writeRawDirectoryEntry(int entryIndex, const uint8_t bytes[32])
{
	int entriesPerSector = dpb_.sectorSize / 32;
	int sectorIndex = entryIndex / entriesPerSector;
	int offsetInSector = (entryIndex % entriesPerSector) * 32;
	int len = 0;
	uint8_t* p = logicalSector(sectorIndex, len);
	if (!p || offsetInSector + 32 > len) return false;
	memcpy(p + offsetInSector, bytes, 32);
	return true;
}

std::vector<int> CpmFs::usedBlocks(const std::vector<std::vector<uint8_t>>& rawDir) const
{
	std::vector<int> used;
	for (const auto& e : rawDir) {
		if (e[0] == 0xE5) continue;
		if (dpb_.blocks16bit) {
			for (int i = 0; i < 8; i++) {
				int b = e[16 + i * 2] | (e[16 + i * 2 + 1] << 8);
				if (b != 0) used.push_back(b);
			}
		} else {
			for (int i = 0; i < 16; i++) {
				int b = e[16 + i];
				if (b != 0) used.push_back(b);
			}
		}
	}
	return used;
}

std::vector<CpmFs::DirEntry> CpmFs::list() const
{
	auto rawDir = readRawDirectory();
	// Group extents by (name, user) -- several raw 32-byte entries can
	// belong to the same logical file (EX=0,1,2,... for files bigger than
	// one extent's worth of blocks).
	struct Accum { int records = 0; bool readOnly = false, system = false; };
	std::vector<std::pair<std::string, Accum>> files;

	for (const auto& e : rawDir) {
		if (e[0] == 0xE5) continue;
		std::string name = decodeEntryName(e.data());
		int rc = e[15];

		auto it = std::find_if(files.begin(), files.end(),
			[&](const auto& p) { return p.first == name; });
		if (it == files.end()) {
			Accum a;
			a.readOnly = (e[9] & 0x80) != 0;
			a.system = (e[10] & 0x80) != 0;
			files.emplace_back(name, a);
			it = files.end() - 1;
		}
		// Sum stored RC across every extent -- matches every sample file
		// checked (all single-extent) and is the correct CP/M 2.2 rule for
		// well-formed multi-extent files too (every extent but the last
		// carries RC=128, the file-size arithmetic doesn't need extent
		// ORDER, just the total).
		it->second.records += rc;
	}

	std::vector<DirEntry> out;
	for (auto& [name, a] : files) {
		DirEntry d;
		d.name = name;
		d.records = a.records;
		d.sizeBytes = (size_t)a.records * 128;
		d.readOnly = a.readOnly;
		d.system = a.system;
		out.push_back(d);
	}
	std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
	return out;
}

bool CpmFs::readFile(const std::string& name, std::vector<uint8_t>& out) const
{
	std::string normalized = normalizeName(name);

	auto rawDir = readRawDirectory();
	std::vector<const std::vector<uint8_t>*> extents;
	for (const auto& e : rawDir) {
		if (e[0] == 0xE5) continue;
		if (!matchesName(e.data(), normalized)) continue;
		extents.push_back(&e);
	}
	if (extents.empty()) return false;

	std::sort(extents.begin(), extents.end(), [](const std::vector<uint8_t>* a, const std::vector<uint8_t>* b) {
		int oa = (*a)[14] * 32 + (*a)[12];
		int ob = (*b)[14] * 32 + (*b)[12];
		return oa < ob;
	});

	out.clear();
	for (const auto* ePtr : extents) {
		const auto& e = *ePtr;
		int rc = e[15];
		std::vector<int> blocks;
		if (dpb_.blocks16bit) {
			for (int i = 0; i < 8; i++) {
				int b = e[16 + i * 2] | (e[16 + i * 2 + 1] << 8);
				if (b != 0) blocks.push_back(b);
			}
		} else {
			for (int i = 0; i < 16; i++) {
				int b = e[16 + i];
				if (b != 0) blocks.push_back(b);
			}
		}
		std::vector<uint8_t> extentData;
		for (int b : blocks) {
			for (int s = 0; s < dpb_.sectorsPerBlock; s++) {
				int len = 0;
				uint8_t* p = logicalSector(b * dpb_.sectorsPerBlock + s, len);
				if (!p) return false;
				extentData.insert(extentData.end(), p, p + len);
			}
		}
		size_t wantBytes = (size_t)rc * 128;
		if (wantBytes > extentData.size()) wantBytes = extentData.size();
		out.insert(out.end(), extentData.begin(), extentData.begin() + wantBytes);
	}
	return true;
}

bool CpmFs::writeFile(const std::string& name, const std::vector<uint8_t>& data)
{
	// See class comment: only the 170KB (8-bit block pointer) geometry has
	// been checked against a real sample. Refuse rather than risk writing
	// a subtly-wrong extent layout to a density we can't verify.
	if (dpb_.blocks16bit) return false;
	if (!dsk_->isWritable()) return false; // see DskImage::isWritable()'s own comment -- checked BEFORE any mutation

	auto rawDir = readRawDirectory();
	std::string normalized = normalizeName(name);
	for (const auto& e : rawDir) {
		if (e[0] != 0xE5 && matchesName(e.data(), normalized)) return false; // already exists
	}

	auto used = usedBlocks(rawDir);
	std::vector<bool> inUse(dpb_.totalBlocks, false);
	for (int b : used) if (b >= 0 && b < dpb_.totalBlocks) inUse[b] = true;
	// Real AL0/AL1 reserve dirReservedBlocks (4) for the directory -- NOT
	// derivable from dirEntries*32/blockSize (would give 2): the real disk
	// physically reserves twice as much block space as DRM actually scans
	// as valid entries (see class comment). Never allocate those blocks to
	// a file even though half of them hold no real directory entries.
	for (int b = 0; b < dpb_.dirReservedBlocks && b < dpb_.totalBlocks; b++) inUse[b] = true;

	int blocksNeeded = (int)((data.size() + dpb_.blockSize - 1) / dpb_.blockSize);
	std::vector<int> freeBlocks;
	for (int b = 0; b < dpb_.totalBlocks && (int)freeBlocks.size() < blocksNeeded; b++) {
		if (!inUse[b]) freeBlocks.push_back(b);
	}
	if ((int)freeBlocks.size() < blocksNeeded) return false; // disk full

	int freeEntryCursor = 0;
	auto nextFreeEntry = [&]() -> int {
		for (; freeEntryCursor < (int)rawDir.size(); freeEntryCursor++) {
			if (rawDir[freeEntryCursor][0] == 0xE5) return freeEntryCursor++;
		}
		return -1;
	};

	const int kRecordsPerBlock = dpb_.blockSize / 128;      // 1024/128 = 8
	const int kBlocksPerExtent = 16;                         // standard CP/M 2.2, 8-bit block pointers
	const int kRecordsPerExtent = kBlocksPerExtent * kRecordsPerBlock; // 128 (RC's own max)

	int totalRecords = (int)((data.size() + 127) / 128);
	int blockCursor = 0;
	int recordsPlaced = 0;
	size_t fileOffset = 0; // running byte offset into `data`, in physical-sector-sized steps

	for (int extentNumber = 0; recordsPlaced < totalRecords || (data.empty() && extentNumber == 0); extentNumber++) {
		int entryIdx = nextFreeEntry();
		if (entryIdx < 0) return false; // directory full -- blocks already written are simply unreferenced garbage until the next free-block scan, harmless

		int recordsThisExtent = std::min(kRecordsPerExtent, totalRecords - recordsPlaced);
		int blocksThisExtent = (recordsThisExtent + kRecordsPerBlock - 1) / kRecordsPerBlock;

		uint8_t entry[32] = {};
		entry[0] = 0x00; // user 0
		memcpy(entry + 1, normalized.data(), 11);
		entry[12] = (uint8_t)extentNumber; // EX
		entry[13] = 0;                      // S1
		entry[14] = 0;                      // S2 (only needed for EX >= 32, unreachable at 170KB sizes)
		entry[15] = (uint8_t)recordsThisExtent;

		for (int i = 0; i < blocksThisExtent; i++, blockCursor++) {
			int b = freeBlocks[blockCursor];
			entry[16 + i] = (uint8_t)b;
			for (int s = 0; s < dpb_.sectorsPerBlock; s++) {
				int len = 0;
				uint8_t* p = logicalSector(b * dpb_.sectorsPerBlock + s, len);
				if (!p) return false;
				size_t chunk = (fileOffset < data.size()) ? std::min((size_t)len, data.size() - fileOffset) : 0;
				if (chunk > 0) memcpy(p, data.data() + fileOffset, chunk);
				if (chunk < (size_t)len) memset(p + chunk, 0x1A, len - chunk); // CP/M EOF pad
				fileOffset += (size_t)len;
			}
		}
		recordsPlaced += recordsThisExtent;

		if (!writeRawDirectoryEntry(entryIdx, entry)) return false;
		rawDir[entryIdx] = std::vector<uint8_t>(entry, entry + 32);
		if (blocksThisExtent == 0) break; // data.empty() case: one zero-length directory entry, done
	}

	return dsk_->flush();
}

bool CpmFs::deleteFile(const std::string& name)
{
	if (!dsk_->isWritable()) return false; // see DskImage::isWritable()'s own comment -- checked BEFORE any mutation
	auto rawDir = readRawDirectory();
	std::string normalized = normalizeName(name);

	bool any = false;
	for (size_t i = 0; i < rawDir.size(); i++) {
		if (rawDir[i][0] == 0xE5) continue;
		if (!matchesName(rawDir[i].data(), normalized)) continue;
		uint8_t entry[32];
		memcpy(entry, rawDir[i].data(), 32);
		entry[0] = 0xE5;
		if (!writeRawDirectoryEntry((int)i, entry)) return false;
		any = true;
	}
	if (!any) return false;
	return dsk_->flush();
}
