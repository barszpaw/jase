#include "SDI.h"
#include <cstring>
#include <cstdlib>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>

SDI::SDI()
{
	memset(window_.data, 0x00, MemoryMap::PAGE_SIZE);
	window_.isRom = true;
}

SDI::~SDI()
{
	if (bridgeFd_ >= 0)
		close(bridgeFd_);
}

bool SDI::loadRom(const char* path)
{
	FILE* pf = fopen(path, "rb");
	if (pf == NULL) {
		fprintf(stderr, "SDI::loadRom: cannot open %s\n", path);
		return false;
	}
	uint8_t buf[sizeof(rom_)];
	size_t n = fread(buf, 1, sizeof(buf), pf);
	fclose(pf);
	if (n != sizeof(buf)) {
		fprintf(stderr, "SDI::loadRom: %s is %zu bytes, expected exactly %zu\n",
		        path, n, sizeof(buf));
		return false;
	}
	memcpy(rom_, buf, sizeof(buf));
	romLoaded_ = true;
	printf("SDI::loadRom: loaded SDI103J ROM from %s\n", path);
	return true;
}

// Snapshots the current page-1 bank's lower 4K ($2000-$2FFF) instead of
// overwriting it -- see SDI.h's class comment for why that's always safe
// (always the other half of a read-only ROM pair, on every machine type).
void SDI::pageIn(MemoryMap& memory)
{
	if (active_) return;
	active_ = true;
	MemoryMap::Bank* currentPage1 = memory.getPage(1);
	memcpy(window_.data, currentPage1->data, MemoryMap::PAGE_SIZE / 2);
	memcpy(window_.data + MemoryMap::PAGE_SIZE / 2, rom_, sizeof(rom_));
	memory.mapPage(1, &window_);
	memory.setRomcsAsserted(true);
}

// Deliberately doesn't restore page 1 itself -- same reasoning as
// Multiface/DivIde: what belongs there once ROMCS releases is a
// MACHINE-level question only MinZX can answer (see
// MinZX::reapplyMachinePaging(), triggered off ROMCS's falling edge).
void SDI::pageOut(MemoryMap& memory)
{
	if (!active_) return;
	active_ = false;
	memory.setRomcsAsserted(false);
}

void SDI::onInstructionFetch(uint16_t pc, MemoryMap& memory)
{
	lastPC_ = pc; // for IOTRACE logging in in()/out(), regardless of enabled_/romLoaded_
	if (!enabled_ || !romLoaded_) return;

	// Initial trigger is the NARROWER $3C00-$3FFF (not the full 4K ROM
	// window) -- see class comment for the evidence. Once latched,
	// pageOut() still releases on the full $3000-$3FFF window, since the
	// EPROM physically spans all 4K and SDI's own code genuinely calls
	// into $3000-$39F0 ("library" routines) while paged in.
	if (!active_ && pc >= 0x3C00 && pc <= 0x3FFF) {
		pageIn(memory);
	} else if (active_ && (pc < 0x3000 || pc > 0x3FFF)) {
		pageOut(memory);
	}

	// EOI approximation tick -- see class comment. Called every
	// instruction regardless of ROMCS state (same hook every peripheral
	// gets), so this keeps counting even while some other code is
	// running, which is exactly what's needed: the delay that signals
	// EOI happens while the ROM is busy elsewhere in RAM/ROM, not
	// necessarily while SDI's own ROM is paged in.
	if (atnLine() || iecRole_ != IecRole::None)
		idleInstructions_++;
}

