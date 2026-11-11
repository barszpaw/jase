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
//     <path>, REGS, SETREG <reg> <hex>, MEM <addr> <len>, POKE <addr>
//     <b1,b2,...>, STEP [n], LINKTRACE ON/OFF, WDTRACE ON/OFF, STATUS.
//     Same line-based shape as JASE's own --debug-port protocol.

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

static FddStation g_station;

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
	// FddStation doesn't track a "dirty" flag (it's a plain latch, same
	// as the in-process side just reads it on demand) -- this process
	// instead pushes the CURRENT latch value on a timer-free, best-effort
	// basis: every time the station writes to the link port, the new
	// value becomes visible to stationByte() immediately, so sending it
	// every batch (see the step loop below) is simply harmless redundant
	// traffic when it hasn't changed, not a correctness issue -- TCP is
	// a byte stream, not datagrams, so a human-imperceptible rate of
	// duplicate bytes would be wrong. Instead: track the last value SENT
	// here and only send on change.
	static uint8_t lastSent = 0;
	static bool first = true;
	uint8_t v = g_station.stationByte();
	if (first || v != lastSent) {
		first = false;
		lastSent = v;
		if (g_linkListener.clientFd >= 0) {
			if (send(g_linkListener.clientFd, &v, 1, 0) < 0) g_linkListener.dropClient("link");
		}
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

	if (cmd == "LINKTRACE" && toks.size() >= 2) {
		g_station.setLinkTraceEnabled(toks[1] == "ON");
		snprintf(out, sizeof(out), "OK linktrace=%d", g_station.isLinkTraceEnabled() ? 1 : 0);
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

int main(int argc, char** argv)
{
	int linkPort = 9600;
	int debugPort = 9601;
	std::string bootRomPath;
	std::string diskPaths[FddStation::kNumDrives];

	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--link-port" && i + 1 < argc) linkPort = atoi(argv[++i]);
		else if (a == "--debug-port" && i + 1 < argc) debugPort = atoi(argv[++i]);
		else if (a == "--boot-rom" && i + 1 < argc) bootRomPath = argv[++i];
		else if (a == "--disk-a" && i + 1 < argc) diskPaths[0] = argv[++i];
		else if (a == "--disk-b" && i + 1 < argc) diskPaths[1] = argv[++i];
		else if (a == "--disk-c" && i + 1 < argc) diskPaths[2] = argv[++i];
		else if (a == "--disk-d" && i + 1 < argc) diskPaths[3] = argv[++i];
		else if (a == "--linktrace") g_station.setLinkTraceEnabled(true);
		else { fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 1; }
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
			flushLinkByteIfDirty();
			pumpLinkSocket();
		}
		pumpDebugSocket();

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
