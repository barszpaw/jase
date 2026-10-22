#ifndef _MINZX_H_
#define _MINZX_H_

#include <inttypes.h>
#include <string>
#include <vector>
#include <SDL.h>
#include "z80.h"
#include "Peripheral.h"
#include "MemoryMap.h"
#include "IAySound.h"

// Agent/debug port tracing: when set (via --trace-port on the command
// line, see main.cpp), every IN from this port that changes value gets
// printed. -1 = disabled. A single global is enough — there's one Z80
// and one port space in this process.
extern int g_tracePort;

// The "rampack factory" selector: each value is a self-contained memory
// configuration (ROM count/content, RAM bank count, initial page table)
// plus which paging port(s) processOutputPort activates. Adding a machine
// means adding one enum value, one case in setMachine(), one case in
// init()'s bank-count switch, one case in setupInitialPaging(), and (if it
// pages at runtime) one gated block in processOutputPort -- nothing else
// changes, same shape as adding a new Peripheral.
enum class MachineType { Spectrum48K, TimexTC2048, Spectrum128K };

// One playable tape block, with its own pulse timing. A plain .TAP block
// is just `data` (flag + payload + checksum) with every timing field left
// at the standard ROM defaults below. TZX gives each block (ID 0x11 Turbo
// Speed Data, ID 0x14 Pure Data, ID 0x12 Pure Tone, ID 0x20 Pause) its own
// parameters instead -- that's what "turbo loaders" actually are, not a
// different protocol, just different pilot/sync/bit lengths encoded per
// block. See FileMgr::loadTAP()/loadTZX() for how these get filled in, and
// MinZX::tapeStartBlock()/tapeNextEdge() for how they're consumed.
struct TapeBlock
{
	std::vector<uint8_t> data; // flag + payload + checksum, as on a real tape

	uint32_t pilotPulseLen     = 2168;
	uint32_t pilotCountHeader  = 8063; // used when data[0] bit7==0 (flag<0x80)
	uint32_t pilotCountData    = 3223; // used when data[0] bit7==1
	uint32_t sync1Len          = 667;
	uint32_t sync2Len          = 735;
	uint32_t bit0Len           = 855;
	uint32_t bit1Len           = 1710;
	uint8_t  usedBitsLastByte  = 8;    // TZX: bits actually used in the final data byte
	uint32_t pauseAfterMs      = 1000; // silence after this block before the next

	// TZX ID 0x14 Pure Data: straight to bit-encoding, no pilot/sync at all.
	bool hasPilotSync = true;

	// TZX ID 0x12 Pure Tone: just N pulses of a given length, no data.
	bool isPureTone = false;
	uint32_t pureTonePulseLen = 0;
	uint32_t pureTonePulseCount = 0;

	// TZX ID 0x20 Pause/Stop the tape: silence only. pauseAfterMs==0 here
	// specifically means "stop the tape" (per the TZX spec), not "no gap" --
	// unlike every other block type above, where 0 just means back-to-back.
	bool isPauseOnly = false;
};

class MinZX : public Z80operations
{
public:
	// Shared by main.cpp (SDL texture size) and generateScreen() so the two
	// can't drift apart like the old hardcoded 320/240 in each file did.
	// Width covers the Timex hi-res case (32 border + 512 content + 32
	// border) -- in standard 256px-content modes generateScreen() pixel-
	// doubles to fill the same width, so geometry never changes at runtime.
	static const int SCREEN_W = 576;
	static const int SCREEN_H = 240;

	void init();

	// Must be called before init() -- it decides ROM(s)/bank counts/the
	// initial page table that init() sets up. name: "48k" (default
	// behavior if never called), "tc2048", "128k".
	bool setMachine(const char* name);
	MachineType getMachine() const { return machine_; }

	// Must also be called before init() -- decides which of AyPsg (one
	// AY-3-8912) or TurboSound (two, NedoPC chip-select convention -- see
	// TurboSound.h) gets constructed as ay_. Off by default (plain AyPsg).
	void setTurboSound(bool on) { turboSoundRequested_ = on; }

