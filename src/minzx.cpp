#include "minzx.h"
#include "z80.h"
#include "Kempston.h"
#include "AyPsg.h"
#include "ZiFi.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <strings.h>

#define TRACE  printf
#define DEBUG  printf
#define LOG    printf
#define INFO   printf
#define NOTICE printf
#define WARN   printf
#define ERROR  printf
#define FATAL  printf


#define VAL_BRIGHT 255
#define VAL_NO_BRIGHT 176

int g_tracePort = -1;

// Full ZX Spectrum keyboard matrix, scancode -> (row-select hi-byte mask,
// bit within that row). Mirrors the per-row checks in processInputPort's
// port 0xFE handler exactly — used by pressKeys()'s forced-key override so
// scripted/auto-typed input can land on any key, not just the handful a
// hand-written switch happened to cover.
struct KeyMapEntry { SDL_Scancode sc; uint8_t hiMask; uint8_t loBit; };
static const KeyMapEntry kKeyMap[] = {
	{ SDL_SCANCODE_LSHIFT, 0x01, 0x01 }, { SDL_SCANCODE_RSHIFT, 0x01, 0x01 },
	{ SDL_SCANCODE_Z, 0x01, 0x02 }, { SDL_SCANCODE_X, 0x01, 0x04 },
	{ SDL_SCANCODE_C, 0x01, 0x08 }, { SDL_SCANCODE_V, 0x01, 0x10 },
	{ SDL_SCANCODE_A, 0x02, 0x01 }, { SDL_SCANCODE_S, 0x02, 0x02 },
	{ SDL_SCANCODE_D, 0x02, 0x04 }, { SDL_SCANCODE_F, 0x02, 0x08 }, { SDL_SCANCODE_G, 0x02, 0x10 },
	{ SDL_SCANCODE_Q, 0x04, 0x01 }, { SDL_SCANCODE_W, 0x04, 0x02 },
	{ SDL_SCANCODE_E, 0x04, 0x04 }, { SDL_SCANCODE_R, 0x04, 0x08 }, { SDL_SCANCODE_T, 0x04, 0x10 },
	{ SDL_SCANCODE_1, 0x08, 0x01 }, { SDL_SCANCODE_2, 0x08, 0x02 },
	{ SDL_SCANCODE_3, 0x08, 0x04 }, { SDL_SCANCODE_4, 0x08, 0x08 }, { SDL_SCANCODE_5, 0x08, 0x10 },
	{ SDL_SCANCODE_0, 0x10, 0x01 }, { SDL_SCANCODE_9, 0x10, 0x02 },
	{ SDL_SCANCODE_8, 0x10, 0x04 }, { SDL_SCANCODE_7, 0x10, 0x08 }, { SDL_SCANCODE_6, 0x10, 0x10 },
	{ SDL_SCANCODE_P, 0x20, 0x01 }, { SDL_SCANCODE_O, 0x20, 0x02 },
	{ SDL_SCANCODE_I, 0x20, 0x04 }, { SDL_SCANCODE_U, 0x20, 0x08 }, { SDL_SCANCODE_Y, 0x20, 0x10 },
	{ SDL_SCANCODE_RETURN, 0x40, 0x01 }, { SDL_SCANCODE_L, 0x40, 0x02 },
	{ SDL_SCANCODE_K, 0x40, 0x04 }, { SDL_SCANCODE_J, 0x40, 0x08 }, { SDL_SCANCODE_H, 0x40, 0x10 },
	{ SDL_SCANCODE_SPACE, 0x80, 0x01 }, { SDL_SCANCODE_LALT, 0x80, 0x02 }, { SDL_SCANCODE_RALT, 0x80, 0x02 },
	{ SDL_SCANCODE_M, 0x80, 0x04 }, { SDL_SCANCODE_N, 0x80, 0x08 }, { SDL_SCANCODE_B, 0x80, 0x10 },
};
static const int kKeyMapSize = sizeof(kKeyMap) / sizeof(kKeyMap[0]);

uint32_t speColors[16];

static void createSpectrumColors()
{
	uint32_t A = 0xFF000000;
	uint32_t r = VAL_NO_BRIGHT << 16;
	uint32_t g = VAL_NO_BRIGHT << 8;
	uint32_t b = VAL_NO_BRIGHT;
	uint32_t R = VAL_BRIGHT << 16;
	uint32_t G = VAL_BRIGHT << 8;
	uint32_t B = VAL_BRIGHT;
	speColors[ 0] = A            ;
	speColors[ 1] = A |         b;
	speColors[ 2] = A | r        ;
	speColors[ 3] = A | r |     b;
	speColors[ 4] = A |     g    ;
	speColors[ 5] = A |     g | b;
	speColors[ 6] = A | r | g    ;
	speColors[ 7] = A | r | g | b;
	speColors[ 8] = A;
	speColors[ 9] = A |         B;
	speColors[10] = A | R        ;
	speColors[11] = A | R |     B;
	speColors[12] = A |     G    ;
	speColors[13] = A |     G | B;
	speColors[14] = A | R | G    ;
	speColors[15] = A | R | G | B;
}

static uint32_t zxColor(int c, bool b)
{
	c = c & 7;
	if (b) c += 8;
	return speColors[c];
}


bool MinZX::setMachine(const char* name)
{
	if (0 == strcasecmp(name, "48k")) {
		machine_ = MachineType::Spectrum48K;
		romPaths_ = { "zx48.rom" };
	} else if (0 == strcasecmp(name, "tc2048")) {
		machine_ = MachineType::TimexTC2048;
		romPaths_ = { "/usr/share/fusex/tc2048.rom" };
	} else if (0 == strcasecmp(name, "128k")) {
		machine_ = MachineType::Spectrum128K;
		romPaths_ = { "/usr/share/fusex/128-0.rom", "/usr/share/fusex/128-1.rom" };
	} else {
		return false;
	}
	return true;
}

