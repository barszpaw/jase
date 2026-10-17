#include "filemgr.h"
#include "minzx.h"

#define _CRT_SECURE_NO_WARNINGS 1

#include <stdio.h>
#include <string.h>
#include <vector>

#define TRACE  printf
#define DEBUG  printf
#define LOG    printf
#define INFO   printf
#define NOTICE printf
#define WARN   printf
#define ERROR  printf
#define FATAL  printf

#define SNA_48K_SIZE 49179

static uint16_t fgetWordLE(FILE* pf) {
	uint8_t lo = fgetc(pf);
	uint8_t hi = fgetc(pf);
	return lo | (hi << 8);
}

static void fputWordLE(uint16_t word, FILE* pf) {
	fputc(word & 0xFF, pf);
	fputc((word >> 8) & 0xFF, pf);
}

bool FileMgr::loadSNA(const char* filename, MinZX* targetEmulator)
{
	if (NULL == filename || '\0' == *filename) {
		WARN("FileMgr::loadSNA: no filename specified\n");
		return false;
	}

	if (NULL == targetEmulator) {
		WARN("FileMgr::loadSNA: no target emulator\n");
		return false;
	}

	FILE* pf = fopen(filename, "rb");
	if (pf == NULL) {
		WARN("FileMgr::loadSNA: cannot open file %s\n", filename);
		return false;
	}

	fseek(pf, 0, SEEK_END);
	size_t filesize = ftell(pf);
	if (filesize != SNA_48K_SIZE) {
		WARN("FileMgr::loadSNA: bad size %u for file %s, should be %u", filesize, filename, SNA_48K_SIZE);
		return false;
	}
	fseek(pf, 0, SEEK_SET);

	Z80* z80 = targetEmulator->getCPU();

	targetEmulator->reset();

	// Read in the registers
	z80->setRegI(fgetc(pf));
	z80->setRegLx(fgetc(pf));
	z80->setRegHx(fgetc(pf));
	z80->setRegEx(fgetc(pf));
	z80->setRegDx(fgetc(pf));
	z80->setRegCx(fgetc(pf));
	z80->setRegBx(fgetc(pf));
	z80->setRegFx(fgetc(pf));
	z80->setRegAx(fgetc(pf));

	//_zxCpu.i = lhandle.read();
	//_zxCpu.registers.byte[Z80_L] = lhandle.read();
	//_zxCpu.registers.byte[Z80_H] = lhandle.read();
	//_zxCpu.registers.byte[Z80_E] = lhandle.read();
	//_zxCpu.registers.byte[Z80_D] = lhandle.read();
	//_zxCpu.registers.byte[Z80_C] = lhandle.read();
	//_zxCpu.registers.byte[Z80_B] = lhandle.read();
	//_zxCpu.registers.byte[Z80_F] = lhandle.read();
	//_zxCpu.registers.byte[Z80_A] = lhandle.read();

	//_zxCpu.alternates[Z80_HL] = _zxCpu.registers.word[Z80_HL];
	//_zxCpu.alternates[Z80_DE] = _zxCpu.registers.word[Z80_DE];
	//_zxCpu.alternates[Z80_BC] = _zxCpu.registers.word[Z80_BC];
	//_zxCpu.alternates[Z80_AF] = _zxCpu.registers.word[Z80_AF];

	z80->setRegL(fgetc(pf));
	z80->setRegH(fgetc(pf));
	z80->setRegE(fgetc(pf));
	z80->setRegD(fgetc(pf));
	z80->setRegC(fgetc(pf));
	z80->setRegB(fgetc(pf));
	z80->setRegIY(fgetWordLE(pf));
	z80->setRegIX(fgetWordLE(pf));

	//_zxCpu.registers.byte[Z80_L] = lhandle.read();
	//_zxCpu.registers.byte[Z80_H] = lhandle.read();
	//_zxCpu.registers.byte[Z80_E] = lhandle.read();
	//_zxCpu.registers.byte[Z80_D] = lhandle.read();
	//_zxCpu.registers.byte[Z80_C] = lhandle.read();
	//_zxCpu.registers.byte[Z80_B] = lhandle.read();
	//_zxCpu.registers.byte[Z80_IYL] = lhandle.read();
	//_zxCpu.registers.byte[Z80_IYH] = lhandle.read();
	//_zxCpu.registers.byte[Z80_IXL] = lhandle.read();
	//_zxCpu.registers.byte[Z80_IXH] = lhandle.read();

	uint8_t inter = fgetc(pf);
	z80->setIFF2(inter & 0x04 ? 1 : 0);
	z80->setRegR(fgetc(pf));

	z80->setFlags(fgetc(pf));
	z80->setRegA(fgetc(pf));

	z80->setRegSP(fgetWordLE(pf));

	z80->setIM((Z80::IntMode)fgetc(pf));
	targetEmulator->setBorderColor(fgetc(pf));

	z80->setIFF1(z80->isIFF2());
	
	//byte inter = lhandle.read();
	//_zxCpu.iff2 = (inter & 0x04) ? 1 : 0;
	//_zxCpu.r = lhandle.read();

	//_zxCpu.registers.byte[Z80_F] = lhandle.read();
	//_zxCpu.registers.byte[Z80_A] = lhandle.read();

	//sp_l = lhandle.read();
	//sp_h = lhandle.read();
	//_zxCpu.registers.word[Z80_SP] = sp_l + sp_h * 0x100;

	//_zxCpu.im = lhandle.read();
	//byte bordercol = lhandle.read();

	//ESPectrum::borderColor = bordercol;

	//_zxCpu.iff1 = _zxCpu.iff2;

	uint16_t bytesToRead = 0xC000;
	uint16_t offset = 0x4000;
	while (bytesToRead--) {
		targetEmulator->poke8(offset++, (uint8_t)fgetc(pf));
	}
	

	uint16_t SP = z80->getRegSP();
	uint16_t retaddr = targetEmulator->peek16(SP);

	SP += 2;
	z80->setRegSP(SP);

	z80->setRegPC(retaddr);

	fclose(pf);

	//uint16_t buf_p = 0x4000;
	//while (lhandle.available()) {
	//	writebyte(buf_p, lhandle.read());
	//	buf_p++;
	//}

	//uint16_t thestack = _zxCpu.registers.word[Z80_SP];
	//retaddr = readword(thestack);
	//Serial.printf("%x\n", retaddr);
	//_zxCpu.registers.word[Z80_SP]++;
	//_zxCpu.registers.word[Z80_SP]++;

	//lhandle.close();

	//_zxCpu.pc = retaddr;

	return true;
}

