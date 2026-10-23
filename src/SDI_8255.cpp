#include "SDI_8255.h"
#include <cstring>

SDI_8255::SDI_8255()
{
	memset(eprom_[1], 0xff, sizeof(eprom_[1])); // erased-EPROM default, see class comment
	chip_.outC = [this](uint8_t v) { onPortCChanged(v); };
}

bool SDI_8255::in(uint16_t port, uint8_t& value)
{
	if (!enabled_) return false;
	if ((port & 0x9f) != 0x9f) return false; // U3A: A0-A4,A7 must all be 1
	value = chip_.read((port >> 5) & 3); // A6,A5 -> 8255's own A1,A0
	return true;
}

bool SDI_8255::out(uint16_t port, uint8_t value)
{
	if (!enabled_) return false;
	if ((port & 0x9f) != 0x9f) return false;
	chip_.write((port >> 5) & 3, value);
	return true;
}

void SDI_8255::onInstructionFetch(uint16_t pc, MemoryMap& memory)
{
	if (!enabled_ || !romLoaded_) return;

	// Narrow SET condition (U5A+U2A, see class comment): M1 fetch
	// exactly in $3C00-$3FFF.
	if (!active_ && pc >= 0x3c00 && pc <= 0x3fff) {
		pageIn(memory);
	} else if (active_ && pc > 0x3fff) {
		// Broad RESET condition -- see class comment on why this is an
		// inference (CPU has moved on from the whole $0000-$3FFF window),
		// not a directly-confirmed schematic fact the way the SET side is.
		pageOut(memory);
	}
}

void SDI_8255::pageIn(MemoryMap& memory)
{
	if (active_) return;
	active_ = true;
	rebuildRomWindows();
	memory.mapPage(0, &page0_);
	memory.mapPage(1, &page1_);
	memory.setRomcsAsserted(true);
}

void SDI_8255::pageOut(MemoryMap& memory)
{
	if (!active_) return;
	active_ = false;
	memory.setRomcsAsserted(false);
}

void SDI_8255::onPortCChanged(uint8_t value)
{
	bool bank1 = (value & 0x02) != 0; // PC1 -> EPROM A12
	if (bank1 != romBank1Selected_) {
		romBank1Selected_ = bank1;
		if (active_) rebuildRomWindows();
	}
}

void SDI_8255::rebuildRomWindows()
{
	const uint8_t* bank = eprom_[romBank1Selected_ ? 1 : 0];
	// The EPROM only has A0-A11 from the Z80 bus (see class comment) --
	// its 4K content repeats twice within each 8K MemoryMap page.
	memcpy(page0_.data, bank, 0x1000);
	memcpy(page0_.data + 0x1000, bank, 0x1000);
	memcpy(page1_.data, bank, 0x1000);
	memcpy(page1_.data + 0x1000, bank, 0x1000);
	page0_.isRom = true;
	page1_.isRom = true;
}

bool SDI_8255::loadRom(const char* path)
{
	FILE* pf = fopen(path, "rb");
	if (!pf) {
		fprintf(stderr, "SDI_8255::loadRom: cannot open %s\n", path);
		return false;
	}
	size_t n = fread(eprom_[0], 1, sizeof(eprom_[0]), pf);
	fclose(pf);
	if (n != sizeof(eprom_[0])) {
		fprintf(stderr, "SDI_8255::loadRom: %s is %zu bytes, expected exactly %zu\n",
		        path, n, sizeof(eprom_[0]));
		return false;
	}
	romLoaded_ = true;
	printf("SDI_8255::loadRom: loaded SDI103J ROM (bank 0) from %s\n", path);
	return true;
}

std::string SDI_8255::debugCommand(const std::vector<std::string>& toks)
{
	if (toks.empty() || toks[0] != "SDI8255")
		return std::string();

	char out[256];
	snprintf(out, sizeof(out),
	         "OK enabled=%d active=%d romBank=%d pa=%02x pb=%02x pc=%02x control=%02x",
	         enabled_ ? 1 : 0, active_ ? 1 : 0, romBank1Selected_ ? 1 : 0,
	         chip_.read(0), chip_.read(1), chip_.read(2), chip_.read(3));
	return out;
}