void MinZX::init()
{
	z80   = new Z80(this);

	int numRomBanks, numRamBanks;
	switch (machine_) {
		case MachineType::TimexTC2048:
			// 1 ROM pair (2x8K); RAM: standard 6 (48K) + a full 8-bank DOCK
			// pool so real DOCK-paging software has something to page in.
			numRomBanks = 2;
			numRamBanks = 6 + kDockBankCount;
			break;
		case MachineType::Spectrum128K:
			// 2 ROM pairs (editor + 48K BASIC); RAM: 8 full 16K banks, each
			// as 2x8K sub-banks -- bank N lives at ramBank(2N)/ramBank(2N+1).
			numRomBanks = 4;
			numRamBanks = 16;
			break;
		default: // Spectrum48K
			numRomBanks = 2;
			numRamBanks = 6;
			break;
	}
	memory_.init(numRomBanks, numRamBanks);
	ports = new uint8_t[0x10000];

	memset(ports, 0xFF, 0x10000);

	cycleTstates = 69888;
	loadROM();
	setupInitialPaging();

	createSpectrumColors();
	initAudio();

	peripherals_.push_back(new Kempston());
	ay_ = new AyPsg();
	peripherals_.push_back(ay_);
	zifi_ = new ZiFi();
	peripherals_.push_back(zifi_);

	reset();
}

