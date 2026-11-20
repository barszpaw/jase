#include "Fdd3000.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <sstream>
#include <vector>
#include <SDL2/SDL.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

Fdd3000::Fdd3000()
{
	memset(interfaceRom_.data, 0xFF, MemoryMap::PAGE_SIZE);
	interfaceRom_.isRom = true;
	memset(interfaceRam_.data, 0x00, MemoryMap::PAGE_SIZE);
	interfaceRam_.isRom = false;
}

Fdd3000::~Fdd3000()
{
	if (linkFd_ >= 0) close(linkFd_);
	if (debugFd_ >= 0) close(debugFd_);
	for (int ch = 0; ch < 2; ch++) {
		if (serialListener_[ch].clientFd >= 0) close(serialListener_[ch].clientFd);
		if (serialListener_[ch].listenFd >= 0) close(serialListener_[ch].listenFd);
	}
}

// Accepts three real file shapes, all confirmed against docs/FDD3000/M397
// (a modern, documented TTL clone of the interface -- real schematic +
// EPROM jumper table + a real 32768-byte dump, `Ti_of_TTL_128k.rom`):
//   - 8192 bytes: the real, FULL ROM content for the host's 8K ROMCS
//     window (page 0, 0x0000-0x1FFF) -- used as-is.
//   - 32768 bytes: a full 27C256 EPROM dump. The schematic's own
//     address-line jumper table ties both A13/A14 to a fixed 0 -- i.e.
//     no bank switching; only the chip's OWN first 8192 bytes are ever
//     hardware-addressable. That first 8K is what gets used here.
//   - 4096 bytes (e.g. fdd3stavi.rom/fdd3timex.rom): mirrored into both
//     halves of the 8K window -- a confirmed-approximate fallback, NOT
//     accurate (the real second 4K is NOT a mirror).
bool Fdd3000::loadInterfaceRom(const char* path)
{
	FILE* pf = fopen(path, "rb");
	if (pf == NULL) {
		fprintf(stderr, "Fdd3000::loadInterfaceRom: cannot open %s\n", path);
		return false;
	}
	static const size_t kHalfRomSize = 4096;
	static const size_t kFullRomSize = 8192;
	static const size_t kEpromDumpSize = 32768;
	std::vector<uint8_t> buf(kEpromDumpSize);
	size_t n = fread(buf.data(), 1, buf.size(), pf);
	fclose(pf);

	if (n == kFullRomSize || n == kEpromDumpSize) {
		memcpy(interfaceRom_.data, buf.data(), kFullRomSize);
	} else if (n == kHalfRomSize) {
		fprintf(stderr, "Fdd3000::loadInterfaceRom: %s is 4096 bytes -- mirroring into both "
		        "halves of the 8K window, but this is KNOWN WRONG for real hardware (the real "
		        "second 4K is NOT a mirror). Use an 8192 or 32768-byte dump if you have one.\n", path);
		memcpy(interfaceRom_.data, buf.data(), kHalfRomSize);
		memcpy(interfaceRom_.data + kHalfRomSize, buf.data(), kHalfRomSize);
	} else {
		fprintf(stderr, "Fdd3000::loadInterfaceRom: %s is %zu bytes, expected 4096, 8192, or 32768\n",
		        path, n);
		return false;
	}
	romPath_ = path;
	printf("Fdd3000::loadInterfaceRom: loaded interface ROM from %s (%zu bytes)\n", path, n);
	return true;
}

static int connectNonBlocking(const char* host, int port)
{
	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		close(fd);
		return -1;
	}
	// Blocking connect (fine for a local, fast bridge at startup, not
	// something to fix until a real latency problem shows up -- same
	// tradeoff IecBridgeClient's own comment documents).
	if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
		close(fd);
		return -1;
	}
	fcntl(fd, F_SETFL, O_NONBLOCK);
	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	return fd;
}