bool SDI::in(uint16_t port, uint8_t& value)
{
	if (!enabled_) return false;
	if ((port & 0xFF & 0x9F) != 0x9F) return false;

	switch ((port >> 5) & 3) {
		case 0:
			value = portA_;
			if (iotraceEnabled_)
				fprintf(stderr, "IOTRACE pc=%04x IN  PA -> %02x\n", lastPC_, value);
			break;
		case 1:
			value = portB_;
			if (iotraceEnabled_)
				fprintf(stderr, "IOTRACE pc=%04x IN  PB -> %02x\n", lastPC_, value);
			break;
		case 2: {
			// Bits 0-3: the real output latch (ATN/PC1/CLK/DAT as last
			// driven) -- read back as the LATCH value, matching real
			// 8255 output-pin behaviour (you get back what you wrote,
			// not a re-sense of the pin). Bits 4-5: unused for IEC, no
			// pull-down anywhere on the schematic -- default released.
			// Bits 6-7: PC6/PC7 are wired DIRECTLY to the bus node (not
			// through their own inverter, unlike PC0/2/3's output path
			// via U1A/B/C) -- so unlike the output bits, these need an
			// explicit polarity flip here: "asserted" (pulled low,
			// protocol-logical true) must read back as electrical LOW,
			// i.e. bit CLEAR, the opposite of clkIn/datIn's own
			// internal true=asserted convention. Caught and fixed while
			// building the LOAD encoder below -- never exercised by a
			// real IN instruction before that (everything verified so
			// far only drove bits 0-3 via the debug protocol's own OUT,
			// never read 6-7 back through a real IN), so this wasn't a
			// regression in anything already confirmed working.
			bool clkIn, datIn;
			if (atnLine() && bridgeFd_ >= 0) {
				// Real protocol: every device on the bus must respond to
				// ATN by pulling DATA within 1000us ("device not present"
				// otherwise), THEN release it ("ready for data") before
				// the controller starts clocking the very first byte --
				// the exact same assert-then-release-per-byte cycle
				// listenerDat_ already implements for the WeAreTalker
				// direction below, and listenerDat_ defaults to asserted
				// (the presence response) for free. Reusing it here
				// (rather than a separate hardcoded "always asserted")
				// is what out()'s ATN branch's own edge tracking, added
				// alongside this, keeps correctly updated -- a version of
				// this that stayed asserted for ALL of ATN (tried first)
				// fixed DEVICE NOT PRESENT but then hung waiting for the
				// "ready for data" release every subsequent byte needs,
				// caught by an actual on-screen run, not a synthetic
				// test. No bridge: fall through to self-loopback, i.e.
				// honestly "not present" -- nothing backs the device.
				clkIn = clkLine();
				datIn = listenerDat_;
			} else if (!atnLine() && iecRole_ == IecRole::WeAreListener) {
				// The device (our own LOAD encoder, see advanceLoadEncoder())
				// is the sender in this role -- it drives CLK throughout,
				// and DATA except during the Z80's own ready/ack handshake
				// windows (WaitDataReady/WaitAck below), when the Z80's
				// own driven bit takes over instead -- same shared,
				// time-multiplexed line real hardware has, see class comment.
				//
				// The real protocol (verified against the primary-source timing
				// diagram and an independent writeup) inserts an EXTRA CLK
				// release between every two bytes -- "talker ready to send" --
				// distinct from the 8 per-bit valid releases; electrically
				// it's the exact same kind of asserted->released transition,
				// so it can't be told apart from a real bit sample by edge
				// direction alone. Decide off the encoder's OWN state instead
				// (it knows authoritatively which release is which): only the
				// BitValid state's release is a genuine data bit -- this also
				// fixes a stale-exposure bug at the BitValid->WaitAck edge
				// (the last bit of a byte), where the old post-call state
				// check fell through to datLine() instead of showing the
				// just-sampled bit, same real-hardware-visible bug, not just
				// a self-decode cosmetic issue.
				LoadEncState stateBefore = loadEncState_;
				advanceLoadEncoder();
				clkIn = deviceClk_;
				datIn = (stateBefore == LoadEncState::BitSetup ||
				         stateBefore == LoadEncState::BitValid)
				        ? deviceDat_ : datLine();
				// Self-decode what we just drove, same clockBit() used
				// everywhere else -- not needed for the Z80 to receive
				// correctly (it samples the exposed bits the same
				// state-gated way), but lets SDI's own debug protocol state
				// (lastByte_, dataBuf_ via onDataByte()) independently
				// confirm the encoder reproduced the right bytes.
				if (stateBefore == LoadEncState::BitValid)
					clockBit(true, datIn, false);
			} else if (!atnLine() && iecRole_ == IecRole::WeAreTalker) {
				// Mirror image of the WeAreListener branch above: the Z80
				// is the sender here, driving CLK throughout AND DATA
				// during the 8 bit-sample edges -- so CLK-in just echoes
				// its own driven line. DATA-in is only ours to synthesize
				// during our own ack/not-ready window
				// (awaitingInterByteRelease_, driven from out()'s
				// WeAreTalker branch below); any other time, the Z80's own
				// driven bit is what a real bus would show, same as the
				// non-IEC-active else branch.
				clkIn = clkLine();
				datIn = awaitingInterByteRelease_ ? listenerDat_ : datLine();
			} else {
				clkIn = clkLine();
				datIn = datLine();
			}

			value = (uint8_t)((portC_ & 0x0F) | 0x30
			                   | (clkIn ? 0x00 : 0x40)
			                   | (datIn ? 0x00 : 0x80));
			if (iotraceEnabled_)
				fprintf(stderr, "IOTRACE pc=%04x IN  DF -> %02x  (atn=%d clkIn=%d datIn=%d role=%d)\n",
				        lastPC_, value, atnLine() ? 1 : 0, clkIn ? 1 : 0, datIn ? 1 : 0, (int)iecRole_);
			break;
		}
		case 3:
			value = control_;
			if (iotraceEnabled_)
				fprintf(stderr, "IOTRACE pc=%04x IN  CONF -> %02x\n", lastPC_, value);
			break;
	}
	return true;
}

