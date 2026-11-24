#pragma once
#include "Peripheral.h"
#include "MemoryMap.h"
#include "FddStation.h"
#include "MonitorLink.h"
#include <cstdint>
#include <string>

// Timex FDD 3000 disk station stand-in -- a real, standalone Polish
// peripheral: a separate little computer (Z80A + 64K RAM + WD1772 FDC)
// connected to the Spectrum through a thin "interface" (4K ROM + 1K RAM +
// address-decode glue) that talks to the station over a single byte-wide
// link port. Facts below cross-checked from TWO independent, non-code
// sources (same clean-room policy as Multiface/DivIDE/SDI -- see
// project memory's "GPL firewall practice"): (1) `z80dasm` disassembly of
// the REAL interface ROM (`fdd3stavi.rom`/`fdd3timex.rom`, confirmed port
// 0xEF used repeatedly for IN/OUT, confirmed address #0604 is the
// documented "reactivate standard Spectrum ROM" exit point); (2) the
// 5-part "TOS bez tajemnic" magazine article (Bajtek, Wojciech Jabłoński),
// which documents the interface ROM's memory map, the command protocol,
// and the on-disk filesystem layout byte-for-byte.
//
// GOAL (see project memory, project_jase_fdd3000.md): NOT TOS/game
// compatibility -- running real CP/M 2.2 (confirmed real: a Polbrit
// "BIOS 3.1, 1987" boots on real `CPM 525.dsk`) on the station's OWN Z80,
// with the Spectrum reduced to a dumb terminal. CONFIRMED WORKING,
// 2026-11-11.
//
// Station I/O block 7 (drive/config register) bit layout: bits 0-3 =
// /select drive 0-3 (active LOW -- first zero bit wins), bit 4 = side
// select (0 = side B, 1 = side A), bit 5 = /DDEN (0 = double density),
// bit 6 = /BOOTROM (0 = station's low-memory boot-ROM view visible),
// bit 7 = /HEAD (not modeled).
//
// TWO ways to run the station, selected by connectBridge() having been
// called or not (2026-11-11 unification -- see FddStation.h's own class
// comment for why this used to be two separately-maintained files):
//
//   IN-PROCESS (default, --fdd3000): this class owns a FddStation
//   directly (station_ below) and steps it from onInstructionExecuted(),
//   in lockstep with the host's own virtual time, using a real T-state
//   clock-domain-crossing ratio (stepStationForOneHostInstruction()).
//   Matters for JASE's pausable/single-steppable debug protocol -- see
//   project memory for why an async, wall-clock-paced second process was
//   rejected here.
//
//   BRIDGE (--fdd3000-bridge HOST:LINKPORT[:DEBUGPORT]): the station runs
//   in a SEPARATE, real-time-paced OS process (fdd3000_station), reached
//   over two TCP channels: the link-byte stream (one byte each direction,
//   the real single wire) and a text command channel (RESET, CHANGEDISK,
//   REGS/SETREG/MEM/POKE/STEP for debugging, same shape as JASE's own
//   debug protocol). station_ is NOT used in this mode at all -- the host
//   side (ROMCS paging, interface ROM/RAM, port 0xEF) is identical either
//   way, only where the link byte actually goes differs.
//
//   HOST side (the Spectrum's own Z80, already emulated by MinZX):
//     - port 0xEF: the single link byte, read = what the station last
//       wrote, write = latches a byte for the station to read.
//     - ROMCS paging of the interface ROM/RAM over pages 0/1
//       (0x0000-0x3FFF): page in on PC==0x0000 or PC==0x0008, page out
//       on PC==0x0604 -- IMMEDIATE/same-cycle, NOT deferred by one
//       onInstructionFetch() call like DivIDE's trapAfter_/unmapAfter_.
//       Confirmed from Fuse's real, ACTIVE paging hooks (z80_ops.c's
//       CHECK(fdd3000_early,...)/CHECK(fdd3000_late,...)) -- using
//       DivIDE's deferred pattern here was a real, confirmed bug (host PC
//       stuck looping the boot handshake forever instead of completing
//       it).
//
// Simplifications vs real hardware, documented not hidden (see
// project memory for the full phased plan):
//   - The station's internal bootstrap is, by default, a trivial built-in
//     placeholder (DI; JR $) -- see FddStation::loadBootRom() for
//     supplying the real 2048-byte dump instead.
//   - Station-CPU-to-host-CPU clock ratio: a real clock-domain crossing
//     (stepStationForOneHostInstruction()), currently an EXPERIMENTAL
//     8MHz (16:7 vs host 3.5MHz) rather than the real 4MHz (8:7) -- see
//     that method's own comment for the tuning history. In-process mode
//     only; the standalone station process paces itself independently.
class Fdd3000 : public Peripheral
{
public:
	Fdd3000();
	~Fdd3000() override;

	const char* name() const override { return "FDD3000"; }
	bool in(uint16_t port, uint8_t& value) override;
	bool out(uint16_t port, uint8_t value) override;
	void onInstructionFetch(uint16_t pc, MemoryMap& memory, uint32_t tstates) override;
	void onInstructionExecuted(MemoryMap& memory, uint32_t tstates) override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