	// Can be called any time (unlike setMachine()/setTurboSound(), this
	// doesn't affect init()'s own setup) -- enables the Timex SCLD video
	// mode register (port 0xFF, modes 0-2/6) regardless of --machine, same
	// as pico-spec's Config::timex_video toggle. See timexVideoEnabled_'s
	// comment for why DOCK paging (port 0xF4) isn't included.
	void setTimexVideo(bool on) { timexVideoEnabled_ = on; }

	// Opt-in "type punctuation like a PC keyboard": the live keyboard
	// matrix scan normally only maps letters/digits/RETURN/SPACE/shifts
	// 1:1 -- the real Spectrum's own SYMBOL SHIFT punctuation (same table
	// printed on every real keycap, already used by the debug protocol's
	// TYPE command) stays unmapped from a bare PC key, same as real
	// hardware would (you'd have to hold Symbol Shift yourself). With
	// this on, pressing the matching PC key directly produces the
	// Spectrum's SS+letter combination instead: unshifted for
	// , - . / ; = [ ] \ ' and RIGHT SHIFT + key for the rest
	// (* + : < > ? @ ^ _ { | } ~ and $, which maps to £ instead of a
	// literal dollar sign). RIGHT SHIFT is dedicated to this table so it
	// never fights LEFT shift's normal job (real CAPS SHIFT, e.g. for
	// uppercase letters). Off by default so the unmodified matrix
	// behavior (some emulators' "Spectrum keyboard" mode, as opposed to
	// this "PC keyboard" mode) is unaffected.
	void setPcKeys(bool on) { pcKeysEnabled_ = on; }

	// Opt-in: restores the old instant-delivery shortcut at PC==0x0556
	// (checkFastLoad()) on top of the real pulse engine, same as JASE had
	// before today's tape work. Default off, so the pure/no-shortcut
	// engine (tapeArmOnLoadEntry() only) is what actually gets exercised
	// -- this flag exists to A/B the two against each other, not to be
	// the normal way to load tapes.
	void setTapeFastload(bool on) { tapeFastloadEnabled_ = on; }

	// Test/debug override: replaces ROM pair 0 (the one setMachine() just
	// picked). For standalone diagnostic ROMs (e.g. Brendan Alford's
	// zx-diagnostics testrom.bin) that replace the system ROM outright.
	void setRomOverride(const char* path) { if (!romPaths_.empty()) romPaths_[0] = path; }

	// Resolves a relative asset path (zx48.rom, assets/tape_load_point.sna)
	// against the CURRENT WORKING DIRECTORY first (unchanged behavior --
	// e.g. the OSD's own file browser still opens wherever the user
	// launched jase from, same as any other CLI tool), then falls back to
	// the directory the running executable itself lives in (via
	// /proc/self/exe) if the file isn't there. Without this fallback,
	// running `jase` from anywhere other than its own directory silently
	// fails to find zx48.rom -- the machine never boots (ROM stays all
	// zero, an infinite NOP sled) and just sits there with a blank
	// screen forever, which looks exactly like a hang (confirmed: this
	// is what Pawel hit, not an actual deadlock -- see project notes).
	// Absolute paths (already rooted, or from --rom/--divide-rom/...)
	// pass through unchanged. Returns the original path unresolved if
	// neither location has the file, so the caller's own "cannot open"
	// error still fires with a clear message instead of this silently
	// swallowing the problem.
	static std::string resolveAssetPath(const std::string& relPath);

	// Loads a real TS2068 .DCK cartridge (Warajevo format, confirmed
	// against fusex's libspectrum/dck.c -- 9-byte header: bank ID then one
	// type byte per 8K page [0=null,1=ram-empty,2=rom,3=ram], followed by
	// 8K of data for each ROM/RAM page) straight into the DOCK bank pool
	// (ramBank(6+page)). Does NOT page anything in -- real cartridges only
	// occupy DOCK address space once software pages them in via port
	// 0xF4, same as any other DOCK content; call out(0xF4, mask) (or run
	// code that does) afterward for whichever pages the cartridge expects
	// paged in at boot.
	bool loadDockCartridge(const char* path);

