#pragma once
#include "Peripheral.h"
#include <vector>
#include <string>
#include <cstdint>

// JASE's ZiFi — a TCP client, not a serial port. The "ESP01" on the other
// end (real hardware via a socat bridge, or a standalone virtual-ESP01
// daemon — see tools/virtual_esp01/) is a SEPARATE process either way;
// JASE only ever speaks one simple byte-stream protocol over one TCP
// socket, so there's no backend-switching logic here at all (Pawel's own
// framing: "od tego bytu emulacja nie widzi różnicy").
//
// Primary port layout is pico-spec's REAL "ZiFi-API FIFO" convention —
// this is deliberate, not arbitrary: JASE's whole point is running the
// SAME guest Z80 code as real pico-spec hardware, so this has to match
// pico-spec's actual standard, not a different project's one-off design
// (see project memory 2026-10-15 for the correction this followed: an
// earlier version of this file wrongly adopted espat-zx's "picozx-uart"
// branch's ZxSpectrumEspUart convention here instead — that's a DIFFERENT
// real peripheral from an earlier, pre-pico-spec point in this project,
// not ZiFi, and collides with this one on the same low byte; it is NOT
// implemented here on purpose).
//   hi <= 0xBF : DR   (IN pulls next RX byte, OUT pushes a TX byte)
//   hi == 0xC0 : ZIFR (IN: RX ring fill count, 0-255)
//   hi == 0xC1 : ZOFR (IN: TX ring fill count, 0-255)
// IMR/CR and the 16550 UART window are NOT implemented — out of scope
// for a first working pipe.
//
// Also implements the ZX-UNO window (#FC3B latch / #FD3B data, full
// 16-bit decode, bit8 = data-vs-latch) — Karabas-Pro's native ESP8266
// bridge convention, a genuinely different historical standard real
// existing Z80 software (e.g. Moon Rabbit) targets directly. No collision
// with the FIFO above (different low byte) — both share the same
// rxRing_/txRing_/socket; a guest program can use either path.
//   OUT #FC3B, reg      -- select internal register (only 0xC6/0xC7 below)
//   IN/OUT #FD3B        -- access the selected register:
//     0xC6 (UART data): IN pulls next RX byte (0xFF if empty), OUT pushes a TX byte
//     0xC7 (status):    IN returns bit7=RX_RECV (rx ring non-empty),
//                        bit6=TX_BUSY (tx ring full) -- real ZX-Uno/divMMC
//                        convention, polled via ADD A,A -> CF
//     anything else (e.g. 0xC8/0xC9, UART2): IN returns 0xFF, OUT ignored
class ZiFi : public Peripheral
{
public:
	ZiFi();
	~ZiFi() override;

	const char* name() const override { return "ZIFI"; }
	bool in(uint16_t port, uint8_t& value) override;
	bool out(uint16_t port, uint8_t value) override;
	std::string debugCommand(const std::vector<std::string>& toks) override;

	// Connects (non-blocking) to the virtual-ESP01/socat endpoint. Safe to
	// call even if host/port were never configured — just does nothing.
	// Remembers host/port on success, for reconnect().
	bool connectTo(const char* host, int port);

	// Re-dials whatever connectTo() last succeeded with -- lets a running
	// JASE recover from killing/restarting the bridge or mock process on
	// the other end without restarting JASE itself. False if connectTo()
	// was never called successfully. Also exposed as the debug protocol's
	// "ZIFI RECONNECT" (and "ZIFI CONNECT host port" to dial a fresh
	// target without needing --zifi at startup).
	bool reconnect();

	// Call once per emulated frame (50Hz): drains the TX ring to the
	// socket and pulls any available bytes into the RX ring. Non-blocking
	// either way, like DebugServer::poll().
	void pump();

private:
	int sockFd_ = -1;
	std::vector<uint8_t> rxRing_;
	std::vector<uint8_t> txRing_;
	static const size_t kRingCap = 4096;
	std::string lastHost_;
	int lastPort_ = 0;

	uint8_t unoSelectedReg_ = 0;
};