bool SDI::out(uint16_t port, uint8_t value)
{
	if (!enabled_) return false;
	if ((port & 0xFF & 0x9F) != 0x9F) return false;

	switch ((port >> 5) & 3) {
		case 0:
			portA_ = value;
			if (iotraceEnabled_)
				fprintf(stderr, "IOTRACE pc=%04x OUT PA <- %02x\n", lastPC_, value);
			break;
		case 1:
			portB_ = value;
			if (iotraceEnabled_)
				fprintf(stderr, "IOTRACE pc=%04x OUT PB <- %02x\n", lastPC_, value);
			break;
		case 2: {
			portC_ = value;
			bool atn = atnLine();
			bool clk = clkLine();

			if (atn) {
				// Under ATN we're always the sender, full stop -- see
				// class comment. Sample on CLK's asserted->released
				// transition (!clk && lastClkOut_), matching the
				// verified fact precisely: CLK is pulled (asserted) for
				// the bit's setup/invalid window, then released, and
				// THAT release is when the bit is valid -- not the
				// other way around. (Caught and fixed while building
				// the LOAD encoder's self-decode path: an earlier
				// version of this triggered on the opposite transition,
				// released->asserted. It happened to still decode
				// correctly against this file's own OWN test harness
				// only because that harness held DATA constant across
				// both phases of each bit, which masked the direction
				// being backwards -- never actually exercised against
				// anything that varies DATA the way real hardware or
				// this encoder does.)
				//
				// Every addressed+unaddressed device must also respond
				// to ATN itself (see in()'s port-C case) and release for
				// "ready for data" before each byte, same as the
				// WeAreTalker branch below -- same edge/listenerDat_
				// tracking, since it's the identical per-byte handshake,
				// just still under ATN rather than past it. Caught by an
				// actual on-screen run: without this, in()'s presence
				// response stayed asserted through the whole of ATN and
				// the real ROM hung waiting for the release every byte
				// after the first needs.
				bool edge = !clk && lastClkOut_;
				bool wasAwaitingRelease = awaitingInterByteRelease_;
				bool completingByte = !wasAwaitingRelease && bitCount_ == 7;
				clockBit(edge, datLine(), true);
				if (edge) {
					if (wasAwaitingRelease)
						listenerDat_ = false; // ready for data -- this byte's bits can start
					else if (completingByte)
						listenerDat_ = true;  // ack -- byte received
				}
			} else if (iecRole_ == IecRole::WeAreTalker) {
				// Data phase, and the last LISTEN we sent means WE keep
				// sending -- exact same mechanism, just not gated on ATN
				// anymore. We also stand in for the addressed device's own
				// LISTENER role here (see listenerDat_'s class comment) --
				// capture whether this edge is the inter-byte "ready to
				// send" release BEFORE clockBit() consumes it, and whether
				// it's about to complete a byte, then react the way a real
				// listener would: release our ack ("ready for data") on the
				// first case, assert it (byte acknowledged) on the second --
				// the exact mirror of advanceLoadEncoder()'s own
				// WaitDataReady/WaitAck handling.
				bool edge = !clk && lastClkOut_;
				bool wasAwaitingRelease = awaitingInterByteRelease_;
				bool completingByte = !wasAwaitingRelease && bitCount_ == 7;
				clockBit(edge, datLine(), false);
				if (edge) {
					if (wasAwaitingRelease)
						listenerDat_ = false; // ready for data -- this byte's bits can start
					else if (completingByte)
						listenerDat_ = true;  // ack -- byte received
				}
			}
			// iecRole_ == WeAreListener's clocking happens in in()
			// instead, off the read-back CLK-in line -- we're not the
			// one driving CLK in that role, so there's nothing to do
			// with our OWN clkLine() here.

			if (lastAtnLine_ && !atn) {
				// ATN just released. iecRole_ was already set correctly
				// the moment the primary address byte finished clocking
				// (see clockBit() below) -- just make sure the shifter
				// starts the data phase clean. awaitingInterByteRelease_
				// resets true since the very first byte of a fresh
				// sequence gets the same inter-byte "ready to send" release
				// as every byte after it (see clockBit()); listenerDat_
				// resets to busy/not-ready here too, but it's the ATN
				// branch above's own first edge that releases it for the
				// post-ATN data phase's first byte -- same value, same
				// reason, now shared by both the ATN and WeAreTalker
				// in() reads.
				bitCount_ = 0;
				shiftByte_ = 0;
				awaitingInterByteRelease_ = true;
				listenerDat_ = true;
			} else if (!atn && iecRole_ == IecRole::None) {
				// Idle (no data phase active, e.g. after UNLISTEN/
				// UNTALK) -- keep the shifter clean so stray CLK/DAT
				// wiggling here never accumulates into a bogus byte.
				bitCount_ = 0;
				shiftByte_ = 0;
				awaitingInterByteRelease_ = true;
			}

			if (iotraceEnabled_)
				fprintf(stderr, "IOTRACE pc=%04x OUT DF <- %02x  (atn=%d clk=%d dat=%d role=%d)\n",
				        lastPC_, value, atn ? 1 : 0, clk ? 1 : 0, datLine() ? 1 : 0, (int)iecRole_);
			lastAtnLine_ = atn;
			lastClkOut_ = clk;
			break;
		}
		case 3:
			control_ = value;
			if (iotraceEnabled_)
				fprintf(stderr, "IOTRACE pc=%04x OUT CONF <- %02x\n", lastPC_, value);
			break;
	}
	return true;
}