	// Directly drives a port write without going through the Z80 -- lets
	// test setup page in DOCK banks (or anything else processOutputPort
	// handles) before execution starts.
	void poke_out(uint16_t port, uint8_t value) { processOutputPort(port, value); }

	void update(uint8_t* screen);

	// Dumps the exact buffer generateScreen() last wrote into (ARGB8888, the
	// same bytes handed to SDL_UpdateTexture) as a 32bpp BMP -- for
	// debug-protocol verification of the actual renderer output.
	bool saveScreenshotBMP(const char* path) const;

	void destroy();

	void reset();

	// /NMI is level-triggered on real hardware (held until serviced); the
	// z80cpp core models that as a one-shot flag consumed at the start of
	// the next execute() (see z80cpp/z80.cpp), so a single call here is
	// enough -- no need to hold/release like a real button. The CPU-level
	// signal always fires regardless of which peripherals exist -- it's
	// a generic Z80 feature, not owned by any one card; each peripheral's
	// own onNMI() (see Peripheral.h) decides what, if anything, the
	// signal means to it (e.g. Multiface's "red button").
	void triggerNMI() { z80->triggerNMI(); for (Peripheral* p : peripherals_) p->onNMI(); }

	void setBorderColor(uint8_t bcol) { border = bcol; }
	uint8_t getBorderColor() const { return border; }
	uint32_t getTStates() const { return tstates; }

	// Lets main.cpp pace the loop off the audio device's real queue depth
	// instead of a fixed SDL_Delay — see the beeper section in project
	// memory for why: a wall-clock delay alone drifts from the audio
	// device's fixed 44100 Hz consumption rate and causes underrun
	// crackle, exactly the "random bits" artifact reported and reproduced.
	uint32_t getQueuedAudioBytes() const;
	static const int kAudioSampleRate = 44100;
	static const int kAudioSamplesPerFrame = kAudioSampleRate / 50;
	void notifyKey(uint8_t specKey, bool isDown);

	// accessors (for loaders)
	Z80*     getCPU()    { return z80; }

	// Direct access to a specific RAM/ROM bank regardless of current
	// paging -- poke8()/peek8() only reach whatever's CURRENTLY mapped
	// at a page slot, but a .Z80 snapshot stores all 8 128K RAM banks
	// even ones not presently paged in anywhere. Loaders need this;
	// nothing else should (prefer poke8/peek8 for anything PC-relative).
	MemoryMap& getMemory() { return memory_; }

	// Expansion devices (Kempston now, AY-PSG/ZiFi/DivSD/Spectranext
	// later) — each owns its own port decoding and Layer 3 debug command.
	// DebugServer tries each in turn for a command it doesn't itself
	// recognize. The ULA's own port-0xFE functions (keyboard/border/
	// beeper/EAR) are NOT here — see Peripheral.h for why.
	const std::vector<Peripheral*>& getPeripherals() const { return peripherals_; }

	// Debug protocol Layer 2 (see project memory for the contract): a
	// single step runs exactly n instructions, bypassing update()'s
	// frame/tstate pacing entirely. Breakpoints are checked in both
	// step() and update()'s instruction loop via a cheap "any active?"
	// guard (ZEsarUX's cpu_core_loop_debug_check_breakpoints pattern) —
	// near-zero cost when no breakpoints are set.
	void step(int n);
	void addBreakpoint(uint16_t pc);
	void removeBreakpoint(uint16_t pc);
	std::vector<uint16_t> listBreakpoints() const { return breakpoints_; }
	bool justHitBreakpoint() const { return justHitBreakpoint_; }
	uint16_t lastBreakpointPC() const { return lastBreakpointPC_; }

	// Stages tape blocks for playback -- either via the real pulse engine
	// (tapeArmOnLoadEntry(), the default) or the --tape-fastload shortcut.
	// FileMgr::loadTAP() fills in plain-TAP blocks with the standard ROM
	// timing (TapeBlock's defaults); FileMgr::loadTZX() fills in whatever
	// each TZX block itself declares.
	void loadTapeBlocks(const std::vector<TapeBlock>& blocks);

