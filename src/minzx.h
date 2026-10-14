#ifndef _MINZX_H_
#define _MINZX_H_

#include <inttypes.h>
#include <vector>
#include <SDL.h>
#include "z80.h"

// Agent/debug port tracing: when set (via --trace-port on the command
// line, see main.cpp), every IN from this port that changes value gets
// printed. -1 = disabled. A single global is enough — there's one Z80
// and one port space in this process.
extern int g_tracePort;

class MinZX : public Z80operations
{
public:
	void init();

	void update(uint8_t* screen);

	void destroy();

	void reset();

	void setBorderColor(uint8_t bcol) { border = bcol; }
	uint8_t getBorderColor() const { return border; }
	uint32_t getTStates() const { return tstates; }

	// Lets main.cpp pace the loop off the audio device's real queue depth
	// instead of a fixed SDL_Delay — see the beeper section in project
	// memory for why: a wall-clock delay alone drifts from the audio
	// device's fixed 44100 Hz consumption rate and causes underrun
	// crackle, exactly the "random bits" artifact reported and reproduced.
	uint32_t getQueuedAudioBytes() const;
	static const int kAudioSampleRate = 44100;
	static const int kAudioSamplesPerFrame = kAudioSampleRate / 50;
	void notifyKey(uint8_t specKey, bool isDown);

	// accessors (for loaders)
	Z80*     getCPU()    { return z80; }
	uint8_t* getMemory() { return mem; }

	// Debug protocol Layer 2 (see project memory for the contract): a
	// single step runs exactly n instructions, bypassing update()'s
	// frame/tstate pacing entirely. Breakpoints are checked in both
	// step() and update()'s instruction loop via a cheap "any active?"
	// guard (ZEsarUX's cpu_core_loop_debug_check_breakpoints pattern) —
	// near-zero cost when no breakpoints are set.
	void step(int n);
	void addBreakpoint(uint16_t pc);
	void removeBreakpoint(uint16_t pc);
	std::vector<uint16_t> listBreakpoints() const { return breakpoints_; }
	bool justHitBreakpoint() const { return justHitBreakpoint_; }
	uint16_t lastBreakpointPC() const { return lastBreakpointPC_; }

	// TAP loading via the standard 48K ROM fast-load hook (LD-BYTES,
	// 0x0556) — not real tape-pulse emulation. Covers the standard
	// loader (BASIC LOAD ""); custom/turbo loaders don't call through
	// this exact address and will just fail, same as real hardware with
	// no tape signal connected. `blocks` are raw TAP block bytes
	// (flag + data + checksum each), in file order.
	void loadTapeBlocks(const std::vector<std::vector<uint8_t>>& blocks);

public:
	virtual uint8_t  fetchOpcode (uint16_t address);
	virtual uint8_t  peek8       (uint16_t address);
	virtual void     poke8       (uint16_t address, uint8_t value);
	virtual uint16_t peek16      (uint16_t address);
	virtual void     poke16      (uint16_t address, RegisterPair word);
	virtual uint8_t  inPort      (uint16_t port);
	virtual void     outPort     (uint16_t port, uint8_t value);
	virtual void     addressOnBus(uint16_t address, int32_t wstates);
	virtual void     interruptHandlingTime(int32_t wstates);
	virtual bool     isActiveINT (void);
#ifdef WITH_BREAKPOINT_SUPPORT
	virtual uint8_t  breakpoint  (uint16_t address, uint8_t opcode);
#endif

#ifdef WITH_EXEC_DONE
	virtual void     execDone(void);
#endif

private:
	Z80* z80;
	uint8_t* mem;
	uint8_t* ports;
	uint32_t tstates;

	uint32_t cycleTstates;

	void loadROM();
	void loadDump();

	void generateScreen(uint8_t* screen);

	uint8_t processInputPort(uint16_t port);

	void processOutputPort(uint16_t port, uint8_t value);

	uint8_t border;

	std::vector<uint16_t> breakpoints_;
	bool breakpointsEnabled_ = false;
	bool justHitBreakpoint_ = false;
	uint16_t lastBreakpointPC_ = 0;
	bool isBreakpoint(uint16_t pc) const;

	std::vector<std::vector<uint8_t>> tapBlocks_;
	size_t tapPos_ = 0;
	void checkFastLoad();

	// Beeper (port 0xFE bit 4): a 1-bit speaker signal turned into a real
	// square wave. One frame's worth of samples (kSamplesPerFrame, fixed
	// since kSampleRate/50 is exact) is built up as OUT 0xFE edges land
	// at their real T-state position, then queued to SDL's audio device
	// once per frame in update(). AY-3-8912 is not emulated yet — only
	// the beeper.
	static const int kSampleRate = 44100;
	static const int kSamplesPerFrame = kSampleRate / 50;
	SDL_AudioDeviceID audioDev_ = 0;
	std::vector<uint8_t> audioBuf_;
	uint32_t audioSampleIndex_ = 0;
	uint8_t beeperLevel_ = 128;
	void initAudio();
	void audioWriteEdge(uint8_t newLevel);
	void flushAudioFrame();
};

#endif // _MINZX_H_