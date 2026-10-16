#include "Multiface.h"
#include <cstdio>
#include <cstring>

Multiface::Multiface()
{
	initPlaceholderRom();
}

// Hand-assembled, clean-room (not a copy of the real copyrighted
// Multiface ROM) -- just enough to be externally observable:
//   0066: 3E 03         LD A, 0x03        ; magenta border
//   0068: D3 FE         OUT (0xFE), A
//   006A: 3E 18         LD A, 0x18        ; BORDCR format is colour<<3 -- see below
//   006C: 32 48 5C      LD (0x5C48), A
//   006F: 3E AA         LD A, 0xAA        ; marker byte, for MEM/PEEK confirmation
//   0071: 32 00 80      LD (0x8000), A
//   0074: ED 45         RETN              ; resume whatever NMI interrupted
// The BORDCR write is the non-obvious part: confirmed empirically (on an
// idle 48K boot, MEM 5c48 1 read back 0x38 = 7<<3) that the ROM's own
// idle-loop keyboard click-sound code periodically re-asserts the border
// from this system variable -- which stomped a bare OUT-only border
// write back to border 7 within one frame, long before any debug-port
// poll could ever observe it. Updating BORDCR too makes the colour
// actually stick instead of just flashing for a fraction of a frame.
// No page-out-via-port in this placeholder ROM: it relies entirely on
// onInstructionFetch() noticing PC has left the window instead (see
// pageOut()) -- an OUT- or port-read-triggered unmap executed from the
// SECOND-to-last instruction would take effect immediately, swapping
// memory out from under the CPU before it can fetch RETN's own two
// bytes from the very page that just unmapped itself. A *real* Multiface
// ROM's exit sequence is laid out so that doesn't matter (its last
// access to port 0x1F is arranged to be the actual last thing it does);
// ours doesn't bother, since RETN already works via the window-exit check.
void Multiface::initPlaceholderRom()
{
	static const uint8_t code[] = {
		0x3E, 0x03, 0xD3, 0xFE, 0x3E, 0x18, 0x32, 0x48, 0x5C, 0x3E, 0xAA, 0x32, 0x00, 0x80, 0xED, 0x45
	};
	memset(rom_.data, 0x00, MemoryMap::PAGE_SIZE);
	memcpy(rom_.data + 0x0066, code, sizeof(code));
	rom_.isRom = true;

	memset(ram_.data, 0x00, MemoryMap::PAGE_SIZE);
	ram_.isRom = false;
}

bool Multiface::loadRom(const char* path)
{
	FILE* pf = fopen(path, "rb");
	if (pf == NULL) {
		fprintf(stderr, "Multiface::loadRom: cannot open %s\n", path);
		return false;
	}
	uint8_t buf[MemoryMap::PAGE_SIZE];
	size_t n = fread(buf, 1, sizeof(buf), pf);
	fclose(pf);
	if (n != sizeof(buf)) {
		fprintf(stderr, "Multiface::loadRom: %s is %zu bytes, expected exactly %d\n",
		        path, n, MemoryMap::PAGE_SIZE);
		return false;
	}
	memcpy(rom_.data, buf, sizeof(buf));
	rom_.isRom = true;
	printf("Multiface::loadRom: loaded real Multiface ONE ROM from %s\n", path);
	return true;
}

void Multiface::pageIn(MemoryMap& memory)
{
	if (savedPage0_) return; // already active
	savedPage0_ = memory.getPage(0);
	savedPage1_ = memory.getPage(1);
	memory.mapPage(0, &rom_);
	memory.mapPage(1, &ram_);
}

void Multiface::pageOut(MemoryMap& memory)
{
	if (!savedPage0_) return; // not active
	memory.mapPage(0, savedPage0_);
	memory.mapPage(1, savedPage1_);
	savedPage0_ = nullptr;
	savedPage1_ = nullptr;
}

void Multiface::onNMI()
{
	if (enabled_) pending_ = true;
}

// See class comment for the full timing story -- pending_ is set by
// onNMI(), consumed here the moment PC==0x0066 appears (before that
// address's opcode is fetched); the window-exit half only fires for
// that same NMI path (autoUnpage_), never for a page-in that came from
// a direct IN A,(0x9F) instead.
void Multiface::onInstructionFetch(uint16_t pc, MemoryMap& memory)
{
	if (!enabled_) return;

	// Port-triggered paging, deferred one step from in() -- see the
	// field comments in Multiface.h for why. Checked first so a port
	// page-in on the same step as an NMI-triggered one (an edge case,
	// not real usage) doesn't fight over which wins.
	if (pageOutPending_) { pageOutPending_ = false; pageOut(memory); }
	if (pageInPending_)  { pageInPending_  = false; pageIn(memory); }

	if (pending_ && pc == 0x0066) {
		pending_ = false;
		pageIn(memory);
		autoUnpage_ = true;
		return;
	}
	if (autoUnpage_ && savedPage0_ && pc >= 0x4000) {
		pageOut(memory);
		autoUnpage_ = false;
	}
}

// IN A,(0x9F) / IN A,(0x1F) -- real hardware's documented, NMI-
// independent software paging path (confirmed against fusex's
// multiface_port_in1(), which does exactly this on the same two ports).
// Deliberately always returns false: never claims/supplies the byte
// value itself, since 0x1F is ALSO Kempston's joystick port on real
// hardware -- a genuine, documented conflict between the two interfaces,
// not a bug -- and MinZX now calls every peripheral's in() for side
// effects regardless of who else already claimed the port (see
// Peripheral.h/processInputPort()), so Kempston keeps working
// unaffected while this still gets to page in/out.
bool Multiface::in(uint16_t port, uint8_t& value)
{
	(void)value;
	if (!enabled_) return false;

	uint8_t loport = port & 0xFF;
	// Needs the real MemoryMap, handed in only via onInstructionFetch()
	// -- deferred via the pending-port flags below, consumed on the very
	// next instruction fetch (same one-instruction-later pattern as the
	// NMI path, and just as harmless: nothing observable happens until
	// MinZX calls onInstructionFetch() right after this IN completes).
	if (loport == 0x1F) pageOutPending_ = true;
	else if (loport == 0x9F) pageInPending_ = true;
	return false;
}

std::string Multiface::debugCommand(const std::vector<std::string>& toks)
{
	if (toks.empty() || toks[0] != "MULTIFACE1")
		return std::string();

	char out[96];
	snprintf(out, sizeof(out), "OK enabled=%d active=%d",
	         enabled_ ? 1 : 0, savedPage0_ ? 1 : 0);
	return out;
}