	// Scripted key entry through the SAME keyboard-matrix path a human (or
	// xdotool) would use, one frame at a time, rather than faking the
	// ROM's internal register contracts directly — see project memory for
	// why the human-typed path is the one already proven correct. `chords`
	// is a sequence of steps; each step is the set of scancodes held down
	// together for that step (e.g. {LALT, P} for a shifted symbol). Used
	// both by the debug protocol's PRESSKEYS command and internally by
	// --load-tape's auto-start (LOAD "" + ENTER).
	void pressKeys(const std::vector<std::vector<SDL_Scancode>>& chords);

	// --load-tape's auto-start mode: queues LOAD "" + ENTER via
	// pressKeys(), after a delay long enough to clear the ROM's own boot
	// sequence first — a chord queued from frame 0 lands DURING boot,
	// before the keyboard scan loop is actually listening, and is gone by
	// the time it is (confirmed: a 2s hold spanned past boot and worked,
	// short holds starting at frame 0 didn't register at all).
	void startAutoTypeLoadCommand();

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
	MemoryMap memory_;
	uint8_t* ports;

	MachineType machine_ = MachineType::Spectrum48K;
	std::vector<std::string> romPaths_ = { "zx48.rom" };

	void setupInitialPaging();

	// Restores pages 0/1 (0x0000-0x3FFF) to what the MACHINE itself
	// currently says they should be -- called the instant a ROMCS
	// peripheral (Multiface, DivIDE) releases memory_'s romcs flag (see
	// MemoryMap::setRomcsAsserted()), from the per-instruction loop in
	// update()/step(). Never a raw saved-Bank-pointer restore done by
	// the peripheral itself: on 128K, esxDOS's own boot code writes port
	// 0x7FFD directly (a no-op on 48K, where that port doesn't exist; a
	// real ROM-bank switch on 128K) as ordinary startup housekeeping --
	// confirmed causing exactly this conflict, not a theoretical one.
	void reapplyMachinePaging();

	// Timex SCLD video mode register (port 0xFF, bits 0-2 mode / 3-5 ink) --
	// ported from pico-spec-drewpo's Video.cpp/Ports.cpp (confirmed on real
	// hardware there, matches Fuse). Modes 0/1/2/6 read from the standard
	// $4000-$7FFF 16K (screen0 = RAM bank 0, screen1 = RAM bank 1 -- the
	// renderer reads these two banks directly, bypassing whatever the CPU
	// currently has paged there, same as real SCLD/ULA hardware -- on a
	// 48K/128K machine this is just the ordinary screen memory, since
	// hi-res is wired to specific physical RAM chips on real hardware,
	// not bank-switched, regardless of what else the machine's own MMU
	// is doing). Modes 3/4/5/7 need a second physical bank ("bank 7") and
	// aren't implemented; they fall back to mode 0, matching pico-spec's
	// own "undefined -> standard" convention.
	//
	// Active when machine_ == TimexTC2048 OR timexVideoEnabled_ is set --
	// matching pico-spec's own Config::timex_video, which is a toggle
	// independent of Config::arch (Pawel's request: this should work
	// regardless of --machine, same as there). Port 0xF4 (DOCK paging,
	// right below) stays TC2048-only: unlike hi-res video, DOCK paging
	// genuinely needs the extra bank pool only TC2048's own memory map
	// provides -- pico-spec doesn't gate that port on timex_video either.
	bool timexVideoEnabled_ = false;
	bool pcKeysEnabled_ = false;
	uint8_t timexMode_ = 0;
	uint8_t timexInk_ = 0;

	// Timex DOCK/MMU paging (port 0xF4, one bit per 8K page 0-7): bit=0 ->
	// HOME (standard ROM/RAM for that page), bit=1 -> DOCK bank `page` from
	// the pool past the standard 6 RAM banks (see init()). Real SCLD decodes
	// this on the low byte regardless of the high byte, like port 0xFF.
	uint8_t timexPagedMask_ = 0;
	static const int kDockBankCount = 8; // full 8x8K = 64K DOCK population

