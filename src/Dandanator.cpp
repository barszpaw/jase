#include "Dandanator.h"
#include <cstdio>
#include <cstring>

Dandanator::Dandanator()
{
	initPlaceholderRom();

	// Real hardware auto-engages on every power-on -- the manual's
	// "During Boot" button actions (left=launch diagnostics ROM,
	// right=boot the internal Spectrum ROM instead, both=PIC firmware
	// update) are all ALTERNATIVES you hold a button to get; doing
	// nothing is what shows the Dandanator's own menu (bank 0), since
	// that's the entire point of the cartridge -- no button needed for
	// the common case. pressButton() is for RUNTIME re-entry (the real
	// right button's "always loads the Dandanator menu", used to get
	// back to the menu from a running game), not the initial activation.
	switchedOn_ = true;
	state_ = WaitNormal;
	pendingBank_ = 0;
	bankChangePending_ = true;
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
	(void)pc;
	if (!enabled_) return;
	lastFetchTstates_ = tstates;

	// Finalize the field currently being pulse-counted once a short
	// quiet gap confirms pulsing has stopped (see class comment).
	// Wraparound note: tstates resets to a small value at the start of
	// each frame (see MinZX::update()), so a pulse recorded near the
	// end of one frame and checked early in the next makes these
	// subtractions "underflow" to a huge unsigned value -- which still
	// correctly exceeds the threshold, so the field/timeout is
	// (correctly) finalized rather than lost. tstates only increases
	// within a frame, so there's no direction where this underflow
	// could instead compute a falsely-small difference.
	if (burstActive_ && (tstates - lastPulseTstate_) >= kPulseIdleTstates) {
		uint8_t count = (uint8_t)(burstPulseCount_ & 0xFF); // 256 pulses -> 0, matching SENDNRCMD's own "0 is signaled by 256 pulses"
		burstPulseCount_ = 0;
		burstActive_ = false;
		onFieldComplete(count, tstates);
	}
	else if (awaitingNextField_ && (tstates - lastFieldCompleteTstate_) >= kFieldTimeoutTstates) {
		awaitingNextField_ = false;
		onFieldTimeout(tstates);
	}

	// See class comment + scheduleExecute(): the command's effect is
	// deliberately held back this long after the pulse that decided it,
	// same wraparound-safe subtraction as the checks above.
	if (commandPending_ && (tstates - pendingSinceTstate_) >= pendingDelayTstates_) {
		commandPending_ = false;
		executeCommand();
	}

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
	phase_ = PulsePhase::Command;
	pendingCmd_ = 0;
	burstPulseCount_ = 0;
	burstActive_ = false;
	awaitingNextField_ = false;
	commandPending_ = false; // discard any still-delayed command -- the menu/game context it belonged to no longer exists
	pendingBank_ = 0;
	bankChangePending_ = true; // picked up by the very next onInstructionFetch(),
	                           // which lands before the first post-reset fetch --
	                           // main.cpp calls MinZX::reset() right alongside this.
}

