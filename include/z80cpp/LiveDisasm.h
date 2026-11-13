#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// POC (2026-11-11, user's own design): a "just in time" live disassembler --
// a PC-keyed map built DURING execution, not from a post-hoc trace log like
// tools/sdi_disasm.py. The hook point (FddStation::StationOps::fetchOpcode())
// calls recordExecution() on every instruction fetch; the map grows to cover
// exactly the code that was ACTUALLY executed, nothing more.
//
// Self-modifying-code policy (one of the two open design questions from the
// idea note): ALWAYS overwrite on every visit, never "first-seen wins" --
// decoding a single instruction is cheap enough that there is no real cost to
// re-decoding on every hit, and this keeps the map honest if the underlying
// bytes at an address ever change between visits (the alternative, detecting
// and flagging a byte-mismatch conflict, is a possible future refinement, not
// needed for this POC).
//
// Opcode coverage is DELIBERATELY incomplete for this POC, but now spans all
// four prefix families (CB/ED/DD/FD) in addition to the unprefixed table --
// see decodeOne()/decodeCB()/decodeED()/decodeIndexed()'s own comments for
// exactly what's covered vs. falls back to a labeled placeholder.
class LiveDisasm
{
public:
	struct Entry {
		std::vector<uint8_t> bytes; // raw opcode bytes, as last seen
		std::string mnemonic;       // decoded text, as last seen
		uint32_t hitCount = 0;      // how many times this PC was fetched
	};

	// peekFn(address) must return the byte at that address with NO side
	// effects (no T-state accounting, no port latching) -- same contract
	// Z80operations::peek8() has for debug/inspection reads elsewhere in
	// this codebase.
	template <typename PeekFn>
	void recordExecution(uint16_t pc, PeekFn peekFn)
	{
		uint8_t raw[4];
		for (int i = 0; i < 4; i++) raw[i] = peekFn((uint16_t)(pc + i));

		int len = 1;
		std::string mnem = decodeOne(raw, len);

		Entry& e = entries_[pc];
		e.bytes.assign(raw, raw + len);
		e.mnemonic = mnem;
		e.hitCount++;
	}

	size_t size() const { return entries_.size(); }

	// Dumps the map sorted by address (std::map already keeps that order),
	// one line per distinct executed address: address, raw bytes, mnemonic,
	// hit count -- the same shape sdi_disasm.py's labeled listing uses, but
	// built live instead of from a trace log.
	std::string dump() const
	{
		std::string out;
		char line[256];
		for (const auto& kv : entries_) {
			uint16_t addr = kv.first;
			const Entry& e = kv.second;
			char hex[13] = {0};
			char* p = hex;
			for (uint8_t b : e.bytes) { snprintf(p, 3, "%02x", b); p += 2; }
			snprintf(line, sizeof(line), "%04x: %-8s %-20s  [x%u]\n",
			         addr, hex, e.mnemonic.c_str(), e.hitCount);
			out += line;
		}
		return out;
	}

	void clear() { entries_.clear(); }

private:
	// Shared register/condition/ALU-op/rotate-op name tables -- "inline" so
	// a single definition in this header works across every translation
	// unit that includes it (C++17 inline variables), no separate .cpp
	// needed.
	static inline const char* kReg8[8]    = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
	static inline const char* kReg16[4]   = {"BC", "DE", "HL", "SP"};
	static inline const char* kReg16Af[4] = {"BC", "DE", "HL", "AF"};
	static inline const char* kCond[8]    = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
	static inline const char* kAlu[8]     = {"ADD A,", "ADC A,", "SUB ", "SBC A,", "AND ", "XOR ", "OR ", "CP "};
	static inline const char* kRot[8]     = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"};

