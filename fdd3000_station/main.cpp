// fdd3000_station -- independent FDD3000 station process. Owns ONE
// FddStation (src/FddStation.h -- the SAME class the in-process
// architecture uses, see its own class comment for why this matters:
// two independently-maintained copies of this logic already diverged
// once, 2026-11-11, over a single stray pacing constant). This file owns
// only the process boundary: TCP listeners, the command-channel protocol,
// and real-time pacing at the station's own clock -- no station logic of
// its own at all.
//
// TWO separate TCP channels, matching Fdd3000.h's own class comment:
//   - link port: the real single wire, one latched byte each direction,
//     raw bytes, no framing.
//   - debug port: a text command channel -- RESET, CHANGEDISK <drive>
//     <path>, NEWDISK <drive> <tracks> <sides>, SAVEDISK <drive> <path>,
//     DISKTEMP <drive>, REGS, SETREG <reg> <hex>, MEM <addr> <len>, POKE
//     <addr> <b1,b2,...>, STEP [n], LINKTRACE ON/OFF, WDTRACE ON/OFF,
//     STATUS. Same line-based shape as JASE's own --debug-port protocol.

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
#include <sstream>
#include <chrono>
#include <thread>

#include "../src/FddStation.h"
#include "../src/MonitorLink.h"

static FddStation g_station;

// --max-speed: skip the real-time pacing sleep_for() entirely, running the
// station as fast as the host OS will schedule it. Added to isolate WHICH
// part of "the link and data transmission" (user's own framing) actually
// causes bridge-mode's poor handshake reliability: with pacing removed,
// any remaining failures can't be blamed on the two processes' independent
// sleep_for() clocks drifting out of step with each other -- only on the
// socket transport itself (or the station/host protocol logic) would be
// left as a cause.
static bool g_maxSpeed = false;

// ---- Minimal TCP listener helper (same shape as DebugServer::init(),
// src/debugserver.cpp) ----
struct Listener {
	int listenFd = -1;
	int clientFd = -1;
	std::string rxBuf;

