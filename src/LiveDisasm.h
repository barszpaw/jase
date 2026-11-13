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
// Opcode coverage is DELIBERATELY incomplete for this POC -- common
// unprefixed instructions only (enough to read real Z80 firmware code at a
// glance), with CB/DD/ED/FD-prefixed opcodes shown as a labeled placeholder
// rather than decoded. See decodeOne()'s own comment for the exact set.
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
	// Decodes exactly ONE instruction starting at raw[0]. Sets len to the
	// number of bytes consumed (1-4) and returns its mnemonic text.
	// Unprefixed opcodes covering: NOP, LD r,r'/r,n/rr,nn/(nn),A-and-back/
	// (nn),HL-and-back/SP,HL, INC/DEC r and rr, arithmetic/logic r and n
	// (ADD/ADC/SUB/SBC/AND/XOR/OR/CP), JP/JP cc/JR/JR cc/DJNZ, CALL/CALL cc/
	// RET/RET cc, PUSH/POP, IN A,(n)/OUT (n),A, EX DE,HL/EX AF,AF'/EXX,
	// DI/EI, HALT, RLCA/RRCA/RLA/RRA, RST n. Everything else (CB/ED/DD/FD
	// prefixes, and any unrecognized unprefixed byte) falls back to a
	// labeled placeholder -- not a POC goal to cover every opcode.
	static std::string decodeOne(const uint8_t* raw, int& len)
	{
		static const char* kReg8[8] = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
		static const char* kReg16[4] = {"BC", "DE", "HL", "SP"};
		static const char* kCond[8]  = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
		char buf[48];
		uint8_t op = raw[0];

		if (op == 0xCB) { len = 2; return "(CB-prefixed, not decoded)"; }
		if (op == 0xED) { len = 2; return "(ED-prefixed, not decoded)"; }
		if (op == 0xDD) { len = 2; return "(DD-prefixed/IX, not decoded)"; }
		if (op == 0xFD) { len = 2; return "(FD-prefixed/IY, not decoded)"; }

		if (op == 0x00) { len = 1; return "NOP"; }
		if (op == 0x76) { len = 1; return "HALT"; }
		if (op == 0xF3) { len = 1; return "DI"; }
		if (op == 0xFB) { len = 1; return "EI"; }
		if (op == 0xC9) { len = 1; return "RET"; }
		if (op == 0xEB) { len = 1; return "EX DE,HL"; }
		if (op == 0x08) { len = 1; return "EX AF,AF'"; }
		if (op == 0xD9) { len = 1; return "EXX"; }
		if (op == 0xF9) { len = 1; return "LD SP,HL"; }
		if (op == 0x07) { len = 1; return "RLCA"; }
		if (op == 0x0F) { len = 1; return "RRCA"; }
		if (op == 0x17) { len = 1; return "RLA"; }
		if (op == 0x1F) { len = 1; return "RRA"; }

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
		// INC/DEC r
		if ((op & 0xC7) == 0x04) { len = 1; snprintf(buf, sizeof(buf), "INC %s", kReg8[(op >> 3) & 7]); return buf; }
		if ((op & 0xC7) == 0x05) { len = 1; snprintf(buf, sizeof(buf), "DEC %s", kReg8[(op >> 3) & 7]); return buf; }
		// PUSH/POP rr (AF instead of SP for bit pattern 3)
		if ((op & 0xCF) == 0xC5) {
			len = 1;
			static const char* kReg16Af[4] = {"BC", "DE", "HL", "AF"};
			snprintf(buf, sizeof(buf), "PUSH %s", kReg16Af[(op >> 4) & 3]);
			return buf;
		}
		if ((op & 0xCF) == 0xC1) {
			len = 1;
			static const char* kReg16Af[4] = {"BC", "DE", "HL", "AF"};
			snprintf(buf, sizeof(buf), "POP %s", kReg16Af[(op >> 4) & 3]);
			return buf;
		}
		// ALU A,r (0x80-0xBF)
		if (op >= 0x80 && op <= 0xBF) {
			static const char* kAlu[8] = {"ADD A,", "ADC A,", "SUB ", "SBC A,", "AND ", "XOR ", "OR ", "CP "};
			len = 1;
			snprintf(buf, sizeof(buf), "%s%s", kAlu[(op >> 3) & 7], kReg8[op & 7]);
			return buf;
		}
		// ALU A,n
		if ((op & 0xC7) == 0xC6) {
			static const char* kAlu[8] = {"ADD A,", "ADC A,", "SUB ", "SBC A,", "AND ", "XOR ", "OR ", "CP "};
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
