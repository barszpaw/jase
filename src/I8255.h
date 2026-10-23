#pragma once
#include <cstdint>
#include <functional>

// Intel 8255(A) Programmable Peripheral Interface -- generic chip model.
//
// Adapted from MAME's i8255_device (src/devices/machine/i8255.cpp/.h,
// license:BSD-3-Clause, copyright-holders:Curt Coder -- BSD-3-Clause is
// compatible with JASE's own MIT license, so this is a real adaptation,
// not a clean-room reimplementation: the mode/register logic below
// mirrors MAME's read_mode0/1/2, write_mode0/1/2, read_pc/output_pc and
// set_pc_bit methods closely. What's stripped out is MAME-specific
// plumbing this project has no equivalent of -- device_t lifecycle,
// save_item state-save registration, devcb-template callback binding,
// and live interrupt-line signalling (check_interrupt/set_intr) -- in
// favour of plain std::function callbacks, matching JASE's own
// Peripheral convention. The IBF/OBF/INTE flags themselves are kept
// (Mode 1/2's Port C status-bit reporting in read_pc()/output_pc() needs
// them to read back correctly), just without anything wired to an
// actual IRQ line, since nothing in JASE has one for this chip yet.
//
// This class only knows the 8255's OWN internal register/mode semantics
// -- it has no idea what's wired to PA/PB/PC on any given board. A
// peripheral (e.g. SDI) owns one instance, decodes the Z80 I/O address
// down to a 0-3 register select (PA=0, PB=1, PC=2, Control=3 -- the
// same A1/A0 the real chip exposes, which on SDI's board come from the
// Z80's A5/A6 via the external address decoder, not from the 8255
// itself) and wires up the callbacks below to give each port a
// board-specific meaning.
//
// Verified against SDI's real schematic/ROM this project already
// reverse-engineered: SDI uses Mode 0 only (control byte 0x98, live-
// traced) -- Port A input (unused/floating), Port B output, Port C
// lower nibble output (ATN/x/CLK/DAT drive), Port C upper nibble input
// (CLK-in/DAT-in read-back). Mode 1/2 and the Bit Set/Reset (BSR)
// mechanism are ported for fidelity (a real 8255 supports them, and a
// future peripheral might need them) but are NOT exercised by anything
// in this project yet -- treat their behavior as "matches MAME's own
// logic", not independently live-verified the way Mode 0 is.
class I8255
{
public:
	// External line readers, used when the corresponding port (or Port
	// C's input-configured half) is read while direction-configured as
	// INPUT. Left unbound reads back as 0xFF -- the floating-TTL-input
	// convention real hardware shows when nothing drives a line.
	std::function<uint8_t()> inA, inB, inC;

	// External line drivers, called every time the port's output latch
	// changes -- including the mode-set reset-to-0 and any Port C
	// Bit-Set/Reset op -- so a board can react to live bit changes (e.g.
	// SDI's IEC ATN/CLK/DAT, driven off Port C's output nibble).
	std::function<void(uint8_t)> outA, outB, outC;

	I8255() { reset(); }

	// Mode 0x9B (all three ports input) on /RESET, matching the real
	// chip's documented power-up/reset state.
	void reset();

	// reg: 0=Port A, 1=Port B, 2=Port C, 3=Control -- the chip's own
	// A1/A0 register select, same encoding real hardware exposes.
	uint8_t read(int reg);
	void write(int reg, uint8_t data);

private:
	enum { PORT_A = 0, PORT_B = 1, PORT_C = 2 };
	enum { MODE_0 = 0, MODE_1 = 1, MODE_2 = 2 };
	enum { MODE_OUTPUT = 0, MODE_INPUT = 1 };

	static constexpr uint8_t kPortCLowerInput = 0x01;
	static constexpr uint8_t kPortBInput       = 0x02;
	static constexpr uint8_t kGroupBMode1      = 0x04;
	static constexpr uint8_t kPortCUpperInput  = 0x08;
	static constexpr uint8_t kPortAInput       = 0x10;
	static constexpr uint8_t kGroupAModeMask   = 0x60;
	static constexpr uint8_t kModeSet          = 0x80;

	int groupMode(int group) const;      // GROUP_A=0, GROUP_B=1
	int portMode(int port) const;        // PORT_A/PORT_B only
	int portCLowerMode() const;
	int portCUpperMode() const;

	uint8_t readMode0(int port);
	uint8_t readMode1(int port);
	uint8_t readMode2();
	uint8_t readPC();

	void writeMode0(int port, uint8_t data);
	void writeMode1(int port, uint8_t data);
	void writeMode2(uint8_t data);
	void outputPC();

	void setMode(uint8_t data);
	void setPcBit(int bit, int state);

	void driveOut(int port);

	uint8_t m_control = 0;
	uint8_t m_output[3] = { 0, 0, 0 }; // output latches, PA/PB/PC
	uint8_t m_input[3]  = { 0, 0, 0 }; // input latches (Mode 1/2 only)

	int m_ibf[2]  = { 0, 0 }; // input buffer full, PORT_A/PORT_B
	int m_obf[2]  = { 1, 1 }; // output buffer full (negative logic, 1=empty/ack'd)
	int m_inte[2] = { 0, 0 }; // interrupt enable, PORT_A/PORT_B
	int m_inte1 = 0;          // Mode 2 INTE1 (output side)
	int m_inte2 = 0;          // Mode 2 INTE2 (input side)
};
