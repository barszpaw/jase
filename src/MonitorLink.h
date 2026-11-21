#pragma once
#include "FddStation.h"
#include <cstdint>
#include <vector>

// Native C++ reimplementation of the real CBIOS's own console link
// protocol (TXBY/RXBY, called from CONOUT/CONIN) -- confirmed 2026-11-20
// by directly disassembling a LIVE, running CBIOS's memory (via "FDD3000
// MEM", against a real CPM-Boot_Disk_v2_2.dsk boot), not from the
// (partially scrambled, see docs/FDD3000/sources/) CP/M system-disk text
// extraction, and not from guessing. Two earlier versions of this file
// got this wrong: one reverse-engineered from unrelated routines inside
// monitor.zxs.tap (0x866E/0x86D2, presumably used for the TOS/CP/M
// station menu, not console I/O); another assumed the TOS interface
// ROM's own SENDBL/GETBL primitives (b7/b6 "data request" bit scheme)
// were reused for console I/O, based on a live link trace that, on
// closer reads, was never actually confirmed to BE console I/O at that
// moment. Both were live-tested against a real station and found wrong
// (CBIOS stayed stuck -- the exact station port/bit constants below are
// the ones a direct disassembly of its own running TXBY/RXBY code
// actually uses).
//
// This lets a byte-level TCP client act as a drop-in replacement for the
// real Monitor program -- no ZX Spectrum Z80 code or ROM paging needed at
// all, just this state machine driving the SAME link-byte latch
// (FddStation::setHostByte()/stationByte()) Fdd3000's own port 0xEF
// in()/out() already use.
//
// Protocol (station-side constant names/values from the CP/M system
// disk's own CBIOS.MAC source, confirmed against its live disassembly):
//   BYTEON=0x0A, SERVICO=0x0F, WSTA=0x06, RXACK=0x05, CSTAT=0x10,
//   BSTAT=0x40, MASK=0x5F, ERR=0x07. All comparisons below mask with
//   0x5F first, same as the real code.
//
//   RECEIVE one byte, station -> host (CBIOS's TXBY/CONOUT):
//     Station signals "I have a byte" by writing BYTEON (0x0A),
//     retrying until (host's last write & MASK) == SERVICO (0x0F).
//     Host: on seeing (v&MASK)==BYTEON, write SERVICO.
//     Station then writes low_nibble|CSTAT, waiting for an echo match;
//     host: wait for (v&CSTAT)!=0, echo v back verbatim, keep the low
//     nibble.
//     Station then writes high_nibble|BSTAT, waiting for an echo match;
//     host: wait for (v&BSTAT)!=0, echo v back verbatim, combine
//     nibbles into the decoded byte. Done -- station doesn't wait for
//     anything further (fire-and-forget STMSK on its side).
//
//   SEND one byte, host -> station (CBIOS's RXBY/CONIN):
//     Host signals "I have a byte" only once the station is already
//     asking for one: station writes WSTA (0x06) repeatedly, waiting
//     for (host's last write & MASK) == RXACK (0x05).
//     Host: on seeing (v&MASK)==WSTA AND a byte is actually queued,
//     write RXACK.
//     Host then writes low_nibble|CSTAT, waiting for the station to
//     echo it back (CBIOS's own RXBY echoes whatever it reads);
//     then high_nibble|BSTAT, same echo-wait. Done.
//
//   Either direction times out (writes ERR=0x07) after a bounded number
//   of polls -- kTimeoutAttempts mirrors the real code's own 8-bit
//   DJNZ/DEC-C counter (256).
class MonitorLink
{
public:
	void tick(FddStation& station)
	{
		switch (state_) {
		case State::Idle: {
			uint8_t v = station.stationByte();
			if ((v & kMask) == kByteOn) {
				station.setHostByte(kServico);
				state_ = State::RxWaitNibbleLo;
				retries_ = kTimeoutAttempts;
			} else if ((v & kMask) == kWsta && !inQueue_.empty()) {
				txByte_ = inQueue_.front();
				station.setHostByte(kRxAck);
				state_ = State::TxSettle;
				retries_ = kTimeoutAttempts;
			}
			break;
		}

		// Found live, 2026-11-21: unlike every OTHER state transition here,
		// writing RXACK isn't a reaction to something the station just
		// wrote -- there's nothing from the station to wait for before
		// moving on, so a version of this file that went straight to
		// TxSendLo on the very next tick raced CBIOS's own RXBY polling
		// loop and (at standalone fdd3000_station's 1:1 tick-per-station-
		// instruction rate, unlike in-process's clock-ratio stepping,
		// which happened to leave enough of a gap to mask this) NEVER
		// delivered a single byte. Holding RXACK stable for a full
		// kTimeoutAttempts-worth of ticks gives RXBY's own ~256-iteration
		// poll loop room to actually see it. Confirmed live: standalone
		// DIR/TYPE now work end-to-end for the first time.
		case State::TxSettle:
			station.setHostByte(kRxAck); // keep re-asserting -- harmless, same value
			if (--retries_ == 0) state_ = State::TxSendLo;
			break;

		// ---- RX: station -> host (mirrors CBIOS's TXBY) ----
		case State::RxWaitNibbleLo: {
			uint8_t v = station.stationByte();
			if (v & kCstat) {
				rxLowNibble_ = v & 0x0F;
				station.setHostByte(v); // echo exactly what was read
				state_ = State::RxWaitNibbleHi;
				retries_ = kTimeoutAttempts;
			} else if (--retries_ == 0) {
				timeout(station);
			}
			break;
		}
		case State::RxWaitNibbleHi: {
			uint8_t v = station.stationByte();
			if (v & kBstat) {
				station.setHostByte(v); // echo exactly what was read
				uint8_t byte = (uint8_t)(((v & 0x0F) << 4) | rxLowNibble_);
				if (outQueue_.size() < kFifoCap) outQueue_.push_back(byte);
				state_ = State::Idle;
			} else if (--retries_ == 0) {
				timeout(station);
			}
			break;
		}

		// ---- TX: host -> station (mirrors CBIOS's RXBY) ----
		case State::TxSendLo:
			station.setHostByte((uint8_t)((txByte_ & 0x0F) | kCstat));
			txLastSent_ = (uint8_t)((txByte_ & 0x0F) | kCstat);
			state_ = State::TxWaitEchoLo;
			retries_ = kTimeoutAttempts;
			break;
		case State::TxWaitEchoLo: {
			uint8_t v = station.stationByte();
			if (v == txLastSent_) {
				uint8_t hi = (uint8_t)(((txByte_ >> 4) & 0x0F) | kBstat);
				station.setHostByte(hi);
				txLastSent_ = hi;
				state_ = State::TxWaitEchoHi;
				retries_ = kTimeoutAttempts;
			} else if (--retries_ == 0) {
				timeout(station);
			}
			break;
		}
		case State::TxWaitEchoHi: {
			uint8_t v = station.stationByte();
			if (v == txLastSent_) {
				// Found live, 2026-11-20 (disassembly of CBIOS's own
				// running RXBY, not guessed): after both nibble echoes,
				// RXBY does ONE MORE read, waiting for (v&MASK)==STMSK
				// (0x50) -- the host must send this end marker too (and
				// RXBY echoes it back) before returning, unlike TXBY's
				// own STMSK, which is fire-and-forget on the station's
				// side.
				station.setHostByte(kStmsk);
				txLastSent_ = kStmsk;
				state_ = State::TxWaitEndEcho;
				retries_ = kTimeoutAttempts;
			} else if (--retries_ == 0) {
				timeout(station);
			}
			break;
		}
		case State::TxWaitEndEcho: {
			uint8_t v = station.stationByte();
			if (v == txLastSent_) {
				if (!inQueue_.empty()) inQueue_.erase(inQueue_.begin());
				state_ = State::Idle;
			} else if (--retries_ == 0) {
				timeout(station);
			}
			break;
		}
		}
	}