bool FileMgr::loadTAP(const char* filename, MinZX* targetEmulator)
{
	if (NULL == filename || '\0' == *filename) {
		WARN("FileMgr::loadTAP: no filename specified\n");
		return false;
	}
	if (NULL == targetEmulator) {
		WARN("FileMgr::loadTAP: no target emulator\n");
		return false;
	}

	FILE* pf = fopen(filename, "rb");
	if (pf == NULL) {
		WARN("FileMgr::loadTAP: cannot open file %s\n", filename);
		return false;
	}

	std::vector<TapeBlock> blocks;
	for (;;) {
		int lo = fgetc(pf);
		if (lo == EOF) break;
		int hi = fgetc(pf);
		if (hi == EOF) break;
		uint16_t len = (uint16_t)(lo | (hi << 8));
		if (len == 0) continue;
		TapeBlock block; // every timing field left at TapeBlock's standard-ROM defaults
		block.data.resize(len);
		size_t n = fread(block.data.data(), 1, len, pf);
		if (n != len) break; // truncated file — keep whatever loaded cleanly
		blocks.push_back(std::move(block));
	}
	fclose(pf);

	if (blocks.empty()) {
		WARN("FileMgr::loadTAP: no blocks found in %s\n", filename);
		return false;
	}

	targetEmulator->loadTapeBlocks(blocks);
	INFO("FileMgr::loadTAP: loaded %zu blocks from %s\n", blocks.size(), filename);
	return true;
}

