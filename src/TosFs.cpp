#include "TosFs.h"
#include <algorithm>
#include <cstring>

const char* TosFs::presetName(Preset p)
{
	switch (p) {
		case Preset::TosTimex: return "TOS 170KB";
		case Preset::TosTimex640: return "TOS 640KB (gesty, 2 strony)";
		case Preset::TosTimex320: return "TOS 320KB (80 sciezek, 1 strona)";
		case Preset::TosTimex320Alt: return "TOS 320KB-alt (40 sciezek, 2 strony)";
	}
	return "?";
}

void TosFs::presetGeometry(Preset p, int& tracks, int& sides)
{
	// Total disk geometry each preset expects -- a DIFFERENT fact from
	// Geometry::physicalReservedTracks (how many of those tracks the
	// directory reserves), which geometryForPreset() already handles.
	switch (p) {
		case Preset::TosTimex:       tracks = 40; sides = 1; break;
		case Preset::TosTimex640:    tracks = 80; sides = 2; break;
		case Preset::TosTimex320:    tracks = 80; sides = 1; break;
		case Preset::TosTimex320Alt: tracks = 40; sides = 2; break;
	}
}

const char* TosFs::presetDescription(Preset p)
{
	switch (p) {
		case Preset::TosTimex:
			return "Zweryfikowany na 2 realnych dyskach (40 sciezek, 1 strona).";
		case Preset::TosTimex640:
			return "Zweryfikowany na realnym dysku (80 sciezek, 2 strony, przeplot stron).";
		case Preset::TosTimex320:
			return "NIEZWERYFIKOWANY -- parametry z zewnetrznego narzedzia (~/src/TOStools), brak realnej probki.";
		case Preset::TosTimex320Alt:
			return "NIEZWERYFIKOWANY -- parametry z zewnetrznego narzedzia (~/src/TOStools), brak realnej probki.";
	}
	return "";
}

TosFs::Geometry TosFs::geometryForPreset(Preset preset)
{
	Geometry g;
	switch (preset) {
		case Preset::TosTimex:
			g.numSides = 1; g.physicalReservedTracks = 4; g.dirEntries = 128;
			break;
		case Preset::TosTimex640:
			g.numSides = 2; g.physicalReservedTracks = 2; g.dirEntries = 128;
			break;
		case Preset::TosTimex320:
			// 80 tracks, 1 side -- per ~/src/TOStools's own 'tos-timex-320'
			// entry (boottrk=2, maxdir=64). UNVERIFIED, see class comment.
			g.numSides = 1; g.physicalReservedTracks = 2; g.dirEntries = 64;
			break;
		case Preset::TosTimex320Alt:
			// 40 tracks, 2 sides -- per that same table's 'tos-timex-320-alt'
			// entry (boottrk=4, maxdir=64). UNVERIFIED, see class comment.
			g.numSides = 2; g.physicalReservedTracks = 4; g.dirEntries = 64;
			break;
	}
	return g;
}

bool TosFs::open(DskImage& dsk)
{
	if (!dsk.loaded()) return false;
	// Auto-detect only between the two INDEPENDENTLY VERIFIED presets --
	// the other two need an explicit openWithPreset() call (see
	// dsk_browser.cpp's format-choice dialog), since there's no reliable
	// signature to pick them out automatically from numSides() alone (both
	// unverified presets share a numSides value with a verified one).
	if (dsk.numSides() == 1) return openWithPreset(dsk, Preset::TosTimex);
	if (dsk.numSides() == 2) return openWithPreset(dsk, Preset::TosTimex640);
	return false;
}

bool TosFs::openWithPreset(DskImage& dsk, Preset preset)
{
	if (!dsk.loaded()) return false;
	Geometry g = geometryForPreset(preset);
	if (dsk.numSides() != g.numSides) return false;
	dsk_ = &dsk;
	geo_ = g;
	return true;
}

uint8_t* TosFs::blockSector(int blockNumber, int sectorInBlock, int& len) const
{
	int unitsPerTrack = geo_.sectorsPerTrack / geo_.sectorsPerBlock; // 4, per side
	if (geo_.numSides == 2) {
		// Cylinder-interleaved: side alternates every physical track, same
		// cylinder fully used (both sides) before stepping -- see class
		// comment for how this was confirmed against a real 2-sided sample.
		int unitsPerCylinder = unitsPerTrack * 2; // 8
		int cylinder = blockNumber / unitsPerCylinder;
		int rem = blockNumber % unitsPerCylinder;
		int side = rem / unitsPerTrack;
		int unitInTrack = rem % unitsPerTrack;
		int track = geo_.physicalReservedTracks + cylinder;
		int sectorId = unitInTrack * geo_.sectorsPerBlock + sectorInBlock;
		return dsk_->findSector(track, side, sectorId, len);
	}
	int track = geo_.physicalReservedTracks + blockNumber / unitsPerTrack;
	int unitInTrack = blockNumber % unitsPerTrack;
	int sectorId = unitInTrack * geo_.sectorsPerBlock + sectorInBlock; // natural order, see class comment
	return dsk_->findSector(track, 0, sectorId, len);
}

