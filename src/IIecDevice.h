#pragma once
#include <cstdint>
#include <string>

// What the (future) IEC protocol layer needs from "whatever's at the
// other end of the bus" -- today that's IecBridgeClient (a TCP
// connection to tools/c64iec_fs/c64iec_fs_tcp.py or an equivalent
// companion); later it can be a native 1541 emulation living entirely
// inside this process, implementing the same four calls, with nothing
// else in the codebase needing to change. Same split as IAySound/AyPsg
// (an abstract interface the caller holds a pointer to, decoupled from
// which concrete backend is behind it).
//
// Deliberately channel/filename/byte-level, not bit-level -- the IEC
// protocol layer's job is turning ATN/CLK/DAT bit-banging into "channel
// N opened with name X" / "byte(s) arrived on channel N" events; this
// interface is what it calls once it's done that translation, not
// before.
class IIecDevice
{
public:
	virtual ~IIecDevice() {}

	// True once a transport-level connection exists (TCP connected, or
	// a native backend that's always "connected"). Callers fall back to
	// honest "device not present" behavior when this is false, same as
	// no bridge configured on the old SDI.cpp.
	virtual bool isConnected() const = 0;

	// OPEN channel `channel` with filename `name` (wildcards etc. are
	// the backend's own business, same as a real 1541's DOS). Returns
	// true on success.
	virtual bool open(int channel, const std::string& name) = 0;

	// CLOSE channel `channel`.
	virtual void close(int channel) = 0;

	// WRITE (SAVE) -- `data` on `channel`, `eoi` marking the last write
	// of the stream (end-of-information, same meaning as the real bus
	// signal). Returns true on success.
	virtual bool write(int channel, const std::string& data, bool eoi) = 0;

	// READ (LOAD) -- request up to `size` bytes from `channel`. Returns
	// true if the call itself succeeded (even an empty/EOI result is
	// "true"); fills `outData` with whatever was available and sets
	// `outEoi` if this was the last chunk of the stream.
	virtual bool requestRead(int channel, int size, std::string& outData, bool& outEoi) = 0;
};