	bool init(int port)
	{
		listenFd = socket(AF_INET, SOCK_STREAM, 0);
		if (listenFd < 0) return false;
		int opt = 1;
		setsockopt(listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = INADDR_ANY;
		addr.sin_port = htons((uint16_t)port);
		if (bind(listenFd, (sockaddr*)&addr, sizeof(addr)) < 0) { close(listenFd); listenFd = -1; return false; }
		if (listen(listenFd, 1) < 0) { close(listenFd); listenFd = -1; return false; }
		fcntl(listenFd, F_SETFL, O_NONBLOCK);
		return true;
	}

	void acceptIfIdle(const char* label)
	{
		if (clientFd >= 0 || listenFd < 0) return;
		int fd = accept(listenFd, nullptr, nullptr);
		if (fd >= 0) {
			fcntl(fd, F_SETFL, O_NONBLOCK);
			int one = 1;
			setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
			clientFd = fd;
			rxBuf.clear();
			fprintf(stderr, "fdd3000_station: %s client connected\n", label);
		}
	}

	void dropClient(const char* label)
	{
		if (clientFd >= 0) {
			close(clientFd);
			clientFd = -1;
			fprintf(stderr, "fdd3000_station: %s client disconnected\n", label);
		}
	}
};

static Listener g_linkListener;
static Listener g_debugListener;

// WD2123 CH1/CH2 (src/Wd2123.h) -- test listeners, one TCP port per
// channel. Only bound if a port was given on the command line (unlike
// link/debug, which are mandatory). Byte-level: unlike pumpLinkSocket()'s
// deliberate one-byte-per-call restriction (that one's guarding a SINGLE
// shared latch against desync), each channel here has its own FIFO (see
// Wd2123::popTxByte()/pushRxByte()), so draining everything available per
// call is both safe and more efficient -- nothing to desync.
static Listener g_serial1Listener;
static Listener g_serial2Listener;
static int g_serial1Port = 0;
static int g_serial2Port = 0;

static void pumpSerialSocket(Listener& listener, int ch, const char* label)
{
	listener.acceptIfIdle(label);
	if (listener.clientFd < 0) return;

	uint8_t buf[256];
	ssize_t n = recv(listener.clientFd, buf, sizeof(buf), 0);
	if (n == 0) { listener.dropClient(label); }
	else if (n > 0) {
		for (ssize_t i = 0; i < n; i++) g_station.serial().pushRxByte(ch, buf[i]);
	}

	uint8_t out[256];
	size_t outLen = 0;
	uint8_t b;
	while (outLen < sizeof(out) && g_station.serial().popTxByte(ch, b)) out[outLen++] = b;
	if (outLen > 0) {
		if (send(listener.clientFd, out, outLen, 0) < 0) listener.dropClient(label);
	}
}

// MonitorLink (src/MonitorLink.h) -- the standalone-process side of the
// SAME headless-CP/M-console bridge Fdd3000's own setMonitorPort()/
// pumpMonitorSocket() provide in-process (see docs/FDD3000/HEADLESS_CPM.md).
// g_monitorLink.tick() is driven once per g_station.step() in the main
// loop below -- the standalone process has no host CPU to hang a
// per-instruction hook off of, so "once per station instruction" is this
// process's own equivalent of that cadence. Socket I/O itself stays
// separate (pumpMonitorSocket(), once per batch), same split as
// pumpSerialSocket() above.
static MonitorLink g_monitorLink;
static Listener g_monitorListener;
static int g_monitorPort = 0;

static void pumpMonitorSocket()
{
	g_monitorListener.acceptIfIdle("monitor");
	if (g_monitorListener.clientFd < 0) return;

	uint8_t buf[256];
	ssize_t n = recv(g_monitorListener.clientFd, buf, sizeof(buf), 0);
	if (n == 0) { g_monitorListener.dropClient("monitor"); }
	else if (n > 0) {
		for (ssize_t i = 0; i < n; i++) g_monitorLink.pushIncomingByte(buf[i]);
	}

	uint8_t out[256];
	size_t outLen = 0;
	uint8_t b;
	while (outLen < sizeof(out) && g_monitorLink.popOutgoingByte(b)) out[outLen++] = b;
	if (outLen > 0) {
		if (send(g_monitorListener.clientFd, out, outLen, 0) < 0) g_monitorListener.dropClient("monitor");
	}
}

// Consumes AT MOST ONE byte per call, even if more are already queued on
// the socket -- a tight drain-everything loop here would overwrite the
// host-to-station latch N times with nothing ever observing the
// intermediate values, desyncing the single-byte-wide real protocol.
static void pumpLinkSocket()
{
	g_linkListener.acceptIfIdle("link");
	if (g_linkListener.clientFd < 0) return;
	uint8_t b;
	ssize_t n = recv(g_linkListener.clientFd, &b, 1, 0);
	if (n == 1) {
		g_station.setHostByte(b);
	} else if (n == 0) {
		g_linkListener.dropClient("link");
	}
}

static void flushLinkByteIfDirty()
{
	// Send on EVERY station OUT, not just when the value changes --
	// FOUND LIVE, 2026-11-11: an earlier "only resend on value change"
	// version of this function silently broke the handshake whenever the
	// one real transmission of a given value was lost (e.g. a
	// connection-startup race) -- the station's own firmware legitimately
	// retries by writing the SAME byte again, which "no change" logic
	// can't tell apart from "nothing happened" and so never re-sends. See
	// FddStation::stationByteDirty()'s own comment. Real hardware has no
	// such optimization either -- every write is a real latch event.
	if (!g_station.stationByteDirty()) return;
	g_station.clearStationByteDirty();
	uint8_t v = g_station.stationByte();
	if (g_linkListener.clientFd >= 0) {
		if (send(g_linkListener.clientFd, &v, 1, 0) < 0) g_linkListener.dropClient("link");
	}
}

static std::string handleDebugCommand(const std::string& line)
{
	std::istringstream iss(line);
	std::vector<std::string> toks;
	std::string t;
	while (iss >> t) toks.push_back(t);
	if (toks.empty()) return "ERR empty command";

	std::string cmd = toks[0];
	for (char& c : cmd) c = (char)toupper((unsigned char)c);

	char out[320];

	if (cmd == "STATUS") {
		snprintf(out, sizeof(out),
		         "OK bootRomVisible=%d stationPC=%04x wdStatus=%02x wdTrack=%02x wdSector=%02x wdDrq=%d wdIntrq=%d",
		         g_station.bootRomVisible() ? 1 : 0, g_station.getRegPC(),
		         g_station.fdc().peekStatus(), g_station.fdc().readTrackReg(), g_station.fdc().readSectorReg(),
		         g_station.fdc().drq() ? 1 : 0, g_station.fdc().intrq() ? 1 : 0);
		return out;
	}

	if (cmd == "REGS") {
		snprintf(out, sizeof(out),
		         "OK AF=%04x BC=%04x DE=%04x HL=%04x AFx=%04x BCx=%04x DEx=%04x HLx=%04x "
		         "IX=%04x IY=%04x SP=%04x PC=%04x I=%02x R=%02x IFF1=%d IFF2=%d IM=%d",
		         g_station.getRegAF(), g_station.getRegBC(), g_station.getRegDE(), g_station.getRegHL(),
		         g_station.getRegAFx(), g_station.getRegBCx(), g_station.getRegDEx(), g_station.getRegHLx(),
		         g_station.getRegIX(), g_station.getRegIY(), g_station.getRegSP(), g_station.getRegPC(),
		         g_station.getRegI(), g_station.getRegR(),
		         g_station.getIFF1() ? 1 : 0, g_station.getIFF2() ? 1 : 0, g_station.getIM());
		return out;
	}

	if (cmd == "SETREG" && toks.size() >= 3) {
		uint16_t val = (uint16_t)strtoul(toks[2].c_str(), nullptr, 16);
		std::string reg = toks[1];
		for (char& c : reg) c = (char)toupper((unsigned char)c);
		bool ok = true;
		if (reg == "AF") g_station.setRegAF(val);
		else if (reg == "BC") g_station.setRegBC(val);
		else if (reg == "DE") g_station.setRegDE(val);
		else if (reg == "HL") g_station.setRegHL(val);
		else if (reg == "AFX") g_station.setRegAFx(val);
		else if (reg == "BCX") g_station.setRegBCx(val);
		else if (reg == "DEX") g_station.setRegDEx(val);
		else if (reg == "HLX") g_station.setRegHLx(val);
		else if (reg == "IX") g_station.setRegIX(val);
		else if (reg == "IY") g_station.setRegIY(val);
		else if (reg == "SP") g_station.setRegSP(val);
		else if (reg == "PC") g_station.setRegPC(val);
		else ok = false;
		return ok ? "OK" : "ERR unknown register";
	}

	if (cmd == "MEM" && toks.size() >= 3) {
		uint16_t addr = (uint16_t)strtoul(toks[1].c_str(), nullptr, 16);
		long len = strtol(toks[2].c_str(), nullptr, 10);
		if (len < 0) len = 0;
		if (len > 4096) len = 4096;
		std::string resp = "OK ";
		char hex[3];
		for (long i = 0; i < len; i++) {
			snprintf(hex, sizeof(hex), "%02x", g_station.peek((uint16_t)(addr + i)));
			resp += hex;
		}
		return resp;
	}

	if (cmd == "DUMPBIN" && toks.size() >= 4) {
		// Raw-bytes binary dump, as opposed to MEM's hex-text one -- the
		// real motivation (2026-11-13, user-directed): the program loaded
		// into station RAM from disk has no static file anywhere (unlike
		// the boot ROM), so this is the only way to extract a lossless,
		// re-disassemblable binary of it. Pairs with LIVEDISASM's own text
		// report, same two-artifact pattern as
		// docs/FDD3000/FDD35_station_clone/fdd3boot_disasm.md already has
		// for the boot ROM (there: a real file; here: extracted live).
		uint16_t addr = (uint16_t)strtoul(toks[2].c_str(), nullptr, 16);
		long len = strtol(toks[3].c_str(), nullptr, 10);
		if (len < 0) len = 0;
		if (len > 65536) len = 65536;
		FILE* pf = fopen(toks[1].c_str(), "wb");
		if (!pf) return "ERR cannot open output path";
		for (long i = 0; i < len; i++) {
			uint8_t b = g_station.peek((uint16_t)(addr + i));
			fwrite(&b, 1, 1, pf);
		}
		fclose(pf);
		snprintf(out, sizeof(out), "OK wrote %ld bytes to %s", len, toks[1].c_str());
		return out;
	}

	if (cmd == "POKE" && toks.size() >= 3) {
		uint16_t addr = (uint16_t)strtoul(toks[1].c_str(), nullptr, 16);
		std::istringstream bs(toks[2]);
		std::string b;
		int count = 0;
		while (std::getline(bs, b, ',')) {
			g_station.poke((uint16_t)(addr + count), (uint8_t)strtoul(b.c_str(), nullptr, 16));
			count++;
		}
		return "OK";
	}

	if (cmd == "STEP") {
		int n = toks.size() >= 2 ? atoi(toks[1].c_str()) : 1;
		if (n < 1) n = 1;
		for (int i = 0; i < n; i++) g_station.step();
		return "OK";
	}

	if (cmd == "RESET") {
		fprintf(stderr, "fdd3000_station: RESET\n");
		g_station.reset();
		return "OK";
	}

	if (cmd == "CHANGEDISK" && toks.size() >= 3) {
		// NEW (2026-11-11, user-requested): a real runtime command to
		// swap which disk image is in a drive, instead of only being
		// settable via --disk-a/b/c/d at process startup. Matches the
		// real workflow Fdd3000.h's stationReset() comment documents:
		// swap the disk, THEN reset -- the station's boot ROM just loads
		// whatever is in drive A at reset time.
		int drive = atoi(toks[1].c_str());
		// Path may contain spaces (real .dsk filenames in this project's
		// own test collection do) -- rejoin everything after the drive
		// index verbatim instead of using toks[2] alone.
		std::string path;
		for (size_t i = 2; i < toks.size(); i++) {
			if (i > 2) path += " ";
			path += toks[i];
		}
		bool ok = g_station.insertDisk(drive, path);
		return ok ? "OK" : "ERR failed to load disk";
	}

	if (cmd == "NEWDISK" && toks.size() >= 4) {
		// "Insert New" (2026-11-14) -- toks: NEWDISK <drive> <tracks>
		// <sides>. No path -- the station generates its own throwaway
		// temp path (see FddStation::insertNewDisk()'s own comment) and
		// reports it back in the reply so the host can show/remember it.
		int drive = atoi(toks[1].c_str());
		int tracks = atoi(toks[2].c_str());
		int sides = atoi(toks[3].c_str());
		bool ok = g_station.insertNewDisk(drive, tracks, sides);
		if (!ok) return "ERR failed to create disk";
		return "OK " + g_station.diskPath(drive);
	}

	if (cmd == "SAVEDISK" && toks.size() >= 3) {
		// "Save As" -- toks: SAVEDISK <drive> <path>. Path may contain
		// spaces, same rejoin convention as CHANGEDISK.
		int drive = atoi(toks[1].c_str());
		std::string path;
		for (size_t i = 2; i < toks.size(); i++) {
			if (i > 2) path += " ";
			path += toks[i];
		}
		bool ok = g_station.saveDiskAs(drive, path);
		return ok ? "OK" : "ERR failed to save disk";
	}

	if (cmd == "DISKTEMP" && toks.size() >= 2) {
		int drive = atoi(toks[1].c_str());
		snprintf(out, sizeof(out), "OK temporary=%d", g_station.isDiskTemporary(drive) ? 1 : 0);
		return out;
	}

	if (cmd == "LINKTRACE" && toks.size() >= 2) {
		g_station.setLinkTraceEnabled(toks[1] == "ON");
		snprintf(out, sizeof(out), "OK linktrace=%d", g_station.isLinkTraceEnabled() ? 1 : 0);
		return out;
	}

	if (cmd == "LIVEDISASM") {
		// POC (2026-11-11; moved to the station's own Z80 core 2026-11-13
		// -- see include/z80cpp/z80.h's own comment on why). Talks straight
		// to g_station.getCPU() (a plain Z80*) -- no station-specific
		// wrapper for any of this, just routing to the right Z80 instance.
		// OFF by default -- "LIVEDISASM ON"/"OFF" toggles it; "LIVEDISASM
		// CLEAR" resets the map; "LIVEDISASM <path>" dumps it to a file
		// (bigger than one debug-protocol reply would comfortably carry);
		// bare "LIVEDISASM" reports current state.
		Z80* z80 = g_station.getCPU();
		if (toks.size() >= 2 && toks[1] == "ON") {
			z80->setLiveDisasmEnabled(true);
			return "OK";
		}
		if (toks.size() >= 2 && toks[1] == "OFF") {
			z80->setLiveDisasmEnabled(false);
			return "OK";
		}
		if (toks.size() >= 2 && toks[1] == "CLEAR") {
			z80->liveDisasmClear();
			return "OK cleared";
		}
		if (toks.size() < 2) {
			snprintf(out, sizeof(out), "OK livedisasm=%d size=%zu",
			         z80->isLiveDisasmEnabled() ? 1 : 0, z80->liveDisasmSize());
			return out;
		}
		FILE* pf = fopen(toks[1].c_str(), "w");
		if (!pf) return "ERR cannot open output path";
		std::string dump = z80->liveDisasmDump();
		fwrite(dump.data(), 1, dump.size(), pf);
		fclose(pf);
		snprintf(out, sizeof(out), "OK wrote %zu addresses to %s", z80->liveDisasmSize(), toks[1].c_str());
		return out;
	}

	if (cmd == "WDTRACE" && toks.size() >= 2) {
		g_station.fdc().setTraceEnabled(toks[1] == "ON");
		snprintf(out, sizeof(out), "OK wdtrace=%d", g_station.fdc().isTraceEnabled() ? 1 : 0);
		return out;
	}

	return "ERR unknown command";
}

static void pumpDebugSocket()
{
	g_debugListener.acceptIfIdle("debug");
	if (g_debugListener.clientFd < 0) return;
	char buf[1024];
	ssize_t n = recv(g_debugListener.clientFd, buf, sizeof(buf), 0);
	if (n == 0) { g_debugListener.dropClient("debug"); return; }
	if (n < 0) return; // EAGAIN, nothing pending
	g_debugListener.rxBuf.append(buf, (size_t)n);

	size_t pos;
	while ((pos = g_debugListener.rxBuf.find('\n')) != std::string::npos) {
		std::string line = g_debugListener.rxBuf.substr(0, pos);
		g_debugListener.rxBuf.erase(0, pos + 1);
		if (!line.empty() && line.back() == '\r') line.pop_back();
		std::string reply = handleDebugCommand(line) + "\n";
		send(g_debugListener.clientFd, reply.data(), reply.size(), 0);
	}
}

static void printUsage(const char* argv0)
{
	printf(
		"fdd3000_station -- standalone FDD3000 station process.\n"
		"\n"
		"Runs the station computer (Z80A + 64K RAM + WD1772 FDC, see\n"
		"src/FddStation.h) as its own, real-time-paced OS process, reached\n"
		"from JASE over two separate TCP channels -- connect to it with\n"
		"jase's own --fdd3000-bridge HOST:LINKPORT[:DEBUGPORT].\n"
		"\n"
		"Usage: %s [options]\n"
		"\n"
		"Options:\n"
		"  --link-port PORT       TCP port for the real link-byte stream --\n"
		"                         one latched byte each direction, no\n"
		"                         framing (default 9600).\n"
		"  --debug-port PORT      TCP port for the line-based command\n"
		"                         channel, see \"Command channel\" below\n"
		"                         (default 9601).\n"
		"  --boot-rom PATH        the station's own real internal boot ROM\n"
		"                         (exactly 2048 bytes) -- without it the\n"
		"                         station CPU just idles on a placeholder.\n"
		"  --disk-a/b/c/d PATH    a real .dsk image for that station drive\n"
		"                         (CPCEMU/Extended DSK) at startup -- can\n"
		"                         also be changed later at runtime, see\n"
		"                         CHANGEDISK below.\n"
		"  --linktrace            log every station-side IN/OUT on the\n"
		"                         link byte to stderr from startup (same\n"
		"                         shape as \"LINKTRACE ON\" below, and as\n"
		"                         jase's own --debug-fdd3000-linktrace on\n"
		"                         the host side).\n"
		"  --wdtrace              log every WD1772 command/outcome to\n"
		"                         stderr from startup (same shape as\n"
		"                         \"WDTRACE ON\" below, and as jase's own\n"
		"                         --debug-fdd3000-wdtrace).\n"
		"  --serial1-port PORT    TCP port for WD2123 CH1 (src/Wd2123.h) --\n"
		"                         byte-level test passthrough, no framing.\n"
		"                         Not bound unless given.\n"
		"  --serial2-port PORT    same, for WD2123 CH2. Not bound unless\n"
		"                         given.\n"
		"  --monitor-port PORT    src/MonitorLink.h as a local TCP listener --\n"
		"                         a native reimplementation of the real\n"
		"                         Monitor Emulator's host-link protocol, so\n"
		"                         a telnet client can act as CP/M's console\n"
		"                         with no ZX Spectrum Z80 code or screen\n"
		"                         involved. Not bound unless given. See\n"
		"                         docs/FDD3000/HEADLESS_CPM.md.\n"
		"  --max-speed            skip the real-time 8MHz pacing sleep and\n"
		"                         run the station as fast as the OS\n"
		"                         scheduler allows -- diagnostic flag, to\n"
		"                         isolate whether bridge-mode handshake\n"
		"                         failures come from the two processes'\n"
		"                         independent real-time clocks drifting,\n"
		"                         as opposed to the socket transport\n"
		"                         itself.\n"
		"  --help, -h             this information.\n"
		"\n"
		"Command channel (one line in, one line out, same shape as jase's\n"
		"own --debug-port protocol):\n"
		"  STATUS                 bootRomVisible/stationPC/WD1772 summary.\n"
		"  REGS                   station Z80 registers.\n"
		"  SETREG <reg> <hex>     set one station register (AF/BC/.../PC).\n"
		"  MEM <hexaddr> <len>    read station memory as hex.\n"
		"  POKE <hexaddr> <b1,b2,...>  write station memory (hex bytes).\n"
		"  STEP [n]               single-step the station CPU (default 1).\n"
		"  RESET                  the station's own physical reset button --\n"
		"                         resets CPU/paging/FDC, NOT the link latches.\n"
		"  CHANGEDISK <drive> <path>  swap which disk image is in a drive\n"
		"                         (0=A..3=D) at runtime -- the real\n"
		"                         workflow is to do this THEN RESET, since\n"
		"                         the boot ROM only reads drive A at\n"
		"                         reset time.\n"
		"  NEWDISK <drive> <tracks> <sides>  \"Insert New\" -- creates a\n"
		"                         blank, already-formatted disk at a\n"
		"                         throwaway temp path (tracks=40/sides=1\n"
		"                         for 170KB, tracks=40/sides=2 for 340KB,\n"
		"                         tracks=80/sides=2 for 620KB -- see\n"
		"                         DskImage::createBlank()'s own comment)\n"
		"                         and inserts it; replies \"OK <temp-path>\".\n"
		"  SAVEDISK <drive> <path>  \"Save As\" -- re-points the drive's\n"
		"                         disk at a real path and writes it there.\n"
		"  DISKTEMP <drive>       reports whether the drive's disk is\n"
		"                         still at its throwaway NEWDISK temp path\n"
		"                         (never explicitly SAVEDISK'd).\n"
		"  LINKTRACE ON|OFF       toggle the link-byte trace live.\n"
		"  WDTRACE ON|OFF         toggle the WD1772 command/status trace.\n"
		"  LIVEDISASM ON|OFF      POC: toggle the station's own live/JIT\n"
		"                         disassembler (include/z80cpp/LiveDisasm.h/\n"
		"                         z80.h -- a Z80-core-level feature). OFF by\n"
		"                         default. LIVEDISASM <path> dumps the\n"
		"                         current PC-keyed map to a file; LIVEDISASM\n"
		"                         CLEAR resets it.\n"
		"  DUMPBIN <path> <hexaddr> <len>  POC: raw-bytes binary dump of\n"
		"                         station RAM/ROM (as actually seen through\n"
		"                         the current paging view) -- the only way\n"
		"                         to extract the program loaded from disk\n"
		"                         into station RAM as a real file, since\n"
		"                         (unlike the boot ROM) it never exists as\n"
		"                         one anywhere else.\n",
		argv0);
}

int main(int argc, char** argv)
{
	int linkPort = 9600;
	int debugPort = 9601;
	std::string bootRomPath;
	std::string diskPaths[FddStation::kNumDrives];

	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--help" || a == "-h") { printUsage(argv[0]); return 0; }
		else if (a == "--link-port" && i + 1 < argc) linkPort = atoi(argv[++i]);
		else if (a == "--debug-port" && i + 1 < argc) debugPort = atoi(argv[++i]);
		else if (a == "--boot-rom" && i + 1 < argc) bootRomPath = argv[++i];
		else if (a == "--disk-a" && i + 1 < argc) diskPaths[0] = argv[++i];
		else if (a == "--disk-b" && i + 1 < argc) diskPaths[1] = argv[++i];
		else if (a == "--disk-c" && i + 1 < argc) diskPaths[2] = argv[++i];
		else if (a == "--disk-d" && i + 1 < argc) diskPaths[3] = argv[++i];
		else if (a == "--linktrace") g_station.setLinkTraceEnabled(true);
		else if (a == "--wdtrace") g_station.fdc().setTraceEnabled(true);
		else if (a == "--max-speed") g_maxSpeed = true;
		else if (a == "--serial1-port" && i + 1 < argc) g_serial1Port = atoi(argv[++i]);
		else if (a == "--serial2-port" && i + 1 < argc) g_serial2Port = atoi(argv[++i]);
		else if (a == "--monitor-port" && i + 1 < argc) g_monitorPort = atoi(argv[++i]);
		else { fprintf(stderr, "unknown arg: %s (try --help)\n", a.c_str()); return 1; }
	}

