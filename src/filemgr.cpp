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