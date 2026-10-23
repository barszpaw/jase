#pragma once
#include "Peripheral.h"
#include "MemoryMap.h"
#include <cstdio>
#include <string>
#include <array>

// SDI (Spectrum Disk Interface) stand-in -- a real ZX Spectrum cartridge
// (hardware: Uros Milan, firmware: Geren Ivan, PCB: Mario Prato Rev 1.0)
// that bridges to a Commodore 1541 disk drive over the IEC serial bus.
// Facts below come from the real schematic (docs/VC1541/SDI_schemat.png)
// and the real 4KB ROM dump (docs/VC1541/SDI103J_ED3DC6CD.ROM) -- see
// docs/VC1541/VC1541.md for the full writeup, including two corrections to
// an AI-assisted disassembly this was cross-checked against (port B is
// $BF, not $5F; the front panel button pulls /RESET, not /NMI).
//
// Memory map: ROM is 4KB at $3000-$3FFF (U8, a 2764 EPROM) -- half of an
// 8K MemoryMap page, so pageIn() snapshots the OTHER half ($2000-$2FFF)
// from whatever's already mapped there instead of overwriting it. Safe to
// do unconditionally because that range is always the other half of the
// system ROM pair in every machine type JASE supports (see
// MinZX::setupInitialPaging()) -- i.e. always read-only there regardless
// of what's active, so a snapshot copy can never go stale from a write
// that should have landed on real RAM.
//
// ROMCS trigger: VERIFIED, narrower than the first-pass hypothesis.
// Modelled the same way as DivIde/Multiface (see those for the
// established pattern this follows) -- page in on an M1 opcode fetch
// landing in $3C00-$3FFF (not the full $3000-$3FFF ROM window) while
// not already active, page out when PC leaves the full $3000-$3FFF
// window. This directly supersedes the earlier chat's "NMI" claim (no
// /NMI signal exists anywhere on the real schematic -- the panel button
// pulls /RESET, a completely different, already-wired edge-connector
// signal).
//
// The $3C00 narrowing (not the full 4K) is independently confirmed by
// four lines of evidence: (1) the schematic's own gate-network inputs
// for ROMCS list exactly 6 address lines (A10-A15), which decode a
// 1024-byte granularity, i.e. $3C00-$3FFF, not the full 4K; (2) the
// real ROM's own bytes have a 527-byte 0xFF filler run from $39F1-$3BFF
// immediately BEFORE the first real code at $3C00 (five harmless `DI`
// opcodes as address-decode-slop margin, then real code at $3C05) --
// $3000-$39F0 is real code too, but only ever reached via internal
// CALLs from the $3C00+ entry point, never a direct M1 landing; (3) the
// ROM's own official README (SDIv103J.TXT) says verbatim "Intercepts
// 128 ROM at $3C04 and performs a USR 0" -- matching the real ROM bytes
// exactly: DI(x5 padding)/XOR A/OUT($1FFD),0/OUT($7FFD),$10/RST 0,
// i.e. force 48K-compatible paging then restart; (4) empirically
// confirmed by booting JASE's own real, unmodified 128K ROM (no SDI
// loaded) with a breakpoint at $3C04 -- it hits immediately and
// repeatedly, because $3C04 is part of the stock 128K menu's own
// keyboard-scan wait loop (`LD BC,$BFFE`/`IN A,(C)`/`BIT 0,A`/`JR Z,..`),
// which is exactly the kind of address a disk-ROM would want to
// intercept: continuously executed from power-on, before the user ever
// sees the 128K menu. $3C18 (the entry code's own continuation, which
// patches the ERR_SP-pointed stack slot to redirect future BASIC-error
// unwinding to $3C5F for "*COMMAND"/"*CAT" parsing) and $3C5F are both
// also within this same $3C00-$3FFF window, consistent with the SAME
// trigger condition catching all of them on separate later M1 fetches.
//
// The Arena AI chat (see VC1541.md) independently guessed $3C5F was
// significant too, but its specific claim -- a standalone
// "L3C5F_BOOTSTRAP_INIT" routine that prints the DOS banner -- does not
// match the real bytes: the real banner string ("SPECTRUM DISK
// INTERFACE"/"DOS V1.03J") lives at $32B1, and the real banner-print
// loop is at $3C46-$3C50, called from the $3C00 entry's OWN
// continuation, not from a separate routine at $3C5F. $3C5F in the real
// ROM is used purely as a 16-bit DATA VALUE (the vector written to the
// ERR_SP-pointed stack slot), not a label the chat's imagined code ever
// reaches the way it describes. Treat the chat's specific code/labels
// as unreliable even where its broad intuition (that $3C5F matters) was
// directionally right -- exactly the kind of thing to verify against
// the real ROM bytes with a disassembler, not take on faith.
//
// 8255 PPI (U10) port decode, read directly off the schematic's own
// port-map label (not guessed): PA=$9F, PB=$BF, PC=$DF, CONF=$FF. All
// four share one chip-select pattern -- (port & 0xFF & 0x9F) == 0x9F --
// since bits 5/6 of the low byte are the 8255's own A0/A1 register
// select, not part of the decode (this derivation is solid: it's exactly
// the bit pattern the four published addresses have in common).
//
// Port C (the IEC bus) bit map -- traced directly from the real schematic
// (U1A/U1B/U1C, 74LS06 open-collector buffers) and confirmed pin-by-pin
// with Pawel rather than guessed from the rendered image, which wasn't
// legible enough on its own:
//   PC0 (0x01) -> U1A -> ATN   (output, driven by us -- we're always the
//                               IEC controller, like a real C64 would be)
//   PC1 (0x02)    NOT part of the IEC bus -- drives A12 on the 2764 EPROM,
//                  paging between its two 4K halves. Confirmed irrelevant
//                  to IEC emulation: the SDI manual documents the second
//                  4K half as free space reserved for the user's own code,
//                  not alternate firmware -- our 4K ROM dump is the whole
//                  active program, nothing is paged out from under it.
//   PC2 (0x04) -> U1B -> CLK   (output)
//   PC3 (0x08) -> U1C -> DAT   (output)
//   PC4 (0x10)    pulled up 1K to 5V, unused for IEC
//   PC5 (0x20)    pulled up 1K to 5V, unused for IEC
//   PC6 (0x40) <- U1B's own output net (CLK), pulled up 1K to 5V -- reads
//                  the ACTUAL combined bus state, not just what we drove
//   PC7 (0x80) <- U1C's own output net (DAT), same idea
// This 0-3 output / 4-7 input split is exactly what 8255 mode 0 allows
// (PC lower/upper nibbles independently directioned), which is a good
// sign the mapping above is right, not a coincidence.
//
// Protocol facts (verified against Commodore's own IEC Serial Bus Manual
// timing diagrams -- commodore.ca's reproduction of it -- cross-checked
// against pagetable.com's independent writeup, not from any GPL source):
// all lines active-low (pulled low = logical 1/asserted). CLK is always
// driven by whichever side is the active TALKER; the RISING edge of CLK
// is when DATA is valid and gets sampled. ATN is only ever driven by the
// controller (us). A byte sent under ATN is always LISTEN/TALK/secondary-
// address command data, never EOI.
//
// Implemented so far: ATN assert/release detection, clocking 8 bits into
// a byte off the relevant side's CLK rising edge (the generic mechanism
// is the same in both phases -- only WHICH side drives CLK differs), and
// the TALK/LISTEN role switch for the post-ATN data phase:
//   - Under ATN, we (the controller) are always the sender -- clocked off
//     our own driven CLK (PC2).
//   - The moment a PRIMARY address byte (not a secondary/channel one)
//     finishes clocking under ATN, it sets the role for whatever data
//     phase follows once ATN releases: LISTEN (0x20-0x3E, "device, listen
//     to ME") means WE stay the sender; TALK (0x40-0x5E, "device, YOU
//     talk") means the device becomes the sender and we become the
//     receiver. UNLISTEN (0x3F) / UNTALK (0x5F) clear the role entirely.
//     Secondary/channel bytes (0x60-0x6F OPEN, 0xE0-0xEF CLOSE, 0xF0-0xFF
//     plain-TALK-secondary) deliberately leave the role alone.
//   - Once ATN releases, if we're still the sender (role WeAreTalker),
//     the exact same clocking mechanism continues, now gated on "not
//     under ATN" instead of "under ATN" -- clocked off our own CLK.
//   - If the device is the sender (role WeAreListener), clocking instead
//     watches the READ-BACK CLK-in (PC6) for its rising edge and samples
//     DAT-in (PC7) -- this is plumbed in, but since no real device or
//     bridge is connected yet, clkLine()/datLine() currently only ever
//     reflect our OWN drive (see their comment below), so this path
//     won't see real transitions until the TCP bridge exists.
// EOI ("this is the last byte") detection is approximated, not cycle-
// exact: the real signal is the sender delaying >=200us past the normal
// handshake window before clocking the first bit. Peripheral::
// onInstructionFetch() already fires every single Z80 instruction
// regardless of which peripheral is paged in (see minzx.cpp) -- that's
// the only timing signal available without widening in()/out() for
// every other peripheral just for this one case, so idleInstructions_
// counts THAT instead of real microseconds. kIdleInstructionsForEoi is
// a rough per-instruction equivalent of 200us at 3.5MHz (~700 T-states),
// not a verified exact threshold -- flagged here as an approximation,
// not quietly treated as fact, same as the ROMCS trigger above.
//
// Channel/file semantics on top of the raw decoded bytes: a secondary
// address byte under ATN is OPEN (0x60-0x6F, channel = low nibble),
// CLOSE (0xE0-0xEF), or the plain channel-select KERNAL LOAD/SAVE uses
// without a separate OPEN (0xF0-0xFF) -- standard, long-published CBM
// serial-bus convention, not specific to any one codebase. The data-
// phase bytes that follow are either a filename (OPEN) or raw LOAD/SAVE
// payload (plain select), accumulated into one running buffer (the IEC
// bus is strictly one session at a time -- no interleaving to worry
// about) and finalized into `filenames_[channel]` the moment UNLISTEN/
// UNTALK ends the session.
//
// TCP bridge: a plain blocking socket to tools/c64iec_fs/c64iec_fs_tcp.py
// (or Pawel's own GPL companion speaking the same wire format -- see
// VC1541.md), matching the real Arduino-side IECSerialFile's OWN
// blocking-read design (confirmed by reading it for the wire format,
// not copying it -- see VC1541.md's licensing section), not ZiFi's
// non-blocking per-frame pump() -- the right fit here since each of
// these is a discrete request/response at a well-defined decode point,
// not a continuous trickling byte stream.
//   - OPEN finalized -> `O,<ch>,<name>` -- works now, synchronous.
//   - CLOSE finalized -> `C,<ch>` -- works now, synchronous.
//   - SAVE finalized (PlainSelect, we were WeAreTalker so we already
//     have the full payload decoded) -> `W,<ch>,<n>,<E|C>` + bytes --
//     works now, synchronous.
//   - LOAD (PlainSelect, WeAreListener) -> `R,<ch>,<size>` is sent the
//     moment the role+channel is established (onSecondaryByte(), while
//     still under ATN -- NOT at session end like the others, since the
//     bytes need to be ready for the Z80 to poll, not arrive after it's
//     already asked); the response lands in pendingLoadData_/
//     pendingLoadEoi_. NOT YET DONE: actually driving those bytes back
//     onto the simulated bus (CLK-in/DAT-in, synchronized with the
//     Z80's own polling cadence in in()) -- a genuinely separate,
//     real-time-sensitive encoder, the inverse of clockBit(), still not
//     built. Right now pendingLoadData_ just sits there for the debug
//     protocol to inspect.
class SDI : public Peripheral
{
public:
	SDI();
	~SDI() override;