// TZX block IDs turned into playable TapeBlocks: 0x10/0x11 (standard/turbo
// data, the vast majority of real files), 0x12 (pure tone), 0x14 (pure
// data, no pilot/sync), 0x20 (pause/stop). Everything else below is pure
// metadata/structure (groups, loops, text, archive info...) or a format
// (0x13 pulse sequence, 0x15 direct recording) rare enough in practice not
// to be worth a dedicated engine phase yet -- each is still parsed just
// far enough to read its own declared length and skip past it, so later
// real blocks in the same file don't get misaligned. True control-flow
// blocks (jump/call/loop) are skipped rather than honoured -- harmless for
// the common case (they just wrap pilot-tone repeats a loader would sit
// through anyway), but a file that relies on them for actual branching
// logic won't play back exactly right.
bool FileMgr::loadTZX(const char* filename, MinZX* targetEmulator)
{
	if (NULL == filename || '\0' == *filename) {
		WARN("FileMgr::loadTZX: no filename specified\n");
		return false;
	}
	if (NULL == targetEmulator) {
		WARN("FileMgr::loadTZX: no target emulator\n");
		return false;
	}

	FILE* pf = fopen(filename, "rb");
	if (pf == NULL) {
		WARN("FileMgr::loadTZX: cannot open file %s\n", filename);
		return false;
	}

	char sig[8];
	if (fread(sig, 1, 8, pf) != 8 || memcmp(sig, "ZXTape!\x1A", 8) != 0) {
		WARN("FileMgr::loadTZX: bad signature in %s\n", filename);
		fclose(pf);
		return false;
	}
	fgetc(pf); fgetc(pf); // major/minor version -- every block below is self-describing, don't need it

	std::vector<TapeBlock> blocks;
	bool truncated = false;

	auto readU8 = [&](bool& ok) -> uint8_t {
		int c = fgetc(pf);
		if (c == EOF) ok = false;
		return (uint8_t)c;
	};
	auto readU16 = [&](bool& ok) -> uint16_t {
		uint8_t lo = readU8(ok), hi = readU8(ok);
		return (uint16_t)(lo | (hi << 8));
	};
	auto readU24 = [&](bool& ok) -> uint32_t {
		uint8_t b0 = readU8(ok), b1 = readU8(ok), b2 = readU8(ok);
		return (uint32_t)b0 | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16);
	};
	auto readU32 = [&](bool& ok) -> uint32_t {
		uint8_t b0 = readU8(ok), b1 = readU8(ok), b2 = readU8(ok), b3 = readU8(ok);
		return (uint32_t)b0 | ((uint32_t)b1 << 8) | ((uint32_t)b2 << 16) | ((uint32_t)b3 << 24);
	};
	auto skip = [&](long n) { if (n > 0) fseek(pf, n, SEEK_CUR); };

	for (;;) {
		int id = fgetc(pf);
		if (id == EOF) break;
		bool ok = true;

		if (id == 0x10) { // Standard Speed Data
			uint16_t pauseMs = readU16(ok);
			uint16_t len = readU16(ok);
			if (!ok) { truncated = true; break; }
			TapeBlock b;
			b.data.resize(len);
			if (len > 0 && fread(b.data.data(), 1, len, pf) != len) { truncated = true; break; }
			b.pauseAfterMs = pauseMs;
			blocks.push_back(std::move(b));
		}
		else if (id == 0x11) { // Turbo Speed Data -- the actual "turbo loader" mechanism
			uint16_t pilotLen = readU16(ok);
			uint16_t sync1 = readU16(ok);
			uint16_t sync2 = readU16(ok);
			uint16_t bit0 = readU16(ok);
			uint16_t bit1 = readU16(ok);
			uint16_t pilotCount = readU16(ok);
			uint8_t usedBits = readU8(ok);
			uint16_t pauseMs = readU16(ok);
			uint32_t len = readU24(ok);
			if (!ok) { truncated = true; break; }
			TapeBlock b;
			b.data.resize(len);
			if (len > 0 && fread(b.data.data(), 1, len, pf) != len) { truncated = true; break; }
			b.pilotPulseLen = pilotLen;
			b.pilotCountHeader = b.pilotCountData = pilotCount; // turbo blocks give one count regardless of the flag byte
			b.sync1Len = sync1; b.sync2Len = sync2;
			b.bit0Len = bit0; b.bit1Len = bit1;
			b.usedBitsLastByte = usedBits;
			b.pauseAfterMs = pauseMs;
			blocks.push_back(std::move(b));
		}
		else if (id == 0x12) { // Pure Tone
			uint16_t pulseLen = readU16(ok);
			uint16_t pulseCount = readU16(ok);
			if (!ok) { truncated = true; break; }
			TapeBlock b;
			b.isPureTone = true;
			b.pureTonePulseLen = pulseLen;
			b.pureTonePulseCount = pulseCount;
			blocks.push_back(std::move(b));
		}
		else if (id == 0x13) { // Pulse sequence -- rare; skip
			uint8_t n = readU8(ok);
			if (!ok) { truncated = true; break; }
			skip(n * 2L);
			WARN("FileMgr::loadTZX: skipping unsupported Pulse Sequence block\n");
		}
		else if (id == 0x14) { // Pure Data: bit-encoding with no pilot/sync at all
			uint16_t bit0 = readU16(ok);
			uint16_t bit1 = readU16(ok);
			uint8_t usedBits = readU8(ok);
			uint16_t pauseMs = readU16(ok);
			uint32_t len = readU24(ok);
			if (!ok) { truncated = true; break; }
			TapeBlock b;
			b.data.resize(len);
			if (len > 0 && fread(b.data.data(), 1, len, pf) != len) { truncated = true; break; }
			b.hasPilotSync = false;
			b.bit0Len = bit0; b.bit1Len = bit1;
			b.usedBitsLastByte = usedBits;
			b.pauseAfterMs = pauseMs;
			blocks.push_back(std::move(b));
		}
		else if (id == 0x15) { // Direct Recording -- raw sampled bitstream, a different model entirely; skip for now
			readU16(ok); // T-states per sample
			readU16(ok); // pause after, ms
			readU8(ok);  // used bits in last byte
			uint32_t len = readU24(ok);
			if (!ok) { truncated = true; break; }
			skip((long)len);
			WARN("FileMgr::loadTZX: skipping unsupported Direct Recording block\n");
		}
		else if (id == 0x20) { // Pause/Stop the tape
			uint16_t ms = readU16(ok);
			if (!ok) { truncated = true; break; }
			TapeBlock b;
			b.isPauseOnly = true;
			b.pauseAfterMs = ms; // 0 here specifically means "stop" (see MinZX::tapeStartBlock)
			blocks.push_back(std::move(b));
		}
		else if (id == 0x21) { uint8_t n = readU8(ok); if (!ok) { truncated = true; break; } skip(n); }             // Group start: name
		else if (id == 0x22) { /* Group end: no body */ }
		else if (id == 0x23) { skip(2); WARN("FileMgr::loadTZX: ignoring Jump To Block (not honoured)\n"); }        // Jump to block
		else if (id == 0x24) { readU16(ok); if (!ok) { truncated = true; break; } WARN("FileMgr::loadTZX: ignoring Loop Start (not honoured)\n"); } // Loop start: repeat count
		else if (id == 0x25) { /* Loop end: no body */ }
		else if (id == 0x26) { uint16_t n = readU16(ok); if (!ok) { truncated = true; break; } skip(n * 2L); WARN("FileMgr::loadTZX: ignoring Call Sequence (not honoured)\n"); } // Call sequence
		else if (id == 0x27) { /* Return from sequence: no body */ }
		else if (id == 0x28) { uint16_t len = readU16(ok); if (!ok) { truncated = true; break; } skip(len); }       // Select block
		else if (id == 0x2A) { uint32_t len = readU32(ok); if (!ok) { truncated = true; break; } skip((long)len); } // Stop tape if in 48K mode
		else if (id == 0x2B) { skip(5); }                                                                           // Set signal level
		else if (id == 0x30) { uint8_t n = readU8(ok); if (!ok) { truncated = true; break; } skip(n); }             // Text description
		else if (id == 0x31) { readU8(ok); uint8_t n = readU8(ok); if (!ok) { truncated = true; break; } skip(n); } // Message block
		else if (id == 0x32) { uint16_t len = readU16(ok); if (!ok) { truncated = true; break; } skip(len); }       // Archive info
		else if (id == 0x33) { uint8_t n = readU8(ok); if (!ok) { truncated = true; break; } skip(n * 3L); }        // Hardware type
		else if (id == 0x34) { skip(8); }                                                                           // Emulation info (deprecated)
		else if (id == 0x35) { skip(10); uint32_t len = readU32(ok); if (!ok) { truncated = true; break; } skip((long)len); } // Custom info block
		else if (id == 0x40) { skip(1); uint32_t len = readU24(ok); if (!ok) { truncated = true; break; } skip((long)len); }  // Snapshot block (deprecated)
		else if (id == 0x5A) { skip(9); }                                                                           // "Glue" block (merged-tzx marker)
		else {
			WARN("FileMgr::loadTZX: unknown block id 0x%02X, stopping at %s\n", id, filename);
			break; // no safe way to know this block's length -- stop rather than misparse the rest
		}
	}
	fclose(pf);

	if (truncated)
		WARN("FileMgr::loadTZX: %s ended unexpectedly, kept %zu block(s) parsed so far\n", filename, blocks.size());

	if (blocks.empty()) {
		WARN("FileMgr::loadTZX: no blocks found in %s\n", filename);
		return false;
	}

	targetEmulator->loadTapeBlocks(blocks);
	INFO("FileMgr::loadTZX: loaded %zu blocks from %s\n", blocks.size(), filename);
	return true;
}

