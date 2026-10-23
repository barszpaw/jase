#pragma once

// The shared IEC bus itself, as a physical abstraction: open-collector
// wired-AND lines, same as the real connector. Two independent
// participants each drive their own view of CLK/DAT (controller =
// SDI_8255/the Z80 side, device = IecDeviceEmulator) and both read back
// the RESOLVED line state -- asserted (electrically pulled low) if
// EITHER side asserts it, released only if BOTH release it -- instead
// of one side reading the other's raw output directly. ATN is only
// ever driven by the controller (real hardware fact: it's always the
// bus master), so there's only one ATN setter.
//
// This replaces IecProtocolDecoder's earlier design, where "us as
// receiver" and "us as talker" were tangled into one class reading/
// writing a raw Port C byte directly -- that's what led to the
// periodic-CLK-pulse stub (see VC1541.md): with no real bus
// abstraction, there was nowhere clean to put a genuine, symmetric
// "device" participant.
class IecBus
{
public:
	void setAtn(bool asserted) { atn_ = asserted; }
	bool atn() const { return atn_; }

	void setControllerClk(bool asserted) { ctrlClk_ = asserted; }
	void setControllerDat(bool asserted) { ctrlDat_ = asserted; }
	void setDeviceClk(bool asserted) { devClk_ = asserted; }
	void setDeviceDat(bool asserted) { devDat_ = asserted; }

	bool clk() const { return ctrlClk_ || devClk_; } // wired-AND
	bool dat() const { return ctrlDat_ || devDat_; }

	// For IECBUS debug reporting -- which side is actually asserting
	// what, not just the resolved result.
	bool controllerClk() const { return ctrlClk_; }
	bool controllerDat() const { return ctrlDat_; }
	bool deviceClk() const { return devClk_; }
	bool deviceDat() const { return devDat_; }

private:
	bool atn_ = false;
	bool ctrlClk_ = false;
	bool ctrlDat_ = false;
	bool devClk_ = false;
	bool devDat_ = false;
};