// AUTHORITATIVE as of 2026-10-31: the real PIC firmware source
// (`docs/2017.09.22 - Dandanator Mini PIC Firmware/XC8_Source/.../
// main.c`+`commands.h`) confirms the protocol-level special range is
// exactly 40-49 (`if ((SpectrumCommandCounter>39)&&(SpectrumCommandCounter<50))`
// in the TMR0 timeout handler) -- commands 1-39 (including 39 itself,
// CMD_SET_RESETSLOT, confirmed explicitly handled as a single-field
// command in main.c's Pending_Processing switch) are ALWAYS 1-field,
// firing immediately off their own pulse count with NO wait, same as
// everything outside 40-49. A real v10.4.3 romset's idle-loop status
// sender DOES send some of these (e.g. 39) via the full 4-field
// transport anyway, via a generic helper used indiscriminately -- but
// per this firmware source, those extra fields are harmless noise: a
// non-special command fires immediately and disables further pulse
// reception (`INTE=0`) until the main loop processes it, discarding
// anything sent in that gap on real hardware.
// Command 52 (DANCMD_MULTIPLY) is NOT part of this 2017 firmware's
// range at all -- Multiply support postdates this dump (confirmed via
// this project's own cmd52 stub test, see project memory: real
// v10.4.3-era hardware unambiguously DOES treat 52 as 4-field). A newer
// PIC firmware revision (not available to this project) almost
// certainly extended the special range to include it; 52 is kept here
// on that live evidence. 50 stays included on the same live-trace
// basis (observed arriving 4-field in the same idle-sender trace as
// 49/52, suggesting the same newer-firmware extension covers it too).
//
// BUG FOUND AND FIXED 2026-11-01: this switch used to list only a
// SUBSET of the authoritative 40-49 range {40,41,42,43,46,49} --
// despite this very comment already correctly stating the range is
// "exactly 40-49" -- an oversight from only including values actually
// observed live in a trace at the time, rather than the full range the
// primary source already gave us. Caught via a live
// `DANDANATOR_EXECLOG` diagnostic on a real `dandanator_01.rom` (Bomb
// Jack): its loader genuinely sends cmd 45 (CMD_SP_RETRIEVEDATA, see
// executeCommand()'s own comment) as a real 4-field special command;
// treating it as 1-field fired immediately with STALE data1_/data2_
// and then misread cmd 45's own data1/data2/confirm pulses as a brand
// new, bogus command -- a genuine protocol desync that cascaded into
// an unrelated-looking crash much later. Now the full 40-49 range is
// used directly instead of hand-picking, so this class of gap can't
// recur for any future command number already covered by the
// authoritative source.
bool Dandanator::needsMoreFields(uint8_t cmd)
{
	// 40-52 inclusive, confirmed 2026-10-31 via gpdasm disassembly of a
	// real v10.4.3 PIC firmware (merlinkv/ZX_Dandanator_MTD's
	// PIC_16F1826_Dandanator_Multiply_10_4_3.hex): the ISR's own command
	// dispatch range-checks the raw pulse count against exactly [40,52]
	// (`movlw 0x28; subwf pulseCount,W; ...; movlw 0x35; subwf
	// pulseCount,W; ...`, 0x28=40, 0x35=53-exclusive) before indexing a
	// 13-entry jump table (40..52) -- cmd 51 was previously missing here
	// (an oversight, not evidence it behaves differently; no live trace
	// had exercised it).
	return cmd >= 40 && cmd <= 52;
}

// Dispatches one just-completed field (see class comment) through the
// Command/Data1/Data2/Confirm sequence. `count` is address- and
// value-blind by construction -- purely how many writes to addr 0-3
// happened before a quiet gap, per real hardware's own mechanism.
void Dandanator::onFieldComplete(uint8_t count, uint32_t tstates)
{
	switch (phase_) {
	case PulsePhase::Command:
		pendingCmd_ = count;
		if (!needsMoreFields(count)) {
			// Scheduled kNormalConfirmDelayTstates out, NOT fired here --
			// the decision itself is still zero-latency (see class comment
			// for why a tight loader retry loop can't tolerate delaying
			// THAT), only the command's actual effect is now held back a
			// few dozen T-states to match real hardware.
			scheduleExecute(tstates, kNormalConfirmDelayTstates);
			return;
		}
		phase_ = PulsePhase::Data1;
		break;
	case PulsePhase::Data1:
		data1_ = count;
		phase_ = PulsePhase::Data2;
		break;
	case PulsePhase::Data2:
		data2_ = count;
		phase_ = PulsePhase::Confirm;
		break;
	case PulsePhase::Confirm:
		// The real confirm is documented as a single pulse (LONGSPCONF's
		// bare `LD (0),A`, no DJNZ around it) -- any completed field here
		// is treated as that confirm regardless of its own count, since
		// the count itself carries no meaning in this phase.
		scheduleExecute(tstates, kSpecialConfirmDelayTstates);
		phase_ = PulsePhase::Command;
		awaitingNextField_ = false;
		return;
	}
	// Armed to wait for the next field; onFieldTimeout() fires what's
	// been collected so far if nothing more arrives at all (safety net
	// only -- see kFieldTimeoutTstates' own comment).
	awaitingNextField_ = true;
	lastFieldCompleteTstate_ = tstates;
}

// The longer of the two timers (see class comment) elapsed with no
// further field arriving -- the command is complete with whatever
// fields were collected. For a genuine 1-field command (the common
// case: ordinary bank switches, and confirmed-live standalone sends
// like command 42) this is simply pendingCmd_ alone; executeCommand()
// only reads data1_/data2_ for commands confirmed to actually carry
// them, so stale leftovers from an earlier burst are never consulted.
// Scheduled with the "special" delay since only commands already routed
// into the multi-field pipeline (needsMoreFields()==true) ever reach
// this path.
void Dandanator::onFieldTimeout(uint32_t tstates)
{
	scheduleExecute(tstates, kSpecialConfirmDelayTstates);
	phase_ = PulsePhase::Command;
}

