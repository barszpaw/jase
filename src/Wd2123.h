#pragma once
#include <cstdint>
#include <vector>

// FDD3000 station's serial chip. Port map CONFIRMED (2026-11-20) against
// a real, published source -- Jarek Adamski's "The FDD Interface" writeup
// on loadzx.com/timexcomputerworld/ti-technical-info/, which lists the
// station's own internal port map directly:
//   #10  WD2123 configuration (write) CHA
//   #11  WD2123 configuration (write) CHB
//   #40  WD2123 data            CHB
//   #41  WD2123 status/command  CHB
//   #80  WD2123 data            CHA
//   #81  WD2123 status/command  CHA
// This matches PLD_equations.md's own RS1/RS2/RS3 decode (RS1=#80-#9F,
// RS2=#40-#5F, RS3=#00-#1F -- see EMULATION_SPEC.md) with one real
// correction over an earlier version of this file's GUESS: RS3 is not a
// single whole-chip latch with no per-channel meaning -- it's TWO
// separate per-channel "configuration" registers, CHA at #10 and CHB at
// #11, selected by the port's own bit 0 exactly like data-vs-status
// already is within the #80/#40 blocks. CHA=channel index 0 (our RS1/
// #80-#81), CHB=channel index 1 (our RS2/#40-#41) -- matches the TOS
// Secrets article's own ":CH_A"/":CH_B" channel names.
// STILL NOT confirmed anywhere: the actual BIT layout of the
// configuration register or the status/command register's command bits
// -- the source above explicitly says so ("How to use these ports? I
// still wait for the info..."). An earlier version of this file guessed
// a Mode1->Mode2->Command 3-write sequence on the status/command port
// (borrowed from an unrelated generic DUART model) -- dropped now that a
// real, SEPARATE configuration port is confirmed to exist, which
// undermines the reason that guess existed in the first place. The
// status/command port is now a single direct write (confirmed
// read=status / write=command by the source above), same as data.
//
// kStatRxReady CONFIRMED bit0 (2026-11-20), against a second, independent
// real source: the real CBIOS (CBIOS.MAC, pulled off a real TMX Portugal
// CP/M system disk -- see docs/FDD3000/sources/ -- names the chip "WD
// 2123" directly in a comment and defines the exact same port map as
// above (CANALA=#80, SIOASTS/SIOACMD=CANALA+1=#81, SIOABR=#10; CANALB=
// #40, +1=#41, SIOBBR=#11) PLUS `RXRDY EQU 00000001B ;Receiver READY`,
// polled as `BIT RXRDY,A ;byte ready?` in the real RXBYTE routine. An
// earlier version of this file put RX-ready on bit1 (borrowed from the
// same unrelated generic-DUART starting point as the dropped Mode1/
// Mode2/Command sequence above) -- moved to bit0 to match; kStatTxReady
// swapped to bit1 to keep the two apart (its own bit position is NOT
// independently confirmed by either source, unlike kStatRxReady's).
//
// DATA PATH (user-directed, 2026-11-20): a byte-level TCP shortcut, not a
// bit-accurate async serial simulation. There is no real external line on
// either channel to be bit-accurate FOR -- the far end is always another
// TCP socket (see fdd3000_station/main.cpp's own two listeners), so
// modeling start/stop bits, parity and a baud-rate generator would add
// real complexity for zero behavioral benefit. A write to a channel's
// data register is "sent" instantly (pushed to that channel's outgoing
// FIFO); an incoming byte from the far end lands in the receive holding
// register (or its FIFO, if the CPU hasn't read the previous one yet) the
// instant it arrives. An earlier version of this file DID implement a
// real bit-level RX/TX finite-state machine with its own baud-rate
// generator (the user's original Gemini-generated starting point) -- see
// git history if real async timing is ever needed (e.g. a genuine
// external serial backend).
class Wd2123
{
public:
	Wd2123() { reset(); }

	enum StatusFlags : uint8_t {
		kStatRxReady   = 0x01,
		kStatTxReady   = 0x02,
		kStatTxEmpty   = 0x04,
		kStatParityErr = 0x08,
		kStatOverrunErr= 0x10,
		kStatFrameErr  = 0x20,
		kStatBreakDet  = 0x40,
		kStatDcdDsr    = 0x80
	};

	void reset()
	{
		for (int i = 0; i < 2; i++) {
			channels_[i] = Channel();
			channels_[i].status = kStatTxReady | kStatTxEmpty;
		}
	}

