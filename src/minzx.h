#ifndef _MINZX_H_
#define _MINZX_H_

#include <inttypes.h>
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
	void notifyKey(uint8_t specKey, bool isDown);

	// accessors (for loaders)
	Z80*     getCPU()    { return z80; }
	uint8_t* getMemory() { return mem; }

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
};

#endif // _MINZX_H_