// Defers a just-decided command's effect by delayTstates, snapshotting
// its cmd/data1/data2 into scheduled*_ so a NEW command's pulses
// (already accumulating into pendingCmd_/data1_/data2_ via
// onFieldComplete()) can't corrupt it before the delay elapses -- see
// class comment and the scheduled*_ fields' own comment in Dandanator.h.
void Dandanator::scheduleExecute(uint32_t tstates, uint32_t delayTstates)
{
	if (commandPending_) {
		// A previous command's own delay hadn't elapsed yet -- real
		// hardware's INTE=0 gating would make this impossible (no new
		// pulses are even recognized until the pending one is processed),
		// but flush it now rather than silently losing it if it ever
		// does happen here.
		commandPending_ = false;
		executeCommand();
	}
	scheduledCmd_ = pendingCmd_;
	scheduledData1_ = data1_;
	scheduledData2_ = data2_;
	commandPending_ = true;
	pendingSinceTstate_ = tstates;
	pendingDelayTstates_ = delayTstates;
}

// Port 0x1F: the onboard Kempston interface's port, unconditionally
// claimed whenever enabled_ (preempts every other peripheral that also
// looks at this port -- Kempston plugin, a standalone Multiface -- same
// as real hardware's own joystick-socket-disable option; see class
// comment). cmd52Sink_ (Multiply, when registered) gets first refusal on
// the actual VALUE via wantsPortRead(): a genuine in-flight nibble reply
// wins, otherwise idle "nothing pressed" (0x00) -- real joystick/
// keyboard-mapped input is not implemented yet (see class comment).
bool Dandanator::in(uint16_t port, uint8_t& value)
{
	if (!enabled_ || (port & 0xFF) != 0x1F) return false;
	if (cmd52Sink_ && cmd52Sink_->wantsPortRead(value)) return true;
	value = 0x00;
	return true;
}

// Writes to ROM address space are normally a hard no-op (see class
// comment) -- MinZX::poke8()/MemoryMap::poke8() already handle the
// "don't corrupt ROM content" half; this is purely the side-channel
// command bus layered on top, via Peripheral::onMemoryWrite(). Every
// write to addr 0-3 is an indistinguishable raw pulse to real hardware
// (see class comment) -- just count it and record when; dispatch
// happens later in onInstructionFetch() once a field/command goes quiet.
void Dandanator::onMemoryWrite(uint16_t addr, uint8_t value, MemoryMap& memory)
{
	(void)memory; (void)value;
	if (!enabled_ || !switchedOn_ || addr > 3 || state_ == Disabled) return;

	burstPulseCount_++;
	lastPulseTstate_ = lastFetchTstates_;
	burstActive_ = true;
	awaitingNextField_ = false; // a pulse arrived -- cancel any pending "nothing more is coming" timeout
}

// Command 46: lock (1), unlock (16), or permanently disable (31) future
// commands. data1 must equal data2 or the pulse is ignored -- real
// hardware's own noise-immunity check, kept here for fidelity even
// though nothing in software generates mismatched copies deliberately.
void Dandanator::runCommand46()
{
	if (scheduledData1_ != scheduledData2_) return;
	if (scheduledData1_ == 1) state_ = Locked;
	else if (scheduledData1_ == 16) state_ = WaitNormal;
	else if (scheduledData1_ == 31) state_ = Disabled;
}

