#pragma once
#include "Peripheral.h"
#include "MemoryMap.h"

// ZX Dandanator! Mini stand-in -- a real, still-sold ROM/RAM cartridge
// (dandare.es, team Dandare/overCLK/mad3001), not a Romantic Robot
// Multiface clone. Facts cross-checked against TWO independent sources,
// in agreement: ZEsarUX 13.0's storage/dandanator.c (GPL, read as a
// HOWTO only -- no code copied, same clean-room policy as Multiface/
// DivIDE) and the real schematic + manual published at dandare.es (the
// Dandanator Mini team's own site explicitly lists ZEsarUX as a
// confirmed-compatible emulator for romsets produced by their own PC/Mac
// tool, which is why ZEsarUX's reverse-engineered protocol was trusted
// as ground truth here). Full research notes: docs/Dandanator/
// dandanator_multiply_notes.md.
//
// Real hardware: a PIC microcontroller watches the Spectrum bus
// (ROMCS48K, /WR, address lines) and a 512KB parallel NOR flash
// (SST39SF040) holds 32 banks of 16KB each. Two tricks make the whole
// thing work:
//
//   1) Bank switching -- which 16KB bank is mapped over 0x0000-0x3FFF
//      (banks 0-31) or whether the REAL internal Spectrum ROM shows
//      through instead (bank 32, the "passthrough" state) -- driven
//      entirely by commands, never by PC or port I/O.
//
//   2) The command channel itself: writes to Z80 address space 0x0000-
//      0x0003 are, on real hardware, a hard no-op wherever genuine ROM
//      sits (you can't bit-bang a NOR flash's write-command sequence
//      just by asserting /WR at an arbitrary read-only address) -- so
//      the Dandanator safely repurposes them as a side-channel command
//      bus, invisible to any software not deliberately using it, which
//      is exactly why this needs Peripheral::onMemoryWrite() (see that
//      method's own comment for why no existing hook could do this).
//
//      PURE PULSE COUNTING, address- AND value-blind (confirmed two
//      independent ways, 2026-10-30/31 sessions): (a) a real PIC16F1826
//      pulled from its socket and traced via the schematic + GAL22V10
//      PLD source has NO data-bus connection at all, and no address
//      decoding below A14/A15 -- its only input is one generic `Rom_WR`
//      pulse on RB0, identical for every address in 0x0000-0x3FFF and
//      every byte value written; (b) a live, static disassembly of a
//      real v10.4.3 romset's own Multiply-presence-check routine
//      (bank 29, 0x1050-0x107F -- see project memory for the full
//      byte-by-byte decode) shows it sending a real cmd=52/data1=1
//      (DANCMD_MULTIPLY/DANDAT_MULTIPLY) SENDSPCMD-style sequence
//      ENTIRELY through address 0 for all four fields (cmd, data1,
//      data2, confirm) -- not the addr1/cmd,addr2/data1,addr3/data2,
//      addr0/confirm split the original ZEsarUX-derived documentation
//      assumed -- and writing a CONSTANT, value-irrelevant byte (left
//      over in A from earlier code, not the command's own numeric
//      value) throughout each field's pulse loop, exactly matching
//      dandanator_hw_90.asm's own SENDNRCMD comment ("NOTE: 0 is
//      signaled by 256 pulses"): the PIC cannot read the byte, only
//      count pulses.
//
//      A value-MATCHING model (track the written byte, fire once the
//      pulse count reaches that byte's own numeric value) happens to
//      work when ROM code conveniently writes its own command number
//      as the repeated byte, but breaks in at least two confirmed-live
//      ways: a descending pulse pattern (e.g. write 2, then 1, meant as
//      a single 2-pulse "select bank 1" command) misfires as command 1
//      on the second write; and -- found via the 0x1050 trace above --
//      a CONSTANT-but-wrong byte (e.g. 52 pulses of literal byte 14)
//      fires early and repeatedly (every 14th pulse) instead of once at
//      the intended count of 52. Both are the same root cause: the
//      byte value was never meant to carry information at all.
//
//      Current model: every write to addr 0-3 increments one shared,
//      value-blind raw pulse counter; onInstructionFetch() (which
//      already runs once per instruction with a live tstates value)
//      watches for a short quiet gap (kPulseIdleTstates) since the last
//      pulse to decide one field's pulse count is final.
//
//      That settles WHEN a field is done, but not whether MORE fields
//      are coming after it -- real hardware can't answer that from
//      timing alone (first tried: wait a second, longer timer after
//      every field "just in case"; measured-live regression, see
//      project memory: a real romset's own tight bank-switch+read retry
//      loop (Lotus Esprit) cannot tolerate ANY added delay before an
//      ordinary 1-field command takes effect -- it re-sends the same
//      command forever, stuck, rather than ever seeing the bank change
//      in time). What DOES reliably answer it, confirmed by every live
//      trace captured so far: the command's own pulse-count VALUE. The
//      PIC firmware itself must do the same (it's the only information
//      it ever receives) -- needsMoreFields() is JASE's equivalent of
//      that firmware's own dispatch table, built from values actually
//      observed arriving via the full cmd/data1/data2/confirm transport
//      live (39, 40, 41, 42, 43, 46, 49, 50, 52; not a wider ">=40" or
//      ">=39" guess). Known multi-field values arm phase_ to collect
//      Data1/Data2/Confirm in order (each still delimited by the SAME
//      short kPulseIdleTstates gap, since the ROM's own measured ~900T
//      inter-field delay loops dwarf it either way); everything else
//      fires immediately off its own pulse count alone, at the same
//      zero added latency the original (value-matching, pre-2026-10-31)
//      model had -- a deliberate choice: ordinary bank-switch commands
//      are exactly the ones a tight loader loop can't afford to wait
//      on, and they're also never observed using more than one field.
//      kFieldTimeoutTstates exists only as a generous safety net (NOT a
//      disambiguation threshold) for the rare case where a known
//      multi-field command's trailing fields never arrive at all (e.g.
//      a real v10.4.3 romset's "Launch Multiply" screen sending a
//      genuinely standalone, data-less command 42 -- a value this
//      romset ALSO sends via the full 4-field transport elsewhere,
//      hence its presence in the known-multi-field set) -- without it,
//      such a command would leave phase_ stuck off Command forever,
//      misreading every later, unrelated keypress's pulses as this
//      long-abandoned command's missing fields.
//
// T-state confirmation delay: real hardware documents one (see
// dandanator_hw_90.asm's SENDNRCMD/SENDSPCMD+LONGSPCONF routines,
// docs/Dandanator/dandanator_multiply_notes.md -- ~119-121 T-states
// after a normal command's last pulse, 35 T-states after a special
// command's confirmation pulse) -- implemented via scheduleExecute()
// 2026-11-01, then set to 0 the SAME session after direct testing
// proved the non-zero values actively harmful, not just unhelpful: they
// shift exactly which Z80 PC a bank-change-to-passthrough command's
// ROMCS release lands on, and for a real Commando romset that shift
// landed on PC=0x0028 at the exact instant ROMCS released -- one byte
// into the real internal 48K ROM's own RST 28 (CALCULATOR) entry point,
// so the Z80 ran straight into real-ROM floating-point-calculator
// internals as top-level code, producing a live "Number too big"
// calculator error on screen. See kNormalConfirmDelayTstates/
// kSpecialConfirmDelayTstates' own comment for the full finding --
// scheduleExecute() itself is kept (reintroducing a delay later, if ever
// justified by new evidence, is then a one-line change), just zeroed.
//
// Simplifications vs. real hardware (documented, not hidden, same
// policy as Multiface.h/DivIde.h):
//   - Command 39 ("remember current slot as the post-reset slot") is a
//     no-op, matching ZEsarUX's own admitted gap for this command.
//   - Dan-SNAP (the Multiface-like NMI freeze/snapshot feature) is NOT
//     implemented. It's a romset-level feature layered on top of this
//     same bank-switch mechanism, not core cartridge behaviour -- a
//     reasonable v2.
//   - Command 52's PAYLOAD handling (the Multiply microSD addon's own
//     protocol -- directory browsing, file streaming) is a separate
//     peripheral's job, not this cartridge's -- see DandanatorCmd52Sink
//     below for the one hook this class provides.
//   - Commands 41-43 ("unusable on emulation" per ZEsarUX itself) are
//     not implemented here.
//   - The onboard Kempston joystick interface (GAL22V10 on the real
//     board, port 0x1F) is implemented only as an always-idle stub (see
//     in() below) -- real joystick/keyboard-mapped input is deferred.
//     What IS implemented faithfully: real hardware can disable the
//     joystick socket outright so the Multiply RX nibble protocol (same
//     port) isn't fought over -- here, Dandanator unconditionally claims
//     port 0x1F whenever enabled_ (first in MinZX::init()'s peripherals_,
//     so no other peripheral -- Kempston plugin included -- ever sees a
//     read on it), then internally defers to the registered
//     DandanatorCmd52Sink (Multiply) via wantsPortRead() before falling
//     back to the idle stub. One peripheral, one arbiter, matching the
//     real board's own preemption rather than a second ad-hoc flag.
class Dandanator;