uint8_t* TosFs::logicalSector(int logicalIndex, int& len) const
{
	return blockSector(logicalIndex / geo_.sectorsPerBlock, logicalIndex % geo_.sectorsPerBlock, len);
}

std::string TosFs::decodeName(const uint8_t entry[32])
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

std::string TosFs::normalizeName(const std::string& name)
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
	return out;
}

bool TosFs::matchesName(const uint8_t entry[32], const std::string& normalized11)
{
	for (int i = 0; i < 11; i++) {
		if ((entry[1 + i] & 0x7F) != (uint8_t)normalized11[i]) return false;
	}
	return true;
}

std::vector<std::vector<uint8_t>> TosFs::readRawDirectory() const
{
	std::vector<std::vector<uint8_t>> entries;
	int entriesPerSector = geo_.sectorSize / 32; // 8
	int dirSectors = (geo_.dirEntries + entriesPerSector - 1) / entriesPerSector; // 16
	for (int s = 0; s < dirSectors; s++) {
		int len = 0;
		uint8_t* p = logicalSector(s, len);
		if (!p) break;
		for (int i = 0; i < len / 32 && (int)entries.size() < geo_.dirEntries; i++) {
			entries.emplace_back(p + i * 32, p + i * 32 + 32);
		}
	}
	return entries;
}

namespace {

bool isDirRecord(const std::vector<uint8_t>& e)
{
	return (e[9] & 0x7F) == 'D' && (e[10] & 0x7F) == 'I' && (e[11] & 0x7F) == 'R';
}

// Full CP/M-style extent number (S2<<5 | EX, not just EX) -- matters once a
// file exceeds 32 extents (>512KB on this format), which none of this
// project's own samples do, but matches the independent ~/src/TOStools
// project's own `full_extent_no` exactly (cross-checked 2026-11-22, see
// TosFs.h class comment), so there's no reason to cut the corner here.
int fullExtentNo(const std::vector<uint8_t>& e) { return (e[14] << 5) | e[12]; }

void sortByExtentNo(std::vector<const std::vector<uint8_t>*>& extents)
{
	std::sort(extents.begin(), extents.end(), [](const std::vector<uint8_t>* a, const std::vector<uint8_t>* b) {
		return fullExtentNo(*a) < fullExtentNo(*b);
	});
}

// RC (byte15, standard CP/M-style 128-byte record count for THIS extent --
// NOT a 2-byte field; byte14 is S2, part of the extent number above, see
// TosFs.h's own class comment on how this was found wrong) and S1/L
// (byte13, exact byte count in the file's last 256-byte sector) together
// give the exact file size. Formula independently cross-checked against
// ~/src/TOStools/tostools/formats.py's own `_calculate_final_size()`
// (2026-11-22, user pointed at it): `(sum(rc) - 2)*128 + s1` when s1 != 0,
// else `sum(rc)*128` -- algebraically identical to this function's own
// per-extent running total, confirmed by hand. `extents` need not be
// pre-sorted; this sorts its own local copy by full extent number to find
// the real last one.
size_t computeFileSize(std::vector<const std::vector<uint8_t>*> extents)
{
	sortByExtentNo(extents);
	int totalRecords = 0;
	for (auto* e : extents) totalRecords += (*e)[15];
	if (totalRecords == 0) return 0;
	int l = (*extents.back())[13];
	if (l != 0) return (size_t)(totalRecords - 2) * 128 + (size_t)l;
	return (size_t)totalRecords * 128;
}

} // namespace

std::vector<TosFs::DirEntry> TosFs::list(int userNumber) const
{
	auto rawDir = readRawDirectory();

	struct Accum {
		std::vector<const std::vector<uint8_t>*> extents;
		bool isDir = false;
		int dirNumber = 0;
		bool hidden = false, protected_ = false;
	};
	std::vector<std::pair<std::string, Accum>> files;

	for (const auto& e : rawDir) {
		if (e[0] == 0xE5) continue;   // deleted/unused
		if (e[0] == 0xFF) continue;   // disk-name record, not a browsable entry
		if (((int)e[0] & 0x7F) != userNumber) continue; // mask bit7 -- see class comment

		std::string name = decodeName(e.data());
		auto it = std::find_if(files.begin(), files.end(),
			[&](const auto& p) { return p.first == name; });
		if (it == files.end()) {
			Accum a;
			a.isDir = isDirRecord(e);
			a.hidden = (e[9] & 0x80) != 0;
			a.protected_ = (e[10] & 0x80) != 0;
			if (a.isDir) a.dirNumber = e[16]; // see TosFs.h class comment -- corrected 2026-11-22, was +15
			files.emplace_back(name, a);
			it = files.end() - 1;
		}
		if (!it->second.isDir) it->second.extents.push_back(&e);
	}

	std::vector<DirEntry> out;
	for (auto& [name, a] : files) {
		DirEntry d;
		d.name = name;
		d.isDir = a.isDir;
		d.userNumber = userNumber;
		d.dirNumber = a.dirNumber;
		d.hidden = a.hidden;
		d.protected_ = a.protected_;
		d.sizeBytes = a.isDir ? 0 : computeFileSize(a.extents);
		out.push_back(d);
	}
	std::sort(out.begin(), out.end(), [](const DirEntry& a, const DirEntry& b) {
		if (a.isDir != b.isDir) return a.isDir > b.isDir;
		return a.name < b.name;
	});
	return out;
}