// Rising-edge-of-CLK bit clocker -- see SDI.h's class comment for the
// verified protocol fact this implements ("RISING EDGE OF THE CLOCK means
// data bit is valid") and the two phases it serves. Bits arrive LSB-first;
// shifting new bits in at the top and shifting right each time lands the
// first-received bit at position 0 and the last at position 7, which is
// the correct final byte order.
void SDI::clockBit(bool clkRisingEdge, bool dataBit, bool fromAtnPhase)
{
	if (!clkRisingEdge) return;

	if (awaitingInterByteRelease_) {
		// The real protocol (verified against both primary sources --
		// see VC1541.md) inserts an extra CLK release between every two
		// bytes ("talker ready to send again"), electrically identical
		// to a per-bit sample release and impossible to tell apart by
		// edge direction alone -- the same ambiguity already fixed for
		// the LOAD/self-decode path in in(). This flag is always set
		// the instant the previous byte completed (or at a fresh
		// sequence/session start -- see out()'s two bitCount_ reset
		// branches), so this is always that extra release: consume it
		// here instead of counting it as bit 0 of the next byte.
		awaitingInterByteRelease_ = false;
		return;
	}

	if (bitCount_ == 0) {
		// First bit of a new byte -- this is the one point where the
		// EOI delay (measured BEFORE any bit of the byte clocks) is
		// decided. See class comment for why this is an approximation.
		currentByteIsEoi_ = (idleInstructions_ >= kIdleInstructionsForEoi);
	}
	idleInstructions_ = 0; // a bit just clocked -- restart the "waiting" measurement

	shiftByte_ >>= 1;
	if (dataBit) shiftByte_ |= 0x80;
	bitCount_++;

	if (bitCount_ == 8) {
		lastByte_ = shiftByte_;
		byteReady_ = true;
		lastByteFromAtn_ = fromAtnPhase;
		lastByteWasEoi_ = currentByteIsEoi_;
		bitCount_ = 0;
		shiftByte_ = 0;
		awaitingInterByteRelease_ = true; // next edge is the inter-byte release, not bit 0

		if (fromAtnPhase) {
			// A PRIMARY address decides who sends once ATN releases;
			// secondary/channel bytes (OPEN $60-$6F, CLOSE $E0-$EF,
			// plain-TALK-secondary $F0-$FF) deliberately leave the
			// current role alone -- see class comment.
			if (lastByte_ == 0x3F || lastByte_ == 0x5F) {
				finalizeSession();         // UNLISTEN / UNTALK ends this exchange
				iecRole_ = IecRole::None;
			} else if (lastByte_ >= 0x20 && lastByte_ <= 0x3E) {
				iecRole_ = IecRole::WeAreTalker;   // LISTEN: device listens, we send
			} else if (lastByte_ >= 0x40 && lastByte_ <= 0x5E) {
				iecRole_ = IecRole::WeAreListener; // TALK: device talks, we receive
			} else {
				onSecondaryByte(lastByte_); // OPEN/CLOSE/plain channel select
			}
		} else {
			onDataByte(lastByte_); // filename char (OPEN) or LOAD/SAVE payload byte
		}
	}
}