	if (!bootRomPath.empty() && !g_station.loadBootRom(bootRomPath)) return 1;
	for (int i = 0; i < FddStation::kNumDrives; i++) {
		if (diskPaths[i].empty()) continue;
		if (!g_station.insertDisk(i, diskPaths[i])) {
			fprintf(stderr, "fdd3000_station: failed to load disk %s into drive %d\n", diskPaths[i].c_str(), i);
			return 1;
		}
		fprintf(stderr, "fdd3000_station: loaded disk %s into drive %d\n", diskPaths[i].c_str(), i);
	}

	if (!g_linkListener.init(linkPort)) { fprintf(stderr, "cannot listen on link port %d\n", linkPort); return 1; }
	if (!g_debugListener.init(debugPort)) { fprintf(stderr, "cannot listen on debug port %d\n", debugPort); return 1; }
	fprintf(stderr, "fdd3000_station: link port %d, debug port %d\n", linkPort, debugPort);

	if (g_serial1Port != 0) {
		if (!g_serial1Listener.init(g_serial1Port)) { fprintf(stderr, "cannot listen on serial1 port %d\n", g_serial1Port); return 1; }
		fprintf(stderr, "fdd3000_station: serial1 (WD2123 CH1) port %d\n", g_serial1Port);
	}
	if (g_serial2Port != 0) {
		if (!g_serial2Listener.init(g_serial2Port)) { fprintf(stderr, "cannot listen on serial2 port %d\n", g_serial2Port); return 1; }
		fprintf(stderr, "fdd3000_station: serial2 (WD2123 CH2) port %d\n", g_serial2Port);
	}
	if (g_monitorPort != 0) {
		if (!g_monitorListener.init(g_monitorPort)) { fprintf(stderr, "cannot listen on monitor port %d\n", g_monitorPort); return 1; }
		fprintf(stderr, "fdd3000_station: monitor (headless CP/M console) port %d\n", g_monitorPort);
	}