bool FileMgr::saveSNA(const char* filename, MinZX* sourceEmulator)
{
	if (NULL == filename || '\0' == *filename) {
		WARN("FileMgr::saveSNA: no filename specified\n");
		return false;
	}
	if (NULL == sourceEmulator) {
		WARN("FileMgr::saveSNA: no source emulator\n");
		return false;
	}

	FILE* pf = fopen(filename, "wb");
	if (pf == NULL) {
		WARN("FileMgr::saveSNA: cannot create file %s\n", filename);
		return false;
	}

	Z80* z80 = sourceEmulator->getCPU();

	fputc(z80->getRegI(), pf);
	fputc(z80->getRegLx(), pf);
	fputc(z80->getRegHx(), pf);
	fputc(z80->getRegEx(), pf);
	fputc(z80->getRegDx(), pf);
	fputc(z80->getRegCx(), pf);
	fputc(z80->getRegBx(), pf);
	fputc(z80->getRegFx(), pf);
	fputc(z80->getRegAx(), pf);

	fputc(z80->getRegL(), pf);
	fputc(z80->getRegH(), pf);
	fputc(z80->getRegE(), pf);
	fputc(z80->getRegD(), pf);
	fputc(z80->getRegC(), pf);
	fputc(z80->getRegB(), pf);
	fputWordLE(z80->getRegIY(), pf);
	fputWordLE(z80->getRegIX(), pf);

	fputc(z80->isIFF2() ? 0x04 : 0x00, pf);
	fputc(z80->getRegR(), pf);

	fputc(z80->getFlags(), pf);
	fputc(z80->getRegA(), pf);

	// .SNA has no PC field — loadSNA pops it off the stack on load, so we
	// push it here instead: SP written to the file is 2 less than the
	// real SP, and the two bytes "below" the real stack are PC's bytes.
	// Everything else is the UNMODIFIED real stack/memory — this is the
	// whole point versus a register-only capture (see project memory):
	// the real call chain beneath SP comes along for free.
	uint16_t sp = z80->getRegSP();
	uint16_t pc = z80->getRegPC();
	uint16_t snaSP = (uint16_t)(sp - 2);
	fputWordLE(snaSP, pf);

	fputc((uint8_t)z80->getIM(), pf);
	fputc(sourceEmulator->getBorderColor(), pf);

	for (uint32_t addr = 0x4000; addr <= 0xFFFF; addr++) {
		uint8_t b;
		if ((uint16_t)addr == snaSP)
			b = (uint8_t)(pc & 0xFF);
		else if ((uint16_t)addr == (uint16_t)(snaSP + 1))
			b = (uint8_t)((pc >> 8) & 0xFF);
		else
			b = sourceEmulator->peek8((uint16_t)addr);
		fputc(b, pf);
	}

	fclose(pf);
	INFO("FileMgr::saveSNA: wrote %s (PC=%04x via stack at %04x)\n", filename, pc, snaSP);
	return true;
}

