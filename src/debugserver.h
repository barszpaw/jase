#pragma once
#include <cstdint>
#include <string>

class MinZX;

// Layer 1 of the JASE debug protocol (see project memory for the full
// contract): a simple line-based TCP text protocol exposing the EMULATED
// machine's state (Z80 registers, memory, I/O ports) — not JASE's own
// C++ internals, which stay a plain `gdb -p PID` job, no socket involved.
//
// One client at a time, non-blocking, polled once per frame from the SDL
// main loop so it never stalls rendering. Request: "COMMAND arg1 arg2\n".
// Response: "OK ...\n" or "ERR ...\n".
class DebugServer {
public:
    // Create, bind, and listen on `port` (non-blocking). Returns false on
    // any socket error.
    bool init(int port);

    // Call once per frame: accept a pending connection if none is active,
    // drain available bytes, dispatch any complete lines, reply. Also
    // notices if minZX just hit a breakpoint during the frame that was
    // run before this call and auto-pauses (matches real debuggers: a
    // breakpoint always stops free-run, the client doesn't have to poll
    // for it separately).
    void poll(MinZX& minZX);

    // Layer 2 run control: when false, main.cpp should skip calling
    // minZX.update() for that frame (keep rendering/polling only).
    // Defaults to true — with no debug client ever connecting, JASE
    // behaves exactly as if this server didn't exist.
    bool isRunning() const { return running_; }

    void shutdown();

private:
    int listenFd_ = -1;
    int clientFd_ = -1;
    std::string rxBuf_;
    bool running_ = true;

    void handleLine(const std::string& line, MinZX& minZX);
    void reply(const std::string& msg); // appends '\n', writes to clientFd_
};