	const char* name() const override { return "SDI"; }
	bool in(uint16_t port, uint8_t& value) override;
	bool out(uint16_t port, uint8_t value) override;
	void onInstructionFetch(uint16_t pc, MemoryMap& memory) override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

	// --sdi on the command line -- off by default, same convention as
	// --multiface1/--divide.
	void setEnabled(bool on) { enabled_ = on; }
	bool isEnabled() const { return enabled_; }

	// The real SDI103J ROM (exactly 4096 bytes -- see
	// docs/VC1541/SDI103J_ED3DC6CD.ROM, CRC32 0xED3DC6CD).
	bool loadRom(const char* path);

	// Connects (blocking) to the c64iec_fs_tcp.py companion (or
	// equivalent) -- see class comment for why blocking is the right
	// fit here, unlike ZiFi's non-blocking pump(). Safe to call even if
	// never called at all -- every bridge* method below silently no-ops
	// when bridgeFd_ < 0, same graceful-degradation convention as ZiFi.
	bool connectBridge(const char* host, int port);

	// Per-instruction IN/OUT logging to all four 8255 registers (PA/PB/
	// PC/CONF), used throughout VC1541.md's investigation to trace real
	// IEC bit timing instead of re-deriving it from static disassembly.
	// Was a #ifdef SDI_IOTRACE compile-time flag (needed a separate
	// rebuild to use); now a runtime toggle -- set from the command
	// line (--debug-iotrace) or at runtime via the debug protocol
	// ("SDI IOTRACE ON"/"SDI IOTRACE OFF", see debugCommand()).
	void setIotraceEnabled(bool on) { iotraceEnabled_ = on; }
	bool isIotraceEnabled() const { return iotraceEnabled_; }

private:
	void pageIn(MemoryMap& memory);
	void pageOut(MemoryMap& memory);