void MinZX::initAudio()
{
	SDL_AudioSpec want, have;
	SDL_zero(want);
	want.freq = kSampleRate;
	want.format = AUDIO_U8;
	want.channels = 1;
	want.samples = 1024;
	audioDev_ = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (audioDev_ == 0) {
		ERROR("SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
		return;
	}
	audioBuf_.assign(kSamplesPerFrame, beeperLevel_);
	SDL_PauseAudioDevice(audioDev_, 0);
}

// Fills audioBuf_ up to the sample matching the CPU's current tstates with
// the level that was active until now, then switches to newLevel from that
// point on — i.e. a real square-wave edge at its correct position in time.
void MinZX::audioWriteEdge(uint8_t newLevel)
{
	if (audioDev_ == 0) return;
	uint32_t idx = (uint32_t)(((uint64_t)tstates * kSampleRate) / 3500000ULL);
	if (idx > (uint32_t)kSamplesPerFrame) idx = kSamplesPerFrame;
	for (uint32_t i = audioSampleIndex_; i < idx; i++)
		audioBuf_[i] = beeperLevel_;
	audioSampleIndex_ = idx;
	beeperLevel_ = newLevel;
}

uint32_t MinZX::getQueuedAudioBytes() const
{
	return audioDev_ ? SDL_GetQueuedAudioSize(audioDev_) : 0;
}

void MinZX::flushAudioFrame()
{
	if (audioDev_ == 0) return;
	for (uint32_t i = audioSampleIndex_; i < (uint32_t)kSamplesPerFrame; i++)
		audioBuf_[i] = beeperLevel_;
	audioSampleIndex_ = 0;
	if (ay_)
		ay_->generate(audioBuf_.data(), kSamplesPerFrame, kSampleRate);
	// Drop this frame's audio if the device's queue is already backed up
	// (e.g. the host loop briefly outran real time) rather than let
	// latency grow unbounded.
	if (SDL_GetQueuedAudioSize(audioDev_) < (Uint32)(kSamplesPerFrame * 4))
		SDL_QueueAudio(audioDev_, audioBuf_.data(), audioBuf_.size());
}

bool MinZX::saveScreenshotBMP(const char* path) const
{
	if (!lastScreenBuf_) return false;

	const int w = SCREEN_W, h = SCREEN_H;
	const uint32_t pixelBytes = (uint32_t)w * h * 4;
	const uint32_t fileSize = 54 + pixelBytes;

	FILE* f = fopen(path, "wb");
	if (!f) return false;

	uint8_t header[54] = {0};
	header[0] = 'B'; header[1] = 'M';
	*(uint32_t*)(header + 2)  = fileSize;
	*(uint32_t*)(header + 10) = 54; // pixel data offset
	*(uint32_t*)(header + 14) = 40; // DIB header size
	*(int32_t*)(header + 18)  = w;
	*(int32_t*)(header + 22)  = -h; // negative = top-down, matches generateScreen's row order
	*(uint16_t*)(header + 26) = 1;  // planes
	*(uint16_t*)(header + 28) = 32; // bpp
	*(uint32_t*)(header + 34) = pixelBytes;

	fwrite(header, 1, 54, f);
	// Our ARGB8888 uint32s are already stored in memory as B,G,R,A bytes
	// (little-endian), which is exactly BMP's 32bpp pixel layout -- no
	// conversion needed.
	fwrite(lastScreenBuf_, 1, pixelBytes, f);
	fclose(f);
	return true;
}

void MinZX::reset()
{
	border = 7;
	z80->reset();

	ports[0x001F] = 0;
	ports[0x011F] = 0;
}

void MinZX::update(uint8_t* screen)
{
	lastScreenBuf_ = screen;
	tstates = 0;
	uint32_t prevTstates = 0;
	justHitBreakpoint_ = false;
	advanceAutoType();
	if (zifi_) zifi_->pump();

	while (tstates < cycleTstates)
	{
		// Fast-load hook fires before anything else, regardless of
		// breakpoints — it's not a debug feature, it's standing in for
		// a cassette deck. Doesn't cost any tstates (treated as instant,
		// same simplification every fast-loading emulator makes).
		if (tapPos_ < tapBlocks_.size() && z80->getRegPC() == 0x0556)
		{
			checkFastLoad();
			continue;
		}
		// Check AFTER executing, not before: if we're resuming from a
		// breakpoint we're currently sitting on, checking first would
		// re-trigger immediately without ever advancing — CONT would
		// look like it does nothing. Executing first means a breakpoint
		// always stops you at the NEXT instruction, same as a real
		// debugger's "continue".
		z80->execute();
		prevTstates = tstates;
		if (breakpointsEnabled_ && isBreakpoint(z80->getRegPC()))
		{
			justHitBreakpoint_ = true;
			lastBreakpointPC_ = z80->getRegPC();
			break;
		}
	}

	tapeAdvance(tstates);

	generateScreen(screen);
	flushAudioFrame();

	// Only valid when the frame actually completed — an early breakpoint
	// exit left tstates well under cycleTstates, and subtracting would
	// underflow this unsigned counter into a huge garbage value.
	if (!justHitBreakpoint_)
		tstates -= cycleTstates;
}

bool MinZX::isBreakpoint(uint16_t pc) const
{
	for (uint16_t bp : breakpoints_)
		if (bp == pc) return true;
	return false;
}

void MinZX::addBreakpoint(uint16_t pc)
{
	if (!isBreakpoint(pc)) breakpoints_.push_back(pc);
	breakpointsEnabled_ = !breakpoints_.empty();
}

void MinZX::removeBreakpoint(uint16_t pc)
{
	for (size_t i = 0; i < breakpoints_.size(); i++)
	{
		if (breakpoints_[i] == pc) { breakpoints_.erase(breakpoints_.begin() + i); break; }
	}
	breakpointsEnabled_ = !breakpoints_.empty();
}

void MinZX::step(int n)
{
	// Same execute-then-check order as update(), and for the same
	// reason: STEP is almost always issued while sitting exactly on a
	// breakpoint (that's how you got stopped), so checking first would
	// never actually advance.
	justHitBreakpoint_ = false;
	for (int i = 0; i < n; i++)
	{
		if (tapPos_ < tapBlocks_.size() && z80->getRegPC() == 0x0556)
		{
			checkFastLoad();
			continue;
		}
		z80->execute();
		if (breakpointsEnabled_ && isBreakpoint(z80->getRegPC()))
		{
			justHitBreakpoint_ = true;
			lastBreakpointPC_ = z80->getRegPC();
			break;
		}
	}
}

void MinZX::destroy()
{
	if (audioDev_ != 0)
		SDL_CloseAudioDevice(audioDev_);
	for (Peripheral* p : peripherals_)
		delete p;
	peripherals_.clear();
	delete z80;
	memory_.destroy();
}

void MinZX::loadROM()
{
	// Each 16K file loads into one ROM bank pair: file i -> romBank(2i)/
	// romBank(2i+1). 48K/TC2048 have one file (the editor/BASIC pair for
	// 128K is two separate 16K files, not one 32K file -- matches how
	// fusex ships them, /usr/share/fusex/128-0.rom + 128-1.rom).
	for (size_t i = 0; i < romPaths_.size(); i++) {
		FILE* pf = fopen(romPaths_[i].c_str(), "rb");
		if (pf == NULL) {
			ERROR("Cannot load ROM from file\n");
			continue;
		}
		// Read into a scratch buffer first, not directly into the ROM
		// banks: MemoryMap::Bank's two banks aren't guaranteed contiguous
		// (struct padding after the data[] array), so a single fread
		// spanning both would rely on layout outside the class's contract.
		uint8_t romBuf[0x4000];
		size_t elemsRead = fread(romBuf, 1, 0x4000, pf);
		fclose(pf);
		memcpy(memory_.romBank(2 * i).data, romBuf, 0x2000);
		memcpy(memory_.romBank(2 * i + 1).data, romBuf + 0x2000, 0x2000);
	}
}

void MinZX::setupInitialPaging()
{
	switch (machine_) {
		case MachineType::Spectrum128K:
			// page0-1: ROM pair 0 (editor) -- bit4=0 is the soft-reset
			// default. page2-3: RAM bank 5, page4-5: RAM bank 2 -- these
			// two NEVER move, unlike TC2048's sequential HOME. page6-7:
			// RAM bank 0 (bits2:0=0 is the soft-reset default).
			memory_.mapPage(0, &memory_.romBank(0));
			memory_.mapPage(1, &memory_.romBank(1));
			memory_.mapPage(2, &memory_.ramBank(10)); // bank 5 low half
			memory_.mapPage(3, &memory_.ramBank(11)); // bank 5 high half
			memory_.mapPage(4, &memory_.ramBank(4));  // bank 2 low half
			memory_.mapPage(5, &memory_.ramBank(5));  // bank 2 high half
			memory_.mapPage(6, &memory_.ramBank(0));  // bank 0 low half
			memory_.mapPage(7, &memory_.ramBank(1));  // bank 0 high half
			break;
		default: // Spectrum48K, TimexTC2048: sequential ROM+RAM
			for (int page = 0; page < MemoryMap::NUM_PAGES; page++)
				memory_.mapHome(page);
			break;
	}
}

bool MinZX::loadDockCartridge(const char* path)
{
	FILE* pf = fopen(path, "rb");
	if (!pf) { ERROR("loadDockCartridge: cannot open file\n"); return false; }

	uint8_t header[9];
	if (fread(header, 1, 9, pf) != 9) { fclose(pf); return false; }
	// header[0]: bank ID (0=DOCK, 254=EXROM, 255=HOME) -- only DOCK is
	// meaningful here, the pool this loads into IS the DOCK bank pool.
	for (int page = 0; page < 8; page++) {
		uint8_t type = header[1 + page];
		MemoryMap::Bank& bank = memory_.ramBank(6 + page);
		if (type == 2 || type == 3) { // ROM or RAM: data follows
			if (fread(bank.data, 1, 0x2000, pf) != 0x2000) { fclose(pf); return false; }
		} else { // NULL or RAM_EMPTY: no data in the file, zero it
			memset(bank.data, 0x00, 0x2000);
		}
	}
	fclose(pf);
	return true;
}

#define HBMP 192
#define WBOR 32
#define HBOR 24

// BGRA

#define ULA_SWAP(y) ((y & 0xC0) | ((y & 0x38) >> 3) | ((y & 0x07) << 3))

// Timex SCLD video modes 0/1/2/6, ported from pico-spec-drewpo's
// Video.cpp/Ports.cpp (hardware-confirmed there, matches Fuse). Real SCLD
// hardware decodes all four from the SAME standard $4000-$7FFF 16K already
// present on a 64K machine: "screen 1" is just that area +0x2000, so no
// extra memory/banking is needed for any of these. Modes 3/4/5/7 need a
// second physical bank ("bank 7") only reachable via DOCK/MMU paging (port
// 0xF4) and aren't implemented yet -- see minzx.h.
//
// The content area is always rendered at full 512px width (SCREEN_W = 32
// border + 512 + 32 border). Mode 6 (true hi-res) draws 16 genuinely
// distinct samples per byte-pair (8 from screen 0, 8 from screen 1,
// "columns taken alternately" per Timex's own docs -- confirmed against
// pico-spec's packed-pair driver code, which processes a whole source byte
// at a time the same way). Modes 0/1/2 stay at native 256px resolution and
// pixel-double each sample to fill the same 512px span, so geometry never
// has to change at runtime when a guest flips hi-res on/off mid-frame.
void MinZX::generateScreen(uint8_t* screen)
{
	uint32_t* dptr = (uint32_t*)screen;

	uint32_t borderColor = zxColor(border, 0);

	uint8_t mode = (machine_ == MachineType::TimexTC2048) ? timexMode_ : 0;
	bool hiRes = (mode == 6);

	// Standard-mode (mode 0/1-fallback) screen source: 48K/TC2048 always
	// read RAM bank 0 (TC2048's own screen0 convention happens to be the
	// same bank). 128K's screen can be bank 5 or bank 7 (port 0x7FFD bit
	// 3) -- read directly from the physical bank regardless of what's
	// currently paged at $4000 or $C000, same "renderer bypasses the MMU"
	// rule as everywhere else here.
	int stdScreenRamBank = 0;
	if (machine_ == MachineType::Spectrum128K)
		stdScreenRamBank = (spectrum128ScreenBank_ == 7) ? 14 : 10;

	uint32_t hrInkColor = 0, hrPapColor = 0;
	if (hiRes) {
		hrInkColor = zxColor(timexInk_ & 7, true);
		hrPapColor = zxColor((~timexInk_) & 7, true);
		// Real SCLD/Fuse behaviour: border = the bright PAPER colour in
		// mode 6, not the independent ULA border register -- ported from
		// pico-spec-drewpo's updateBorderBrd() (caught there the same way:
		// a black/grey border where the reference showed yellow).
		borderColor = hrPapColor;
	}

	for (int scrY = 0; scrY < SCREEN_H; scrY++)
	{
		if (scrY < HBOR || scrY >= HBOR + HBMP)
		{
			for (int scrX = 0; scrX < SCREEN_W; scrX++)
				*dptr++ = borderColor;
			continue;
		}

		int speY = scrY - HBOR;
		int ulaY = ULA_SWAP(speY);
		int bmpRowOff = ulaY << 5;
		int attRowOff = ((speY >> 3) << 5) + 0x1800;

		// RAM bank 0/1 are screen0/screen1 -- read directly, bypassing
		// whatever the CPU currently has paged at $4000/$6000 (same as real
		// SCLD/ULA hardware, which has its own fixed physical connection to
		// these banks regardless of the Z80's MMU view). bmpOff/bmp2Off are
		// always a within-bank offset (0-0x1FFF) now that each screen is
		// its own 8K Bank rather than +0x2000 into one flat 16K region.
		//
		// bmp2: mode 0/1 -> that bitmap's own attributes; mode 2 -> 8x1
		// hi-colour attributes (reuses the bitmap addressing, not offAtt);
		// mode 6 -> screen 1's bitmap (no attributes at all -- ink/paper
		// are the global port-0xFF colour, not per-cell).
		MemoryMap::Bank *bmpBank, *bmp2Bank;
		int bmpOff, bmp2Off;
		switch (mode) {
			case 1:  bmpBank = &memory_.ramBank(1); bmpOff = bmpRowOff;
			         bmp2Bank = &memory_.ramBank(1); bmp2Off = attRowOff; break;
			case 2:  bmpBank = &memory_.ramBank(0); bmpOff = bmpRowOff;
			         bmp2Bank = &memory_.ramBank(1); bmp2Off = bmpRowOff; break;
			case 6:  bmpBank = &memory_.ramBank(0); bmpOff = bmpRowOff;
			         bmp2Bank = &memory_.ramBank(1); bmp2Off = bmpRowOff; break;
			default: bmpBank = &memory_.ramBank(stdScreenRamBank); bmpOff = bmpRowOff;
			         bmp2Bank = &memory_.ramBank(stdScreenRamBank); bmp2Off = attRowOff; break;
		}

		uint8_t* bmpptr  = bmpBank->data  + bmpOff;
		uint8_t* bmp2ptr = bmp2Bank->data + bmp2Off;

		for (int scrX = 0; scrX < WBOR; scrX++)
			*dptr++ = borderColor;

		for (int col = 0; col < 32; col++)
		{
			if (hiRes)
			{
				// Each byte-column contributes two adjacent 8px groups:
				// screen 0's byte first, then screen 1's byte -- NOT bit-by-
				// bit interleaved. Confirmed against PortFF.tap (real Timex
				// demo, TimexHiResExample) vs its own reference SpecEmu
				// screenshot: it draws "A" at screen0 column 0, "B" at
				// screen1 column 0, "C" at screen0 column 1, and the
				// reference shows three separate legible letters "ABC" in a
				// row -- which only matches this grouping, not a per-pixel
				// alternation (that would merge A and B into one glyph).
				uint8_t e = *bmpptr++;
				uint8_t o = *bmp2ptr++;
				uint8_t b = e;
				for (int n = 0; n < 8; n++, b <<= 1)
					*dptr++ = (b & 0x80) ? hrInkColor : hrPapColor;
				b = o;
				for (int n = 0; n < 8; n++, b <<= 1)
					*dptr++ = (b & 0x80) ? hrInkColor : hrPapColor;
			}
			else
			{
				uint8_t bmp = *bmpptr++;
				uint8_t att = *bmp2ptr++;

				int ink = (att)      & 7;
				int pap = (att >> 3) & 7;
				int bri = (att >> 6) & 1;
				uint32_t fore = zxColor(ink, bri);
				uint32_t back = zxColor(pap, bri);

				uint8_t b = bmp;
				for (int n = 0; n < 8; n++, b <<= 1)
				{
					uint32_t c = (b & 0x80) ? fore : back;
					*dptr++ = c;
					*dptr++ = c; // pixel-double: fills the 512px content area
				}
			}
		}

		for (int scrX = 0; scrX < WBOR; scrX++)
			*dptr++ = borderColor;
	}
}

uint8_t MinZX::processInputPort(uint16_t port)
{
	uint8_t hiport = port >> 8;
	uint8_t loport = port & 0xFF;

	// Expansion devices get first refusal on every port — the ULA's own
	// port-0xFE functions below are intrinsic to the machine, not a
	// peripheral, so they're not in this list (see Peripheral.h).
	for (Peripheral* p : peripherals_) {
		uint8_t value;
		if (p->in(port, value))
			return value;
	}

	if (loport == 0xFE) {
		uint8_t result = 0xFF;

		// EAR_PIN -- real tape-pulse engine (see tapeCheckPoll()), not the
		// old always-1 stub: that EAR bit never toggling is exactly what
		// let the ROM's own pilot-tone wait loop spin forever (confirmed
		// 2026-10-15 on a real commercial 128K tape that doesn't go
		// through the 0x0556 fast-load hook for its pilot/sync stage).
		// Also deliberately NOT gated on hiport==0xFE: real tape-reading
		// code addresses this port with whatever high byte it already has
		// in A for other reasons, not necessarily 0xFE.
		if (tapeCheckPoll(z80->getRegPC()))
			result |= 0x40;
		else
			result &= (uint8_t)~0x40;

		// Keyboard: 8 half-rows selected by clear bits of the port's high
		// byte (standard ZX Spectrum ULA convention — real hardware ANDs
		// together any rows whose select line is low, so multiple rows can
		// be read at once; we only ever need one at a time here). Each row
		// returns 5 keys in bits 0-4, active low (0 = pressed).
		const Uint8* ks = SDL_GetKeyboardState(NULL);

		if (!(hiport & 0x01)) { // 0xFEFE: SHIFT, Z, X, C, V
			if (ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT]) result &= ~0x01;
			if (ks[SDL_SCANCODE_Z]) result &= ~0x02;
			if (ks[SDL_SCANCODE_X]) result &= ~0x04;
			if (ks[SDL_SCANCODE_C]) result &= ~0x08;
			if (ks[SDL_SCANCODE_V]) result &= ~0x10;
		}
		if (!(hiport & 0x02)) { // 0xFDFE: A, S, D, F, G
			if (ks[SDL_SCANCODE_A]) result &= ~0x01;
			if (ks[SDL_SCANCODE_S]) result &= ~0x02;
			if (ks[SDL_SCANCODE_D]) result &= ~0x04;
			if (ks[SDL_SCANCODE_F]) result &= ~0x08;
			if (ks[SDL_SCANCODE_G]) result &= ~0x10;
		}
		if (!(hiport & 0x04)) { // 0xFBFE: Q, W, E, R, T
			if (ks[SDL_SCANCODE_Q]) result &= ~0x01;
			if (ks[SDL_SCANCODE_W]) result &= ~0x02;
			if (ks[SDL_SCANCODE_E]) result &= ~0x04;
			if (ks[SDL_SCANCODE_R]) result &= ~0x08;
			if (ks[SDL_SCANCODE_T]) result &= ~0x10;
		}
		if (!(hiport & 0x08)) { // 0xF7FE: 1, 2, 3, 4, 5
			if (ks[SDL_SCANCODE_1]) result &= ~0x01;
			if (ks[SDL_SCANCODE_2]) result &= ~0x02;
			if (ks[SDL_SCANCODE_3]) result &= ~0x04;
			if (ks[SDL_SCANCODE_4]) result &= ~0x08;
			if (ks[SDL_SCANCODE_5]) result &= ~0x10;
		}
		if (!(hiport & 0x10)) { // 0xEFFE: 0, 9, 8, 7, 6
			if (ks[SDL_SCANCODE_0]) result &= ~0x01;
			if (ks[SDL_SCANCODE_9]) result &= ~0x02;
			if (ks[SDL_SCANCODE_8]) result &= ~0x04;
			if (ks[SDL_SCANCODE_7]) result &= ~0x08;
			if (ks[SDL_SCANCODE_6]) result &= ~0x10;
		}
		if (!(hiport & 0x20)) { // 0xDFFE: P, O, I, U, Y
			if (ks[SDL_SCANCODE_P]) result &= ~0x01;
			if (ks[SDL_SCANCODE_O]) result &= ~0x02;
			if (ks[SDL_SCANCODE_I]) result &= ~0x04;
			if (ks[SDL_SCANCODE_U]) result &= ~0x08;
			if (ks[SDL_SCANCODE_Y]) result &= ~0x10;
		}
		if (!(hiport & 0x40)) { // 0xBFFE: ENTER, L, K, J, H
			if (ks[SDL_SCANCODE_RETURN]) result &= ~0x01;
			if (ks[SDL_SCANCODE_L]) result &= ~0x02;
			if (ks[SDL_SCANCODE_K]) result &= ~0x04;
			if (ks[SDL_SCANCODE_J]) result &= ~0x08;
			if (ks[SDL_SCANCODE_H]) result &= ~0x10;
		}
		if (!(hiport & 0x80)) { // 0x7FFE: SPACE, SYM SHIFT, M, N, B
			if (ks[SDL_SCANCODE_SPACE]) result &= ~0x01;
			if (ks[SDL_SCANCODE_LALT] || ks[SDL_SCANCODE_RALT]) result &= ~0x02;
			if (ks[SDL_SCANCODE_M]) result &= ~0x04;
			if (ks[SDL_SCANCODE_N]) result &= ~0x08;
			if (ks[SDL_SCANCODE_B]) result &= ~0x10;
		}

		// pressKeys() override: forces specific keys down regardless of
		// real SDL state, through this SAME matrix path a human/xdotool
		// press already goes through — not a separate ROM-register-contract
		// shortcut, since that would mean re-deriving internal ROM state
		// the already-proven human-typed path sidesteps entirely.
		if (autoTypeIdx_ < autoTypeSeq_.size()) {
			for (SDL_Scancode sc : autoTypeSeq_[autoTypeIdx_].keys) {
				for (int i = 0; i < kKeyMapSize; i++) {
					if (kKeyMap[i].sc == sc && !(hiport & kKeyMap[i].hiMask))
						result &= ~kKeyMap[i].loBit;
				}
			}
		}

		return result;
	}
	// Sound (AY-3-8912)

	uint8_t data = 0;
	data |= (0xe0); /* Set bits 5-7 - as reset above */
	data &= ~0x40;
	// Serial.printf("Port %x%x  Data %x\n", portHigh,loport,data);
	return data;
}