// Secondary/channel address decoded under ATN, right after a LISTEN or
// TALK primary -- see class comment for the three forms and what each
// means. Starts a fresh data buffer: the bus is strictly one session at
// a time, so whatever was accumulating before this point (if anything)
// is unrelated to the channel this secondary just selected.
void SDI::onSecondaryByte(uint8_t b)
{
	dataBuf_.clear();
	bool isClose = (b >= 0xE0 && b <= 0xEF);
	if (b >= 0x60 && b <= 0x6F) {
		secondaryCmd_ = SecondaryCmd::Open;
		activeChannel_ = b & 0x0F;
	} else if (isClose) {
		secondaryCmd_ = SecondaryCmd::Close;
		activeChannel_ = b & 0x0F;
	} else if (b >= 0xF0) {
		secondaryCmd_ = SecondaryCmd::PlainSelect;
		activeChannel_ = b & 0x0F;
	}
	// Live-tested finding (see VC1541.md): a TALK primary followed by a
	// CHANNEL-15 OPEN secondary ($6F, not the $F0-$FF this was
	// originally written for) is how the real ROM re-opens the command
	// channel to read a response back -- iecRole_ is the only thing
	// that actually determines who's about to drive CLK (the secondary
	// byte's own 0x60/0xE0/0xF0 shape just says Open/Close/PlainSelect,
	// nothing about direction), so the LOAD-encoder kickoff below has
	// to fire for ANY non-Close secondary while we're WeAreListener, not
	// just the 0xF0-0xFF form -- restricting it to PlainSelect only
	// left Open+WeAreListener with no bus-grab at all, hanging forever
	// in the real ROM's $3142/$3145 CLK-wait (confirmed live: PC stuck
	// at $3025, return address $336F on the stack, iecRole_ correctly
	// WeAreListener but loadEncState_ still sitting at its default Idle
	// since nothing had ever kicked it into TalkTurnaround).
	if (iecRole_ == IecRole::WeAreListener && !isClose) {
		loadChannel_ = activeChannel_;
		bridgeRequestRead(loadChannel_, 254);
		loadPos_ = 0;
		loadBitIdx_ = 0;
		deviceClk_ = false;
		deviceDat_ = false;
		loadEncState_ = LoadEncState::TalkTurnaround; // always grab the bus first, data or not
	}
}

// A data-phase byte -- either a filename character (while an OPEN is in
// progress) or a raw LOAD/SAVE payload byte (plain channel select).
// Either way it's just accumulated here; finalizeSession() is what
// decides what the buffer MEANT once the session ends.
void SDI::onDataByte(uint8_t b)
{
	dataBuf_ += (char)b;
}

// Called when UNLISTEN/UNTALK ends the current LISTEN/TALK exchange. If
// it was an OPEN, the accumulated buffer IS the filename -- commit it.
// Nothing happens yet with a finished LOAD/SAVE payload (plain channel
// select) beyond keeping it in dataBuf_ for the debug protocol to
// inspect -- there's no file I/O backend connected yet to hand it to
// (that's the still-unwired TCP bridge's job, not this decoder's).
void SDI::finalizeSession()
{
	if (secondaryCmd_ == SecondaryCmd::Open) {
		if (activeChannel_ >= 0 && !dataBuf_.empty()) {
			filenames_[activeChannel_] = dataBuf_;
			bridgeOpen(activeChannel_, dataBuf_);
		}
		dataBuf_.clear();
		secondaryCmd_ = SecondaryCmd::None;
		activeChannel_ = -1;
	} else if (secondaryCmd_ == SecondaryCmd::Close) {
		bridgeClose(activeChannel_);
		secondaryCmd_ = SecondaryCmd::None;
		activeChannel_ = -1;
	} else if (secondaryCmd_ == SecondaryCmd::PlainSelect) {
		// LOAD (WeAreListener) was already kicked off earlier, in
		// onSecondaryByte(), not here -- nothing to do for it at this
		// point except leave dataBuf_/pendingLoadData_ alone for the
		// debug protocol to inspect (the next secondary byte clears it
		// when a new session starts).
		//
		// WeAreTalker + PlainSelect ($F0-$FF): real-ROM-traced (see
		// VC1541.md's "Filename-send mechanism" section) -- the real
		// SDI103J ROM's own *LOAD sends the filename this way, under
		// channel 15, NOT via a textbook $60-$6F OPEN -- so treat this
		// the same as an OPEN (filename to the bridge), not a raw SAVE
		// write. The same ROM's *SAVE handler couldn't be confirmed to
		// exist at all in this revision (its table entry decodes to
		// dead/borrowed code, not a real implementation) -- so there's
		// no confirmed real-world case this would misclassify yet. If
		// a future ROM revision's SAVE turns out to use this same path
		// for genuine binary payload, this will need a way to tell the
		// two apart (e.g. a flag threaded through from whichever *
		// command kicked the session off) -- not guessed at here.
		if (iecRole_ == IecRole::WeAreTalker && activeChannel_ >= 0 && !dataBuf_.empty()) {
			filenames_[activeChannel_] = dataBuf_;
			bridgeOpen(activeChannel_, dataBuf_);
		}
	}
}

