#pragma once
#include "IIecDevice.h"
#include <cstdint>

// TCP transport to tools/c64iec_fs/c64iec_fs_tcp.py (or an equivalent
// companion speaking the same wire protocol) -- the "bridge" this
// project's been calling it. Wire format read directly from upstream
// fox20io/c64-fdemu's IECSerialFile.cpp (GPLv3, not copied -- see
// docs/VC1541/VC1541.md's "Bridging to a real/virtual 1541" section for
// the licensing reasoning: this is a fact about an interoperability
// format, reused the same way this project already reused pico-spec's
// port numbers and ZEsarUX's debug-protocol shape):
//
//   -> O,<ch_hex>,<name>\n          <- OK...\n (or anything else = fail)
//   -> R,<ch_hex>,<chunksize>\n     <- <M|L|E>,<n>\n + n raw bytes
//   -> W,<ch_hex>,<n>,<E|C>\n + n raw bytes   <- OK...\n (or fail)
//   -> C,<ch_hex>\n                 <- (one line, ignored)
//
// Blocking line/byte I/O throughout -- deliberately, not async: the IEC
// protocol layer calling this already has to block the emulated bus
// waiting for a response anyway (real IEC is a slow serial bus, not
// something the Z80 side can race ahead of), so there's no benefit to
// a non-blocking design here, only complexity. Every call is also a
// no-op/false/empty-safe when not connected, so callers work whether or
// not a bridge was ever configured, same convention ZiFi's pump() uses.
class IecBridgeClient : public IIecDevice
{
public:
	~IecBridgeClient() override;

	bool connect(const char* host, int port);

	bool isConnected() const override { return fd_ >= 0; }
	bool open(int channel, const std::string& name) override;
	void close(int channel) override;
	bool write(int channel, const std::string& data, bool eoi) override;
	bool requestRead(int channel, int size, std::string& outData, bool& outEoi) override;

	// For debug reporting (see project memory on this project's
	// debug-protocol convention) -- the most recent reply line, whatever
	// it was.
	const std::string& lastReply() const { return lastReply_; }

private:
	bool sendLine(const std::string& line);
	bool readLine(std::string& out, int timeoutMs = 2000);
	bool readExact(uint8_t* buf, size_t n, int timeoutMs = 2000);

	int fd_ = -1;
	std::string lastReply_;
};