void Dandanator::executeCommand()
{
	uint8_t cmd = scheduledCmd_;

	// While Locked, only commands 46 and 52 are ever processed -- the
	// Lock exists to protect a RUNNING GAME's own incidental ROM-space
	// writes from being misread as bank-switch commands (see cmd 34's
	// comment), not to shut the whole channel down: a normal (1-39)
	// bank-switch command still gets pulse-counted and "arrives" here
	// (see onMemoryWrite()), it's just discarded. Command 52
	// (DANCMD_MULTIPLY) is a pure side-channel byte-pipe to Multiply,
	// unrelated to banking, and MUST stay live while Locked -- found
	// live, 2026-10-31: a real v10.4.3 romset's own "Launch Multiply"
	// code (bank 29) gets there via cmd 40 (CMD_SP_FASTCHANGE) with
	// data2's lock bit set (switching to bank 29 AND locking in the same
	// command), then immediately sends its REQ_ID probe over cmd 52 --
	// if 52 were blocked here like every other command, that probe would
	// go to /dev/null forever and Multiply could never be detected even
	// when genuinely present, which contradicts the whole point of this
	// specific Locked+bank-29 sequence existing. The 2017 PIC firmware
	// source this Locked model was originally drawn from predates
	// command 52 entirely (see the no-op comment on cmd 44/45 above) and
	// so never had to answer this question -- this is new evidence, not
	// a contradiction of that source.
	if (state_ == Locked) {
		if (cmd == 46) runCommand46();
		else if (cmd == 52 && scheduledData1_ == 1 && cmd52Sink_) cmd52Sink_->onDandanatorByte(scheduledData2_);
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
	else if (cmd == 37) {
		// CMD_NMI: confirmed via the real PIC firmware source
		// (2026-10-31, `docs/2017.09.22 - Dandanator Mini PIC
		// Firmware/.../commands.h`+`main.c`'s `case CMD_NMI:
		// Z80_Send_NMI();`) -- a genuine single-field normal command,
		// previously missing here entirely (fell through to the
		// cmd52Sink_ forward like an unrecognized command).
		wantsNMI_ = true;
	}
	else if (cmd == 40) {
		// Fast switch: data1 = bank+1 (1-based, same encoding as the
		// 1-33 bank-switch commands above), data2 = action bitmask.
		pendingBank_ = scheduledData1_ - 1;
		bankChangePending_ = true;
		if (scheduledData2_ & 0x08) state_ = Disabled;
		if (scheduledData2_ & 0x04) state_ = Locked;
		if (scheduledData2_ & 0x02) wantsNMI_ = true;
		if (scheduledData2_ & 0x01) wantsReset_ = true;
	}
	else if (cmd == 44 || cmd == 45) {
		// CMD_SP_STOREDATA/CMD_SP_RETRIEVEDATA: deliberate no-op -- see
		// Dandanator.h's class comment for the full 2026-11-01 story.
		// Short version: the real PIC firmware source DOES implement
		// these (EEData[256] + send-value-as-NMI-pulses), and an earlier
		// attempt this session faithfully matched that source exactly --
		// it fixed Bomb Jack (by fixing the *field-consumption* desync,
		// not the NMI response itself) but then produced a WORSE,
		// non-linear retry-with-growing-range failure in Chuckie Egg,
		// Bruce Lee, and (less visibly) Commando. The real ZEsarUX
		// (`chernandezba/zesarux`, `src/storage/dandanator.c`) --
		// this project's own established cross-reference since session
		// 1 -- settled it: its command switch only has cases for
		// 40/41/42/43/46; 44 and 45 fall to `default: UNKNOWN command`,
		// i.e. real reference emulation NEVER sends an NMI reply to
		// these at all. Matching that (no-op, zero NMI pulses) instead
		// of the firmware-literal behavior fixed all four games
		// end-to-end, confirmed via live ROM-menu-to-gameplay testing,
		// zero regression on Lotus Esprit. Needing 4-field consumption
		// (see needsMoreFields()) to avoid the protocol-desync class of
		// bug stands regardless of what the command actually DOES once
		// collected -- that part of the original fix was correct and is
		// kept.
	}
	else if (cmd == 46) {
		runCommand46();
	}
	else if (cmd == 52) {
		// DANCMD_MULTIPLY: data1 is a fixed marker (DANDAT_MULTIPLY == 1
		// in the real firmware) identifying this as Multiply-addressed
		// traffic rather than a plain unrecognized command; data2 is the
		// actual payload byte. See DandanatorCmd52Sink.
		if (scheduledData1_ == 1 && cmd52Sink_) cmd52Sink_->onDandanatorByte(scheduledData2_);
	}
	// 35, 37, 38, 39 (remember reset slot -- not implemented, matches
	// ZEsarUX's own gap), 41-43/49/50 etc: a no-op, matching ZEsarUX's
	// own gap for these. A 2026-10-31 session speculatively forwarded
	// ALL of these straight to cmd52Sink_ as raw Multiply bytes, on the
	// theory that two observed values (41, 49) matched real Multiply
	// Arduino firmware constants byte-for-byte -- this was WRONG,
	// confirmed the same session once the real menu-firmware source
	// (mad3001/Multiply's Src/MultMenu/resources/*.asm, not found
	// earlier) turned up: EVERY real Z80->Multiply byte goes exclusively
	// through command 52 (DANCMD_MULTIPLY/DANDAT_MULTIPLY), confirmed in
	// SendSerByte/SendSerByteLC/GetMultiplyVer, all three routed through
	// the DAN_BIG_COMMAND_NOW DANCMD_MULTIPLY,DANDAT_MULTIPLY,A macro --
	// no other command number ever carries Multiply payload. These other
	// commands are this romset's own ordinary (if unimplemented-by-JASE)
	// menu-bootstrap traffic; forwarding them to Multiply was actively
	// harmful, not merely unhelpful -- confirmed live: it interleaved
	// garbage bytes (the raw command numbers, e.g. 0x29/0x31/0x2a/0x2b/
	// 0x27/0x32) into the bridge's command stream BEFORE the real
	// cmd-52-forwarded CMD_ZX2INO_REQ_ID byte, desyncing
	// tools/multiply_bridge/session.py's one-command-at-a-time dispatch
	// loop and silently breaking an otherwise-working REQ_ID handshake.
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
