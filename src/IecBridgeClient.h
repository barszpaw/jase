#pragma once
#include "IIecDevice.h"
#include <cstdint>
#include <string>
#include <map>

// TCP transport to tools/sdi_bridge/sdi_bridge.py -- a purpose-built
// companion for SDI specifically (replaces the earlier tools/c64iec_fs/
// c64iec_fs_tcp.py, an fdemu-derived real-C64-protocol bridge: live-traced
// against the real SDI103J ROM, its *CAT does NOT read a "$" directory the
// way a real C64/1541 does -- see sdi_bridge.py's own top-of-file comment
// for the full trace. Rather than keep forcing real-CBM wire semantics
// onto a peripheral that doesn't follow them, this bridge/client pair
// speaks a protocol written directly against what SDI was traced wanting).
//
//   -> OPEN <ch_hex> <name>\n   <- OK <len>\n<len raw bytes>  (success)
//                                <- ERR <reason>\n             (failure)
//   -> CLOSE <ch_hex>\n         <- OK\n
//   -> WRITE <ch_hex> <len> <E|C>\n<len raw bytes>  <- OK\n / ERR <reason>\n
//
// Deliberately pre-fetch at OPEN, not per-byte: the whole channel's
// content comes back in one round-trip and is buffered here; requestRead()
// drains the LOCAL buffer with no further network I/O. The IEC bit-banging
// this feeds is time-critical (SDI_8255/IecDeviceEmulator's TX state
// machine) -- a TCP round-trip per byte was never something to risk that
// on, pre-fetching removes the question entirely.
//
// Every call is a no-op/false/empty-safe when not connected, so callers
// work whether or not a bridge was ever configured, same convention
// ZiFi's pump() uses.
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

	// Whole-channel content fetched at open(), drained locally by
	// requestRead(). pos_ tracks how much of data_ has been consumed.
	struct ChannelBuffer {
		std::string data;
		size_t pos = 0;
	};
	std::map<int, ChannelBuffer> buffers_;
};