void MinZX::processOutputPort(uint16_t port, uint8_t value)
{
	uint8_t hiport = port >> 8;
	uint8_t loport = port & 0xFF;

	for (Peripheral* p : peripherals_) {
		if (p->out(port, value))
			return;
	}

	// 128K paging (port 0x7FFD) isn't low-byte-only decoded -- real
	// hardware only checks bit15=0, bit1=0, bit0=1 (ZX Next ports.txt),
	// so it can't be a case in the switch(loport) below.
	if (machine_ == MachineType::Spectrum128K && (port & 0x8003) == 0x0001) {
		// Once locked, a write has NO effect at all on real hardware --
		// including on what the port "reads back as" (get7FFDRaw() is for
		// debug/STATE reporting, and must reflect the configuration that's
		// actually active, not the last attempted write; it had been
		// updated unconditionally, which made a post-lock OUT look like it
		// changed the RAM bank in STATE's output when the real page table
		// -- confirmed unchanged via MEM -- hadn't moved at all).
		if (!spectrum128Locked_) {
			memory_.mapPage(0, value & 0x10 ? &memory_.romBank(2) : &memory_.romBank(0));
			memory_.mapPage(1, value & 0x10 ? &memory_.romBank(3) : &memory_.romBank(1));
			int ramBankSel = value & 0x07;
			memory_.mapPage(6, &memory_.ramBank(2 * ramBankSel));
			memory_.mapPage(7, &memory_.ramBank(2 * ramBankSel + 1));
			spectrum128ScreenBank_ = (value & 0x08) ? 7 : 5;
			spectrum128Paging_ = value;
			if (value & 0x20) spectrum128Locked_ = true;
		}
		return;
	}

	uint8_t tmp_data = value;
	switch (loport) {
		case 0xFE: {

			// delayMicroseconds(CONTENTION_TIME);

			// border color (no bright colors)
			border = value & 0x07;

			audioWriteEdge((value & 0x10) ? 148 : 108);

#ifdef SPEAKER_PRESENT
			digitalWrite(SPEAKER_PIN, bitRead(data, 4)); // speaker
#endif

#ifdef MIC_PRESENT
			digitalWrite(MIC_PIN, bitRead(data, 3)); // tape_out
#endif

			//Ports::base[0x20] = data;
			break;
		}
		case 0xFF: {
			// Timex SCLD video mode register -- real hardware decodes this
			// on all 8 low address bits regardless of the high byte
			// (confirmed against spectrusty's scld.rs). Absent on real 48K
			// hardware (no SCLD chip there), so gated on machine_ to leave
			// other machines' behavior untouched.
			if (machine_ != MachineType::TimexTC2048) break;
			timexMode_ = value & 0x07;
			timexInk_  = (value >> 3) & 0x07;
			break;
		}
		case 0xF4: {
			// Timex DOCK/MMU paging: bit N (0-7) selects the 8K page at
			// address N*0x2000. 0 = HOME (standard ROM/RAM for that page),
			// 1 = DOCK bank N from the pool past the standard 6 RAM banks
			// (ramBank(6+N)..ramBank(6+kDockBankCount-1), see init()). Real
			// SCLD also supports an EX-ROM source (ctrl port 0xFF bit 7) --
			// not implemented, DOCK is the only paged source for now.
			if (machine_ != MachineType::TimexTC2048) break;
			uint8_t diff = value ^ timexPagedMask_;
			for (int page = 0; page < MemoryMap::NUM_PAGES; page++) {
				if (!(diff & (1 << page))) continue;
				if (value & (1 << page))
					memory_.mapPage(page, &memory_.ramBank(6 + page));
				else
					memory_.mapHome(page);
			}
			timexPagedMask_ = value;
			break;
		}
	}
}