bool SDI::connectBridge(const char* host, int port)
{
	if (bridgeFd_ >= 0) {
		close(bridgeFd_);
		bridgeFd_ = -1;
	}

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return false;

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		close(fd);
		return false;
	}

	if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "SDI: bridge connect to %s:%d failed\n", host, port);
		close(fd);
		return false;
	}

	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	bridgeFd_ = fd;
	printf("SDI: bridge connected to %s:%d\n", host, port);

	// The companion's own reset handshake (see c64iec_fs_tcp.py) sends a
	// "D:<hex>\n" (or blank) line immediately on accept(), unprompted --
	// drain it now so it doesn't get mistaken for the reply to the
	// first real command later.
	std::string banner;
	bridgeReadLine(banner, 1000);
	return true;
}

// Blocking line I/O to the bridge -- see class comment for why blocking
// is the right fit here. All three silently fail (false/empty) if
// bridgeFd_ < 0, so every bridge* caller below works whether or not
// --sdi-bridge was ever given, same convention as ZiFi's own pump().
bool SDI::bridgeSendLine(const std::string& line)
{
	if (bridgeFd_ < 0) return false;
	std::string withNl = line + "\n";
	return send(bridgeFd_, withNl.data(), withNl.size(), 0) == (ssize_t)withNl.size();
}

bool SDI::bridgeReadLine(std::string& out, int timeoutMs)
{
	out.clear();
	if (bridgeFd_ < 0) return false;
	for (;;) {
		pollfd pfd{bridgeFd_, POLLIN, 0};
		if (poll(&pfd, 1, timeoutMs) <= 0) return false; // timeout or error
		char c;
		ssize_t n = recv(bridgeFd_, &c, 1, 0);
		if (n <= 0) return false; // peer closed
		if (c == '\n') return true;
		if (c != '\r') out += c;
	}
}

bool SDI::bridgeReadExact(uint8_t* buf, size_t n, int timeoutMs)
{
	if (bridgeFd_ < 0) return false;
	size_t got = 0;
	while (got < n) {
		pollfd pfd{bridgeFd_, POLLIN, 0};
		if (poll(&pfd, 1, timeoutMs) <= 0) return false;
		ssize_t r = recv(bridgeFd_, buf + got, n - got, 0);
		if (r <= 0) return false;
		got += (size_t)r;
	}
	return true;
}

// OPEN -- "O,<ch>,<name>\n", response is "OK..." or anything else = failure.
void SDI::bridgeOpen(int channel, const std::string& name)
{
	if (bridgeFd_ < 0) return;
	char hdr[32];
	snprintf(hdr, sizeof(hdr), "O,%X,", channel);
	bridgeSendLine(hdr + name);
	bridgeReadLine(lastBridgeReply_);
}

// CLOSE -- "C,<ch>\n", response line is just acked/ignored (matches
// upstream's own IECSerialFile::close(), which does the same).
void SDI::bridgeClose(int channel)
{
	if (bridgeFd_ < 0) return;
	char cmd[16];
	snprintf(cmd, sizeof(cmd), "C,%X", channel);
	bridgeSendLine(cmd);
	bridgeReadLine(lastBridgeReply_);
}

// WRITE (SAVE) -- "W,<ch>,<n>,<E|C>\n" + n raw bytes, response "OK..." or
// failure.
void SDI::bridgeWrite(int channel, const std::string& data, bool eoi)
{
	if (bridgeFd_ < 0) return;
	char hdr[32];
	snprintf(hdr, sizeof(hdr), "W,%X,%zu,%c", channel, data.size(), eoi ? 'E' : 'C');
	bridgeSendLine(hdr);
	send(bridgeFd_, data.data(), data.size(), 0);
	bridgeReadLine(lastBridgeReply_);
}

