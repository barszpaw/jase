#include "minzx.h"
#include "z80.h"
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <memory.h>

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


void MinZX::init()
{
	z80   = new Z80(this);
	mem   = new uint8_t[0x10000];
	ports = new uint8_t[0x10000];

	memset(mem, 0x00, 0x10000);
	memset(ports, 0xFF, 0x10000);

	cycleTstates = 69888;
	loadROM();

	//loadDump();
	createSpectrumColors();

	reset();
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
	tstates = 0;
	uint32_t prevTstates = 0;
	justHitBreakpoint_ = false;

	while (tstates < cycleTstates)
	{
		// Fast-load hook fires before anything else, regardless of
		// breakpoints — it's not a debug feature, it's standing in for
		// a cassette deck. Doesn't cost any tstates (treated as instant,
		// same simplification every fast-loading emulator makes).
		if (!tapBlocks_.empty() && z80->getRegPC() == 0x0556)
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

	generateScreen(screen);

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
		if (!tapBlocks_.empty() && z80->getRegPC() == 0x0556)
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
	delete z80;
	delete[] mem;
}

void MinZX::loadROM()
{
	FILE* pf = fopen("zx48.rom", "rb");
	if (pf == NULL) {
		ERROR("Cannot load ROM from file\n");
		return;
	}

	size_t elemsRead = fread(mem, 0x4000, 1, pf);
}

void MinZX::loadDump()
{
	FILE* pf = fopen("mm_mem_dump", "rb");
	if (pf == NULL) {
		ERROR("Cannot load dump from file\n");
		return;
	}

	size_t elemsRead = fread(mem, 0x10000, 1, pf);
}

#define WSCR 320
#define HSCR 240
#define WBMP 256
#define HBMP 192
#define WBOR 32
#define HBOR 24

// BGRA

#define ULA_SWAP(y) ((y & 0xC0) | ((y & 0x38) >> 3) | ((y & 0x07) << 3))

void MinZX::generateScreen(uint8_t* screen)
{
	uint8_t* bptr = screen;
	uint32_t* dptr = (uint32_t*)bptr;

	//               AARRGGBB
	uint32_t blk = 0xFF000000;
	uint32_t whi = 0xFFFFFFFF;

	uint32_t borderColor = zxColor(border, 0);

	for (int scrY = 0; scrY < HSCR; scrY++)
	{
		if (scrY < HBOR)
		{
			for (int scrX = 0; scrX < WSCR; scrX++)
			{
				*dptr++ = borderColor;
			}
		}
		else if (scrY < HBOR + HBMP)
		{
			int speY = scrY - HBOR;
			int ulaY = ULA_SWAP(speY);

			int bmpoff = 0x4000 + ( ulaY       << 5);
			int attoff = 0x5800 + ((speY >> 3) << 5);
			uint8_t* bmpptr = mem + bmpoff;
			uint8_t* attptr = mem + attoff;

			for (int scrX = 0; scrX < WSCR; scrX++)
			{
				if (scrX < WBOR)
				{
					*dptr++ = borderColor;
				}
				else if (scrX < WBOR + WBMP)
				{
					int speX = scrX - WBOR;

					uint8_t bmp = *bmpptr++;
					uint8_t att = *attptr++;

					int ink = (att)      & 7;
					int pap = (att >> 3) & 7;
					int bri = (att >> 6) & 1;
					int fla = (att >> 7);
					uint32_t fore = zxColor(ink, bri);
					uint32_t back = zxColor(pap, bri);

					// B & W
					// uint32_t fore = blk;
					// uint32_t back = whi;

					for (int mask = 0x80; mask > 0; mask >>= 1)
					{
						if (mask & bmp)
							*dptr++ = fore;
						else
							*dptr++ = back;
					}

					scrX += 7;
				}
				else
				{
					*dptr++ = borderColor;
				}
			}
		}
		else
		{
			for (int scrX = 0; scrX < WSCR; scrX++)
			{
				*dptr++ = borderColor;
			}
		}
	}
}

uint8_t MinZX::processInputPort(uint16_t port)
{
	uint8_t hiport = port >> 8;
	uint8_t loport = port & 0xFF;

	if (loport == 0xFE) {
		uint8_t result = 0xFF;

		// EAR_PIN
		if (hiport == 0xFE) {
#ifdef EAR_PRESENT
			bitWrite(result, 6, digitalRead(EAR_PIN));
#endif
		}

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

		return result;
	}
	// Kempston joystick: arrow keys for direction, left Alt for fire.
	// Standard bit layout: 0=right,1=left,2=down,3=up,4=fire (active high).
	if (loport == 0x1F) {
		const Uint8* ks = SDL_GetKeyboardState(NULL);
		uint8_t result = 0;
		if (ks[SDL_SCANCODE_RIGHT]) result |= 0x01;
		if (ks[SDL_SCANCODE_LEFT])  result |= 0x02;
		if (ks[SDL_SCANCODE_DOWN])  result |= 0x04;
		if (ks[SDL_SCANCODE_UP])   result |= 0x08;
		if (ks[SDL_SCANCODE_LALT])  result |= 0x10;
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

	uint8_t tmp_data = value;
	switch (loport) {
		case 0xFE: {

			// delayMicroseconds(CONTENTION_TIME);

			// border color (no bright colors)
			border = value & 0x07;

#ifdef SPEAKER_PRESENT
			digitalWrite(SPEAKER_PIN, bitRead(data, 4)); // speaker
#endif

#ifdef MIC_PRESENT
			digitalWrite(MIC_PIN, bitRead(data, 3)); // tape_out
#endif

			//Ports::base[0x20] = data;
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
	return mem[address];
}

/* Read/Write byte from/to RAM */
uint8_t MinZX::peek8(uint16_t address)
{
	// 3 clocks for read byte from RAM
	if (ADDRESS_IN_LOW_RAM(address))
		tstates += delay_contention(address, tstates);

	tstates += 3;
	return mem[address];
}

void MinZX::poke8(uint16_t address, uint8_t value)
{
	// 3 clocks for write byte to RAM
	if (ADDRESS_IN_LOW_RAM(address))
		tstates += delay_contention(address, tstates);

	tstates += 3;
	mem[address] = value;
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
}

// Standard 48K ROM LD-BYTES contract: A = expected flag byte, DE = length,
// IX = destination, Carry set on entry = LOAD (vs VERIFY, treated the same
// here). We skip the bit-banged tape routine entirely and inject the next
// block directly, then simulate the RET this call would have ended with.
void MinZX::checkFastLoad()
{
	if (tapPos_ >= tapBlocks_.size())
		return;

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
			mem[(uint16_t)(ix + i)] = block[1 + i];
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
