#include "Dandanator.h"
#include <cstdio>
#include <cstring>

Dandanator::Dandanator()
{
	initPlaceholderRom();
}

// Hand-assembled, clean-room (same idea as Multiface::initPlaceholderRom()
// -- not a dump of any real romset, no copyrighted game content): just
// enough to be externally observable once bank 0 is paged in at address
// 0x0000 (where the Z80 starts fetching immediately after the reset that
// pressButton() triggers).
//   0000: 3E 05         LD A, 0x05        ; cyan border
//   0002: D3 FE         OUT (0xFE), A
//   0004: 3E 28         LD A, 0x28        ; BORDCR format is colour<<3 (see Multiface.cpp)
//   0006: 32 48 5C      LD (0x5C48), A
//   0009: 3E BB         LD A, 0xBB        ; marker byte, for MEM/PEEK confirmation
//   000B: 32 00 C0      LD (0xC000), A    ; plain RAM, safe regardless of machine type
//   000E: 18 FE         JR $              ; park here -- no menu in this placeholder
// All other banks are left zeroed (an all-NOP ROM is harmless if ever
// shown) -- a real 512KB romset loaded via loadRom() overwrites all of
// this.
void Dandanator::initPlaceholderRom()
{
	for (auto& b : banks_) {
		memset(b.data, 0x00, MemoryMap::PAGE_SIZE);
		b.isRom = true;
	}
	static const uint8_t code[] = {
		0x3E, 0x05, 0xD3, 0xFE, 0x3E, 0x28, 0x32, 0x48, 0x5C,
		0x3E, 0xBB, 0x32, 0x00, 0xC0, 0x18, 0xFE
	};
	memcpy(banks_[0].data, code, sizeof(code));
}

bool Dandanator::loadRom(const char* path)
{
	// Read straight into banks_ page by page rather than through one
	// 512KB stack buffer (unlike Multiface's single 8KB page, this one's
	// big enough that an intermediate stack copy is worth avoiding).
	FILE* pf = fopen(path, "rb");
	if (pf == NULL) {
		fprintf(stderr, "Dandanator::loadRom: cannot open %s\n", path);
		return false;
	}
	for (int i = 0; i < NUM_BANKS * 2; i++) {
		if (fread(banks_[i].data, 1, MemoryMap::PAGE_SIZE, pf) != (size_t)MemoryMap::PAGE_SIZE) {
			fclose(pf);
			fprintf(stderr, "Dandanator::loadRom: %s is too short, expected exactly %d bytes\n", path, ROM_SIZE);
			return false;
		}
		banks_[i].isRom = true;
	}
	uint8_t extra;
	bool hasTrailingData = fread(&extra, 1, 1, pf) == 1;
	fclose(pf);
	if (hasTrailingData) {
		fprintf(stderr, "Dandanator::loadRom: %s is larger than expected %d bytes\n", path, ROM_SIZE);
		return false;
	}
	printf("Dandanator::loadRom: loaded 512K romset from %s\n", path);
	return true;
}

// See the bankChangePending_ comment in Dandanator.h for why this is
// deferred rather than called straight from onMemoryWrite()/pressButton().
void Dandanator::applyPaging(MemoryMap& memory)
{
	if (activeBank_ == PASSTHROUGH_BANK) {
		// Same split as Multiface::pageOut(): just release ROMCS and let
		// MinZX's reapplyMachinePaging() (triggered by the edge check
		// right after this call returns) decide what belongs at pages
		// 0/1 -- a MACHINE-level question (128K's current 0x7FFD latch
		// may have changed while we were active), not ours to answer.
		memory.setRomcsAsserted(false);
	} else {
		memory.mapPage(0, &banks_[activeBank_ * 2]);
		memory.mapPage(1, &banks_[activeBank_ * 2 + 1]);
		memory.setRomcsAsserted(true);
	}
}

void Dandanator::onInstructionFetch(uint16_t pc, MemoryMap& memory, uint32_t tstates)
{
	(void)pc; (void)tstates;
	if (!enabled_) return;
	if (bankChangePending_) {
		bankChangePending_ = false;
		activeBank_ = pendingBank_;
		applyPaging(memory);
	}
}

bool Dandanator::takeResetRequest()
{
	bool r = wantsReset_;
	wantsReset_ = false;
	return r;
}

bool Dandanator::takeNMIRequest()
{
	bool r = wantsNMI_;
	wantsNMI_ = false;
	return r;
}

void Dandanator::pressButton()
{
	if (!enabled_) return;
	switchedOn_ = true;
	state_ = WaitNormal;
	pendingCmd_ = 0;
	pulseCount_ = 0;
	pendingBank_ = 0;
	bankChangePending_ = true; // picked up by the very next onInstructionFetch(),
	                           // which lands before the first post-reset fetch --
	                           // main.cpp calls MinZX::reset() right alongside this.
}