	uint8_t rom_[0x1000] = {}; // 4KB, $3000-$3FFF
	bool romLoaded_ = false;

	MemoryMap::Bank window_{}; // constructed 8K page-1 image, see class comment
	bool active_ = false;      // currently overriding page 1 + ROMCS
	bool enabled_ = false;

	uint16_t lastPC_ = 0; // updated every onInstructionFetch() -- for IOTRACE logging in in()/out()
	bool iotraceEnabled_ = false; // runtime IOTRACE toggle -- see setIotraceEnabled() above

	// 8255 PPI registers. PA/PB are raw latches (no IEC role). PC is where
	// IEC lives -- portC_'s bits 0-3 are the real output latch (what we
	// last drove for ATN/PC1/CLK/DAT); bits 4-7 are synthesized on read by
	// in() from the live bus model below, not stored here.
	uint8_t portA_ = 0xFF;
	uint8_t portB_ = 0xFF;
	uint8_t portC_ = 0xFF;
	uint8_t control_ = 0x9B; // 8255 power-on default (all ports input, mode 0)

	// IEC bus model. atnLine()/clkLine()/datLine() give the ACTUAL,
	// combined bus state (true = asserted/pulled-low) -- for now just
	// reflecting our own drive (bits 0/2/3 of portC_), since the far end
	// (real 1541, or eventually a TCP-bridged companion process) isn't
	// wired in yet; once it is, these need to OR in whatever the far end
	// is independently pulling, matching the bus's real wired-AND nature.
	bool atnLine() const { return (portC_ & 0x01) != 0; }
	bool clkLine() const { return (portC_ & 0x04) != 0; }
	bool datLine() const { return (portC_ & 0x08) != 0; }