// sequence of wait states
static unsigned char wait_states[8] = { 6, 5, 4, 3, 2, 1, 0, 0 };

// delay contention: emulates wait states introduced by the ULA (graphic chip)
// whenever there is a memory access to contended memory (shared between ULA and CPU).
// detailed info: https://worldofspectrum.org/faq/reference/48kreference.htm#ZXSpectrum
// from paragraph which starts with "The 50 Hz interrupt is synchronized with..."
// if you only read from https://worldofspectrum.org/faq/reference/48kreference.htm#Contention
// without reading the previous paragraphs about line timings, it may be confusing.
unsigned char delay_contention(uint16_t address, unsigned int tstates)
{
	// delay states one t-state BEFORE the first pixel to be drawn
	tstates += 1;

	// each line spans 224 t-states
	int line = tstates / 224;

	// only the 192 lines between 64 and 255 have graphic data, the rest is border
	if (line < 64 || line >= 256) return 0;

	// only the first 128 t-states of each line correspond to a graphic data transfer
	// the remaining 96 t-states correspond to border
	int halfpix = tstates % 224;
	if (halfpix >= 128) return 0;

	int modulo = halfpix % 8;
	return wait_states[modulo];
}

#define ADDRESS_IN_LOW_RAM(addr) (1 == (addr >> 14))

