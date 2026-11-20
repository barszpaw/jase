#pragma once
#include <cstdint>
#include <vector>

// FDD3000 station's serial chip, per PLD_equations.md's own pin comments
// ("enable WD2123 CH1"/"CH2"/"CTRL" on RS1/RS2/RS3 -- see
// EMULATION_SPEC.md's I/O table). NOT yet exercised by any firmware we've
// disassembled (fdd3boot_disasm.md's 86 known lines never touch these
// ports), so none of this has been verified against a real trace -- unlike
// Wd1772Fsm, which was built against (and iteratively corrected by) real
// boot-ROM behavior. Treat every register-level detail here as a
// best-effort placeholder until real firmware traffic on RS1/RS2/RS3 shows
// otherwise.
//
// Register-level facts corrected against the PLD (an earlier Gemini-
// generated starting point assumed something different and would not
// have matched this board): the PLD decodes THREE SEPARATE enables
// (RS1=CH1, RS2=CH2, RS3=CTRL), not one chip-select plus a 2-bit internal
// address selecting among four registers. Channel is therefore selected
// by WHICH enable fired (FddStation's own block switch), not by an
// address bit -- only one address bit (the port's bit 0) is left to pick
// data-vs-control/status WITHIN a channel. RS3 ("CTRL") is a separate,
// WRITE-ONLY, whole-chip enable with no per-channel meaning in the PLD at
// all; its real function is unknown, modeled here as a latch with no
// behavior (see writeGlobalControl()).
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
// generator (the user's original Gemini-generated starting point) -- it's
// gone now that nothing drives it bit-by-bit; see git history if real
// async timing is ever needed (e.g. a genuine external serial backend).
// The Mode1/Mode2/Command write SEQUENCE (three writes to the control
// register) is kept, because that's the one part of the register-level
// contract that still matters: real firmware still has to perform it to
// reach the Command write that sets the TX/RX enable bits the byte-level
// shortcut gates on. Mode1/Mode2's own CONTENTS (format/baud) are no
// longer interpreted -- nothing left reads them.
class Wd2123
{
public:
	Wd2123() { reset(); }

	enum StatusFlags : uint8_t {
		kStatTxReady   = 0x01,
		kStatRxReady   = 0x02,
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

	// ch: 0=CH1 (RS1), 1=CH2 (RS2). ctrlSel: the port's own bit 0 (the one
	// address bit the PLD leaves undecoded within each 32-port block) --
	// false selects the data register, true selects control/status. See
	// class comment: this split is inferred, not confirmed.
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
		c.ctrlWriteStep = 0; // reading status resets the Mode1/Mode2/Command sequence, same as the generated model
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
			handleControlWrite(c, value);
		}
	}

	// RS3 -- see class comment. No confirmed behavior; stored only, for
	// debug visibility, until real firmware traffic defines it.
	void writeGlobalControl(uint8_t value) { globalControl_ = value; }
	uint8_t peekGlobalControl() const { return globalControl_; }

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
		uint8_t ctrlWriteStep = 0; // 0/1=expect Mode1/Mode2 (contents unused), 2=expect Command

		uint8_t rxHoldingReg = 0;
		std::vector<uint8_t> rxFifo;
		std::vector<uint8_t> txFifo;
	};

	Channel channels_[2];
	uint8_t globalControl_ = 0;

	// Mode1 -> Mode2 -> Command write sequence -- see class comment on why
	// only the Command write (the third one, which sets commandReg) still
	// has any behavioral effect.
	void handleControlWrite(Channel& c, uint8_t val)
	{
		if (c.ctrlWriteStep < 2) {
			c.ctrlWriteStep++;
			return;
		}
		c.commandReg = val;
		if (val & 0x10) c.status &= ~(kStatParityErr | kStatOverrunErr | kStatFrameErr);
		if (val & 0x40) c.ctrlWriteStep = 0; // internal reset -- re-arm the Mode1/Mode2/Command sequence
	}
};