// .Z80 RLE: a run of N>=2 identical bytes becomes ED ED <N> <byte> (N is a
// single byte, so runs cap at 255); any other byte is literal. The one
// wrinkle: a LITERAL lone 0xED must never be immediately followed by what
// the decompressor would read as a run, so the compressor refuses to
// start a new run right after emitting a literal 0xED (z80Compress's
// lastWasLiteralEd) -- and a run of exactly 0xED bytes is ALWAYS encoded
// via the escape (never literally), because two literal EDs in a row is
// indistinguishable from the escape sequence itself otherwise. Algorithm
// confirmed byte-for-byte against libspectrum's compress_block/
// uncompress_block (z80.c) -- GPL, facts only, no code copied.
static std::vector<uint8_t> z80Decompress(const uint8_t* src, size_t srcLen)
{
	std::vector<uint8_t> out;
	out.reserve(srcLen * 2);
	size_t i = 0;
	while (i < srcLen) {
		if (i + 1 == srcLen) { out.push_back(src[i]); i++; continue; }
		if (src[i] == 0xED && src[i + 1] == 0xED && i + 3 < srcLen) {
			size_t runLen = src[i + 2];
			uint8_t value = src[i + 3];
			i += 4;
			out.insert(out.end(), runLen, value);
		} else {
			out.push_back(src[i]);
			i++;
		}
	}
	return out;
}

static std::vector<uint8_t> z80Compress(const uint8_t* src, size_t srcLen)
{
	std::vector<uint8_t> out;
	out.reserve(srcLen);
	size_t i = 0;
	bool lastWasLiteralEd = false;
	while (i < srcLen) {
		if (i + 1 == srcLen) { out.push_back(src[i]); i++; continue; }
		if (src[i] == src[i + 1] && !lastWasLiteralEd) {
			uint8_t repeated = src[i];
			size_t runLen = 2;
			size_t j = i + 2;
			while (j < srcLen && src[j] == repeated && runLen < 0xFF) { runLen++; j++; }
			if (runLen >= 5 || repeated == 0xED) {
				out.push_back(0xED); out.push_back(0xED);
				out.push_back((uint8_t)runLen); out.push_back(repeated);
			} else {
				out.insert(out.end(), runLen, repeated);
			}
			i = j;
			lastWasLiteralEd = false;
		} else {
			lastWasLiteralEd = (src[i] == 0xED);
			out.push_back(src[i]);
			i++;
		}
	}
	return out;
}

// Z80 page-number <-> physical 16K block mapping. Confirmed against
// libspectrum's read_block(): page 3 is always ROM (discard); page 1 is
// Interface 1/+D ROM-or-RAM (discard -- JASE doesn't emulate either);
// page 11 is the Multiface ROM (discard -- not what MinZX's own
// Multiface page uses, and JASE doesn't want snapshot loads fighting
// over it); everything else maps to "page - 3" EXCEPT on 48K machines,
// where pages 4 and 5 are swapped first. The resulting final index (0-7)
// is a real 128K RAM bank number for 128K snapshots; for 48K, only
// 0/2/5 are ever used, meaning 0xC000-0xFFFF / 0x8000-0xBFFF /
// 0x4000-0x7FFF respectively (the fixed 48K memory map, addressed using
// the SAME bank numbers it would occupy if it were a 128K machine).
static int z80PageToFinalIndex(uint8_t rawPage, bool is128k)
{
	if (rawPage == 1 || rawPage == 11) return -1; // IF1/+D or Multiface -- unsupported, discard
	if (!is128k) {
		switch (rawPage) {
			case 3: return -1; // ROM
			case 4: rawPage = 5; break;
			case 5: rawPage = 3; break;
			default: break; // page 8 (screen) passes through unchanged
		}
	} else if (rawPage == 3) {
		return -1; // ROM
	}
	return (int)rawPage - 3;
}