	// Spectrum 128K paging (port 0x7FFD, decoded (port & 0x8003) == 1 per
	// the real partial hardware decode -- ZX Next ports.txt). Unlike Timex,
	// pages 2-3 ($4000-7FFF, RAM bank 5) and pages 4-5 ($8000-BFFF, RAM
	// bank 2) NEVER move -- only page0-1 (ROM pair) and page6-7 (RAM bank
	// 0-7) are switched. Bit3 (screen bank 5 vs 7) is read-only state for
	// the renderer, like timexMode_ -- it doesn't repage anything, bank 7
	// just isn't resident anywhere unless bits2:0 also select it for
	// page6-7. Active only when machine_ == Spectrum128K.
	uint8_t spectrum128Paging_ = 0; // last value written, for the lock bit
	bool spectrum128Locked_ = false;
	uint8_t spectrum128ScreenBank_ = 5; // 5 or 7, bit3 of the port

public:
	// For debug/inspection (see debugserver.cpp's STATE command): the raw
	// last value written to port 0x7FFD, plus the RAM bank it currently
	// selects at $C000 (bits 2:0) -- 0 outside Spectrum128K.
	uint8_t get7FFDRaw() const { return spectrum128Paging_; }
	int get7FFDRamBank() const { return spectrum128Paging_ & 0x07; }
	bool get7FFDLocked() const { return spectrum128Locked_; }

	// Tape engine inspection (see tapeAdvance()): is a block actually
	// moving, or is tapPos_/tapePhase_ stuck?
	size_t getTapeBlockCount() const { return tapBlocks_.size(); }
	size_t getTapePos() const { return tapPos_; }
	int getTapePhase() const { return (int)tapePhase_; } // 0=Stopped 1=Pilot 2=Sync 3=Data 4=Pause
	size_t getTapeByteIdx() const { return tapeByteIdx_; }
	uint32_t getTapePilotPulsesLeft() const { return tapePilotPulsesLeft_; }
private:

	uint8_t* lastScreenBuf_ = nullptr;
	uint32_t tstates;

	uint32_t cycleTstates;

	std::vector<Peripheral*> peripherals_;

	void loadROM();

	void generateScreen(uint8_t* screen);

	uint8_t processInputPort(uint16_t port);

	void processOutputPort(uint16_t port, uint8_t value);

	uint8_t border;

	// Per-scanline border colour (see generateScreen()): (tstates, value)
	// for every port-0xFE border write this frame, plus whatever was
	// already active when the frame started (carried over from the end
	// of the previous one, since `border` itself is never reset).
	std::vector<std::pair<uint32_t, uint8_t>> borderLog_;
	uint8_t borderAtFrameStart_ = 0;

	std::vector<uint16_t> breakpoints_;
	bool breakpointsEnabled_ = false;
	bool justHitBreakpoint_ = false;
	uint16_t lastBreakpointPC_ = 0;
	bool isBreakpoint(uint16_t pc) const;

	bool tapeFastloadEnabled_ = false;
	std::vector<TapeBlock> tapBlocks_;
	size_t tapPos_ = 0;
	void checkFastLoad();

	// General, loader-agnostic tape playback -- the fallback for whenever
	// checkFastLoad()'s exact-PC hook doesn't fire (ROM pilot/sync tone,
	// which blocks in real time no matter which loader is used, or a
	// custom loader that never calls through 0x0556 at all). Real EAR-bit
	// pulse timing (standard TAP pilot/sync/data constants), started
	// unconditionally the moment a tape is loaded and left running
	// straight through to the end -- a physical cassette deck doesn't
	// inspect what the CPU is doing to decide whether to turn; it just
	// plays from the moment it's inserted, continuously, like any other
	// audio source. An earlier version tried to be clever and only start
	// once it saw the Z80 "polling port 0xFE in a way that looks like
	// tape-edge code" -- border-flashing with that scheme proved nothing
	// about whether the ROM was actually decoding anything (the flash is
	// driven by the SAME heuristic, not by successful sync), and it adds
	// a whole class of maybe-it-matched-maybe-it-didn't uncertainty for
	// zero benefit over just always playing.
	enum class TapePhase { Stopped, Pilot, Sync, Data, Pause };
	TapePhase tapePhase_ = TapePhase::Stopped;
	bool tapeEarBit_ = false;
	uint32_t tapeEdgeCountdown_ = 0;   // T-states until the next edge/event
	uint32_t tapePilotPulsesLeft_ = 0;
	size_t tapeByteIdx_ = 0;           // index within tapBlocks_[tapPos_]
	uint8_t tapeBitMask_ = 0;          // current bit within tapeByteIdx_, MSB first
	int tapeBitHalf_ = 0;              // 0 = first half-pulse of the bit, 1 = second