/* Read opcode from RAM */
uint8_t MinZX::fetchOpcode(uint16_t address)
{
	// 3 clocks to fetch opcode from RAM and 1 execution clock
	if (ADDRESS_IN_LOW_RAM(address))
		tstates += delay_contention(address, tstates);

	tstates += 4;
	return memory_.peek8(address);
}

/* Read/Write byte from/to RAM */
uint8_t MinZX::peek8(uint16_t address)
{
	// 3 clocks for read byte from RAM
	if (ADDRESS_IN_LOW_RAM(address))
		tstates += delay_contention(address, tstates);

	tstates += 3;
	return memory_.peek8(address);
}

void MinZX::poke8(uint16_t address, uint8_t value)
{
	// 3 clocks for write byte to RAM
	if (ADDRESS_IN_LOW_RAM(address))
		tstates += delay_contention(address, tstates);

	tstates += 3;
	memory_.poke8(address, value);
}

/* Read/Write word from/to RAM */
uint16_t MinZX::peek16(uint16_t address)
{
	// Order matters, first read lsb, then read msb, don't "optimize"
	uint8_t lsb = peek8(address);
	uint8_t msb = peek8(address + 1);
	return (msb << 8) | lsb;
}

void MinZX::poke16(uint16_t address, RegisterPair word)
{
	// Order matters, first write lsb, then write msb, don't "optimize"
	poke8(address, word.byte8.lo);
	poke8(address + 1, word.byte8.hi);
}

/* In/Out byte from/to IO Bus */
uint8_t MinZX::inPort(uint16_t port)
{
	// 4 clocks for read byte from bus
	tstates += 3;

	uint8_t result = processInputPort(port);

	if (g_tracePort == port) {
		static uint8_t lastTraced = 0xFF;
		if (result != lastTraced) {
			printf("PORT %04x = %02x\n", port, result);
			fflush(stdout);
			lastTraced = result;
		}
	}

	return result;
}

void MinZX::outPort(uint16_t port, uint8_t value)
{
	// 4 clocks for write byte to bus
	tstates += 4;

	// ports[port] = value;
	processOutputPort(port, value);
}

/* Put an address on bus lasting 'tstates' cycles */
void MinZX::addressOnBus(uint16_t address, int32_t wstates)
{
	// Additional clocks to be added on some instructions
	if (ADDRESS_IN_LOW_RAM(address)) {
		for (int idx = 0; idx < wstates; idx++) {
			tstates += delay_contention(address, tstates) + 1;
		}
	}
	else
		tstates += wstates;
}

