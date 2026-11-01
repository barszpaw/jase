#include "Multiply.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

Multiply::~Multiply()
{
	if (sockFd_ >= 0)
		close(sockFd_);
}

bool Multiply::connectTo(const char* host, int port)
{
	if (sockFd_ >= 0) {
		close(sockFd_);
		sockFd_ = -1;
	}

	int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0)
		return false;

	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
		close(fd);
		return false;
	}

	if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "Multiply: connect to %s:%d failed\n", host, port);
		close(fd);
		return false;
	}

	int one = 1;
	setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
	fcntl(fd, F_SETFL, O_NONBLOCK);

	sockFd_ = fd;
	lastHost_ = host;
	lastPort_ = port;
	printf("Multiply: connected to %s:%d\n", host, port);
	return true;
}

bool Multiply::reconnect()
{
	if (lastHost_.empty() || lastPort_ <= 0)
		return false;
	// Stale bytes from the dead connection would otherwise confuse the
	// bridge's own command-boundary tracking on the new one (same
	// reasoning as ZiFi::reconnect()) -- start clean.
	rxRing_.clear();
	txRing_.clear();
	rxPhase_ = RxPhase::Idle;
	return connectTo(lastHost_.c_str(), lastPort_);
}

// Called synchronously from Dandanator::executeCommand() the moment a
// command-52 byte arrives -- see DandanatorCmd52Sink. Just queues it;
// pump() does the actual socket I/O.
void Multiply::onDandanatorByte(uint8_t value)
{
	if (!enabled_) return;

	// A fresh outgoing byte means the Z80 is issuing a (possibly retried)
	// request -- any REPLY data still sitting unconsumed from an earlier
	// exchange is now stale/abandoned, not a continuation of this one.
	// Found live, 2026-10-31: the real v10.4.3 ROM's GetMultiplyVer
	// retries its REQ_ID probe up to ~10 times, each retry triggered by
	// a genuine 50Hz interrupt aborting the PREVIOUS attempt's
	// Load4bitBlk mid-nibble (see Dandanator.h's Locked/cmd-52 comment
	// for the abort mechanism). Without this reset, a reply that arrived
	// too late for one attempt just sat in rxRing_ and got served to the
	// NEXT attempt starting mid-nibble-phase instead of fresh, silently
	// corrupting the assembled bytes (observed: rxRing_ growing to 15
	// bytes -- the stale 8 plus a fresh 8 racing each other -- instead
	// of a clean 8, and the ROM never recognizing "MULT" despite the
	// bridge replying correctly every time).
	if (rxPhase_ != RxPhase::Idle || !rxRing_.empty()) {
		rxRing_.clear();
		rxPhase_ = RxPhase::Idle;
	}

	if (txRing_.size() < kRingCap)
		txRing_.push_back(value);
}

// See class comment for the real 2-edge nibble handshake this
// approximates. Called by Dandanator::in() (the actual port 0x1F
// arbiter) before it falls back to its own idle joystick stub -- not a
// Peripheral::in() override itself, since Dandanator -- not Multiply --
// owns this port now.
bool Multiply::wantsPortRead(uint8_t& value)
{
	if (!enabled_) return false;

	if (rxPhase_ == RxPhase::Idle) {
		// Opportunistic poll: pump() is normally called once per emulated
		// frame (50Hz) from MinZX's main loop, but the real firmware's
		// busy-wait (Load4bitBlk, and the "is Arduino still busy" 256-read
		// pre-check before it -- see GetMultiplyVer in the real menu
		// source) expects a reply to show up WITHIN the same attempt that
		// requested it, same as real hardware's ~150us round trip. A
		// once-per-frame pump() is too coarse: found live, 2026-10-31,
		// against a real v10.4.3 romset -- a REQ_ID reply consistently
		// arrived one frame too late (after the requesting attempt had
		// already been IM2-abort-retried), and the busy-check loop of the
		// NEXT attempt then misread that late reply's nonzero nibbles as
		// "Arduino busy" and silently drained/discarded it, forever
		// alternating between "no data yet" and "stale data, discarded"
		// without the two ever lining up. Polling the socket right here,
		// on every port read that would otherwise return "nothing queued,"
		// closes that gap -- a local loopback recv() is cheap and a real
		// Multiply reply is usually already sitting in the kernel buffer
		// well before the Z80 even finishes sending the request.
		if (rxRing_.empty()) pump();
		if (rxRing_.empty()) return false; // genuinely nothing queued -- defer to Dandanator's idle stub
		replyByte_ = rxRing_.front();
		rxRing_.erase(rxRing_.begin());
		rxPhase_ = RxPhase::HighReady;
	}

	// Real port layout (same bit assignment as Kempston's joystick
	// byte): bits 4-1 = 4-bit nibble, bit 0 = the strobe the real
	// firmware's Load4bitBlk edge-detects on (RRA puts bit0 in carry,
	// shifting bits 4-1 down into 3-0).
	bool highPhase = (rxPhase_ == RxPhase::HighReady);
	uint8_t nibble = highPhase ? (replyByte_ >> 4) : (replyByte_ & 0x0F);
	uint8_t strobe = highPhase ? 1 : 0;
	value = (uint8_t)((nibble << 1) | strobe);

	rxPhase_ = highPhase ? RxPhase::LowReady : RxPhase::Idle;
	return true;
}

void Multiply::pump()
{
	if (sockFd_ < 0)
		return;

	// Drain the TX ring to the socket.
	while (!txRing_.empty()) {
		ssize_t n = send(sockFd_, txRing_.data(), txRing_.size(), 0);
		if (n > 0)
			txRing_.erase(txRing_.begin(), txRing_.begin() + n);
		else
			break; // EAGAIN or error -- try again next frame
	}

	// Pull whatever's waiting into the RX ring (bounded).
	uint8_t buf[1024];
	for (;;) {
		ssize_t n = recv(sockFd_, buf, sizeof(buf), 0);
		if (n == 0) {
			// Peer closed (e.g. the bridge process was killed) --
			// distinct from n<0 (EAGAIN, just no data yet on a non-
			// blocking socket). Same reasoning as ZiFi::pump(): without
			// this, a dead peer leaves sockFd_ looking "connected"
			// forever.
			close(sockFd_);
			sockFd_ = -1;
			break;
		}
		if (n < 0)
			break;
		for (ssize_t i = 0; i < n && rxRing_.size() < kRingCap; i++)
			rxRing_.push_back(buf[i]);
	}
}

std::string Multiply::debugCommand(const std::vector<std::string>& toks)
{
	if (toks.empty() || toks[0] != "MULTIPLY")
		return std::string();

	// "MULTIPLY RECONNECT" / "MULTIPLY CONNECT host port" -- same
	// convention as ZIFI's debug commands.
	if (toks.size() >= 2 && toks[1] == "RECONNECT")
		return reconnect() ? "OK" : "ERR no prior connectTo()";
	if (toks.size() >= 4 && toks[1] == "CONNECT")
		return connectTo(toks[2].c_str(), atoi(toks[3].c_str())) ? "OK" : "ERR connect failed";

	char out[128];
	snprintf(out, sizeof(out), "OK enabled=%d connected=%d rx=%zu tx=%zu",
	         enabled_ ? 1 : 0, sockFd_ >= 0 ? 1 : 0, rxRing_.size(), txRing_.size());
	return out;
}
