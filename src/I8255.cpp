#include "I8255.h"

// See I8255.h for the MAME provenance/license note (BSD-3-Clause,
// copyright-holders:Curt Coder) this file is adapted from.

void I8255::reset()
{
	setMode(0x9b);
}

int I8255::groupMode(int group) const
{
	if (group == 0) { // GROUP_A
		switch ((m_control & kGroupAModeMask) >> 5) {
			case 0: return MODE_0;
			case 1: return MODE_1;
			default: return MODE_2; // 2 or 3
		}
	}
	// GROUP_B
	return (m_control & kGroupBMode1) ? MODE_1 : MODE_0;
}

int I8255::portMode(int port) const
{
	if (port == PORT_A) return (m_control & kPortAInput) ? MODE_INPUT : MODE_OUTPUT;
	if (port == PORT_B) return (m_control & kPortBInput) ? MODE_INPUT : MODE_OUTPUT;
	return MODE_OUTPUT;
}

int I8255::portCLowerMode() const
{
	return (m_control & kPortCLowerInput) ? MODE_INPUT : MODE_OUTPUT;
}

int I8255::portCUpperMode() const
{
	return (m_control & kPortCUpperInput) ? MODE_INPUT : MODE_OUTPUT;
}

void I8255::driveOut(int port)
{
	uint8_t value = m_output[port];
	switch (port) {
		case PORT_A: if (outA) outA(value); break;
		case PORT_B: if (outB) outB(value); break;
		case PORT_C: if (outC) outC(value); break;
	}
}

uint8_t I8255::readMode0(int port)
{
	if (portMode(port) == MODE_OUTPUT)
		return m_output[port]; // read back our own output latch

	if (port == PORT_A) return inA ? inA() : 0xff;
	return inB ? inB() : 0xff;
}

uint8_t I8255::readMode1(int port)
{
	if (portMode(port) == MODE_OUTPUT)
		return m_output[port];

	// read data from input latch (set by the board calling a strobe --
	// not wired up here, since nothing in this project uses Mode 1 yet)
	uint8_t data = m_input[port];
	m_ibf[port] = 0;
	m_input[port] = 0;
	return data;
}

uint8_t I8255::readMode2()
{
	uint8_t data = m_input[PORT_A];
	m_ibf[PORT_A] = 0;
	m_input[PORT_A] = 0;
	return data;
}

uint8_t I8255::readPC()
{
	uint8_t data = 0;
	uint8_t mask = 0;
	uint8_t bMask = 0x0f;

	switch (groupMode(0)) { // GROUP_A
		case MODE_0:
			if (portCUpperMode() == MODE_OUTPUT)
				data |= m_output[PORT_C] & 0xf0;
			else
				mask |= 0xf0;
			break;

		case MODE_1:
			if (portMode(PORT_A) == MODE_OUTPUT) {
				data |= m_obf[PORT_A] ? 0x80 : 0x00;
				data |= m_inte[PORT_A] ? 0x40 : 0x00;
				mask |= 0x30;
			} else {
				data |= m_ibf[PORT_A] ? 0x20 : 0x00;
				data |= m_inte[PORT_A] ? 0x10 : 0x00;
				mask |= 0xc0;
			}
			if (portCUpperMode() == MODE_OUTPUT) {
				data |= m_output[PORT_C] & mask;
				mask = 0;
			}
			break;

		case MODE_2:
			bMask = 0x07;
			data |= m_inte2 ? 0x10 : 0x00;
			data |= m_ibf[PORT_A] ? 0x20 : 0x00;
			data |= m_inte1 ? 0x40 : 0x00;
			data |= m_obf[PORT_A] ? 0x80 : 0x00;
			break;
	}

	switch (groupMode(1)) { // GROUP_B
		case MODE_0:
			if (portCLowerMode() == MODE_OUTPUT)
				data |= m_output[PORT_C] & bMask;
			else
				mask |= bMask;
			break;

		case MODE_1:
			data |= m_inte[PORT_B] ? 0x04 : 0x00;
			if (portMode(PORT_B) == MODE_OUTPUT)
				data |= m_obf[PORT_B] ? 0x02 : 0x00;
			else
				data |= m_ibf[PORT_B] ? 0x02 : 0x00;
			break;
	}

	if (mask)
		data |= (inC ? inC() : 0xff) & mask;

	return data;
}

void I8255::writeMode0(int port, uint8_t data)
{
	if (portMode(port) != MODE_OUTPUT)
		return;
	m_output[port] = data;
	driveOut(port);
}

void I8255::writeMode1(int port, uint8_t data)
{
	if (portMode(port) != MODE_OUTPUT)
		return;
	m_output[port] = data;
	driveOut(port);
	m_obf[port] = 0;
}

void I8255::writeMode2(uint8_t data)
{
	m_output[PORT_A] = data;
	driveOut(PORT_A);
	m_obf[PORT_A] = 0;
}