static void z80WritePage16k(MinZX* target, bool is128k, int finalIndex, const uint8_t* data, size_t len)
{
	if (len != 0x4000) return; // a block that didn't decompress to a full 16K page is corrupt -- drop it
	if (is128k) {
		if (finalIndex < 0 || finalIndex > 7) return;
		MemoryMap& mem = target->getMemory();
		memcpy(mem.ramBank(2 * finalIndex).data, data, 0x2000);
		memcpy(mem.ramBank(2 * finalIndex + 1).data, data + 0x2000, 0x2000);
	} else {
		uint16_t base;
		switch (finalIndex) {
			case 5: base = 0x4000; break;
			case 2: base = 0x8000; break;
			case 0: base = 0xC000; break;
			default: return; // not one of the fixed 48K banks -- discard
		}
		for (size_t i = 0; i < len; i++)
			target->poke8((uint16_t)(base + i), data[i]);
	}
}

// For saveZ80: the inverse of z80PageToFinalIndex(), enumerated directly
// rather than derived, since the forward mapping isn't cleanly invertible
// in code (it's lossy by design -- several raw pages discard to -1).
static const struct { uint16_t base; uint8_t rawPage; } kZ80Pages48[3] = {
	{ 0x4000, 8 }, { 0x8000, 4 }, { 0xC000, 5 }
};