bool Fdd3000::connectBridge(const char* host, int linkPort, int debugPort)
{
	if (linkFd_ >= 0) { close(linkFd_); linkFd_ = -1; }
	if (debugFd_ >= 0) { close(debugFd_); debugFd_ = -1; }

	int lfd = connectNonBlocking(host, linkPort);
	if (lfd < 0) {
		fprintf(stderr, "Fdd3000::connectBridge: cannot connect link port %s:%d\n", host, linkPort);
		return false;
	}
	linkFd_ = lfd;
	bridgeHost_ = host;
	bridgeLinkPort_ = linkPort;
	bridgeDebugPort_ = (debugPort > 0) ? debugPort : (linkPort + 1);
	bridgeMode_ = true;
	// debugFd_ opened lazily on first debugRequest() -- it's blocking and
	// infrequent, no need to hold it open from startup.
	printf("Fdd3000::connectBridge: connected to station link port %s:%d (debug port %d)\n",
	       host, linkPort, bridgeDebugPort_);
	return true;
}

std::string Fdd3000::debugRequest(const std::string& line)
{
	if (!bridgeMode_) return std::string();

	if (debugFd_ < 0) {
		int fd = socket(AF_INET, SOCK_STREAM, 0);
		if (fd < 0) return std::string();
		sockaddr_in addr{};
		addr.sin_family = AF_INET;
		addr.sin_port = htons((uint16_t)bridgeDebugPort_);
		if (inet_pton(AF_INET, bridgeHost_.c_str(), &addr.sin_addr) != 1 ||
		    connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
			close(fd);
			return std::string();
		}
		int one = 1;
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		debugFd_ = fd;
		debugRxBuf_.clear();
	}

	std::string out = line + "\n";
	if (send(debugFd_, out.data(), out.size(), 0) < 0) {
		close(debugFd_);
		debugFd_ = -1;
		return std::string();
	}

	// Short-timeout blocking read -- debug commands are infrequent and
	// off the hot path (see class comment), unlike the link byte itself.
	struct timeval tv{};
	tv.tv_sec = 2;
	setsockopt(debugFd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	size_t pos;
	while ((pos = debugRxBuf_.find('\n')) == std::string::npos) {
		char buf[1024];
		ssize_t n = recv(debugFd_, buf, sizeof(buf), 0);
		if (n <= 0) { close(debugFd_); debugFd_ = -1; return std::string(); }
		debugRxBuf_.append(buf, (size_t)n);
	}
	std::string reply = debugRxBuf_.substr(0, pos);
	debugRxBuf_.erase(0, pos + 1);
	if (!reply.empty() && reply.back() == '\r') reply.pop_back();
	return reply;
}

bool Fdd3000::loadStationBootRom(const char* path)
{
	if (bridgeMode_) {
		fprintf(stderr, "Fdd3000::loadStationBootRom: not supported in bridge mode -- "
		        "pass --boot-rom to the fdd3000_station process itself instead.\n");
		return false;
	}
	bool ok = station_.loadBootRom(path);
	if (ok) stationRomPath_ = path;
	return ok;
}

bool Fdd3000::insertDisk(int driveIndex, const char* path)
{
	if (driveIndex < 0 || driveIndex >= FddStation::kNumDrives) return false;
	bool ok;
	if (bridgeMode_) {
		std::ostringstream cmd;
		cmd << "CHANGEDISK " << driveIndex << " " << path;
		ok = debugRequest(cmd.str()).rfind("OK", 0) == 0;
	} else {
		ok = station_.insertDisk(driveIndex, path);
	}
	if (ok) diskPath_[driveIndex] = path;
	return ok;
}

bool Fdd3000::insertNewDisk(int driveIndex, int numTracks, int numSides)
{
	if (driveIndex < 0 || driveIndex >= FddStation::kNumDrives) return false;
	bool ok;
	std::string resultPath;
	if (bridgeMode_) {
		std::ostringstream cmd;
		cmd << "NEWDISK " << driveIndex << " " << numTracks << " " << numSides;
		std::string reply = debugRequest(cmd.str());
		ok = reply.rfind("OK ", 0) == 0; // station replies "OK <temp-path>" so the host can show it
		if (ok) resultPath = reply.substr(3);
	} else {
		ok = station_.insertNewDisk(driveIndex, numTracks, numSides);
		if (ok) resultPath = station_.diskPath(driveIndex);
	}
	if (ok) diskPath_[driveIndex] = resultPath;
	return ok;
}

bool Fdd3000::saveDiskAs(int driveIndex, const char* newPath)
{
	if (driveIndex < 0 || driveIndex >= FddStation::kNumDrives) return false;
	bool ok;
	if (bridgeMode_) {
		std::ostringstream cmd;
		cmd << "SAVEDISK " << driveIndex << " " << newPath;
		ok = debugRequest(cmd.str()).rfind("OK", 0) == 0;
	} else {
		ok = station_.saveDiskAs(driveIndex, newPath);
	}
	if (ok) diskPath_[driveIndex] = newPath;
	return ok;
}

bool Fdd3000::isDiskTemporary(int driveIndex)
{
	if (driveIndex < 0 || driveIndex >= FddStation::kNumDrives) return false;
	if (bridgeMode_) {
		std::ostringstream cmd;
		cmd << "DISKTEMP " << driveIndex;
		std::string reply = debugRequest(cmd.str());
		return reply.find("temporary=1") != std::string::npos;
	}
	return station_.isDiskTemporary(driveIndex);
}

bool Fdd3000::hasAnyUnsavedDisk()
{
	for (int i = 0; i < FddStation::kNumDrives; i++)
		if (isDiskTemporary(i)) return true;
	return false;
}

void Fdd3000::stationReset()
{
	if (bridgeMode_) debugRequest("RESET");
	else station_.reset();
	stationTstateBudget7_ = 0; // don't carry a stale backlog across a reset
}

void Fdd3000::onReset(bool hard)
{
	// See Fdd3000.h's own comment on this override: a plain Z80 reset
	// (hard=false) leaves the interface's own 1K RAM alone, matching real
	// hardware; only a genuine power-cycle (hard=true) clears it back to
	// its true power-on content.
	if (hard) memset(interfaceRam_.data, 0x00, MemoryMap::PAGE_SIZE);
}

void Fdd3000::setWdTraceEnabled(bool on)
{
	if (bridgeMode_) debugRequest(on ? "WDTRACE ON" : "WDTRACE OFF");
	else station_.fdc().setTraceEnabled(on);
}

bool Fdd3000::isWdTraceEnabled() const
{
	return bridgeMode_ ? false /* bridge mode doesn't cache remote state locally */
	                    : station_.fdc().isTraceEnabled();
}

// Local TCP listener for one WD2123 channel -- same minimal shape as
// fdd3000_station/main.cpp's own Listener::init(), just as a plain member
// pair instead of a struct method (this class already has its own
// bridge-mode socket fields following that convention).
bool Fdd3000::setSerialPort(int channel, int port)
{
	if (channel != 0 && channel != 1) return false;
	SerialListener& l = serialListener_[channel];
	l.listenFd = socket(AF_INET, SOCK_STREAM, 0);
	if (l.listenFd < 0) return false;
	int opt = 1;
	setsockopt(l.listenFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons((uint16_t)port);
	if (bind(l.listenFd, (sockaddr*)&addr, sizeof(addr)) < 0) { close(l.listenFd); l.listenFd = -1; return false; }
	if (listen(l.listenFd, 1) < 0) { close(l.listenFd); l.listenFd = -1; return false; }
	fcntl(l.listenFd, F_SETFL, O_NONBLOCK);
	serialPort_[channel] = port;
	return true;
}

void Fdd3000::acceptSerialIfIdle(int channel)
{
	SerialListener& l = serialListener_[channel];
	if (l.clientFd >= 0 || l.listenFd < 0) return;
	int fd = accept(l.listenFd, nullptr, nullptr);
	if (fd >= 0) {
		fcntl(fd, F_SETFL, O_NONBLOCK);
		int one = 1;
		setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
		l.clientFd = fd;
		fprintf(stderr, "Fdd3000: serial%d client connected\n", channel + 1);
	}
}

void Fdd3000::dropSerialClient(int channel)
{
	SerialListener& l = serialListener_[channel];
	if (l.clientFd >= 0) {
		close(l.clientFd);
		l.clientFd = -1;
		fprintf(stderr, "Fdd3000: serial%d client disconnected\n", channel + 1);
	}
}

// Byte-level shortcut, same transport as fdd3000_station/main.cpp's own
// pumpSerialSocket() against the SAME FddStation::serial() API (Wd2123's
// per-channel FIFOs) -- this is just the in-process side of it. Bridge
// mode's station lives in a separate process reachable only through the
// existing debug command channel (RESET/REGS/...), which has no
// provision for raw byte streams; wiring CH1/CH2 through there would mean
// another socket hop end-to-end and isn't attempted here.
void Fdd3000::pumpSerial()
{
	if (bridgeMode_) return;
	for (int ch = 0; ch < 2; ch++) {
		if (serialPort_[ch] == 0) continue;
		acceptSerialIfIdle(ch);
		SerialListener& l = serialListener_[ch];
		if (l.clientFd < 0) continue;

		uint8_t buf[256];
		ssize_t n = recv(l.clientFd, buf, sizeof(buf), 0);
		if (n == 0) {
			dropSerialClient(ch);
		} else if (n > 0) {
			for (ssize_t i = 0; i < n; i++) station_.serial().pushRxByte(ch, buf[i]);
		}

		uint8_t out[256];
		size_t outLen = 0;
		uint8_t b;
		while (outLen < sizeof(out) && station_.serial().popTxByte(ch, b)) out[outLen++] = b;
		if (outLen > 0) {
			if (send(l.clientFd, out, outLen, 0) < 0) dropSerialClient(ch);
		}
	}
}

// See Fdd3000.h's setLinkTraceEnabled() comment for why this exists and
// what it logs. Timestamped the same way IECTRACE/IOTRACE are (seconds
// since SDL init via SDL_GetPerformanceCounter) so all three trace
// streams line up on one shared clock if ever captured together. Only the
// HOST side of the link is logged here -- the station's own side logs
// separately (FddStation's own trace, or the external process's stderr
// in bridge mode).
void Fdd3000::logLinkTrace(const char* direction, uint8_t value) const
{
	double ts = (double)SDL_GetPerformanceCounter() / (double)SDL_GetPerformanceFrequency();
	char bin[9];
	for (int b = 0; b < 8; b++) bin[b] = ((value >> (7 - b)) & 1) ? '1' : '0';
	bin[8] = '\0';
	fprintf(stderr, "FDD3000LINK ts=%.6f hostPC=%04x side=HOST dir=%s val=%02x bin=%s\n",
	        ts, lastHostPc_, direction, value, bin);
}

bool Fdd3000::isRecentlyActive() const
{
	return enabled_ && (SDL_GetTicks() - lastActivityMs_) < 150;
}

bool Fdd3000::in(uint16_t port, uint8_t& value)
{
	if (!enabled_) return false;
	if ((port & 0xFF) != 0xEF) return false;
	value = bridgeMode_ ? linkFromStation_ : station_.stationByte();
	if (linkTraceEnabled_) logLinkTrace("IN ", value);
	lastActivityMs_ = SDL_GetTicks(); // see isRecentlyActive()'s own comment
	return true;
}

bool Fdd3000::out(uint16_t port, uint8_t value)
{
	if (!enabled_) return false;
	if ((port & 0xFF) != 0xEF) return false;
	if (bridgeMode_) {
		linkToStation_ = value;
		uint8_t v = value;
		// SIGPIPE itself is ignored process-wide now (see main.cpp), so a
		// dead station just surfaces here as a normal -1/EPIPE return --
		// drop the fd so later calls don't keep hammering a closed socket.
		if (linkFd_ >= 0 && send(linkFd_, &v, 1, 0) < 0) {
			close(linkFd_);
			linkFd_ = -1;
		}
	} else {
		station_.setHostByte(value);
	}
	if (linkTraceEnabled_) logLinkTrace("OUT", value);
	lastActivityMs_ = SDL_GetTicks();
	return true;
}

void Fdd3000::applyHostPaging(MemoryMap& memory)
{
	if (active_) {
		memory.mapPage(0, &interfaceRom_);
		memory.mapPage(1, &interfaceRam_);
		memory.setRomcsAsserted(true);
	} else {
		memory.setRomcsAsserted(false);
	}
}

void Fdd3000::onInstructionFetch(uint16_t pc, MemoryMap& memory, uint32_t tstates)
{
	if (!enabled_) return;
	lastHostPc_ = pc; // stashed for logLinkTrace()'s own hostPC column

	// Host-side ROMCS automap -- IMMEDIATE/same-cycle, NOT deferred by one
	// instruction (unlike DivIde's trapAfter_/unmapAfter_). Confirmed from
	// the real, active Fuse code (z80/z80_ops.c's CHECK(fdd3000_early,...)
	// / CHECK(fdd3000_late,...)): both hooks run BEFORE the opcode at pc
	// is actually fetched, so the trap address's own opcode already comes
	// from the POST-trap mapping. Using DivIDE's deferred pattern here
	// was a real bug (host PC stuck looping the boot handshake forever
	// instead of completing it).
	if (!active_ && (pc == 0x0000 || pc == 0x0008)) {
		active_ = true;
	} else if (active_ && pc == 0x0604) {
		active_ = false;
	}
	applyHostPaging(memory);

	// Host-side bridge link read/write is non-blocking already, but
	// pulling incoming bytes off the TCP socket into linkFromStation_
	// needs to happen SOMEWHERE every instruction even when the host
	// isn't actively doing IN -- otherwise the kernel's own receive
	// buffer silently queues up stale bytes.
	if (bridgeMode_ && linkFd_ >= 0) {
		uint8_t b;
		if (recv(linkFd_, &b, 1, 0) == 1) linkFromStation_ = b;
	}
}

// See Peripheral.h's own comment on why this exists: real Fuse steps its
// station CPU (fddCpuStep()) immediately AFTER the host opcode executes,
// not before/at fetch time. In-process mode only -- bridge mode's station
// paces itself in its own process.
void Fdd3000::onInstructionExecuted(MemoryMap& memory, uint32_t tstates)
{
	(void)memory;
	if (!enabled_ || bridgeMode_) return;
	// tstates is the frame-cumulative counter, not this instruction's own
	// cost -- same wrap-safe delta Dandanator::onInstructionFetch() uses.
	uint32_t delta = (tstates >= lastRawTstates_) ? (tstates - lastRawTstates_) : tstates;
	lastRawTstates_ = tstates;
	stepStationForOneHostInstruction(delta);
}

void Fdd3000::stepStationForOneHostInstruction(uint32_t hostTstates)
{
	// Real clock-domain crossing: accumulate the host's own real T-states
	// into a station-T-state budget, then step the station for real
	// instructions (each consuming whatever T-states it actually takes,
	// via FddStation::step()'s own return value, NOT a guess) until that
	// budget is spent -- Bresenham-style carry-over of any remainder into
	// the next call. kRatioNum/kRatioDen is station-T-states-per-host-
	// T-state.
	//
	// EXPERIMENT (2026-11-10, user-directed): station at 8MHz (2x its
	// real 4MHz) instead of the real 8:7 (station 4MHz / host 3.5MHz,
	// M397/FDD35 schematics). Matches an earlier, separately-run 12MHz
	// (3x) experiment that also still worked -- continuing to probe
	// whether the PROTOCOL itself is clock-ratio-insensitive. 8MHz :
	// 3.5MHz host = 16:7.
	static constexpr int64_t kRatioNum = 16; // station T-states
	static constexpr int64_t kRatioDen = 7; // per host T-state
	stationTstateBudget7_ += (int64_t)hostTstates * kRatioNum;
	while (stationTstateBudget7_ > 0) {
		uint32_t spent = station_.step();
		stationTstateBudget7_ -= (int64_t)spent * kRatioDen;
	}
}

// Sub-commands below give the SAME station-side access the top-level
// debug protocol already gives the host (MEM/POKE/REGS/SETREG/STEP) --
// needed because the station is a second, entirely private Z80 + RAM
// that MinZX/DebugServer never sees directly (see Fdd3000.h's class
// comment). In bridge mode, everything after "FDD3000 " is forwarded
// VERBATIM to the external station's own debug port, which already
// speaks this exact same sub-command vocabulary at its own top level
// (RESET, CHANGEDISK, REGS, SETREG, MEM, POKE, STEP, LINKTRACE, WDTRACE).
std::string Fdd3000::debugCommand(const std::vector<std::string>& toks)
{
	if (toks.empty() || toks[0] != "FDD3000")
		return std::string();

	if (bridgeMode_) {
		if (toks.size() == 1) {
			std::string status = debugRequest("STATUS");
			return status.empty() ? "ERR bridge not responding" : status;
		}
		std::string line;
		for (size_t i = 1; i < toks.size(); i++) {
			if (i > 1) line += " ";
			line += toks[i];
		}
		std::string reply = debugRequest(line);
		return reply.empty() ? "ERR bridge not responding" : reply;
	}

	char out[256];

	if (toks.size() == 1) {
		snprintf(out, sizeof(out),
		         "OK enabled=%d active=%d linkToStation=%02x linkFromStation=%02x stationPaged=%d stationPC=%04x "
		         "wdStatus=%02x wdTrack=%02x wdSector=%02x wdDrq=%d wdIntrq=%d",
		         enabled_ ? 1 : 0, active_ ? 1 : 0, 0, station_.stationByte(),
		         station_.bootRomVisible() ? 1 : 0, station_.getRegPC(),
		         station_.fdc().peekStatus(), station_.fdc().readTrackReg(), station_.fdc().readSectorReg(),
		         station_.fdc().drq() ? 1 : 0, station_.fdc().intrq() ? 1 : 0);
		return out;
	}

	std::string sub = toks[1];
	for (char& c : sub) c = (char)toupper((unsigned char)c);

	if (sub == "REGS") {
		snprintf(out, sizeof(out),
		         "OK AF=%04x BC=%04x DE=%04x HL=%04x AFx=%04x BCx=%04x DEx=%04x HLx=%04x "
		         "IX=%04x IY=%04x SP=%04x PC=%04x I=%02x R=%02x IFF1=%d IFF2=%d IM=%d",
		         station_.getRegAF(), station_.getRegBC(), station_.getRegDE(), station_.getRegHL(),
		         station_.getRegAFx(), station_.getRegBCx(), station_.getRegDEx(), station_.getRegHLx(),
		         station_.getRegIX(), station_.getRegIY(), station_.getRegSP(), station_.getRegPC(),
		         station_.getRegI(), station_.getRegR(),
		         station_.getIFF1() ? 1 : 0, station_.getIFF2() ? 1 : 0, station_.getIM());
		return out;
	}

	if (sub == "SETREG" && toks.size() >= 4) {
		uint16_t val = (uint16_t)strtoul(toks[3].c_str(), nullptr, 16);
		std::string reg = toks[2];
		for (char& c : reg) c = (char)toupper((unsigned char)c);
		bool ok = true;
		if (reg == "AF") station_.setRegAF(val);
		else if (reg == "BC") station_.setRegBC(val);
		else if (reg == "DE") station_.setRegDE(val);
		else if (reg == "HL") station_.setRegHL(val);
		else if (reg == "AFX") station_.setRegAFx(val);
		else if (reg == "BCX") station_.setRegBCx(val);
		else if (reg == "DEX") station_.setRegDEx(val);
		else if (reg == "HLX") station_.setRegHLx(val);
		else if (reg == "IX") station_.setRegIX(val);
		else if (reg == "IY") station_.setRegIY(val);
		else if (reg == "SP") station_.setRegSP(val);
		else if (reg == "PC") station_.setRegPC(val);
		else ok = false;
		return ok ? "OK" : "ERR unknown register";
	}

	if (sub == "MEM" && toks.size() >= 4) {
		uint16_t addr = (uint16_t)strtoul(toks[2].c_str(), nullptr, 16);
		long len = strtol(toks[3].c_str(), nullptr, 10);
		if (len < 0) len = 0;
		if (len > 4096) len = 4096;
		std::string resp = "OK ";
		char hex[3];
		for (long i = 0; i < len; i++) {
			snprintf(hex, sizeof(hex), "%02x", station_.peek((uint16_t)(addr + i)));
			resp += hex;
		}
		return resp;
	}

	if (sub == "DUMPBIN" && toks.size() >= 5) {
		// In-process mirror of fdd3000_station's own DUMPBIN -- see its
		// comment. toks: FDD3000 DUMPBIN <path> <hexaddr> <len>.
		uint16_t addr = (uint16_t)strtoul(toks[3].c_str(), nullptr, 16);
		long len = strtol(toks[4].c_str(), nullptr, 10);
		if (len < 0) len = 0;
		if (len > 65536) len = 65536;
		FILE* pf = fopen(toks[2].c_str(), "wb");
		if (!pf) return "ERR cannot open output path";
		for (long i = 0; i < len; i++) {
			uint8_t b = station_.peek((uint16_t)(addr + i));
			fwrite(&b, 1, 1, pf);
		}
		fclose(pf);
		snprintf(out, sizeof(out), "OK wrote %ld bytes to %s", len, toks[2].c_str());
		return out;
	}

	if (sub == "POKE" && toks.size() >= 4) {
		uint16_t addr = (uint16_t)strtoul(toks[2].c_str(), nullptr, 16);
		std::istringstream bs(toks[3]);
		std::string b;
		int count = 0;
		while (std::getline(bs, b, ',')) {
			station_.poke((uint16_t)(addr + count), (uint8_t)strtoul(b.c_str(), nullptr, 16));
			count++;
		}
		return "OK";
	}

	if (sub == "STEP") {
		// Steps the station CPU directly, independent of the host's own
		// instruction stream -- useful for driving/inspecting the
		// station in isolation without needing the host Z80 to also be
		// running.
		int n = toks.size() >= 3 ? atoi(toks[2].c_str()) : 1;
		if (n < 1) n = 1;
		for (int i = 0; i < n; i++) station_.step();
		return "OK";
	}

	if (sub == "RESET") {
		stationReset();
		return "OK";
	}

	if (sub == "LINKTRACE") {
		if (toks.size() >= 3) {
			if (toks[2] == "ON") setLinkTraceEnabled(true);
			else if (toks[2] == "OFF") setLinkTraceEnabled(false);
		}
		snprintf(out, sizeof(out), "OK linktrace=%d", isLinkTraceEnabled() ? 1 : 0);
		return out;
	}

	if (sub == "WDTRACE") {
		if (toks.size() >= 3) {
			if (toks[2] == "ON") setWdTraceEnabled(true);
			else if (toks[2] == "OFF") setWdTraceEnabled(false);
		}
		snprintf(out, sizeof(out), "OK wdtrace=%d", isWdTraceEnabled() ? 1 : 0);
		return out;
	}

	if (sub == "LIVEDISASM") {
		// In-process mirror of fdd3000_station's own LIVEDISASM -- see its
		// comment. Talks straight to station_.getCPU() (a plain Z80*), the
		// same way debugserver.cpp's own top-level LIVEDISASM talks to
		// minZX.getCPU() -- no FDD3000-specific wrapper for any of this,
		// just routing to the right Z80 instance. Off by default, must be
		// turned ON explicitly.
		Z80* z80 = station_.getCPU();
		if (toks.size() >= 3 && toks[2] == "ON") {
			z80->setLiveDisasmEnabled(true);
			return "OK";
		}
		if (toks.size() >= 3 && toks[2] == "OFF") {
			z80->setLiveDisasmEnabled(false);
			return "OK";
		}
		if (toks.size() >= 3 && toks[2] == "CLEAR") {
			z80->liveDisasmClear();
			return "OK cleared";
		}
		if (toks.size() < 3) {
			snprintf(out, sizeof(out), "OK livedisasm=%d size=%zu",
			         z80->isLiveDisasmEnabled() ? 1 : 0, z80->liveDisasmSize());
			return out;
		}
		FILE* pf = fopen(toks[2].c_str(), "w");
		if (!pf) return "ERR cannot open output path";
		std::string dump = z80->liveDisasmDump();
		fwrite(dump.data(), 1, dump.size(), pf);
		fclose(pf);
		snprintf(out, sizeof(out), "OK wrote %zu addresses to %s", z80->liveDisasmSize(), toks[2].c_str());
		return out;
	}

	return "ERR unknown FDD3000 sub-command";
}
