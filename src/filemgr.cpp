#include "filemgr.h"
#include "minzx.h"

#define _CRT_SECURE_NO_WARNINGS 1

#include <stdio.h>
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

	std::vector<std::vector<uint8_t>> blocks;
	for (;;) {
		int lo = fgetc(pf);
		if (lo == EOF) break;
		int hi = fgetc(pf);
		if (hi == EOF) break;
		uint16_t len = (uint16_t)(lo | (hi << 8));
		if (len == 0) continue;
		std::vector<uint8_t> block(len);
		size_t n = fread(block.data(), 1, len, pf);
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