bool FileMgr::loadZ80(const char* filename, MinZX* targetEmulator)
{
	if (NULL == filename || '\0' == *filename) {
		WARN("FileMgr::loadZ80: no filename specified\n");
		return false;
	}
	if (NULL == targetEmulator) {
		WARN("FileMgr::loadZ80: no target emulator\n");
		return false;
	}

	FILE* pf = fopen(filename, "rb");
	if (pf == NULL) {
		WARN("FileMgr::loadZ80: cannot open file %s\n", filename);
		return false;
	}
	fseek(pf, 0, SEEK_END);
	long fileSize = ftell(pf);
	fseek(pf, 0, SEEK_SET);
	if (fileSize < 30) {
		WARN("FileMgr::loadZ80: %s is too short to be a .z80 file\n", filename);
		fclose(pf);
		return false;
	}
	std::vector<uint8_t> buf((size_t)fileSize);
	size_t n = fread(buf.data(), 1, (size_t)fileSize, pf);
	fclose(pf);
	if (n != (size_t)fileSize) {
		WARN("FileMgr::loadZ80: short read on %s\n", filename);
		return false;
	}

	const uint8_t* h = buf.data();
	uint16_t pcField = h[6] | (h[7] << 8);

	int version;
	size_t dataOffset;
	uint16_t pc;
	bool is128k;

	if (pcField != 0) {
		// v1: no extended header, PC lives in the base header, and the
		// WHOLE 48K (no page framing) follows as one block.
		version = 1;
		pc = pcField;
		dataOffset = 30;
		is128k = false;
	} else {
		if ((size_t)fileSize < 32) {
			WARN("FileMgr::loadZ80: %s truncated before the v2/v3 extra header\n", filename);
			return false;
		}
		uint16_t extraLen = h[30] | (h[31] << 8);
		const uint8_t* eh = h + 32;
		pc = (uint16_t)(eh[0] | (eh[1] << 8));
		uint8_t machineByte = eh[2];
		if (extraLen == 23) {
			version = 2;
			is128k = (machineByte == 3 || machineByte == 4); // v2: 3=128k, 4=128k+IF1
		} else if (extraLen == 54 || extraLen == 55) {
			version = 3;
			is128k = (machineByte >= 4); // v3: 4=128k, 5=128k+IF1, 6=128k+MGT
		} else {
			WARN("FileMgr::loadZ80: %s has an unrecognised extra header length %u\n", filename, extraLen);
			return false;
		}
		dataOffset = 32 + extraLen;
	}

	MachineType current = targetEmulator->getMachine();
	bool currentIs128k = (current == MachineType::Spectrum128K);
	if (is128k != currentIs128k) {
		WARN("FileMgr::loadZ80: %s is a %s snapshot but the emulator is running as %s "
		     "-- machine type can't change after startup, restart with --machine %s\n",
		     filename, is128k ? "128K" : "48K", currentIs128k ? "128K" : "48K",
		     is128k ? "128k" : "48k");
		return false;
	}

	targetEmulator->reset();
	Z80* z80 = targetEmulator->getCPU();

	z80->setRegA(h[0]);
	z80->setFlags(h[1]);
	z80->setRegBC((uint16_t)(h[2] | (h[3] << 8)));
	z80->setRegHL((uint16_t)(h[4] | (h[5] << 8)));
	z80->setRegSP((uint16_t)(h[8] | (h[9] << 8)));
	z80->setRegI(h[10]);
	uint8_t rLow = h[11] & 0x7F;
	uint8_t flagsByte = h[12];
	uint8_t border = (flagsByte & 0x0E) >> 1;
	bool v1Compressed = (flagsByte & 0x20) != 0;
	bool rBit7 = (flagsByte & 0x01) != 0;
	z80->setRegR((uint8_t)(rLow | (rBit7 ? 0x80 : 0)));
	z80->setRegDE((uint16_t)(h[13] | (h[14] << 8)));
	z80->setRegBCx((uint16_t)(h[15] | (h[16] << 8)));
	z80->setRegDEx((uint16_t)(h[17] | (h[18] << 8)));
	z80->setRegHLx((uint16_t)(h[19] | (h[20] << 8)));
	z80->setRegAx(h[21]);
	z80->setRegFx(h[22]);
	z80->setRegIY((uint16_t)(h[23] | (h[24] << 8)));
	z80->setRegIX((uint16_t)(h[25] | (h[26] << 8)));
	z80->setIFF1(h[27] != 0);
	z80->setIFF2(h[28] != 0);
	z80->setIM((Z80::IntMode)(h[29] & 0x03));
	targetEmulator->setBorderColor(border);
	z80->setRegPC(pc);

	if (version == 1) {
		std::vector<uint8_t> mem48k;
		if (v1Compressed) {
			// No explicit length for a v1 block -- scan for the 4-byte
			// "00 ED ED 00" end marker (confirmed state machine against
			// libspectrum's read_v1_block()), then decompress everything
			// before it.
			size_t end = buf.size();
			for (size_t i = dataOffset; i + 4 <= buf.size(); i++) {
				if (buf[i] == 0x00 && buf[i + 1] == 0xED && buf[i + 2] == 0xED && buf[i + 3] == 0x00) {
					end = i;
					break;
				}
			}
			mem48k = z80Decompress(buf.data() + dataOffset, end - dataOffset);
		} else {
			if (buf.size() < dataOffset + 0xC000) {
				WARN("FileMgr::loadZ80: %s truncated (uncompressed v1 body too short)\n", filename);
				return false;
			}
			mem48k.assign(buf.begin() + dataOffset, buf.begin() + dataOffset + 0xC000);
		}
		if (mem48k.size() != 0xC000) {
			WARN("FileMgr::loadZ80: %s v1 body decompressed to %zu bytes, expected 49152\n",
			     filename, mem48k.size());
			return false;
		}
		for (size_t i = 0; i < mem48k.size(); i++)
			targetEmulator->poke8((uint16_t)(0x4000 + i), mem48k[i]);
	} else {
		size_t pos = dataOffset;
		while (pos + 3 <= buf.size()) {
			uint16_t blockLen = (uint16_t)(buf[pos] | (buf[pos + 1] << 8));
			uint8_t rawPage = buf[pos + 2];
			pos += 3;
			bool rawBlock = (blockLen == 0xFFFF); // 0xffff => 16384 bytes, uncompressed
			size_t storedLen = rawBlock ? 0x4000 : blockLen;
			if (pos + storedLen > buf.size()) {
				WARN("FileMgr::loadZ80: %s truncated mid-page (page %u)\n", filename, rawPage);
				break;
			}
			std::vector<uint8_t> pageData = rawBlock
				? std::vector<uint8_t>(buf.begin() + pos, buf.begin() + pos + storedLen)
				: z80Decompress(buf.data() + pos, storedLen);
			pos += storedLen;

			int finalIndex = z80PageToFinalIndex(rawPage, is128k);
			if (finalIndex >= 0)
				z80WritePage16k(targetEmulator, is128k, finalIndex, pageData.data(), pageData.size());
		}
	}

	INFO("FileMgr::loadZ80: loaded %s (v%d, %s)\n", filename, version, is128k ? "128K" : "48K");
	return true;
}