	// Who's the sender once ATN releases, decided by the last PRIMARY
	// LISTEN/TALK address byte clocked under ATN -- see class comment.
	enum class IecRole { None, WeAreTalker, WeAreListener };
	IecRole iecRole_ = IecRole::None;
	bool lastAtnLine_ = false; // previous out()'s atnLine(), to detect release

	// Generic rising-edge-of-CLK byte clocker -- see class comment for
	// the verified protocol fact this implements and the two phases it
	// serves (ATN command bytes, and the post-ATN data phase when we're
	// the sender). `fromAtnPhase` only affects bookkeeping (whether a
	// completed byte updates iecRole_), not the clocking itself.
	//
	// Also owns the receive-side half of the verified "extra CLK release
	// between every two bytes" fact (see awaitingInterByteRelease_
	// below) -- the same ambiguity already fixed for the LOAD/self-
	// decode path in in() (there, the encoder's own state disambiguates
	// it; here, since nothing else tracks byte boundaries for a call
	// site driven by someone ELSE's CLK edges, clockBit() tracks it
	// itself so BOTH out() call sites -- ATN-phase command bytes and
	// WeAreTalker data-phase bytes -- benefit without duplicating the
	// logic).
	void clockBit(bool clkRisingEdge, bool dataBit, bool fromAtnPhase);
	bool lastClkOut_ = false; // previous out()'s clkLine(), for edge detection when WE drive CLK
	int bitCount_ = 0;
	uint8_t shiftByte_ = 0;
	uint8_t lastByte_ = 0xFF;    // most recent fully-clocked byte, 0xFF = none yet
	bool byteReady_ = false;     // set when shiftByte_ just completed a byte
	bool lastByteFromAtn_ = false; // whether lastByte_ was an ATN command or a data-phase byte