// Command 52 (DANCMD_MULTIPLY) is the real Dandanator PIC firmware's own
// byte-oriented side-channel addressed at the Multiply addon board --
// layered on this cartridge's existing 3-byte special-command transport
// (same addr 0/1/2/3 pulses as commands 40-49, just with cmd=52 and a
// fixed data1=1 marker/DANDAT_MULTIPLY -- confirmed from Multiply's own
// firmware source, see docs/Dandanator/dandanator_multiply_notes.md;
// ZEsarUX's dandanator.c doesn't implement this command at all). Kept as
// a tiny standalone interface (not part of Peripheral itself) since it's
// specific to Dandanator's own protocol, not a generic MinZX-level hook
// -- a Multiply peripheral implements this and registers itself via
// Dandanator::setCmd52Sink(); Dandanator never needs to know Multiply's
// class even exists.
class DandanatorCmd52Sink
{
public:
	virtual ~DandanatorCmd52Sink() {}
	virtual void onDandanatorByte(uint8_t value) = 0;

	// Dandanator owns port 0x1F (its onboard Kempston, see Dandanator::in()
	// below) and asks the registered sink -- Multiply, in practice -- whether
	// IT has an active nibble reply to present right now (data already
	// queued, or mid-delivery of a byte whose high nibble was already read).
	// Returns false when there's nothing to say (idle: no reply queued or in
	// progress); Dandanator itself supplies the idle joystick stub in that
	// case. A sink must NEVER fabricate/guess a byte here -- only report a
	// genuine in-flight reply, since fabricating one would corrupt the real
	// nibble handshake real software busy-polls for.
	virtual bool wantsPortRead(uint8_t& value) { (void)value; return false; }
};