	// The host-side interface's own 1K RAM (interfaceRam_ below) is a
	// real, physically separate RAM chip on the interface card -- a plain
	// Z80 RESET pulse (onReset(false), F11/Dandanator-button-triggered)
	// does not touch it on real hardware, same as it doesn't touch the
	// station's own RAM across the link. Only a genuine power-cycle
	// (onReset(true), MinZX::hardReset()) clears it back to its true
	// power-on content.
	void onReset(bool hard) override;

	// --fdd3000 on the command line -- off by default, same convention
	// as --multiface1/--divide.
	void setEnabled(bool on) { enabled_ = on; }
	bool isEnabled() const { return enabled_; }

	// The real interface ROM -- accepts 8192 bytes (the real, full 8K
	// ROMCS window content, preferred), 32768 bytes (a full 27C256 EPROM
	// dump -- only its first 8K is actually used), or 4096 bytes (the
	// older fdd3stavi.rom/fdd3timex.rom dumps -- mirrored into both
	// halves of the 8K window, a confirmed-approximate fallback).
	bool loadInterfaceRom(const char* path);
	const std::string& getRomPath() const { return romPath_; }

	// --fdd3000-bridge HOST:LINKPORT[:DEBUGPORT] -- switches this
	// instance into bridge mode (see class comment). Must be called
	// before the station is used; there's no live mode switch.
	bool connectBridge(const char* host, int linkPort, int debugPort);
	bool isBridgeMode() const { return bridgeMode_; }

	// The STATION's own internal boot ROM -- in-process mode only (see
	// class comment: bridge mode's station loads its own ROM at its own
	// process startup, the host has no wire to push ROM content over).
	bool loadStationBootRom(const char* path);
	const std::string& getStationRomPath() const { return stationRomPath_; }

	// A real disk image (CPCEMU/Extended DSK -- see DskImage.h) for one
	// of the station's 4 drives (0=A..3=D). In bridge mode this sends a
	// real CHANGEDISK command over the command channel instead of
	// touching a local FddStation.
	bool insertDisk(int driveIndex, const char* path);
	const std::string& getDiskPath(int driveIndex) const { return diskPath_[driveIndex]; }

	// Disk-activity LED signal (2026-11-14, user-directed): true for a
	// short, human-visible window after the HOST's own last IN/OUT on
	// port 0xEF (in()/out() below) -- works identically in-process or
	// bridge mode (both always run host-side, zero round-trip cost,
	// unlike querying the remote station's own WD1772 BUSY status
	// would be). Reflects BUS activity (the interface being talked to
	// at all, including retry/poll traffic), same as a real floppy
	// drive's own activity LED -- not "a sector is successfully
	// transferring" specifically.
	bool isRecentlyActive() const;

	// "Insert New" (2026-11-14) -- creates a blank, already-formatted
	// disk at a throwaway temp path and inserts it (see FddStation::
	// insertNewDisk()'s own comment for why no real path is needed up
	// front). In bridge mode this sends a real NEWDISK command over the
	// command channel instead of touching a local FddStation. numSides:
	// 1 for 170KB (numTracks=40), 2 for 340KB (numTracks=40) or 620KB
	// (numTracks=80).
	bool insertNewDisk(int driveIndex, int numTracks, int numSides);

	// "Save As" -- re-points the drive's disk at a REAL, user-chosen
	// path. isDiskTemporary()/hasAnyUnsavedDisk() back the exit-time
	// unsaved-disk guard (see main.cpp's SDL_QUIT handling).
	bool saveDiskAs(int driveIndex, const char* newPath);
	bool isDiskTemporary(int driveIndex); // not const -- bridge mode needs a live debugRequest() round-trip
	bool hasAnyUnsavedDisk();

	// FDD3000's own physical reset button -- separate from MinZX::
	// reset()/F11. Real workflow: swap which disk image is in "drive A",
	// THEN press this -- the station's tiny boot ROM just loads whatever
	// is in drive A at the moment it resets, which is how real hardware
	// switches between booting TOS and booting CP/M.
	void stationReset();

	// Live decoder for the ONE wire that actually connects the interface
	// (host side) to the station (its own Z80): the link byte, port 0xEF
	// on the host / I/O block 1 on the station. Same ON/OFF runtime-toggle
	// convention as SDI_8255's IECTRACE/IOTRACE. Logs EVERY host IN/OUT on
	// 0xEF; the station's own side of the same trace is FddStation's own
	// (in-process) or the external process's own stderr (bridge mode).
	void setLinkTraceEnabled(bool on) { linkTraceEnabled_ = on; station_.setLinkTraceEnabled(on); }
	bool isLinkTraceEnabled() const { return linkTraceEnabled_; }

	// Same convention, for the WD1772's OWN command-level trace -- in
	// bridge mode this toggles it on the EXTERNAL station via the command
	// channel instead of a local FddStation.
	void setWdTraceEnabled(bool on);
	bool isWdTraceEnabled() const;