	// ---- TCP-facing byte queues -- same shape as Wd2123's own
	// popTxByte()/pushRxByte(), see Fdd3000::pumpMonitorSocket(). ----

	bool popOutgoingByte(uint8_t& out)
	{
		if (outQueue_.empty()) return false;
		out = outQueue_.front();
		outQueue_.erase(outQueue_.begin());
		return true;
	}

	void pushIncomingByte(uint8_t value)
	{
		if (inQueue_.size() < kFifoCap) inQueue_.push_back(value);
	}

private:
	enum class State {
		Idle,
		RxWaitNibbleLo, RxWaitNibbleHi,
		TxSettle, TxSendLo, TxWaitEchoLo, TxWaitEchoHi, TxWaitEndEcho
	};

	static const uint8_t kMask = 0x5F;
	static const uint8_t kByteOn = 0x0A;
	static const uint8_t kServico = 0x0F;
	static const uint8_t kStmsk = 0x50;
	static const uint8_t kWsta = 0x06;
	static const uint8_t kRxAck = 0x05;
	static const uint8_t kCstat = 0x10;
	static const uint8_t kBstat = 0x40;
	static const uint8_t kErr = 0x07;

	static const int kTimeoutAttempts = 256; // matches the real code's own DJNZ/DEC-C (0 -> 256 iterations)
	static const size_t kFifoCap = 4096;

	State state_ = State::Idle;
	int retries_ = 0;
	uint8_t txByte_ = 0;
	uint8_t txLastSent_ = 0;
	uint8_t rxLowNibble_ = 0;
	std::vector<uint8_t> inQueue_;  // bytes from the TCP client, waiting to go to the station
	std::vector<uint8_t> outQueue_; // bytes decoded from the station, waiting for the TCP client

	void timeout(FddStation& station)
	{
		station.setHostByte(kErr);
		state_ = State::Idle;
	}
};