	// True when the NEXT CLK rising edge clockBit() sees is the inter-
	// byte "ready to send" release rather than a real bit sample -- set
	// the instant a byte completes (and at every fresh sequence/session
	// start, alongside bitCount_/shiftByte_'s own reset in out() -- see
	// its two reset branches), cleared by clockBit() itself the moment
	// that edge is consumed. Shared by the ATN-phase and WeAreTalker-
	// phase call sites in out() -- structurally the same ambiguity, same
	// fix, in both.
	bool awaitingInterByteRelease_ = true;

	// Virtual listener-ack driver for both the ATN command phase AND the
	// WeAreTalker data phase (OPEN filenames, SAVE payload) -- the
	// inverse of advanceLoadEncoder()'s WaitAck/WaitDataReady states.
	// There, WE drive CLK as the sender and wait for the Z80 (listener)
	// to assert/release its own DATA; here, the Z80 drives CLK as the
	// sender (always true under ATN; also true for WeAreTalker) and WE
	// (playing the addressed device, which has no other emulation
	// backing it) must assert/release our own DATA, or the real SDI ROM
	// -- which, per the same verified protocol, legitimately waits for
	// this ack before sending the next byte, AND for every device to
	// respond to ATN itself within 1000us or report DEVICE NOT PRESENT
	// -- would stall or error out against a response nothing ever
	// gives. Defaults to true (asserted) specifically so it doubles as
	// that very first ATN presence response for free. `listenerDat_` is
	// only meaningful while awaitingInterByteRelease_ is true (our
	// ack/not-ready window); in() exposes it then, datLine() (the Z80's
	// own driven bit) otherwise -- see in()'s port-C case, mirroring the
	// LOAD path's own state-gated PC6/PC7 synthesis in reverse.
	bool listenerDat_ = true; // true = asserted/busy, released only once ready for the next byte's bits

	// Auto-release countdown for listenerDat_ -- see exposedListenerDat()
	// below. Live-traced finding (VC1541.md): the real ROM's own "wait
	// for ready"/"wait for ack" checks ($30f7 before a byte, $312f after
	// one) are PURE READ loops (IN/.../JP, no OUT in between) -- nothing
	// we ever WRITE can trigger an edge-reactive release while the ROM
	// is sitting in one of those, so listenerDat_ needs to transition on
	// its own after a short, fixed number of polls instead.
	//
	// Must survive at least the 2 immediately-adjacent polls $30d4's own
	// body always does right after asserting this ($30d8 then $30de,
	// re-checking "is it still asserted" with no real wait in between) --
	// a first attempt at exactly 2 exhausted on that very 2nd poll,
	// returning false right as $30de's own check ran, which the real ROM
	// reads as "device not present" and reports as such. 8 comfortably
	// clears that pair while still resolving quickly (a handful of tight
	// loop iterations, not wall-clock time) once the ROM moves on to the
	// genuine $30f7 busy-wait that needs the eventual release.
	static const int kListenerDatReadyPolls = 8;
	int listenerDatCountdown_ = kListenerDatReadyPolls;
	bool exposedListenerDat(); // ticks the countdown above, returns the (possibly just-released) value

	// EOI approximation -- see class comment. Ticked once per
	// onInstructionFetch() while waiting for the next bit (bitCount_==0
	// within an active phase); reset to 0 the moment a bit actually
	// clocks. lastByteWasEoi_ is read-and-cleared like byteReady_.
	static const int kIdleInstructionsForEoi = 40;
	int idleInstructions_ = 0;
	bool currentByteIsEoi_ = false; // latched at a byte's first bit, applied when it completes
	bool lastByteWasEoi_ = false;

	// Channel/file semantics -- see class comment. Updated from
	// clockBit() when a completed byte is a secondary address (under
	// ATN) or a data-phase byte (filename/payload).
	enum class SecondaryCmd { None, Open, Close, PlainSelect };
	SecondaryCmd secondaryCmd_ = SecondaryCmd::None;
	int activeChannel_ = -1;           // channel from the current secondary address, -1 = none
	std::string dataBuf_;              // accumulates the current session's filename or payload bytes
	std::array<std::string, 16> filenames_; // filename last OPENed on each channel, once finalized
	void onSecondaryByte(uint8_t b);
	void onDataByte(uint8_t b);
	void finalizeSession(); // called on UNLISTEN/UNTALK -- commits dataBuf_ to filenames_[] if it was an OPEN

