#include "TosFs.h"
#include <algorithm>
#include <cstring>

bool TosFs::open(DskImage& dsk)
{
	if (!dsk.loaded()) return false;
	if (dsk.numSides() != 1) return false; // only the 170/320KB single-sided shape, see class comment
	dsk_ = &dsk;
	geo_ = Geometry();
	return true;
}

uint8_t* TosFs::blockSector(int blockNumber, int sectorInBlock, int& len) const
{
	int unitsPerTrack = geo_.sectorsPerTrack / geo_.sectorsPerBlock; // 4
	int track = geo_.reservedTracks + blockNumber / unitsPerTrack;
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

// R (byte14*256+byte15) and L (byte13) together give the exact file size --
// see TosFs.h's own class comment for how this was cross-validated against
// two real samples. `extents` must already be sorted by O (byte12).
size_t computeFileSize(const std::vector<const std::vector<uint8_t>*>& extents)
{
	size_t total = 0;
	for (size_t i = 0; i < extents.size(); i++) {
		const auto& e = *extents[i];
		int r = (e[14] << 8) | e[15];
		int numSectors = r / 2;
		bool isLast = (i + 1 == extents.size());
		if (!isLast) {
			total += (size_t)numSectors * 256;
		} else if (numSectors > 0) {
			total += (size_t)(numSectors - 1) * 256;
			int l = e[13];
			total += (l == 0) ? 256 : (size_t)l; // L==0 on a real last extent is unconfirmed
			                                       // against any sample (see class comment) --
			                                       // treated as "last sector full" rather than
			                                       // "zero real bytes", the only sane reading.
		}
	}
	return total;
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
		if ((int)e[0] != userNumber) continue;

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
		if ((int)e[0] != userNumber) continue;
		if (isDirRecord(e)) continue;
		if (!matchesName(e.data(), normalized)) continue;
		extents.push_back(&e);
	}
	if (extents.empty()) return false;

	std::sort(extents.begin(), extents.end(), [](const std::vector<uint8_t>* a, const std::vector<uint8_t>* b) {
		return (*a)[12] < (*b)[12]; // O, extent/sequence number
	});

	out.clear();
	for (size_t i = 0; i < extents.size(); i++) {
		const auto& e = *extents[i];
		int r = (e[14] << 8) | e[15];
		int numSectors = r / 2;
		bool isLast = (i + 1 == extents.size());

		for (int s = 0; s < numSectors; s++) {
			int unit = e[16 + s / geo_.sectorsPerBlock];
			int sectorInUnit = s % geo_.sectorsPerBlock;
			int len = 0;
			uint8_t* p = blockSector(unit, sectorInUnit, len);
			if (!p) return false;

			size_t realBytes = (size_t)len;
			if (isLast && s == numSectors - 1) {
				int l = e[13];
				realBytes = (l == 0) ? (size_t)len : std::min((size_t)l, (size_t)len);
			}
			out.insert(out.end(), p, p + realBytes);
		}
	}
	return true;
}