	// ch: 0=CHA (#80/#81), 1=CHB (#40/#41). ctrlSel: false selects the
	// data register, true the status/command register -- both CONFIRMED
	// (see class comment), not inferred.
	uint8_t readChannel(int ch, bool ctrlSel)
	{
		Channel& c = channels_[ch & 1];
		if (!ctrlSel) {
			uint8_t v = c.rxHoldingReg;
			if (!c.rxFifo.empty()) {
				// Another byte was already queued (the CPU hadn't read the
				// previous one yet) -- promote it immediately, same as a
				// real UART's holding-register-behind-a-FIFO behavior.
				c.rxHoldingReg = c.rxFifo.front();
				c.rxFifo.erase(c.rxFifo.begin());
				c.status |= kStatRxReady;
			} else {
				c.status &= ~kStatRxReady;
			}
			return v;
		}
		return c.status;
	}

	void writeChannel(int ch, bool ctrlSel, uint8_t value)
	{
		Channel& c = channels_[ch & 1];
		if (!ctrlSel) {
			// Byte-level shortcut (see class comment): no real shift-
			// register delay, so TX_READY/TX_EMPTY just stay set --
			// firmware polling either one always sees "ready". Only
			// actually queues the byte if TX is enabled (commandReg bit
			// 0) -- same gate real hardware would apply, and the one
			// thing that forces a ported driver to enable the channel
			// first instead of writes silently working regardless.
			if ((c.commandReg & 0x01) && c.txFifo.size() < kFifoCap)
				c.txFifo.push_back(value);
		} else {
			// Status/command port write -- confirmed single direct write,
			// no multi-step sequence (see class comment). Bit meanings
			// beyond TX enable (0x01)/RX enable (0x04), which the
			// byte-level shortcut gates on, are NOT confirmed; 0x10
			// (clear error flags) is our own best-effort guess, kept from
			// the original generic-DUART starting point.
			c.commandReg = value;
			if (value & 0x10) c.status &= ~(kStatParityErr | kStatOverrunErr | kStatFrameErr);
		}
	}

	// #10 (CHA) / #11 (CHB) -- CONFIRMED to exist, write-only, per-channel
	// (see class comment), but its CONTENTS are not documented anywhere
	// we have. Stored only, for debug visibility.
	void writeConfig(int ch, uint8_t value) { channels_[ch & 1].configReg = value; }
	uint8_t peekConfig(int ch) const { return channels_[ch & 1].configReg; }

	uint8_t peekStatus(int ch) const { return channels_[ch & 1].status; }

	// ---- The byte-level transport's own two calls -- see
	// fdd3000_station/main.cpp's pumpSerialSocket(), the only caller of
	// either right now. Deliberately NOT gated behind a Peripheral/SDL
	// dependency here (same reasoning as FddStation itself): this class
	// has no idea a socket exists on the other end. ----

	// True if a byte the station CPU wrote is waiting to go out over the
	// wire; consumes it. Call in a loop to drain everything pending.
	bool popTxByte(int ch, uint8_t& out)
	{
		Channel& c = channels_[ch & 1];
		if (c.txFifo.empty()) return false;
		out = c.txFifo.front();
		c.txFifo.erase(c.txFifo.begin());
		return true;
	}

	// A byte arrived from the wire. Dropped if RX isn't enabled (commandReg
	// bit 2) -- real hardware wouldn't have anywhere to put it either.
	// Queues (capped) rather than overwriting if the CPU hasn't read the
	// current holding-register byte yet, same OVERRUN semantics real
	// hardware signals when the queue is what actually overflows.
	void pushRxByte(int ch, uint8_t value)
	{
		Channel& c = channels_[ch & 1];
		if (!(c.commandReg & 0x04)) return;
		if ((c.status & kStatRxReady) || !c.rxFifo.empty()) {
			if (c.rxFifo.size() >= kFifoCap) { c.status |= kStatOverrunErr; return; }
			c.rxFifo.push_back(value);
		} else {
			c.rxHoldingReg = value;
			c.status |= kStatRxReady;
		}
	}

private:
	static const size_t kFifoCap = 4096;

	struct Channel {
		uint8_t status = 0;
		uint8_t commandReg = 0;
		uint8_t configReg = 0; // see writeConfig()'s own comment

		uint8_t rxHoldingReg = 0;
		std::vector<uint8_t> rxFifo;
		std::vector<uint8_t> txFifo;
	};

	Channel channels_[2];
};