// READ (LOAD) -- "R,<ch>,<size>\n", response header "<M|L|E>,<n>\n" + n
// raw bytes. Stores the result in pendingLoadData_/pendingLoadEoi_ for
// advanceLoadEncoder() (below) to drain onto the simulated bus.
void SDI::bridgeRequestRead(int channel, int size)
{
	pendingLoadData_.clear();
	pendingLoadEoi_ = false;
	if (bridgeFd_ < 0) return;

	char cmd[32];
	snprintf(cmd, sizeof(cmd), "R,%X,%d", channel, size);
	bridgeSendLine(cmd);

	std::string header;
	if (!bridgeReadLine(header) || header.empty()) return;
	char flag = header[0];
	size_t comma = header.find(',');
	if (comma == std::string::npos) return;
	int n = atoi(header.c_str() + comma + 1);
	if (n <= 0) {
		pendingLoadEoi_ = (flag == 'L' || flag == 'E');
		return;
	}

	std::vector<uint8_t> buf(n);
	if (!bridgeReadExact(buf.data(), (size_t)n)) return;
	pendingLoadData_.assign((const char*)buf.data(), buf.size());
	pendingLoadEoi_ = (flag == 'L');
}

// Re-fills pendingLoadData_ (same R,<ch>,<size> call as the initial
// kickoff in onSecondaryByte()) once the encoder has drained what's
// there -- transparent to the Z80 side, which just sees bytes keep
// coming; chunk boundaries are purely this bridge connection's own
// implementation detail.
void SDI::fetchMoreLoadData()
{
	bridgeRequestRead(loadChannel_, 254);
	loadPos_ = 0;
}

// See SDI.h's class comment for the full state-machine description.
// Called once per in() poll while role==WeAreListener and not under
// ATN; updates deviceClk_/deviceDat_ for THIS poll.
void SDI::advanceLoadEncoder()
{
	switch (loadEncState_) {
		case LoadEncState::TalkTurnaround:
			// One-shot CLK grab -- see SDI.h's class comment. Exposed
			// for exactly this one poll; the real ROM only needs to see
			// CLK go asserted then released again, no DATA handshake in
			// between, so there's nothing to wait ON here -- just move
			// on unconditionally.
			deviceClk_ = true;
			deviceDat_ = false;
			loadEncState_ = pendingLoadData_.empty() ? LoadEncState::Idle
			                                          : LoadEncState::WaitDataReady;
			break;

		case LoadEncState::Idle:
			// Parked -- bridge had nothing queued. CLK released; stays
			// here until the next LOAD/CAT session restarts from
			// TalkTurnaround (see onSecondaryByte()).
			deviceClk_ = false;
			deviceDat_ = false;
			break;

		case LoadEncState::WaitDataReady:
			deviceClk_ = false; // "talker ready to send"
			if (!datLine()) {
				// Z80 released its own DATA bit -- "ready for data".
				loadBitIdx_ = 0;
				loadEncState_ = LoadEncState::BitSetup;
			}
			break;

		case LoadEncState::BitSetup: {
			bool bit = (pendingLoadData_[loadPos_] >> loadBitIdx_) & 1;
			deviceClk_ = true;  // asserted -- bit setup, not valid yet
			deviceDat_ = bit;
			loadEncState_ = LoadEncState::BitValid;
			break;
		}

		case LoadEncState::BitValid:
			deviceClk_ = false; // released -- this rising edge IS "valid, sample now"
			loadBitIdx_++;
			loadEncState_ = (loadBitIdx_ < 8) ? LoadEncState::BitSetup : LoadEncState::WaitAck;
			break;

		case LoadEncState::WaitAck:
			deviceClk_ = true; // asserted, matches the real post-byte Tf/Tbb window
			if (datLine()) {
				// Z80 asserted its own DATA bit -- "byte accepted".
				loadPos_++;
				if (loadPos_ >= pendingLoadData_.size()) {
					if (pendingLoadEoi_) {
						loadEncState_ = LoadEncState::Idle;
						break;
					}
					fetchMoreLoadData();
					if (pendingLoadData_.empty()) {
						loadEncState_ = LoadEncState::Idle; // bridge gave us nothing more
						break;
					}
				}
				loadEncState_ = LoadEncState::WaitDataReady;
			}
			break;
	}
}