/* Clocks needed for processing INT and NMI */
void MinZX::interruptHandlingTime(int32_t wstates)
{
	tstates += wstates;
}

/* Callback to know when the INT signal is active */
bool MinZX::isActiveINT(void)
{
	// The ULA holds /INT low for 32 T-states at the start of each 50Hz
	// frame. Z80::execute() polls this every instruction boundary and
	// fires the interrupt itself when it's true — without it, nothing
	// timed off the ROM's interrupt handler (keyboard scan via LAST-K,
	// FRAMES counter, etc.) ever runs, even though raw port reads work.
	return tstates < 32;
}

#ifdef WITH_BREAKPOINT_SUPPORT
/* Callback for notify at PC address */
uint8_t MinZX::breakpoint(uint16_t address, uint8_t opcode)
{
}

#endif

#ifdef WITH_EXEC_DONE
/* Callback to notify that one instruction has ended */
void MinZX::execDone(void)
{
}
#endif

void MinZX::loadTapeBlocks(const std::vector<std::vector<uint8_t>>& blocks)
{
	tapBlocks_ = blocks;
	tapPos_ = 0;
	tapePhase_ = TapePhase::Stopped;
	tapeEarBit_ = false;
	tapeLoopPC_ = 0;
	tapeLoopCount_ = 0;
}

// 15 frames held / 10 released per chord (300ms/200ms at 50Hz): long
// enough for the ROM's interrupt-driven keyboard scan to register it,
// comfortably under its ~35-frame REPDEL auto-repeat threshold (a
// 100-frame hold was tested and DID trigger real ROM auto-repeat,
// corrupting the line with repeated characters — don't go that long).
void MinZX::pressKeys(const std::vector<std::vector<SDL_Scancode>>& chords)
{
	autoTypeSeq_.clear();
	for (const auto& chord : chords) {
		autoTypeSeq_.push_back({ chord, 15 });
		autoTypeSeq_.push_back({ {}, 10 });
	}
	autoTypeIdx_ = 0;
	autoTypeFrameCounter_ = 0;
}

void MinZX::startAutoTypeLoadCommand()
{
	pressKeys({
		{SDL_SCANCODE_J},                        // LOAD keyword
		{SDL_SCANCODE_LALT, SDL_SCANCODE_P},      // first "
		{SDL_SCANCODE_LALT, SDL_SCANCODE_P},      // second "
		{SDL_SCANCODE_RETURN},
	});
	// Prepend a boot-settle delay (100 frames / 2s): a chord queued from
	// frame 0 lands DURING the ROM's own boot/copyright-message routine,
	// before the keyboard scan loop is actually listening, and is long
	// released by the time it is — confirmed by testing (a 2s hold spans
	// past boot and registers; short holds starting at frame 0 don't
	// register at all). A debug client calling PRESSKEYS directly doesn't
	// need this — it can check STATE/REGS first to know boot is done.
	autoTypeSeq_.insert(autoTypeSeq_.begin(), { {}, 100 });
}

void MinZX::advanceAutoType()
{
	if (autoTypeIdx_ >= autoTypeSeq_.size())
		return;
	autoTypeFrameCounter_++;
	if (autoTypeFrameCounter_ >= autoTypeSeq_[autoTypeIdx_].frames)
	{
		autoTypeFrameCounter_ = 0;
		autoTypeIdx_++;
	}
}

// Standard 48K ROM LD-BYTES contract: A = expected flag byte, DE = length,
// IX = destination, Carry set on entry = LOAD (vs VERIFY, treated the same
// here). We skip the bit-banged tape routine entirely and inject the next
// block directly, then simulate the RET this call would have ended with.
void MinZX::checkFastLoad()
{
	if (tapPos_ >= tapBlocks_.size())
		return;

	// This block is about to be delivered instantly -- stop the real-pulse
	// engine so it doesn't go on simulating pilot/sync/data for a block
	// that's already gone (it may have been the one that got this PC
	// reached in the first place, via its pilot/sync tone; tapPos_ below
	// now points past it either way, so the engine's next loop-detected
	// start will correctly begin the NEXT block from a clean Stopped state).
	tapePhase_ = TapePhase::Stopped;
	tapeLoopCount_ = 0;

	const std::vector<uint8_t>& block = tapBlocks_[tapPos_];
	tapPos_++;

	if (block.empty())
		return;

	uint8_t expectedFlag = z80->getRegA();
	uint8_t actualFlag = block[0];

	if (actualFlag == expectedFlag)
	{
		uint16_t de = z80->getRegDE();
		uint16_t ix = z80->getRegIX();
		// Payload excludes the leading flag byte (already checked) and
		// the trailing checksum byte (only needed for the real ROM's
		// own verification, which we're bypassing).
		size_t payloadLen = block.size() >= 2 ? block.size() - 2 : 0;
		size_t copyLen = (size_t)de < payloadLen ? (size_t)de : payloadLen;
		for (size_t i = 0; i < copyLen; i++)
			memory_.poke8((uint16_t)(ix + i), block[1 + i]);
		z80->setRegIX((uint16_t)(ix + copyLen));
		z80->setRegDE((uint16_t)(de - copyLen));
		z80->setCarryFlag(true);
	}
	else
	{
		z80->setCarryFlag(false);
	}

	uint16_t sp = z80->getRegSP();
	uint16_t retAddr = peek16(sp);
	z80->setRegSP((uint16_t)(sp + 2));
	z80->setRegPC(retAddr);
}

// Standard ZX Spectrum ROM tape timing (T-states at 3.5MHz) -- these are
// not guesses, they're the well-known constants the ROM's own SA/LD-BYTES
// routine uses, confirmed against pico-spec-drewpo's Tape.cpp.
static const uint32_t kTapePilotPulse  = 2168;
static const uint32_t kTapePilotHeader = 8063; // flag byte bit7==0
static const uint32_t kTapePilotData   = 3223; // flag byte bit7==1
static const uint32_t kTapeSync1       = 667;
static const uint32_t kTapeSync2       = 735;
static const uint32_t kTapeBit0Pulse   = 855;
static const uint32_t kTapeBit1Pulse   = 1710;
static const uint32_t kTapePauseTStates = 3500000; // ~1s gap between blocks

