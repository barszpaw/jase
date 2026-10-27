#include "SDI_8255.h"
#include <cstring>
#include <SDL2/SDL.h>

SDI_8255::SDI_8255()
{
	memset(eprom_[1], 0xff, sizeof(eprom_[1])); // erased-EPROM default, see class comment
	chip_.outC = [this](uint8_t v) {
		onPortCChanged(v);
		bus_.setAtn((v & 0x01) != 0);
		bus_.setControllerClk((v & 0x04) != 0);
		bus_.setControllerDat((v & 0x08) != 0);
		iec_.onControllerChanged(bus_, lastTstates_);
		if (iectraceEnabled_) logIectrace("OUT", v);
	};
	chip_.inC = [this]() {
		// Bits this doesn't drive (0-5, and 6/7 themselves when
		// released) read back as 1 -- floating/pulled-up, matching
		// real unconnected-TTL-input behavior.
		uint8_t v = (uint8_t)(0xff & ~((bus_.clk() ? 0x40 : 0) | (bus_.dat() ? 0x80 : 0)));

		// Apply listener DAT override for DEVICE NOT PRESENT fix
		if (listenerDatOverride_) {
			// Force DAT line asserted (bit 7 = 0) when simulating device present
			v &= 0x7F;
		}

		if (iectraceEnabled_) logIectrace("IN ", v);
		return v;
	};
	iec_.setDevice(&bridge_);
}

// See setIectraceEnabled()'s comment in the header -- logs the
// RESOLVED bus state (not just the raw Port C byte) on every access,
// timestamped the same way INSTRTRACE is (seconds.microseconds since
// SDL init) so the two trace streams can be correlated directly.
void SDI_8255::logIectrace(const char* direction, uint8_t value)
{
	double ts = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
	fprintf(stderr,
	        "IECTRACE ts=%.6f pc=%04x %s pc_byte=%02x atn=%d ctrlClk=%d ctrlDat=%d devClk=%d devDat=%d "
	        "rxPhase=%d txPhase=%d role=%d bitCount=%d shiftByte=%02x secCmd=%d chan=%d txBitIndex=%d txByte=%02x\n",
	        ts, lastPc_, direction, value,
	        bus_.atn() ? 1 : 0, bus_.controllerClk() ? 1 : 0, bus_.controllerDat() ? 1 : 0,
		        bus_.deviceClk() ? 1 : 0, bus_.deviceDat() ? 1 : 0,
	        iec_.rxPhase(), iec_.txPhase(), iec_.role(), iec_.bitCount(), iec_.shiftByte(),
	        iec_.secondaryCmd(), iec_.activeChannel(), iec_.txBitIndex(), iec_.txByte());
}

bool SDI_8255::in(uint16_t port, uint8_t& value)
{
	// Validate that port is one of the SDI 8255 ports: $9F, $BF, $DF, $FF
    // These are the only ports that map to the PPI chip's PA, PB, PC, CONF registers
	if (!enabled_) return false;
	if ((port != 0x9F) && (port != 0xBF) && (port != 0xDF) && (port != 0xFF))
		return false;
	uint8_t reg = (port >> 5) & 3;
	value = chip_.read(reg); // A6,A5 -> 8255's own A1,A0
	if (iotraceEnabled_) {
		double ts = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
		fprintf(stderr, "IOTRACE ts=%.6f pc=%04x IN  port=%02x reg=%d -> %02x\n", ts, lastPc_, port, reg, value);
	}
	return true;
}

bool SDI_8255::out(uint16_t port, uint8_t value)
{
	// Validate that port is one of the SDI 8255 ports: $9F, $BF, $DF, $FF
    // These are the only ports that map to the PPI chip's PA, PB, PC, CONF registers
	if (!enabled_) return false;
	if ((port != 0x9F) && (port != 0xBF) && (port != 0xDF) && (port != 0xFF))
		return false;
	uint8_t reg = (port >> 5) & 3;
	chip_.write(reg, value);
	if (iotraceEnabled_) {
		double ts = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
		fprintf(stderr, "IOTRACE ts=%.6f pc=%04x OUT port=%02x reg=%d <- %02x\n", ts, lastPc_, port, reg, value);
	}
	return true;
}