bool FileMgr::saveZ80(const char* filename, MinZX* sourceEmulator)
{
	if (NULL == filename || '\0' == *filename) {
		WARN("FileMgr::saveZ80: no filename specified\n");
		return false;
	}
	if (NULL == sourceEmulator) {
		WARN("FileMgr::saveZ80: no source emulator\n");
		return false;
	}

	FILE* pf = fopen(filename, "wb");
	if (pf == NULL) {
		WARN("FileMgr::saveZ80: cannot create file %s\n", filename);
		return false;
	}

	Z80* z80 = sourceEmulator->getCPU();
	bool is128k = (sourceEmulator->getMachine() == MachineType::Spectrum128K);

	uint8_t header[30] = {};
	header[0] = z80->getRegA();
	header[1] = z80->getFlags();
	uint16_t bc = z80->getRegBC(); header[2] = (uint8_t)bc; header[3] = (uint8_t)(bc >> 8);
	uint16_t hl = z80->getRegHL(); header[4] = (uint8_t)hl; header[5] = (uint8_t)(hl >> 8);
	header[6] = 0; header[7] = 0; // PC=0 in the base header signals a v2/v3 file
	uint16_t sp = z80->getRegSP(); header[8] = (uint8_t)sp; header[9] = (uint8_t)(sp >> 8);
	header[10] = z80->getRegI();
	uint8_t rFull = z80->getRegR();
	header[11] = rFull & 0x7F;
	uint8_t border = sourceEmulator->getBorderColor();
	header[12] = (uint8_t)(((rFull & 0x80) ? 0x01 : 0x00) | ((border & 0x07) << 1));
	uint16_t de = z80->getRegDE(); header[13] = (uint8_t)de; header[14] = (uint8_t)(de >> 8);
	uint16_t bcx = z80->getRegBCx(); header[15] = (uint8_t)bcx; header[16] = (uint8_t)(bcx >> 8);
	uint16_t dex = z80->getRegDEx(); header[17] = (uint8_t)dex; header[18] = (uint8_t)(dex >> 8);
	uint16_t hlx = z80->getRegHLx(); header[19] = (uint8_t)hlx; header[20] = (uint8_t)(hlx >> 8);
	header[21] = z80->getRegAx();
	header[22] = z80->getRegFx();
	uint16_t iy = z80->getRegIY(); header[23] = (uint8_t)iy; header[24] = (uint8_t)(iy >> 8);
	uint16_t ix = z80->getRegIX(); header[25] = (uint8_t)ix; header[26] = (uint8_t)(ix >> 8);
	header[27] = z80->isIFF1() ? 1 : 0;
	header[28] = z80->isIFF2() ? 1 : 0;
	header[29] = (uint8_t)z80->getIM();
	fwrite(header, 1, sizeof(header), pf);

	// v3, 54-byte extra header. AY register state (extra[6..22]) is left
	// zeroed -- AyPsg doesn't expose its current registers for read-back
	// yet, so a reloaded snapshot's AY chip starts silent/default rather
	// than wherever it actually was. Everything else not set below
	// (Spectaculator extension flags, MGT/Disciple/joystick fields,
	// hardware-modify byte) is genuinely inapplicable to JASE's machines.
	uint8_t extra[54] = {};
	uint16_t pc = z80->getRegPC();
	extra[0] = (uint8_t)pc; extra[1] = (uint8_t)(pc >> 8);
	extra[2] = is128k ? 4 : 0; // v3 machine byte: 0=48k, 4=128k
	extra[3] = is128k ? sourceEmulator->get7FFDRaw() : 0;
	// The 2-byte extra-header LENGTH field (separate from the extra
	// header's own content) -- this is what a loader's "PC==0 means v2/
	// v3, read bytes 30-31 for how long the extra header is" check
	// actually reads. Forgetting it shifts everything after by 2 bytes,
	// which a loader can only detect as "unrecognised extra header
	// length" garbage, not point back at this specific omission.
	fputc((int)sizeof(extra), pf);
	fputc((int)(sizeof(extra) >> 8), pf);
	fwrite(extra, 1, sizeof(extra), pf);

	if (is128k) {
		MemoryMap& mem = sourceEmulator->getMemory();
		for (int k = 0; k < 8; k++) {
			uint8_t raw[0x4000];
			memcpy(raw, mem.ramBank(2 * k).data, 0x2000);
			memcpy(raw + 0x2000, mem.ramBank(2 * k + 1).data, 0x2000);
			std::vector<uint8_t> compressed = z80Compress(raw, sizeof(raw));
			uint8_t blockHeader[3] = {
				(uint8_t)compressed.size(), (uint8_t)(compressed.size() >> 8), (uint8_t)(k + 3)
			};
			fwrite(blockHeader, 1, sizeof(blockHeader), pf);
			fwrite(compressed.data(), 1, compressed.size(), pf);
		}
	} else {
		for (const auto& p : kZ80Pages48) {
			uint8_t raw[0x4000];
			for (int i = 0; i < 0x4000; i++)
				raw[i] = sourceEmulator->peek8((uint16_t)(p.base + i));
			std::vector<uint8_t> compressed = z80Compress(raw, sizeof(raw));
			uint8_t blockHeader[3] = {
				(uint8_t)compressed.size(), (uint8_t)(compressed.size() >> 8), p.rawPage
			};
			fwrite(blockHeader, 1, sizeof(blockHeader), pf);
			fwrite(compressed.data(), 1, compressed.size(), pf);
		}
	}

	fclose(pf);
	INFO("FileMgr::saveZ80: wrote %s (%s)\n", filename, is128k ? "128K" : "48K");
	return true;
}