	// TCP bridge to the file-serving companion -- see class comment.
	int bridgeFd_ = -1;
	bool bridgeSendLine(const std::string& line);
	bool bridgeReadLine(std::string& out, int timeoutMs = 3000);
	bool bridgeReadExact(uint8_t* buf, size_t n, int timeoutMs = 3000);
	void bridgeOpen(int channel, const std::string& name);
	void bridgeClose(int channel);
	void bridgeWrite(int channel, const std::string& data, bool eoi);
	void bridgeRequestRead(int channel, int size);
	std::string lastBridgeReply_;      // most recent OPEN/CLOSE/WRITE response line, for debugCommand
	std::string pendingLoadData_;      // bytes fetched for the LOAD encoder below to drain
	bool pendingLoadEoi_ = false;
	void fetchMoreLoadData();          // re-fills pendingLoadData_ once the encoder drains it, if !pendingLoadEoi_
	int loadChannel_ = -1;             // channel captured at LOAD kickoff, for fetchMoreLoadData()'s R,<ch>,.. calls

	// LOAD encoder -- drives pendingLoadData_ back onto the simulated
	// bus (deviceClk_/deviceDat_, read by in()'s WeAreListener branch
	// above), impersonating the "device" (we're the only thing that CAN
	// drive CLK in this role -- the real 1541/bridge has no actual
	// electrical presence here). The inverse of clockBit(): instead of
	// sampling an externally-driven CLK edge, THIS drives the edges,
	// following the exact same verified byte-handshake sequence
	// (VC1541.md's sources) from the sender's side instead of the
	// receiver's:
	//   TalkTurnaround: one-shot "I've taken the bus" CLK grab, entered
	//     unconditionally the instant we become WeAreListener (see
	//     onSecondaryByte()) -- real-ROM-traced via $336F/$3142/$3145/
	//     $3151-$3158 in VC1541.md: the Z80 waits for CLK to go
	//     asserted (grabbed), THEN waits for that same CLK to release
	//     again, with no DATA-based handshake in between. Exposed for
	//     exactly one in() poll (one half-step), then unconditionally
	//     moves on to WaitDataReady (if the bridge already has bytes
	//     queued) or the parked Idle (if not) on the very next poll --
	//     an earlier attempt made this permanent instead of transient,
	//     which fixed the same hang but then broke the following
	//     release-wait since nothing ever un-stuck it; reverted at the
	//     time, now fixed properly as its own distinct one-shot state.
	//   Idle: parked, nothing queued -- CLK released, no further state
	//     changes until a fresh LOAD/CAT session restarts from
	//     TalkTurnaround again.
	//   WaitDataReady: release CLK ("ready to send"); wait for the Z80
	//     (listener) to release its own DATA bit ("ready for data").
	//   BitSetup/BitValid (x8, LSB first): assert CLK + set DATA to the
	//     bit value (setup), then release CLK -- the Z80 samples on
	//     this rising edge (valid) -- alternating every poll. No per-
	//     bit listener handshake exists in the real protocol, so this
	//     has full autonomy over the pacing; advancing exactly one
	//     half-step per in() poll is a deliberate simplification (real
	//     Ts/Tv hold times aren't modelled), same approximation
	//     category as kIdleInstructionsForEoi, not hidden as fact.
	//   WaitAck: assert CLK again (matches the real post-byte Tf/Tbb
	//     window); wait for the Z80 to assert its own DATA bit ("byte
	//     accepted"), then move to the next byte (or Idle, if this was
	//     the last one -- see pendingLoadEoi_. NOT YET DONE: talker-side
	//     EOI signalling, i.e. deliberately delaying before the FINAL
	//     byte's WaitDataReady the way a real sender would -- this
	//     encoder currently treats the last byte the same as any other).
	enum class LoadEncState { TalkTurnaround, Idle, WaitDataReady, BitSetup, BitValid, WaitAck };
	LoadEncState loadEncState_ = LoadEncState::Idle;
	bool deviceClk_ = false; // our virtual-talker CLK drive (true=asserted/low, same convention as clkLine())
	bool deviceDat_ = false; // our virtual-talker DATA drive, valid only during BitSetup/BitValid
	size_t loadPos_ = 0;     // index into pendingLoadData_ of the byte currently being clocked
	int loadBitIdx_ = 0;     // 0-7, which bit of that byte
	void advanceLoadEncoder();
};
