#pragma once
#include "Peripheral.h"
#include <vector>
#include <cstdint>

// JASE's ZiFi — a TCP client, not a serial port. The "ESP01" on the other
// end (real hardware via a socat bridge, or a standalone virtual-ESP01
// daemon — see tools/virtual_esp01/) is a SEPARATE process either way;
// JASE only ever speaks one simple byte-stream protocol over one TCP
// socket, so there's no backend-switching logic here at all (Pawel's own
// framing: "od tego bytu emulacja nie widzi różnicy").
//
// Primary port layout matches THIS project's own real
// ZxSpectrumEspUart (the actual PicoZX/RP2350 hardware UART-passthrough
// ports, from earlier in this project — see espat-zx's "picozx-uart"
// branch, esp_uart_driver.c): low byte only, high byte ignored (a real
// Z80 "IN A,(n)"/"OUT (n),A" puts register A's CURRENT, unpredictable
// value on the high byte — real simple port decode logic, this one
// included, ignores it rather than depend on it):
//   0xEF (data)   IN  = next RX byte (0xFF if none), OUT = send a byte
//   0xE7 (status) IN  = bit0: RX ready (rx ring non-empty),
//                       bit1: TX ready (tx ring not full)
//
// Also implements the ZX-UNO window (#FC3B latch / #FD3B data, full
// 16-bit decode, bit8 = data-vs-latch) — Karabas-Pro's native ESP8266
// bridge convention. Pawel's call (2026-10-15): this is the better target
// than the 0xEF FIFO above for REAL existing Z80 software (e.g. Moon
// Rabbit, a pure-Z80 Gopher client speaking AT commands directly over
// these exact ports, no host help) — the FIFO above is pico-spec's own
// higher-level API, not a historical hardware standard anything else
// actually targets. Both share the same rxRing_/txRing_/socket; a guest
// program can use either path, they're just two views onto the same pipe.
//   OUT #FC3B, reg      -- select internal register (only 0xC6/0xC7 below)
//   IN/OUT #FD3B        -- access the selected register:
//     0xC6 (UART data): IN pulls next RX byte (0xFF if empty), OUT pushes a TX byte
//     0xC7 (status):    IN returns bit0=RX_RECV (rx ring non-empty),
//                        bit1=TX_BUSY (tx ring full)
//     anything else (e.g. 0xC8/0xC9, UART2): IN returns 0xFF, OUT ignored
// IMR/CR (0xEF window) and UART2 are NOT implemented — out of scope.
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
	bool connectTo(const char* host, int port);

	// Call once per emulated frame (50Hz): drains the TX ring to the
	// socket and pulls any available bytes into the RX ring. Non-blocking
	// either way, like DebugServer::poll().
	void pump();

private:
	int sockFd_ = -1;
	std::vector<uint8_t> rxRing_;
	std::vector<uint8_t> txRing_;
	static const size_t kRingCap = 4096;

	uint8_t unoSelectedReg_ = 0;
};
