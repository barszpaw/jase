#pragma once
#include <cstdint>
#include <string>
#include <vector>

// TNFS (Trivial Network File System, FujiNet/8-bit-retro ecosystem) client
// over its native UDP transport. Clean-room implementation for JASE --
// protocol facts (packet header layout, op-codes, retry semantics) are
// the author's own understanding from implementing/confirming this same
// protocol against a real public tnfsd elsewhere; no code copied (that
// other implementation lives in a GPL-licensed repo).
//
// UDP has no delivery guarantee either way, so a dropped/garbled packet
// is normal, not fatal: transact() resends the SAME packet (same session
// id + sequence number -- a genuine retransmission, not a new request)
// up to a bounded number of times, and discards any reply that doesn't
// match the outstanding cmd+seq (a stray duplicate from an earlier
// retransmit, or a slow-but-not-lost reply arriving after we've already
// given up and resent).
//
// TNFS paths are genuinely hierarchical and the server is stateless about
// "current directory" -- every OPENDIR/OPENFILE request carries a full
// absolute path. cwd()/cwdPath() are plain client-side string bookkeeping.
class Tnfs
{
public:
	struct Entry { std::string name; bool isDir; uint32_t size; };

	// Opens a UDP socket and MOUNTs "/" anonymously (every public TNFS
	// test server is anonymous-only; real credentials would go in the
	// MOUNT payload, not implemented here).
	bool connect(const std::string& host, uint16_t port);

	bool list(std::vector<Entry>& out); // lists curDir_
	bool cwd(const std::string& name);  // relative descend, ".." to go up, "/" for root
	std::string cwdPath() const { return curDir_; }

	bool getToFile(const std::string& name, const std::string& destPath);

	void disconnect(); // UMOUNT + close
	bool isConnected() const { return connected_; }

private:
	bool connected_ = false;
	int sock_ = -1;
	std::string host_;
	uint16_t port_ = 0;
	uint16_t sid_ = 0;
	uint8_t seq_ = 0;
	std::string curDir_ = "/";

	uint8_t nextSeq();
	std::string absPath(const std::string& name) const;
	void closeHandle(uint8_t fd);

	// Builds a request (sid_ + seq + cmd + payload), sends/resends it
	// until a reply with matching cmd+seq arrives or the retry budget is
	// exhausted. outSid is the response HEADER's session id (only
	// meaningful for MOUNT, which assigns a fresh one -- every other
	// command just echoes sid_ back). outStatus is the TNFS status byte;
	// the response's data bytes (after the 5-byte header) land in outBuf,
	// truncated to outCap.
	bool transact(uint8_t cmd, const uint8_t* payload, size_t plen,
	              uint16_t& outSid, uint8_t& outStatus, uint8_t* outBuf, size_t outCap, size_t& outLen);
};