bool TosFs::readFile(const std::string& name, int userNumber, std::vector<uint8_t>& out) const
{
	std::string normalized = normalizeName(name);
	auto rawDir = readRawDirectory();

	std::vector<const std::vector<uint8_t>*> extents;
	for (const auto& e : rawDir) {
		if (e[0] == 0xE5 || e[0] == 0xFF) continue;
		if (((int)e[0] & 0x7F) != userNumber) continue; // mask bit7 -- see class comment
		if (isDirRecord(e)) continue;
		if (!matchesName(e.data(), normalized)) continue;
		extents.push_back(&e);
	}
	if (extents.empty()) return false;
	sortByExtentNo(extents);

	// Read every sector actually ALLOCATED to each extent (RC rounded up,
	// not down -- see class comment/computeFileSize()'s own, for why this
	// matters if RC is ever odd), then trim to the exact size at the end.
	// Matches ~/src/TOStools's own read_file_content()'s approach (read
	// every allocation_blocks entry in full, slice to size_in_bytes after)
	// rather than trying to compute a precise per-sector cutoff while
	// reading, which is what this function did before and is more fragile.
	out.clear();
	for (auto* ePtr : extents) {
		const auto& e = *ePtr;
		int rc = e[15];
		int numSectors = (rc + 1) / 2; // round up
		for (int s = 0; s < numSectors; s++) {
			int unit = e[16 + s / geo_.sectorsPerBlock];
			int sectorInUnit = s % geo_.sectorsPerBlock;
			int len = 0;
			uint8_t* p = blockSector(unit, sectorInUnit, len);
			if (!p) return false;
			out.insert(out.end(), p, p + len);
		}
	}

	size_t exact = computeFileSize(extents);
	if (exact < out.size()) out.resize(exact);
	return true;
}

bool TosFs::writeRawDirectoryEntry(int entryIndex, const uint8_t bytes[32])
{
	int entriesPerSector = geo_.sectorSize / 32;
	int sectorIndex = entryIndex / entriesPerSector;
	int offsetInSector = (entryIndex % entriesPerSector) * 32;
	int len = 0;
	uint8_t* p = logicalSector(sectorIndex, len);
	if (!p || offsetInSector + 32 > len) return false;
	memcpy(p + offsetInSector, bytes, 32);
	return true;
}

bool TosFs::deleteFile(const std::string& name, int userNumber)
{
	if (!isWritableGeometry()) return false;
	std::string normalized = normalizeName(name);
	auto rawDir = readRawDirectory();

	bool any = false;
	for (size_t i = 0; i < rawDir.size(); i++) {
		if (rawDir[i][0] == 0xE5 || rawDir[i][0] == 0xFF) continue;
		if (((int)rawDir[i][0] & 0x7F) != userNumber) continue;
		if (isDirRecord(rawDir[i])) continue; // F8 deletes FILES; rmdir needs its own
		                                        // emptiness-check convention, not
		                                        // confirmed against any real sample
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

bool TosFs::createDirectory(const std::string& name, int parentUserNumber, int& outDirNumber)
{
	if (!isWritableGeometry()) return false;
	std::string normalized = normalizeName(name);
	auto rawDir = readRawDirectory();

	int freeEntry = -1;
	int maxD = 0;
	for (size_t i = 0; i < rawDir.size(); i++) {
		const auto& e = rawDir[i];
		if (e[0] == 0xE5) { if (freeEntry < 0) freeEntry = (int)i; continue; }
		if (e[0] == 0xFF) continue;
		if (!isDirRecord(e)) continue;
		if (e[16] > maxD) maxD = e[16];
		if (((int)e[0] & 0x7F) == parentUserNumber && matchesName(e.data(), normalized)) {
			return false; // already exists under this parent
		}
	}
	if (freeEntry < 0) return false; // directory full

	int newD = maxD + 1;
	if (newD > 0x7F) return false; // would collide with the bit7 flag -- see class comment

	uint8_t entry[32] = {};
	entry[0] = (uint8_t)parentUserNumber;
	memcpy(entry + 1, normalized.data(), 11);
	// ext must be the literal text "DIR" per the format, regardless of
	// whatever normalizeName() computed from the caller's own name string.
	entry[9] = 'D';
	entry[10] = 'I';
	entry[11] = 'R';
	entry[16] = (uint8_t)newD;

	if (!writeRawDirectoryEntry(freeEntry, entry)) return false;
	if (!dsk_->flush()) return false;
	outDirNumber = newD;
	return true;
}