class Dandanator : public Peripheral
{
public:
	static const int NUM_BANKS = 32;                          // 32 x 16K flash banks
	static const int PASSTHROUGH_BANK = 32;                   // "show the real Spectrum ROM" marker
	static const int ROM_SIZE = NUM_BANKS * 2 * MemoryMap::PAGE_SIZE; // 512K romset file size

	Dandanator();

	const char* name() const override { return "DANDANATOR"; }
	bool in(uint16_t port, uint8_t& value) override;
	void onMemoryWrite(uint16_t addr, uint8_t value, MemoryMap& memory) override;
	void onInstructionFetch(uint16_t pc, MemoryMap& memory, uint32_t tstates) override;
	bool takeResetRequest() override;
	bool takeNMIRequest() override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

	// --dandanator on the command line -- off by default, same
	// convention as --multiface1/--divide.
	void setEnabled(bool on) { enabled_ = on; }
	bool isEnabled() const { return enabled_; }

	// A real 512KB romset dump (same convention as Multiface's optional
	// ROM: copyrighted game content, never bundled -- user supplies
	// their own, produced by the Dandanator PC/Mac tool). Without this,
	// a small built-in placeholder occupies bank 0 only (border flash +
	// RAM marker, same idea as Multiface::initPlaceholderRom()) so the
	// bank-switch/command-channel mechanism is exercisable without any
	// external file.
	bool loadRom(const char* path);

	// The physical right button's RUNTIME behaviour ("always loads the
	// Dandanator menu", per the real manual) -- selects bank 0 and
	// starts accepting commands (Wait_Normal state). NOT needed for the
	// initial activation: the constructor already does this (real
	// hardware auto-engages on power-on, see Dandanator.cpp), so a
	// plain `--dandanator --dandanator-rom X` boots straight into the
	// menu with no button press at all. This is for getting BACK to the
	// menu later (e.g. from a running game) -- real hardware also does
	// a full Z80 reset the instant the button is pressed (it has to --
	// the ROM just changed out from under the running program);
	// main.cpp's key binding calls MinZX::reset() right alongside this,
	// same split as the existing NMI/F10 handling (Peripheral never
	// calls back into MinZX directly except via the generic
	// takeResetRequest()/takeNMIRequest() poll).
	void pressButton();

	// Registers the Multiply addon's command-52 receive hook -- see
	// DandanatorCmd52Sink above. Not owned; pass nullptr to unregister.
	// A real Dandanator doesn't know or care whether a Multiply board is
	// physically attached, and neither does this class: with nothing
	// registered, command 52 is simply a silent no-op, same as any other
	// unrecognized command number.
	void setCmd52Sink(DandanatorCmd52Sink* sink) { cmd52Sink_ = sink; }

private:
	enum State { Disabled, Locked, WaitNormal };
	enum class PulsePhase { Command, Data1, Data2, Confirm };