	void tapeAdvance(uint32_t deltaT);
	void tapeStartBlock();
	void tapeNextEdge();

	// The ONE trigger for starting continuous tape playback: the first
	// time PC reaches 0x0556 (confirmed present at the same address in
	// both the 48K ROM and 128K ROM1 -- two real call sites, 0x076E and
	// 0x0802, both "CALL 0x0556") while the tape hasn't started yet. This
	// is LD-BYTES's actual, singular, 100%-reliable entry point -- not a
	// guess about what a polling loop looks like, so it can't miss the
	// leader/pilot signal by arming too late, and it can't mis-fire on
	// unrelated code the way an address-range-plus-opcode heuristic can.
	// Deliberately does nothing else (no instant delivery) -- once armed,
	// tapeAdvance() alone carries the whole tape through to the end.
	void tapeArmOnLoadEntry(uint16_t pc);

	struct AutoTypeKeyStep { std::vector<SDL_Scancode> keys; int frames; };
	std::vector<AutoTypeKeyStep> autoTypeSeq_;
	size_t autoTypeIdx_ = 0;
	int autoTypeFrameCounter_ = 0;
	void advanceAutoType();

	// Beeper (port 0xFE bit 4): a 1-bit speaker signal turned into a real
	// square wave. One frame's worth of samples (kSamplesPerFrame, fixed
	// since kSampleRate/50 is exact) is built up as OUT 0xFE edges land
	// at their real T-state position, then the AY (if present, see
	// AyPsg) is mixed in on top, then it's all queued to SDL's audio
	// device once per frame in update().
	static const int kSampleRate = 44100;
	static const int kSamplesPerFrame = kSampleRate / 50;
	SDL_AudioDeviceID audioDev_ = 0;
	std::vector<uint8_t> audioBuf_;
	uint32_t audioSampleIndex_ = 0;
	uint8_t beeperLevel_ = 128;
	void initAudio();
	void audioWriteEdge(uint8_t newLevel);
	void flushAudioFrame();

	// Set once in init() to whichever of AyPsg/TurboSound got constructed
	// (see setTurboSound()) -- flushAudioFrame() mixes its output into
	// audioBuf_ alongside the beeper through this one shared interface
	// (IAySound), without needing to know which concrete type it is. Not
	// part of the generic Peripheral interface since most devices
	// (Kempston, future ZiFi/DivSD) don't generate audio; this is a
	// direct pointer specifically for the audio pipeline, separate from
	// the generic peripherals_ list (which the AY peripheral is ALSO in,
	// for port IN/OUT and its own debug command).
	IAySound* ay_ = nullptr;
	bool turboSoundRequested_ = false;

public:
	// Non-owning accessor so main.cpp can call connectTo() with a
	// user-supplied host:port after init() constructs the peripheral —
	// same "separate concerns" split as ay_ above.
	class ZiFi* getZiFi() const { return zifi_; }

	// Same idea: main.cpp calls setEnabled()/loadRom() on this after
	// init() constructs it, for --multiface1/--multiface1-rom.
	class Multiface* getMultiface() const { return multiface_; }

	// Same idea: main.cpp calls setEnabled()/loadRom()/insertDisk() on
	// this after init() constructs it, for --divide/--divide-rom/
	// --divide-disk.
	class DivIde* getDivIde() const { return divide_; }

	// Same idea: main.cpp calls setEnabled()/loadRom() on this after
	// init() constructs it, for --sdi/--sdi-rom.
	class SDI* getSDI() const { return sdi_; }
private:
	class ZiFi* zifi_ = nullptr;
	class Multiface* multiface_ = nullptr;
	class DivIde* divide_ = nullptr;
	class SDI* sdi_ = nullptr;
};

#endif // _MINZX_H_