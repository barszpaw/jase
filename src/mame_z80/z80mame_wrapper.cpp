#include "z80mame_wrapper.h"
#include "z80.h"

// Station I/O decode lives in fdd3000_station/main.cpp, same functions
// Z80free_Rd/Wr/In/Out used to call -- these are the mamez80_* names
// mame_stub.h declares and z80.h's MameMemAccess/MameIoAccess call
// through to directly.
extern "C" {
	uint8_t mamez80_mem_read(uint16_t addr);
	void    mamez80_mem_write(uint16_t addr, uint8_t value);
	uint8_t mamez80_io_read(uint16_t port);
	void    mamez80_io_write(uint16_t port, uint8_t value);
}

// Public accessors over z80_device's protected register state -- see
// z80mame_wrapper.h for why stepping is T-state-budgeted rather than
// one-call-per-instruction.
class Z80MameCpu : public z80_device
{
public:
	Z80MameCpu() : z80_device(s_mconfig, "z80", nullptr, 4000000)
	{
		device_start();
		device_reset();
	}

	void doReset() { device_reset(); }

	int step(int tstates_budget)
	{
		m_icount = tstates_budget;
		int before = m_icount;
		execute_run();
		return before - m_icount;
	}

	void setIrq(bool asserted)
	{
		// Level-triggered, mirrors Z80free_INT()/Z80free_INTserved()'s own
		// contract (see Fdd3000.cpp's syncStationInterrupt() comment) --
		// IM0 data byte 0xFF (RST 38h) matches the real station's own
		// wiring (a single interrupt source, no real vectoring hardware).
		execute_set_input(INPUT_LINE_IRQ0, asserted ? ASSERT_LINE : CLEAR_LINE);
	}

	uint16_t pc() const { return m_pc.w; }
	void setPc(uint16_t v) { m_pc.w = v; m_prvpc.w = v; }
	uint16_t sp() const { return m_sp.w; }
	void setSp(uint16_t v) { m_sp.w = v; }
	uint16_t af() const { return m_af.w; }
	void setAf(uint16_t v) { m_af.w = v; }
	uint16_t bc() const { return m_bc.w; }
	void setBc(uint16_t v) { m_bc.w = v; }
	uint16_t de() const { return m_de.w; }
	void setDe(uint16_t v) { m_de.w = v; }
	uint16_t hl() const { return m_hl.w; }
	void setHl(uint16_t v) { m_hl.w = v; }
	uint16_t ix() const { return m_ix.w; }
	void setIx(uint16_t v) { m_ix.w = v; }
	uint16_t iy() const { return m_iy.w; }
	void setIy(uint16_t v) { m_iy.w = v; }
	uint16_t af2() const { return m_af2.w; }
	void setAf2(uint16_t v) { m_af2.w = v; }
	uint16_t bc2() const { return m_bc2.w; }
	void setBc2(uint16_t v) { m_bc2.w = v; }
	uint16_t de2() const { return m_de2.w; }
	void setDe2(uint16_t v) { m_de2.w = v; }
	uint16_t hl2() const { return m_hl2.w; }
	void setHl2(uint16_t v) { m_hl2.w = v; }
	uint8_t i() const { return m_i; }
	uint8_t r() const { return (m_r & 0x7f) | (m_r2 & 0x80); }
	int iff1() const { return m_iff1 ? 1 : 0; }
	int iff2() const { return m_iff2 ? 1 : 0; }
	int im() const { return m_im; }

private:
	static machine_config s_mconfig;
};

machine_config Z80MameCpu::s_mconfig;

// One instance per process -- same convention Fdd3000.cpp's own
// s_activeForZ80free comment documents for Z80free (Z80free's API is a
// single global instance too; MAME's z80_device is a real C++ object, so
// this isn't strictly required here, but keeping one global instance
// matches how fdd3000_station/main.cpp is structured around a single
// g_cpu already).
static Z80MameCpu g_mameCpu;

extern "C" {

void z80mame_reset(void) { g_mameCpu.doReset(); }

int z80mame_step(void)
{
	static const int kStepTstates = 4; // see z80mame_wrapper.h's own comment
	return g_mameCpu.step(kStepTstates);
}

void z80mame_set_irq(int state) { g_mameCpu.setIrq(state != 0); }

uint16_t z80mame_get_pc(void) { return g_mameCpu.pc(); }
void z80mame_set_pc(uint16_t pc) { g_mameCpu.setPc(pc); }
uint16_t z80mame_get_sp(void) { return g_mameCpu.sp(); }
void z80mame_set_sp(uint16_t sp) { g_mameCpu.setSp(sp); }
uint16_t z80mame_get_af(void) { return g_mameCpu.af(); }
void z80mame_set_af(uint16_t af) { g_mameCpu.setAf(af); }
uint16_t z80mame_get_bc(void) { return g_mameCpu.bc(); }
void z80mame_set_bc(uint16_t bc) { g_mameCpu.setBc(bc); }
uint16_t z80mame_get_de(void) { return g_mameCpu.de(); }
void z80mame_set_de(uint16_t de) { g_mameCpu.setDe(de); }
uint16_t z80mame_get_hl(void) { return g_mameCpu.hl(); }
void z80mame_set_hl(uint16_t hl) { g_mameCpu.setHl(hl); }
uint16_t z80mame_get_ix(void) { return g_mameCpu.ix(); }
void z80mame_set_ix(uint16_t v) { g_mameCpu.setIx(v); }
uint16_t z80mame_get_iy(void) { return g_mameCpu.iy(); }
void z80mame_set_iy(uint16_t v) { g_mameCpu.setIy(v); }
uint16_t z80mame_get_af2(void) { return g_mameCpu.af2(); }
void z80mame_set_af2(uint16_t v) { g_mameCpu.setAf2(v); }
uint16_t z80mame_get_bc2(void) { return g_mameCpu.bc2(); }
void z80mame_set_bc2(uint16_t v) { g_mameCpu.setBc2(v); }
uint16_t z80mame_get_de2(void) { return g_mameCpu.de2(); }
void z80mame_set_de2(uint16_t v) { g_mameCpu.setDe2(v); }
uint16_t z80mame_get_hl2(void) { return g_mameCpu.hl2(); }
void z80mame_set_hl2(uint16_t v) { g_mameCpu.setHl2(v); }
uint8_t z80mame_get_i(void) { return g_mameCpu.i(); }
uint8_t z80mame_get_r(void) { return g_mameCpu.r(); }
int z80mame_get_iff1(void) { return g_mameCpu.iff1(); }
int z80mame_get_iff2(void) { return g_mameCpu.iff2(); }
int z80mame_get_im(void) { return g_mameCpu.im(); }

} // extern "C"