	// CB-prefixed: rotate/shift/BIT/RES/SET on an 8-bit register (or (HL)).
	// raw points at the CB byte itself; len is relative to raw[0] (always 2
	// here). Full coverage of the documented CB space, including the
	// undocumented SLL ("SL1") slot at the rotate group's bit pattern 6.
	static std::string decodeCB(const uint8_t* raw, int& len)
	{
		char buf[32];
		uint8_t op2 = raw[1];
		uint8_t reg = op2 & 7;
		uint8_t group = (op2 >> 6) & 3;
		uint8_t sub = (op2 >> 3) & 7;
		len = 2;
		if (group == 0) { snprintf(buf, sizeof(buf), "%s %s", kRot[sub], kReg8[reg]); return buf; }
		if (group == 1) { snprintf(buf, sizeof(buf), "BIT %d,%s", sub, kReg8[reg]); return buf; }
		if (group == 2) { snprintf(buf, sizeof(buf), "RES %d,%s", sub, kReg8[reg]); return buf; }
		snprintf(buf, sizeof(buf), "SET %d,%s", sub, kReg8[reg]);
		return buf;
	}

	// ED-prefixed: the commonly-seen documented subset -- IN r,(C)/OUT (C),r,
	// SBC/ADC HL,rr, LD (nn),rr/LD rr,(nn), NEG, RETN/RETI, IM 0/1/2,
	// LD I,A/LD R,A/LD A,I/LD A,R, RRD/RLD, and the full LDI/LDD/CPI/CPD/
	// INI/IND/OUTI/OUTD block (plain and *R repeating forms). Anything else
	// ED-prefixed (the less common bit-twiddly undocumented duplicates)
	// falls back to a placeholder -- not a POC goal to cover every byte.
	static std::string decodeED(const uint8_t* raw, int& len)
	{
		char buf[32];
		uint8_t op2 = raw[1];
		len = 2;

		switch (op2) {
		case 0x47: return "LD I,A";
		case 0x4F: return "LD R,A";
		case 0x57: return "LD A,I";
		case 0x5F: return "LD A,R";
		case 0x44: case 0x4C: case 0x54: case 0x5C:
		case 0x64: case 0x6C: case 0x74: case 0x7C: return "NEG";
		case 0x45: case 0x4D: case 0x55: case 0x5D:
		case 0x65: case 0x6D: case 0x75: case 0x7D: return (op2 == 0x4D) ? "RETI" : "RETN";
		case 0x46: case 0x4E: case 0x66: case 0x6E: return "IM 0";
		case 0x56: case 0x76: return "IM 1";
		case 0x5E: case 0x7E: return "IM 2";
		case 0x6F: return "RLD";
		case 0x67: return "RRD";
		case 0xA0: return "LDI";
		case 0xA1: return "CPI";
		case 0xA2: return "INI";
		case 0xA3: return "OUTI";
		case 0xA8: return "LDD";
		case 0xA9: return "CPD";
		case 0xAA: return "IND";
		case 0xAB: return "OUTD";
		case 0xB0: return "LDIR";
		case 0xB1: return "CPIR";
		case 0xB2: return "INIR";
		case 0xB3: return "OTIR";
		case 0xB8: return "LDDR";
		case 0xB9: return "CPDR";
		case 0xBA: return "INDR";
		case 0xBB: return "OTDR";
		default: break;
		}

		// IN r,(C) / OUT (C),r -- r==6 is the undocumented flags-only form.
		if ((op2 & 0xC7) == 0x40) {
			uint8_t r = (op2 >> 3) & 7;
			snprintf(buf, sizeof(buf), r == 6 ? "IN (C)" : "IN %s,(C)", kReg8[r]);
			return buf;
		}
		if ((op2 & 0xC7) == 0x41) {
			uint8_t r = (op2 >> 3) & 7;
			snprintf(buf, sizeof(buf), r == 6 ? "OUT (C),0" : "OUT (C),%s", kReg8[r]);
			return buf;
		}
		// SBC HL,rr / ADC HL,rr
		if ((op2 & 0xCF) == 0x42) { snprintf(buf, sizeof(buf), "SBC HL,%s", kReg16[(op2 >> 4) & 3]); return buf; }
		if ((op2 & 0xCF) == 0x4A) { snprintf(buf, sizeof(buf), "ADC HL,%s", kReg16[(op2 >> 4) & 3]); return buf; }
		// LD (nn),rr / LD rr,(nn)
		if ((op2 & 0xCF) == 0x43) {
			len = 4;
			snprintf(buf, sizeof(buf), "LD (0x%04X),%s", (uint16_t)(raw[2] | (raw[3] << 8)), kReg16[(op2 >> 4) & 3]);
			return buf;
		}
		if ((op2 & 0xCF) == 0x4B) {
			len = 4;
			snprintf(buf, sizeof(buf), "LD %s,(0x%04X)", kReg16[(op2 >> 4) & 3], (uint16_t)(raw[2] | (raw[3] << 8)));
			return buf;
		}

		snprintf(buf, sizeof(buf), "??? (ED 0x%02X)", op2);
		return buf;
	}