void I8255::outputPC()
{
	uint8_t data = 0;
	uint8_t mask = 0;
	uint8_t bMask = 0x0f;

	switch (groupMode(0)) { // GROUP_A
		case MODE_0:
			if (portCUpperMode() == MODE_OUTPUT)
				mask |= 0xf0;
			break;

		case MODE_1:
			if (portMode(PORT_A) == MODE_OUTPUT) {
				data |= m_obf[PORT_A] ? 0x80 : 0x00;
				mask |= 0x30;
			} else {
				data |= m_ibf[PORT_A] ? 0x20 : 0x00;
				mask |= 0xc0;
			}
			break;

		case MODE_2:
			bMask = 0x07;
			data |= m_ibf[PORT_A] ? 0x20 : 0x00;
			data |= m_obf[PORT_A] ? 0x80 : 0x00;
			break;
	}

	switch (groupMode(1)) { // GROUP_B
		case MODE_0:
			if (portCLowerMode() == MODE_OUTPUT)
				mask |= bMask;
			break;

		case MODE_1:
			if (portMode(PORT_B) == MODE_OUTPUT)
				data |= m_obf[PORT_B] ? 0x02 : 0x00;
			else
				data |= m_ibf[PORT_B] ? 0x02 : 0x00;
			break;
	}

	data |= m_output[PORT_C] & mask;
	if (outC) outC(data);
}

void I8255::setMode(uint8_t data)
{
	m_control = data;

	// group A
	m_input[PORT_A] = 0;
	m_ibf[PORT_A] = 0;
	m_obf[PORT_A] = 1;
	m_inte[PORT_A] = 0;
	m_inte1 = 0;
	m_inte2 = 0;
	m_output[PORT_A] = 0;
	if (portMode(PORT_A) == MODE_OUTPUT)
		driveOut(PORT_A);
	else if (outA)
		outA(inA ? inA() : 0xff); // floating input side, mirror what a real idle bus shows

	// group B
	m_input[PORT_B] = 0;
	m_ibf[PORT_B] = 0;
	m_obf[PORT_B] = 1;
	m_inte[PORT_B] = 0;
	m_output[PORT_B] = 0;
	if (portMode(PORT_B) == MODE_OUTPUT)
		driveOut(PORT_B);
	else if (outB)
		outB(inB ? inB() : 0xff);

	m_input[PORT_C] = 0;
	m_output[PORT_C] = 0;
	outputPC();
}

void I8255::setPcBit(int bit, int state)
{
	m_output[PORT_C] &= ~(uint8_t)(1 << bit);
	m_output[PORT_C] |= (uint8_t)(state << bit);

	if (groupMode(0) == MODE_1) {
		if (portMode(PORT_A) == MODE_OUTPUT) {
			if (bit == 7) m_obf[PORT_A] = state;
			else if (bit == 6) m_inte[PORT_A] = state;
		} else {
			if (bit == 5) m_ibf[PORT_A] = state;
			else if (bit == 4) m_inte[PORT_A] = state;
		}
	} else if (groupMode(0) == MODE_2) {
		switch (bit) {
			case 4: m_inte2 = state; break;
			case 5: m_ibf[PORT_A] = state; break;
			case 6: m_inte1 = state; break;
			case 7: m_obf[PORT_A] = state; break;
			default: break;
		}
	}

	if (groupMode(1) == MODE_1) {
		if (bit == 1) {
			if (portMode(PORT_B) == MODE_OUTPUT) m_obf[PORT_B] = state;
			else m_ibf[PORT_B] = state;
		} else if (bit == 2) {
			m_inte[PORT_B] = state;
		}
	}

	outputPC();
}

uint8_t I8255::read(int reg)
{
	switch (reg & 3) {
		case PORT_A:
			switch (groupMode(0)) {
				case MODE_0: return readMode0(PORT_A);
				case MODE_1: return readMode1(PORT_A);
				default:     return readMode2();
			}
		case PORT_B:
			return (groupMode(1) == MODE_1) ? readMode1(PORT_B) : readMode0(PORT_B);
		case PORT_C:
			return readPC();
		default: // Control -- real 8255 doesn't support reading it back;
			return m_control;
	}
}

void I8255::write(int reg, uint8_t data)
{
	switch (reg & 3) {
		case PORT_A:
			switch (groupMode(0)) {
				case MODE_0: writeMode0(PORT_A, data); break;
				case MODE_1: writeMode1(PORT_A, data); break;
				default:     writeMode2(data); break;
			}
			break;
		case PORT_B:
			if (groupMode(1) == MODE_1) writeMode1(PORT_B, data);
			else writeMode0(PORT_B, data);
			break;
		case PORT_C:
			m_output[PORT_C] = data;
			outputPC();
			break;
		default: // Control
			if (data & kModeSet) {
				setMode(data);
			} else {
				int bit = (data >> 1) & 0x07;
				int state = data & 1;
				setPcBit(bit, state);
			}
			break;
	}
}
