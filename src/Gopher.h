#pragma once
#include <cstdint>
#include <string>
#include <vector>

// RFC 1436 Gopher client -- plain TCP, no TLS, no auth, port 70 default.
// Clean-room implementation for JASE (facts from RFC 1436 itself, not
// ported from pico-spec's GPL Gopher.cpp, even though the author of this
// file wrote that one too -- see project notes).
//
// A Gopher menu line is "TChar Display \t Selector \t Host \t Port". The
// selector is an opaque per-entry string assigned by the server (not
// necessarily a hierarchical path), and a menu entry can point at a
// DIFFERENT host/port than the one currently browsed -- so unlike a plain
// filesystem, this keeps a stack of (host, port, selector) frames, one per
// directory level descended into. ".." pops it.
class Gopher
{
public:
	struct Entry { std::string display; std::string selector; std::string host; uint16_t port; bool isDir; };

	// Fetches the server's root menu once to confirm it actually answers.
	bool connect(const std::string& host, uint16_t port);

	// Lists the CURRENT directory (stack top) into both `out` and an
	// internal cache that cwd()/getToFile() resolve display names against.
	bool list(std::vector<Entry>& out);

	// Descends into a directory entry from the last list() call, by its
	// display name.
	bool cwd(const std::string& display);
	bool cwdUp(); // pops the stack, no-op at the root
	std::string cwdPath() const;

	// Downloads a FILE entry (by display name, from the last list() call)
	// to a local path, overwriting it.
	bool getToFile(const std::string& display, const std::string& destPath);

	// Gopher display text is a human description, not necessarily a
	// filename (e.g. "Some Game (tap)") -- the real name with extension
	// lives in the entry's selector. Returns the last '/'-segment of that
	// selector, or `display` unchanged if the entry/selector can't be
	// resolved.
	std::string downloadBasename(const std::string& display) const;

	void disconnect();
	bool isConnected() const { return connected_; }

private:
	struct Frame { std::string host; uint16_t port; std::string selector; std::string dispName; };
	bool connected_ = false;
	std::vector<Frame> stack_;
	std::vector<Entry> lastEntries_;

	// Opens a fresh TCP connection to host:port, sends selector+"\r\n",
	// reads lines until a lone "." terminator (RFC 1436) or EOF.
	bool fetchLines(const std::string& host, uint16_t port, const std::string& selector, std::vector<std::string>& lines);
};