	// DD/FD-prefixed (IX/IY): the common documented forms -- 16-bit direct
	// load/store/arith (LD IX,nn / LD (nn),IX / LD IX,(nn) / INC IX / DEC IX
	// / ADD IX,rr), PUSH/POP/EX (SP),IX/JP (IX)/LD SP,IX, and the
	// displacement-addressed memory forms (INC/DEC (IX+d), LD (IX+d),n,
	// LD r,(IX+d)/LD (IX+d),r, ALU A,(IX+d)), plus DD CB d op bit-ops on
	// (IX+d) via decodeCB()'s own group/bit logic (ignoring the
	// undocumented copy-to-register variant). Undocumented IXH/IXL
	// half-register forms are NOT covered -- fall back to a placeholder.
	// raw points at the DD/FD prefix byte itself; len is relative to raw[0].
	static std::string decodeIndexed(const char* idx, const uint8_t* raw, int& len)
	{
		char buf[48];
		uint8_t op = raw[1];

		if (op == 0xCB) {
			// DD CB d op -- bit op on (IX+d). Reuse decodeCB()'s group/bit
			// logic against raw[3] (the real op byte), but the operand is
			// always the displaced memory location here, never a register.
			len = 4;
			int8_t d = (int8_t)raw[2];
			uint8_t op2 = raw[3];
			uint8_t group = (op2 >> 6) & 3;
			uint8_t sub = (op2 >> 3) & 7;
			if (group == 0) snprintf(buf, sizeof(buf), "%s (%s%+d)", kRot[sub], idx, d);
			else if (group == 1) snprintf(buf, sizeof(buf), "BIT %d,(%s%+d)", sub, idx, d);
			else if (group == 2) snprintf(buf, sizeof(buf), "RES %d,(%s%+d)", sub, idx, d);
			else snprintf(buf, sizeof(buf), "SET %d,(%s%+d)", sub, idx, d);
			return buf;
		}

		if (op == 0x21) { len = 4; snprintf(buf, sizeof(buf), "LD %s,0x%04X", idx, (uint16_t)(raw[2] | (raw[3] << 8))); return buf; }
		if (op == 0x22) { len = 4; snprintf(buf, sizeof(buf), "LD (0x%04X),%s", (uint16_t)(raw[2] | (raw[3] << 8)), idx); return buf; }
		if (op == 0x2A) { len = 4; snprintf(buf, sizeof(buf), "LD %s,(0x%04X)", idx, (uint16_t)(raw[2] | (raw[3] << 8))); return buf; }
		if (op == 0x23) { len = 2; snprintf(buf, sizeof(buf), "INC %s", idx); return buf; }
		if (op == 0x2B) { len = 2; snprintf(buf, sizeof(buf), "DEC %s", idx); return buf; }
		if (op == 0x09 || op == 0x19 || op == 0x29 || op == 0x39) {
			len = 2;
			const char* rr = (op == 0x09) ? "BC" : (op == 0x19) ? "DE" : (op == 0x29) ? idx : "SP";
			snprintf(buf, sizeof(buf), "ADD %s,%s", idx, rr);
			return buf;
		}
		if (op == 0xE5) { len = 2; snprintf(buf, sizeof(buf), "PUSH %s", idx); return buf; }
		if (op == 0xE1) { len = 2; snprintf(buf, sizeof(buf), "POP %s", idx); return buf; }
		if (op == 0xE3) { len = 2; snprintf(buf, sizeof(buf), "EX (SP),%s", idx); return buf; }
		if (op == 0xE9) { len = 2; snprintf(buf, sizeof(buf), "JP (%s)", idx); return buf; }
		if (op == 0xF9) { len = 2; snprintf(buf, sizeof(buf), "LD SP,%s", idx); return buf; }
		if (op == 0x34) { len = 3; snprintf(buf, sizeof(buf), "INC (%s%+d)", idx, (int8_t)raw[2]); return buf; }
		if (op == 0x35) { len = 3; snprintf(buf, sizeof(buf), "DEC (%s%+d)", idx, (int8_t)raw[2]); return buf; }
		if (op == 0x36) { len = 4; snprintf(buf, sizeof(buf), "LD (%s%+d),0x%02X", idx, (int8_t)raw[2], raw[3]); return buf; }
		// LD r,(IX+d) -- dest field (op>>3)&7, low 3 bits fixed at 6
		if ((op & 0xC7) == 0x46 && op != 0x76) {
			len = 3;
			snprintf(buf, sizeof(buf), "LD %s,(%s%+d)", kReg8[(op >> 3) & 7], idx, (int8_t)raw[2]);
			return buf;
		}
		// LD (IX+d),r -- low 3 bits is the source register, must not be 6 itself
		if ((op & 0xF8) == 0x70 && (op & 7) != 6) {
			len = 3;
			snprintf(buf, sizeof(buf), "LD (%s%+d),%s", idx, (int8_t)raw[2], kReg8[op & 7]);
			return buf;
		}
		// ALU A,(IX+d)
		if (op >= 0x80 && op <= 0xBF && (op & 7) == 6) {
			len = 3;
			snprintf(buf, sizeof(buf), "%s(%s%+d)", kAlu[(op >> 3) & 7], idx, (int8_t)raw[2]);
			return buf;
		}

		len = 2;
		snprintf(buf, sizeof(buf), "??? (%s 0x%02X)", (idx[1] == 'X') ? "DD" : "FD", op);
		return buf;
	}