	// Real-time pacing to the station's own clock -- EXPERIMENTAL 8MHz
	// (2x the real 4MHz Z80A, M397/FDD35 schematics), matching the
	// in-process side's own kRatioNum/kRatioDen in Fdd3000.cpp's
	// stepStationForOneHostInstruction() (16:7 vs host 3.5MHz). The
	// wall-clock pacing CHECK itself is only done once per batch (keeps
	// clock_gettime() overhead down), but pumpLinkSocket()/
	// flushLinkByteIfDirty() run every single instruction, NOT batched --
	// batching that would silently collapse multiple queued host bytes
	// into "whatever's last", skipping a transition the station's own
	// polling loop needed to see.
	const double kStationHz = 8000000.0;
	const int kBatchInstructions = 64;
	auto t0 = std::chrono::steady_clock::now();
	long long virtualTstatesRun = 0;

	for (;;) {
		for (int i = 0; i < kBatchInstructions; i++) {
			virtualTstatesRun += g_station.step();
			// --monitor-port drives the SAME link-byte latch as the raw
			// link-port bridge above (flushLinkByteIfDirty()/
			// pumpLinkSocket()) -- both at once would fight over it, same
			// caveat as a real Monitor program running at the same time
			// as Fdd3000's own MonitorLink in-process. Gated mutually
			// exclusive here rather than just documented, since nothing
			// stops a remote --fdd3000-bridge host from also being
			// connected to g_linkListener at the same time otherwise.
			if (g_monitorPort != 0) {
				g_monitorLink.tick(g_station);
			} else {
				flushLinkByteIfDirty();
				pumpLinkSocket();
			}
		}
		pumpDebugSocket();
		if (g_serial1Port != 0) pumpSerialSocket(g_serial1Listener, 0, "serial1");
		if (g_serial2Port != 0) pumpSerialSocket(g_serial2Listener, 1, "serial2");
		if (g_monitorPort != 0) pumpMonitorSocket();

		if (g_maxSpeed) continue; // see --max-speed's own comment

		double targetElapsed = (double)virtualTstatesRun / kStationHz;
		auto now = std::chrono::steady_clock::now();
		double actualElapsed = std::chrono::duration<double>(now - t0).count();
		double sleepFor = targetElapsed - actualElapsed;
		if (sleepFor > 0.0) {
			if (sleepFor > 0.05) sleepFor = 0.05; // cap a single sleep so RESET/debug stay responsive
			std::this_thread::sleep_for(std::chrono::duration<double>(sleepFor));
		} else if (actualElapsed - targetElapsed > 0.25) {
			// Fell far behind real time (e.g. this process got paused/
			// descheduled) -- resync instead of ever trying to "catch up"
			// by running flat out, which would desync the two sides even
			// worse.
			t0 = now;
			virtualTstatesRun = 0;
		}
	}
}