	// WD2123 CH1/CH2 (src/Wd2123.h) as local TCP listeners -- in-process
	// mode only (see pumpSerial()'s own comment on why bridge mode is a
	// no-op here). channel: 0=CH1, 1=CH2. Same byte-level-shortcut
	// transport fdd3000_station/main.cpp's own --serial1/2-port already
	// uses against the SAME FddStation::serial() API -- this is the
	// in-process side of that, not a different protocol.
	bool setSerialPort(int channel, int port);
	int getSerialPort(int channel) const { return (channel == 0 || channel == 1) ? serialPort_[channel] : 0; }

	// Call once per emulated frame (50Hz, same convention as ZiFi::pump()/
	// Multiply::pump() -- see MinZX::update()): accepts/drops clients and
	// drains both channels' FIFOs against their sockets.
	void pumpSerial();

	// MonitorLink (src/MonitorLink.h) as a local TCP listener -- a native
	// C++ reimplementation of the real Monitor Emulator's host-link
	// protocol, so a TCP client (telnet) can act as CP/M's console
	// without any ZX Spectrum Z80 code or screen at all. In-process mode
	// only, same reasoning as setSerialPort() above. Not meant to be used
	// AT THE SAME TIME as a real Monitor program also running on the
	// emulated host CPU -- both would drive the same link-byte latch.
	bool setMonitorPort(int port);
	int getMonitorPort() const { return monitorPort_; }
	void pumpMonitorSocket(); // once per frame -- the actual socket I/O

private:
	void applyHostPaging(MemoryMap& memory);
	void logLinkTrace(const char* direction, uint8_t value) const;
	void stepStationForOneHostInstruction(uint32_t hostTstates);
	void acceptSerialIfIdle(int channel);
	void dropSerialClient(int channel);
	void acceptMonitorIfIdle();
	void dropMonitorClient();

	// ---- Bridge-mode-only: the command channel to the external station
	// process. debugRequest() is blocking/short-timeout -- fine for
	// infrequent debug commands (RESET, CHANGEDISK, REGS/SETREG/...),
	// NOT on the hot path (that's the link byte, handled directly in
	// in()/out() via a non-blocking socket). ----
	std::string debugRequest(const std::string& line);

	bool bridgeMode_ = false;
	int linkFd_ = -1;
	int debugFd_ = -1;
	std::string bridgeHost_;
	int bridgeLinkPort_ = 0;
	int bridgeDebugPort_ = 0;
	std::string debugRxBuf_;

	// Real clock-domain-crossing budget (in-process mode only), in units
	// of "1/kRatioDen-th of a station T-state" -- see
	// stepStationForOneHostInstruction()'s own comment.
	int64_t stationTstateBudget7_ = 0;

	// onInstructionExecuted()'s tstates param is MinZX::update()'s raw
	// per-FRAME cumulative counter, not a per-instruction delta -- same
	// trap Dandanator.h's monoTstates_ comment documents. Feeding the raw
	// cumulative value straight into the station T-state budget grows it
	// unboundedly across a frame (confirmed live, 2026-11-09: native
	// process spinning at 100% CPU, needing kill -9). lastRawTstates_ +
	// the same wrap-safe delta computation Dandanator uses fixes it.
	uint32_t lastRawTstates_ = 0;

	bool enabled_ = false;
	bool linkTraceEnabled_ = false;
	uint16_t lastHostPc_ = 0; // stashed in onInstructionFetch(), for logLinkTrace()'s own hostPC column
	uint32_t lastActivityMs_ = 0; // see isRecentlyActive()'s own comment

	// Host-side ROMCS paging state -- IMMEDIATE/same-cycle PC trap, see
	// class comment for why this differs from DivIDE's deferred pattern.
	bool active_ = false;

	MemoryMap::Bank interfaceRom_{}; // host page 0 (0x0000-0x1FFF): real 4K ROM, mirrored to fill 8K
	MemoryMap::Bank interfaceRam_{}; // host page 1 (0x2000-0x3FFF): real 1K RAM (article: #2000-#23FF), rest of the 8K window along for the ride
	uint8_t linkToStation_ = 0;  // bridge-mode mirror of the link byte written to the station (in-process mode keeps this in station_ instead)
	uint8_t linkFromStation_ = 0; // bridge-mode mirror of the link byte read from the station

	std::string romPath_;
	std::string stationRomPath_;
	std::string diskPath_[FddStation::kNumDrives];

	// In-process mode's actual station computer (see class comment) --
	// unused, but still constructed, in bridge mode.
	FddStation station_;

	// WD2123 CH1/CH2 local TCP listeners -- see setSerialPort()/
	// pumpSerial()'s own comments. Index 0=CH1, 1=CH2.
	struct SerialListener {
		int listenFd = -1;
		int clientFd = -1;
	};
	SerialListener serialListener_[2];
	int serialPort_[2] = {0, 0};

	// MonitorLink local TCP listener -- see setMonitorPort()/
	// pumpMonitorSocket()'s own comments. Reuses the same SerialListener
	// shape (listenFd/clientFd) since the accept/drop logic is identical.
	MonitorLink monitorLink_;
	SerialListener monitorListener_;
	int monitorPort_ = 0;
	Vt52ToAnsiFilter monitorVt52Filter_; // see pumpMonitorSocket()
};