	// Decodes exactly ONE instruction starting at raw[0]. Sets len to the
	// number of bytes consumed (1-4) and returns its mnemonic text.
	// Unprefixed opcodes covering: NOP, LD r,r'/r,n/rr,nn/(nn),A-and-back/
	// (nn),HL-and-back/SP,HL/(BC),A/(DE),A/A,(BC)/A,(DE), INC/DEC r and rr,
	// ADD HL,rr, arithmetic/logic r and n (ADD/ADC/SUB/SBC/AND/XOR/OR/CP),
	// JP/JP cc/JR/JR cc/DJNZ, CALL/CALL cc/RET/RET cc, PUSH/POP,
	// IN A,(n)/OUT (n),A, EX DE,HL/EX (SP),HL/EX AF,AF'/EXX, DI/EI, HALT,
	// RLCA/RRCA/RLA/RRA, CPL/SCF/CCF, RST n. CB/ED/DD/FD prefixes delegate
	// to their own decode* helpers above.
	static std::string decodeOne(const uint8_t* raw, int& len)
	{
		char buf[48];
		uint8_t op = raw[0];

		if (op == 0xCB) return decodeCB(raw, len);
		if (op == 0xED) return decodeED(raw, len);
		if (op == 0xDD) return decodeIndexed("IX", raw, len);
		if (op == 0xFD) return decodeIndexed("IY", raw, len);

		if (op == 0x00) { len = 1; return "NOP"; }
		if (op == 0x76) { len = 1; return "HALT"; }
		if (op == 0xF3) { len = 1; return "DI"; }
		if (op == 0xFB) { len = 1; return "EI"; }
		if (op == 0xC9) { len = 1; return "RET"; }
		if (op == 0xEB) { len = 1; return "EX DE,HL"; }
		if (op == 0xE3) { len = 1; return "EX (SP),HL"; }
		if (op == 0x08) { len = 1; return "EX AF,AF'"; }
		if (op == 0xD9) { len = 1; return "EXX"; }
		if (op == 0xF9) { len = 1; return "LD SP,HL"; }
		if (op == 0x07) { len = 1; return "RLCA"; }
		if (op == 0x0F) { len = 1; return "RRCA"; }
		if (op == 0x17) { len = 1; return "RLA"; }
		if (op == 0x1F) { len = 1; return "RRA"; }
		if (op == 0x2F) { len = 1; return "CPL"; }
		if (op == 0x37) { len = 1; return "SCF"; }
		if (op == 0x3F) { len = 1; return "CCF"; }
		// LD (BC),A / LD A,(BC) / LD (DE),A / LD A,(DE)
		if (op == 0x02) { len = 1; return "LD (BC),A"; }
		if (op == 0x0A) { len = 1; return "LD A,(BC)"; }
		if (op == 0x12) { len = 1; return "LD (DE),A"; }
		if (op == 0x1A) { len = 1; return "LD A,(DE)"; }

		// LD r,r' (0x40-0x7F, excluding 0x76=HALT already handled above)
		if (op >= 0x40 && op <= 0x7F) {
			len = 1;
			snprintf(buf, sizeof(buf), "LD %s,%s", kReg8[(op >> 3) & 7], kReg8[op & 7]);
			return buf;
		}
		// LD r,n
		if ((op & 0xC7) == 0x06) {
			len = 2;
			snprintf(buf, sizeof(buf), "LD %s,0x%02X", kReg8[(op >> 3) & 7], raw[1]);
			return buf;
		}
		// LD rr,nn
		if ((op & 0xCF) == 0x01) {
			len = 3;
			uint16_t nn = (uint16_t)(raw[1] | (raw[2] << 8));
			snprintf(buf, sizeof(buf), "LD %s,0x%04X", kReg16[(op >> 4) & 3], nn);
			return buf;
		}
		// INC/DEC rr
		if ((op & 0xCF) == 0x03) { len = 1; snprintf(buf, sizeof(buf), "INC %s", kReg16[(op >> 4) & 3]); return buf; }
		if ((op & 0xCF) == 0x0B) { len = 1; snprintf(buf, sizeof(buf), "DEC %s", kReg16[(op >> 4) & 3]); return buf; }
		// ADD HL,rr
		if ((op & 0xCF) == 0x09) { len = 1; snprintf(buf, sizeof(buf), "ADD HL,%s", kReg16[(op >> 4) & 3]); return buf; }
		// INC/DEC r
		if ((op & 0xC7) == 0x04) { len = 1; snprintf(buf, sizeof(buf), "INC %s", kReg8[(op >> 3) & 7]); return buf; }
		if ((op & 0xC7) == 0x05) { len = 1; snprintf(buf, sizeof(buf), "DEC %s", kReg8[(op >> 3) & 7]); return buf; }
		// PUSH/POP rr (AF instead of SP for bit pattern 3)
		if ((op & 0xCF) == 0xC5) { len = 1; snprintf(buf, sizeof(buf), "PUSH %s", kReg16Af[(op >> 4) & 3]); return buf; }
		if ((op & 0xCF) == 0xC1) { len = 1; snprintf(buf, sizeof(buf), "POP %s", kReg16Af[(op >> 4) & 3]); return buf; }
		// ALU A,r (0x80-0xBF)
		if (op >= 0x80 && op <= 0xBF) {
			len = 1;
			snprintf(buf, sizeof(buf), "%s%s", kAlu[(op >> 3) & 7], kReg8[op & 7]);
			return buf;
		}
		// ALU A,n
		if ((op & 0xC7) == 0xC6) {
			len = 2;
			snprintf(buf, sizeof(buf), "%s0x%02X", kAlu[(op >> 3) & 7], raw[1]);
			return buf;
		}
		// JR e / JR cc,e / DJNZ e
		if (op == 0x18) { len = 2; snprintf(buf, sizeof(buf), "JR %+d", (int8_t)raw[1] + 2); return buf; }
		if (op == 0x10) { len = 2; snprintf(buf, sizeof(buf), "DJNZ %+d", (int8_t)raw[1] + 2); return buf; }
		if ((op & 0xE7) == 0x20) {
			len = 2;
			snprintf(buf, sizeof(buf), "JR %s,%+d", kCond[(op >> 3) & 3], (int8_t)raw[1] + 2);
			return buf;
		}
		// JP nn / JP cc,nn
		if (op == 0xC3) { len = 3; snprintf(buf, sizeof(buf), "JP 0x%04X", (uint16_t)(raw[1] | (raw[2] << 8))); return buf; }
		if ((op & 0xC7) == 0xC2) {
			len = 3;
			snprintf(buf, sizeof(buf), "JP %s,0x%04X", kCond[(op >> 3) & 7], (uint16_t)(raw[1] | (raw[2] << 8)));
			return buf;
		}
		// CALL nn / CALL cc,nn
		if (op == 0xCD) { len = 3; snprintf(buf, sizeof(buf), "CALL 0x%04X", (uint16_t)(raw[1] | (raw[2] << 8))); return buf; }
		if ((op & 0xC7) == 0xC4) {
			len = 3;
			snprintf(buf, sizeof(buf), "CALL %s,0x%04X", kCond[(op >> 3) & 7], (uint16_t)(raw[1] | (raw[2] << 8)));
			return buf;
		}
		// RET cc
		if ((op & 0xC7) == 0xC0) { len = 1; snprintf(buf, sizeof(buf), "RET %s", kCond[(op >> 3) & 7]); return buf; }
		// RST n
		if ((op & 0xC7) == 0xC7) { len = 1; snprintf(buf, sizeof(buf), "RST 0x%02X", op & 0x38); return buf; }
		// IN A,(n) / OUT (n),A
		if (op == 0xDB) { len = 2; snprintf(buf, sizeof(buf), "IN A,(0x%02X)", raw[1]); return buf; }
		if (op == 0xD3) { len = 2; snprintf(buf, sizeof(buf), "OUT (0x%02X),A", raw[1]); return buf; }
		// LD (nn),A / LD A,(nn) / LD (nn),HL / LD HL,(nn)
		if (op == 0x32) { len = 3; snprintf(buf, sizeof(buf), "LD (0x%04X),A", (uint16_t)(raw[1] | (raw[2] << 8))); return buf; }
		if (op == 0x3A) { len = 3; snprintf(buf, sizeof(buf), "LD A,(0x%04X)", (uint16_t)(raw[1] | (raw[2] << 8))); return buf; }
		if (op == 0x22) { len = 3; snprintf(buf, sizeof(buf), "LD (0x%04X),HL", (uint16_t)(raw[1] | (raw[2] << 8))); return buf; }
		if (op == 0x2A) { len = 3; snprintf(buf, sizeof(buf), "LD HL,(0x%04X)", (uint16_t)(raw[1] | (raw[2] << 8))); return buf; }

		len = 1;
		snprintf(buf, sizeof(buf), "??? (0x%02X)", op);
		return buf;
	}

	std::map<uint16_t, Entry> entries_;
};
