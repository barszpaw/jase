#include "SDI_8255.h"
#include <cstring>

SDI_8255::SDI_8255()
{
	memset(eprom_[1], 0xff, sizeof(eprom_[1])); // erased-EPROM default, see class comment
	chip_.outC = [this](uint8_t v) {
		onPortCChanged(v);
		bus_.setAtn((v & 0x01) != 0);
		bus_.setControllerClk((v & 0x04) != 0);
		bus_.setControllerDat((v & 0x08) != 0);
		iec_.onControllerChanged(bus_, lastTstates_);
	};
	chip_.inC = [this]() {
		// Bits this doesn't drive (0-5, and 6/7 themselves when
		// released) read back as 1 -- floating/pulled-up, matching
		// real unconnected-TTL-input behavior.
		return (uint8_t)(0xff & ~((bus_.clk() ? 0x40 : 0) | (bus_.dat() ? 0x80 : 0)));
	};
	iec_.setDevice(&bridge_);
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

void SDI_8255::onInstructionFetch(uint16_t pc, MemoryMap& memory, uint32_t tstates)
{
	if (!enabled_) return;
	lastTstates_ = tstates;
	iec_.tick(bus_, tstates); // time-driven transitions (EOI timeout, ack release, idle-offer) need this even with no port write

	if (!romLoaded_) return;

	// Narrow SET condition (U5A+U2A, see class comment): M1 fetch
	// exactly in $3C00-$3FFF.
	if (!active_ && pc >= 0x3c00 && pc <= 0x3fff) {
		pageIn(memory);
	} else if (active_ && (pc < 0x3000 || pc > 0x3fff)) {
		// Narrowed back to SDI.cpp's own original release condition (see
		// class comment) -- release on leaving $3000-$3FFF, not the
		// broader $0000-$3FFF the schematic's own truth tables suggest.
		pageOut(memory);
	}
}

void SDI_8255::pageIn(MemoryMap& memory)
{
	if (active_) return;
	active_ = true;
	// Snapshot page 1's CURRENT lower half ($2000-$2FFF) instead of
	// overwriting it -- always the other half of the system ROM pair,
	// always read-only, so a snapshot copy can never go stale (same
	// reasoning the original SDI.cpp used). Page 0 is never touched.
	MemoryMap::Bank* currentPage1 = memory.getPage(1);
	memcpy(page1_.data, currentPage1->data, MemoryMap::PAGE_SIZE / 2);
	memcpy(page1_.data + MemoryMap::PAGE_SIZE / 2, eprom_[romBank1Selected_ ? 1 : 0], MemoryMap::PAGE_SIZE / 2);
	page1_.isRom = true;
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
	// Called when PC1 (bank select) changes while active -- updates only
	// page1_'s upper half ($3000-$3FFF); the lower half stays whatever
	// was snapshotted at pageIn() time. pageIn() itself does the initial
	// full setup (snapshot + upper half) directly, not through here.
	memcpy(page1_.data + MemoryMap::PAGE_SIZE / 2, eprom_[romBank1Selected_ ? 1 : 0], MemoryMap::PAGE_SIZE / 2);
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
	if (toks.empty())
		return std::string();

	if (toks[0] == "SDI8255") {
		char out[256];
		snprintf(out, sizeof(out),
		         "OK enabled=%d active=%d romBank=%d pa=%02x pb=%02x pc=%02x control=%02x",
		         enabled_ ? 1 : 0, active_ ? 1 : 0, romBank1Selected_ ? 1 : 0,
		         chip_.read(0), chip_.read(1), chip_.read(2), chip_.read(3));
		return out;
	}

	if (toks[0] == "IECBUS") {
		// Live bus-line state -- see IecBus's class comment. Deliberately
		// a non-stopping query (unlike a GDB breakpoint/watchpoint,
		// which would freeze the Z80's own T-state clock mid-handshake)
		// so it can be polled while the emulator keeps running.
		char out[256];
		snprintf(out, sizeof(out),
		         "OK atn=%d clk=%d dat=%d ctrlClk=%d ctrlDat=%d devClk=%d devDat=%d rxPhase=%d txPhase=%d role=%d",
		         bus_.atn() ? 1 : 0, bus_.clk() ? 1 : 0, bus_.dat() ? 1 : 0,
		         bus_.controllerClk() ? 1 : 0, bus_.controllerDat() ? 1 : 0,
		         bus_.deviceClk() ? 1 : 0, bus_.deviceDat() ? 1 : 0,
		         iec_.rxPhase(), iec_.txPhase(), iec_.role());
		return out;
	}

	return std::string();
}