void SDI_8255::onInstructionFetch(uint16_t pc, MemoryMap& memory, uint32_t tstates)
{
	if (!enabled_) return;
	lastTstates_ = tstates;
	lastPc_ = pc;
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
	// Handle PC1 (bank select) changes while active -- updates only
	// page1_'s upper half ($3000-$3FFF); the lower half stays whatever
	// was snapshotted at pageIn() time. pageIn() itself does the initial
	// full setup (snapshot + upper half) directly, not through here.
	bool bank1 = (value & 0x02) != 0; // PC1 -> EPROM A12
	if (bank1 != romBank1Selected_) {
		romBank1Selected_ = bank1;
		if (active_) rebuildRomWindows();
	}

	// Update listener DAT override for DEVICE NOT PRESENT fix
	// When ATN is asserted (bit 0 = 0 in real hardware, but bus_.atn() is inverted)
	// and bridge is connected, simulate device presence.
	// bus_.atn() is true when asserted.
	if (bus_.atn() && bridge_.isConnected()) {
		listenerDatOverride_ = true;
	} else {
		listenerDatOverride_ = false;
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
		char out[320];
		snprintf(out, sizeof(out),
		         "OK atn=%d clk=%d dat=%d ctrlClk=%d ctrlDat=%d devClk=%d devDat=%d "
		         "rxPhase=%d txPhase=%d role=%d bitCount=%d shiftByte=%02x secCmd=%d chan=%d txBitIndex=%d txByte=%02x",
		         bus_.atn() ? 1 : 0, bus_.clk() ? 1 : 0, bus_.dat() ? 1 : 0,
		         bus_.controllerClk() ? 1 : 0, bus_.controllerDat() ? 1 : 0,
		         bus_.deviceClk() ? 1 : 0, bus_.deviceDat() ? 1 : 0,
		         iec_.rxPhase(), iec_.txPhase(), iec_.role(),
		         iec_.bitCount(), iec_.shiftByte(), iec_.lastByte(), iec_.secondaryCmd(),
		         iec_.txBitIndex(), iec_.txByte());
		return out;
	}

	if (toks[0] == "IECNAME") {
		// Diagnostic-only (not a stable API): what OPEN actually saw,
		// and the bridge's own last reply line -- for tracing why
		// requestRead() isn't producing data.
		char out[320];
		snprintf(out, sizeof(out),
		         "OK channel=%d connected=%d name=\"%s\" lastReply=\"%s\"",
		         iec_.activeChannel(), bridge_.isConnected() ? 1 : 0,
		         iec_.nameBuf().c_str(), bridge_.lastReply().c_str());
		return out;
	}

	if (toks[0] == "IECTRACE") {
		// Same ON/OFF convention as INSTRTRACE (minzx.cpp) -- see
		// setIectraceEnabled()'s header comment for what this logs and
		// why it was worth rebuilding. No address range here (unlike
		// INSTRTRACE): every Port C access IS the thing worth tracing,
		// there's no "outside the window of interest" to exclude.
		if (toks.size() >= 2) {
			if (toks[1] == "ON") setIectraceEnabled(true);
			else if (toks[1] == "OFF") setIectraceEnabled(false);
		}
		char out[64];
		snprintf(out, sizeof(out), "OK iectrace=%d", isIectraceEnabled() ? 1 : 0);
		return out;
	}

	if (toks[0] == "IOTRACE") {
		if (toks.size() >= 2) {
			if (toks[1] == "ON") setIotraceEnabled(true);
			else if (toks[1] == "OFF") setIotraceEnabled(false);
		}
		char out[64];
		snprintf(out, sizeof(out), "OK iotrace=%d", isIotraceEnabled() ? 1 : 0);
		return out;
	}

	return std::string();
}