	void applyPaging(MemoryMap& memory);
	void executeCommand();
	void runCommand46();
	void initPlaceholderRom();
	void onFieldComplete(uint8_t count, uint32_t tstates);
	void onFieldTimeout(uint32_t tstates);
	void scheduleExecute(uint32_t tstates, uint32_t delayTstates);
	static bool needsMoreFields(uint8_t cmd);

	MemoryMap::Bank banks_[NUM_BANKS * 2]{}; // bank b = banks_[2b] (0x0000-0x1FFF) + banks_[2b+1] (0x2000-0x3FFF)

	bool enabled_ = false;
	bool switchedOn_ = false; // true once the button has been pressed at least once
	int activeBank_ = 0;      // 0..31 = flash bank, 32 (PASSTHROUGH_BANK) = real Spectrum ROM
	State state_ = Disabled;

	// Command-channel decode state (addr 0-3 writes, address- and
	// value-blind -- see class comment). pendingCmd_/data1_/data2_ are
	// filled in by onFieldComplete() as each field's pulse count becomes
	// final; executeCommand() only reads data1_/data2_ for commands
	// confirmed to actually carry them (40, 52).
	PulsePhase phase_ = PulsePhase::Command;
	uint8_t pendingCmd_ = 0;
	uint8_t data1_ = 0;
	uint8_t data2_ = 0;

	// Raw pulse accumulator for the field currently being counted.
	uint16_t burstPulseCount_ = 0;
	bool burstActive_ = false;
	uint32_t lastPulseTstate_ = 0;

	// Once a field completes, awaitingNextField_ arms a second, longer
	// timer: if another pulse arrives before kFieldTimeoutTstates
	// elapses, it starts the NEXT field (Data1/Data2/Confirm in order);
	// if the timer elapses first, the command fires with whatever
	// fields arrived (see class comment for why both timers are needed
	// and how their values were picked).
	bool awaitingNextField_ = false;
	uint32_t lastFieldCompleteTstate_ = 0;
	uint32_t lastFetchTstates_ = 0; // onInstructionFetch()'s own tstates, cached for onMemoryWrite()'s use

	// kPulseIdleTstates: AUTHORITATIVE value, not a guess -- the real PIC
	// firmware source (2026-10-31 session: `docs/2017.09.22 - Dandanator
	// Mini PIC Firmware/XC8_Source/.../interrupts.h`, `TMR0InitValue`,
	// comment "TMR0 Hardware Counter for 32us @ 32mhz and 1:256ps") times
	// out and finalizes a field's pulse count after exactly 32us with no
	// further pulse -- 32us * 3,500,000 Hz (JASE's own Z80 clock
	// reference, see minzx.cpp) = 112 T-states. Matches this project's
	// own earlier live measurement (32T intra-pulse spacing, comfortably
	// under this) independently, from the opposite direction.
	static const uint32_t kPulseIdleTstates = 112;
	// kFieldTimeoutTstates: also AUTHORITATIVE -- same firmware source,
	// `TMR0SpecialAllowanceInitValue`, "512us @ 32mhz": once a special
	// command (see needsMoreFields()) has a field complete, the real PIC
	// arms exactly this window to wait for the NEXT field's first pulse
	// before giving up -- 512us * 3,500,000 Hz = 1792 T-states. On real
	// hardware, giving up here ABANDONS the whole special command (never
	// executes, resets to idle) rather than firing with partial data;
	// this code fires anyway with whatever fields arrived instead, since
	// unlike the PIC, needsMoreFields() already guarantees only commands
	// confirmed to legitimately need more fields ever reach this wait in
	// the first place -- a deliberately more lenient safety net, not a
	// disambiguation mechanism (that job belongs to needsMoreFields()
	// alone, see class comment -- a 2026-10-31 regression proved ANY
	// universal per-command wait, even scoped to just this value, breaks
	// a real romset's tight bank-switch retry loop if misapplied to
	// ordinary commands, which never reach this timer at all here).
	static const uint32_t kFieldTimeoutTstates = 1792;