// Writes to ROM address space are normally a hard no-op (see class
// comment) -- MinZX::poke8()/MemoryMap::poke8() already handle the
// "don't corrupt ROM content" half; this is purely the side-channel
// command bus layered on top, via Peripheral::onMemoryWrite().
void Dandanator::onMemoryWrite(uint16_t addr, uint8_t value, MemoryMap& memory)
{
	(void)memory;
	if (!enabled_ || !switchedOn_ || addr > 3 || state_ == Disabled) return;

	switch (addr) {
	case 1: // command byte -- confirmed by repeating the SAME byte `value` times
		if (value != pendingCmd_ || pulseCount_ == 0) {
			pendingCmd_ = value;
			pulseCount_ = 1;
		} else {
			pulseCount_++;
		}
		if (value < 40 && pulseCount_ == value) {
			executeCommand();
			pulseCount_ = 0;
		}
		break;

	case 2:
		data1_ = value;
		break;

	case 3:
		data2_ = value;
		break;

	case 0:
		// Confirmation pulse -- only meaningful for 3-byte "special"
		// commands (40 and up: 40-49 plus 52/DANCMD_MULTIPLY, confirmed
		// from Multiply's own firmware to use this exact same transport
		// -- see DandanatorCmd52Sink). Open-ended on purpose rather than
		// hard-capped at 49: a cmd<40 can never reach here anyway (it
		// always self-executes from the pulse-count path in case 1
		// above), so any cmd>=40 arriving via a confirm pulse is, by
		// construction, meant to use this transport -- executeCommand()
		// itself still no-ops anything it doesn't specifically recognize.
		if (pendingCmd_ >= 40)
			executeCommand();
		break;
	}
}

// Command 46: lock (1), unlock (16), or permanently disable (31) future
// commands. data1 must equal data2 or the pulse is ignored -- real
// hardware's own noise-immunity check, kept here for fidelity even
// though nothing in software generates mismatched copies deliberately.
void Dandanator::runCommand46()
{
	if (data1_ != data2_) return;
	if (data1_ == 1) state_ = Locked;
	else if (data1_ == 16) state_ = WaitNormal;
	else if (data1_ == 31) state_ = Disabled;
}

void Dandanator::executeCommand()
{
	uint8_t cmd = pendingCmd_;

	// While Locked, ONLY command 46 is ever processed -- matches real
	// hardware: a normal (1-39) command still gets pulse-counted and
	// "arrives" here (see onMemoryWrite()), it's just discarded.
	if (state_ == Locked) {
		if (cmd == 46) runCommand46();
		return;
	}

	if (cmd >= 1 && cmd <= 33) {
		// Bank switch: cmd 1 -> bank 0, ... cmd 33 -> bank 32 (passthrough).
		pendingBank_ = cmd - 1;
		bankChangePending_ = true;
	}
	else if (cmd == 34) {
		// Switch to the real Spectrum ROM AND lock future commands --
		// used by a romset's menu right before handing control to a
		// loaded game, so the game's own ROM-space writes (if any)
		// can't accidentally be misread as Dandanator commands.
		pendingBank_ = PASSTHROUGH_BANK;
		bankChangePending_ = true;
		state_ = Locked;
	}
	else if (cmd == 36) {
		wantsReset_ = true;
	}
	else if (cmd == 40) {
		// Fast switch: data1 = bank+1 (1-based, same encoding as the
		// 1-33 bank-switch commands above), data2 = action bitmask.
		pendingBank_ = data1_ - 1;
		bankChangePending_ = true;
		if (data2_ & 0x08) state_ = Disabled;
		if (data2_ & 0x04) state_ = Locked;
		if (data2_ & 0x02) wantsNMI_ = true;
		if (data2_ & 0x01) wantsReset_ = true;
	}
	else if (cmd == 46) {
		runCommand46();
	}
	else if (cmd == 52) {
		// DANCMD_MULTIPLY: data1 is a fixed marker (DANDAT_MULTIPLY == 1
		// in the real firmware) identifying this as Multiply-addressed
		// traffic rather than a plain unrecognized command; data2 is the
		// actual payload byte. See DandanatorCmd52Sink.
		if (data1_ == 1 && cmd52Sink_) cmd52Sink_->onDandanatorByte(data2_);
	}
	// 35, 37, 38, 39 (remember reset slot -- not implemented, matches
	// ZEsarUX's own gap), 41-43 ("unusable on emulation" per ZEsarUX),
	// 44-45, 47-51, 53+: no-op.
}

std::string Dandanator::debugCommand(const std::vector<std::string>& toks)
{
	if (toks.empty() || toks[0] != "DANDANATOR")
		return std::string();

	// "DANDANATOR BUTTON" -- press the physical button over the debug
	// protocol, same contract as main.cpp's F9 key binding (which also
	// issues a top-level "RESET" right alongside this one, same two-
	// calls-together split -- see Dandanator::pressButton()'s comment).
	if (toks.size() >= 2 && toks[1] == "BUTTON") {
		pressButton();
		return "OK";
	}

	char out[160];
	snprintf(out, sizeof(out), "OK enabled=%d switchedOn=%d bank=%d state=%d",
	         enabled_ ? 1 : 0, switchedOn_ ? 1 : 0, activeBank_, (int)state_);
	return out;
}