std::string SDI::debugCommand(const std::vector<std::string>& toks)
{
	if (toks.empty() || toks[0] != "SDI")
		return std::string();

	static const char* kRoleNames[] = { "none", "weTalk", "weListen" };
	static const char* kCmdNames[] = { "none", "open", "close", "plain" };

	// "SDI IOTRACE ON"/"SDI IOTRACE OFF" -- runtime toggle for the
	// per-instruction PA/PB/PC/CONF logging (see setIotraceEnabled()'s
	// comment in SDI.h) -- same effect as the old SDI_IOTRACE compile
	// flag, no rebuild needed. "SDI IOTRACE" alone reports the current
	// state without changing it.
	if (toks.size() >= 2 && toks[1] == "IOTRACE") {
		if (toks.size() >= 3) {
			if (toks[2] == "ON") iotraceEnabled_ = true;
			else if (toks[2] == "OFF") iotraceEnabled_ = false;
			else return "ERR expected ON or OFF";
		}
		char out[32];
		snprintf(out, sizeof(out), "OK iotrace=%d", iotraceEnabled_ ? 1 : 0);
		return out;
	}

	// "SDI FILES" -- channel/file state, kept separate from the main
	// line-level status below so neither gets too cluttered to read.
	if (toks.size() >= 2 && toks[1] == "FILES") {
		char out[1536]; // dataBuf_ can hold a full LOAD/SAVE payload, worst-case all hex-escaped
		int n = snprintf(out, sizeof(out), "OK channel=%d cmd=%s bufLen=%zu buf=",
		                  activeChannel_, kCmdNames[(int)secondaryCmd_], dataBuf_.size());
		// dataBuf_ can contain non-printable bytes (raw LOAD/SAVE
		// payload) -- hex-escape anything outside printable ASCII so
		// this is always safe to print over the debug protocol's
		// single-line-reply convention.
		for (char c : dataBuf_) {
			if (n >= (int)sizeof(out) - 5) break;
			unsigned char uc = (unsigned char)c;
			if (uc >= 0x20 && uc < 0x7F)
				out[n++] = (char)uc;
			else
				n += snprintf(out + n, sizeof(out) - n, "\\x%02x", uc);
		}
		n += snprintf(out + n, sizeof(out) - n, " filenames=");
		for (int i = 0; i < 16 && n < (int)sizeof(out) - 1; i++) {
			if (filenames_[i].empty()) continue;
			n += snprintf(out + n, sizeof(out) - n, "%d:%s,", i, filenames_[i].c_str());
		}
		return out;
	}

	// "SDI BRIDGE" -- TCP bridge connection + the most recent exchange +
	// LOAD encoder state, kept separate for the same reason as FILES above.
	if (toks.size() >= 2 && toks[1] == "BRIDGE") {
		static const char* kEncNames[] = { "talkTurn", "idle", "waitReady", "bitSetup", "bitValid", "waitAck" };
		char out[1536]; // pendingLoadData_ can be a full 254-byte chunk, worst-case all hex-escaped (4 chars/byte)
		int n = snprintf(out, sizeof(out),
		                  "OK connected=%d lastReply=%s pendingLoadLen=%zu pendingLoadEoi=%d "
		                  "encState=%s loadPos=%zu loadBit=%d deviceClk=%d deviceDat=%d data=",
		                  bridgeFd_ >= 0 ? 1 : 0, lastBridgeReply_.c_str(),
		                  pendingLoadData_.size(), pendingLoadEoi_ ? 1 : 0,
		                  kEncNames[(int)loadEncState_], loadPos_, loadBitIdx_,
		                  deviceClk_ ? 1 : 0, deviceDat_ ? 1 : 0);
		for (char c : pendingLoadData_) {
			if (n >= (int)sizeof(out) - 5) break;
			unsigned char uc = (unsigned char)c;
			if (uc >= 0x20 && uc < 0x7F)
				out[n++] = (char)uc;
			else
				n += snprintf(out + n, sizeof(out) - n, "\\x%02x", uc);
		}
		out[n] = '\0';
		return out;
	}

	char out[360];
	snprintf(out, sizeof(out),
	         "OK enabled=%d active=%d romLoaded=%d pa=%02x pb=%02x pc=%02x ctrl=%02x "
	         "atn=%d clk=%d dat=%d role=%s bits=%d lastByte=%02x fromAtn=%d eoi=%d byteReady=%d "
	         "awaitRelease=%d listenerDat=%d",
	         enabled_ ? 1 : 0, active_ ? 1 : 0, romLoaded_ ? 1 : 0,
	         portA_, portB_, portC_, control_,
	         atnLine() ? 1 : 0, clkLine() ? 1 : 0, datLine() ? 1 : 0,
	         kRoleNames[(int)iecRole_], bitCount_, lastByte_,
	         lastByteFromAtn_ ? 1 : 0, lastByteWasEoi_ ? 1 : 0, byteReady_ ? 1 : 0,
	         awaitingInterByteRelease_ ? 1 : 0, listenerDat_ ? 1 : 0);
	byteReady_ = false; // read-and-clear, like a status flag
	return out;
}