void MinZX::tapeStartBlock()
{
	if (tapPos_ >= tapBlocks_.size()) { tapePhase_ = TapePhase::Stopped; return; }
	const std::vector<uint8_t>& block = tapBlocks_[tapPos_];
	if (block.empty()) { tapPos_++; tapePhase_ = TapePhase::Stopped; return; }

	uint8_t flag = block[0];
	tapePilotPulsesLeft_ = (flag & 0x80) ? kTapePilotData : kTapePilotHeader;
	tapeByteIdx_ = 0;
	tapeBitMask_ = 0x80;
	tapeBitHalf_ = 0;
	tapeEarBit_ = true;
	tapeEdgeCountdown_ = kTapePilotPulse;
	tapePhase_ = TapePhase::Pilot;
}

// Called whenever tapeEdgeCountdown_ reaches zero: toggles the EAR bit and
// either continues the current phase or advances to the next one (pilot ->
// sync -> data bit-by-bit -> pause -> next block's pilot, looping through
// every block in tapBlocks_ the same way a real cassette just keeps
// rolling). The fast-load hook (checkFastLoad(), 0x0556) normally cuts
// this short partway into Data once the ROM re-syncs on real pulses --
// this full state machine only has to run to completion for loaders that
// never call through that exact address.
void MinZX::tapeNextEdge()
{
	switch (tapePhase_)
	{
		case TapePhase::Pilot:
			tapeEarBit_ = !tapeEarBit_;
			if (tapePilotPulsesLeft_ > 0) tapePilotPulsesLeft_--;
			if (tapePilotPulsesLeft_ == 0) {
				tapePhase_ = TapePhase::Sync;
				tapeBitHalf_ = 0;
				tapeEdgeCountdown_ = kTapeSync1;
			} else {
				tapeEdgeCountdown_ = kTapePilotPulse;
			}
			break;

		case TapePhase::Sync:
			tapeEarBit_ = !tapeEarBit_;
			if (tapeBitHalf_ == 0) {
				tapeBitHalf_ = 1;
				tapeEdgeCountdown_ = kTapeSync2;
			} else {
				tapePhase_ = TapePhase::Data;
				tapeByteIdx_ = 0;
				tapeBitMask_ = 0x80;
				tapeBitHalf_ = 0;
				bool bit = (tapBlocks_[tapPos_][0] & tapeBitMask_) != 0;
				tapeEdgeCountdown_ = bit ? kTapeBit1Pulse : kTapeBit0Pulse;
			}
			break;

		case TapePhase::Data: {
			tapeEarBit_ = !tapeEarBit_;
			const std::vector<uint8_t>& block = tapBlocks_[tapPos_];
			if (tapeBitHalf_ == 0) {
				tapeBitHalf_ = 1;
				bool bit = (block[tapeByteIdx_] & tapeBitMask_) != 0;
				tapeEdgeCountdown_ = bit ? kTapeBit1Pulse : kTapeBit0Pulse;
			} else {
				tapeBitHalf_ = 0;
				tapeBitMask_ >>= 1;
				if (tapeBitMask_ == 0) {
					tapeBitMask_ = 0x80;
					tapeByteIdx_++;
				}
				if (tapeByteIdx_ >= block.size()) {
					tapPos_++;
					tapePhase_ = TapePhase::Pause;
					tapeEarBit_ = false;
					tapeEdgeCountdown_ = kTapePauseTStates;
				} else {
					bool bit = (block[tapeByteIdx_] & tapeBitMask_) != 0;
					tapeEdgeCountdown_ = bit ? kTapeBit1Pulse : kTapeBit0Pulse;
				}
			}
			break;
		}

		case TapePhase::Pause:
			// Real tape just keeps rolling into the next block rather than
			// going idle -- only a loop-detected poll restarts playback
			// from a genuinely Stopped state (first block, or one the fast-
			// load hook already consumed).
			tapeStartBlock();
			break;

		case TapePhase::Stopped:
			break;
	}
}

void MinZX::tapeAdvance(uint32_t deltaT)
{
	while (deltaT > 0 && tapePhase_ != TapePhase::Stopped) {
		if (tapeEdgeCountdown_ > deltaT) {
			tapeEdgeCountdown_ -= deltaT;
			return;
		}
		deltaT -= tapeEdgeCountdown_;
		tapeNextEdge();
	}
}

// Generic "this looks like a custom loader reading a tape edge" heuristic,
// ported from pico-spec-drewpo's Tape.cpp isTapeEdgePoll: the instruction
// right before pc is IN A,(n) (0xDB) and pc itself is a bit-test that
// would plausibly follow one (RRA/RLCA, a masking AND, or BIT 6,A) --
// deliberately narrow so ordinary keyboard scanning (different opcodes
// after its own IN) never matches.
bool MinZX::tapeLooksLikeEdgePoll(uint16_t pc) const
{
	if (pc < 0x4000) return false;
	if (memory_.peek8((uint16_t)(pc - 2)) != 0xDB) return false;
	uint8_t nextOp = memory_.peek8(pc);
	if (nextOp == 0x1F || nextOp == 0x07) return true; // RRA / RLCA
	if (nextOp == 0xE6 && (memory_.peek8((uint16_t)(pc + 1)) & 0x60)) return true; // AND n
	if (nextOp == 0xCB && memory_.peek8((uint16_t)(pc + 1)) == 0x77) return true; // BIT 6,A
	return false;
}

// Called on every IN A,(0xFE) (see processInputPort): a real cassette deck
// doesn't know the guest wants it playing -- it starts turning once
// something is actually listening. Detects that by watching for the same
// PC reading this port over and over (a polling loop, by definition) at an
// address that looks like tape-edge code, and only then starts real pulse
// playback. Ported from pico-spec-drewpo's Tape::TapePortRead(), minus the
// per-game Cerikopik/Jumping-Jack signature shortcuts -- this is the
// general path those are an optimization on top of, not a replacement for.
bool MinZX::tapeCheckPoll(uint16_t pc)
{
	if (tapePhase_ == TapePhase::Stopped) {
		bool looksLikePoll = (pc >= 0x05E3 && pc <= 0x05F5) || tapeLooksLikeEdgePoll(pc);
		if (looksLikePoll && tapPos_ < tapBlocks_.size()) {
			if (pc == tapeLoopPC_) {
				if (++tapeLoopCount_ > 200) {
					tapeLoopCount_ = 0;
					tapeStartBlock();
				}
			} else {
				tapeLoopPC_ = pc;
				tapeLoopCount_ = 1;
			}
		}
	}
	return tapeEarBit_;
}