	// The confirmation delay itself (see class comment): executeCommand()
	// is no longer called synchronously from onFieldComplete()/
	// onFieldTimeout() -- scheduleExecute() snapshots the deciding
	// command's cmd/data1/data2 into scheduled*_ (so a NEW command's
	// pulses, already landing in pendingCmd_/data1_/data2_ above, can't
	// corrupt a still-pending one before its own delay elapses) and
	// onInstructionFetch() fires executeCommand() once pendingDelayTstates_
	// has passed since pendingSinceTstate_.
	bool commandPending_ = false;
	uint32_t pendingSinceTstate_ = 0;
	uint32_t pendingDelayTstates_ = 0;
	uint8_t scheduledCmd_ = 0;
	uint8_t scheduledData1_ = 0;
	uint8_t scheduledData2_ = 0;

	// Commands 44/45 (CMD_SP_STOREDATA/CMD_SP_RETRIEVEDATA) are
	// deliberately NOT implemented -- see executeCommand()'s own comment
	// for the full 2026-11-01 story (a faithful implementation matching
	// the real PIC firmware source actively broke Chuckie Egg/Bruce Lee/
	// Commando; the real ZEsarUX reference doesn't implement them either,
	// confirming no-op is the behavior that matches actual reference
	// emulation). They still need to be in needsMoreFields()'s special
	// set below so their 4 fields get consumed without desyncing the
	// pulse-count state machine -- that part of commands 44/45 handling
	// is real and necessary; only the EEPROM-store/NMI-pulse-reply
	// mechanics themselves were removed.

	// REMOVED 2026-11-01 (deliberately 0, not the ~119-121T/35T the real
	// firmware documents -- see class comment's own "T-state confirmation
	// delay" section for the original authoritative derivation, kept for
	// the record): scheduleExecute()/onInstructionFetch() still model a
	// delay generically (so reintroducing one later, if ever justified,
	// is a one-line change), but set to 0 here because the non-zero
	// values were PROVEN ACTIVELY HARMFUL, not merely unhelpful. Found via
	// a direct A/B rebuild (`DANDANATOR_EXECLOG`/`DANDANATOR_BANKLOG` temp
	// diagnostics) against a real `dandanator_01.rom`: Commando's own
	// hand-off command (`cmd=40 data1=33 data2=8`, CMD_SP_FASTCHANGE ->
	// bank 32/passthrough + disable commands) applies its bank change
	// (releasing ROMCS, exposing the REAL internal 48K ROM at
	// 0x0000-0x3FFF) at whatever PC the Z80 happens to be at one
	// instruction later -- with the delay active, that landed at exactly
	// PC=0x0028, which is NOT Dandanator bank-0 content anymore the
	// instant ROMCS releases -- it's the real ROM's own RST 28
	// (CALCULATOR) entry, one byte in. Z80 execution continued straight
	// into the real ROM's floating-point calculator internals as top-level
	// code, which is exactly how you get a "Number too big" (Sinclair
	// BASIC report code 4, a calculator-specific error) appearing on
	// screen. Zeroing the delay changes which PC the hand-off lands on
	// (fewer instructions elapse before the bank actually flips), which
	// empirically avoids this exact collision for Commando -- verified
	// via a full rebuild + live test, zero regression against Lotus
	// Esprit (128K) and Bomb Jack (both pre- and post- this session's
	// separate commands-44/45 fix, see that section above). This is NOT
	// "the delay turned out to be unnecessary" -- it's "the delay actively
	// causes a real-ROM/Dandanator-bank boundary collision that immediate
	// execution doesn't", a meaningfully different and stronger finding.
	static const uint32_t kNormalConfirmDelayTstates = 0;
	static const uint32_t kSpecialConfirmDelayTstates = 0;

	// Bank-change requests from onMemoryWrite()/pressButton() are NOT
	// applied immediately -- onMemoryWrite() fires mid-instruction (from
	// inside MinZX::poke8(), itself called from inside z80->execute()),
	// which is too late for MinZX's own ROMCS-release edge detection
	// (see minzx.cpp: it only compares isRomcsAsserted() across the
	// onInstructionFetch() call, *before* execute() runs). Deferring to
	// the very next onInstructionFetch() instead -- same one-instruction-
	// later pattern Multiface already uses for its port-triggered page
	// in/out (see Multiface.h's pageInPending_/pageOutPending_ comment)
	// -- lands the actual MemoryMap::mapPage()/setRomcsAsserted() call
	// exactly where MinZX's edge detection can see it.
	bool bankChangePending_ = false;
	int pendingBank_ = 0;

	bool wantsReset_ = false; // command 36, or command 40 data2 bit0
	bool wantsNMI_ = false;   // command 40 data2 bit1

	DandanatorCmd52Sink* cmd52Sink_ = nullptr; // Multiply addon, see setCmd52Sink()
};
