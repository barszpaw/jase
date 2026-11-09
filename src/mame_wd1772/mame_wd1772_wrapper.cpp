#include "mame_wd1772_wrapper.h"
#include "wd_fdc.h"
#include "floppy_image_device.h"
#include "mfm_track_encoder.h"
#include "../DskImage.h"

// bitCellSeconds: NOT a free parameter, and the first attempt at pinning
// it down (2026-11-08, "it's clocks_to_attotime(1) = 125ns") was ALSO
// wrong, caught live by instrumenting get_next_transition() and seeing
// consecutive calls land ~1.6-2us apart instead of ~125ns. Re-reading
// digital_pll_t::get_next_bit()'s own loop explains why:
// pll_reset()->set_clock(clocks_to_attotime(1)) does set `period` to one
// wd1772 clock cycle (125ns @ 8MHz), but get_next_bit() doesn't decode a
// bit per call-to-period -- its inner for(;;) loop advances `counter` by
// `increment` (default 128) once per `period`-sized slot and only
// returns once `counter & 0x800` (0x800=2048) goes high, i.e. after
// 2048/128 = 16 slots. So ONE DECODED BIT CELL = 16 wd1772 clock cycles
// = 16/8000000 = 2us, not 1 clock cycle -- which is also exactly the
// real-world standard MFM double-density bit cell (250kbps raw-coded
// rate), so this is the physically correct value, not a coincidence.
// Must stay exactly 16/8000000 unless wd1772_device's own clock argument
// changes too.
static const double kBitCellSeconds = 16.0 / 8000000.0;

// device_start()/device_reset() are protected on wd_fdc_device_base in
// real MAME (normally invoked through its own device-lifecycle manager,
// not called directly) -- same reason the Z80 port needed a public
// subclass, see z80mame_wrapper.cpp's own Z80MameCpu.
class PublicWd1772 : public wd1772_device
{
public:
	using wd1772_device::wd1772_device;
	void publicStart() { device_start(); }
	void publicReset() { device_reset(); }
	uint8_t peekStatus() const { return status_peek(); } // diagnostic read without the INTRQ-clear side effect of status_r()
};

struct MameWd1772::Impl
{
	static machine_config s_mconfig;

	PublicWd1772 dev;
	floppy_image_device floppies[kNumDrives];
	DskImage disks[kNumDrives];
	bool diskLoaded[kNumDrives] = {};

	int selectedDrive = -1;
	bool side = false;

	// What's currently loaded into the active floppy_image_device's MFM
	// track buffer -- tick() refreshes it whenever any of these change,
	// since wd_fdc.cpp's own stepping changes track_r() internally with
	// no direct hook back to this wrapper.
	int loadedDrive = -1;
	int loadedTrack = -1;
	bool loadedSide = false;

	Impl() : dev(s_mconfig, "wd1772", nullptr, 8000000)
	{
		dev.publicStart();
		dev.publicReset();
		dev.dden_w(0); // MFM (not FM) -- matches JASE's own Wd1772 default (density_=true)
	}

	void applyFloppySelection()
	{
		if (selectedDrive < 0 || selectedDrive >= kNumDrives) {
			dev.set_floppy(nullptr);
			return;
		}
		dev.set_floppy(&floppies[selectedDrive]);
	}

	void refreshTrackIfNeeded()
	{
		int track = dev.track_r();
		if (selectedDrive == loadedDrive && track == loadedTrack && side == loadedSide)
			return;
		loadedDrive = selectedDrive;
		loadedTrack = track;
		loadedSide = side;
		if (selectedDrive < 0 || selectedDrive >= kNumDrives || !diskLoaded[selectedDrive]) return;
		floppy_image_device &f = floppies[selectedDrive];
		f.setTrackNumber(track);
		MfmTrack mt = encodeTrackMFM(disks[selectedDrive], track, side ? 1 : 0, kBitCellSeconds);
		f.loadTrack(mt);
	}
};

machine_config MameWd1772::Impl::s_mconfig;

MameWd1772::MameWd1772() : m_impl(new Impl()) {}
MameWd1772::~MameWd1772() { delete m_impl; }

void MameWd1772::reset()
{
	m_impl->dev.publicReset();
}

void MameWd1772::tick(uint32_t stationTstatesAt4MHz)
{
	mame_sched::advance(attotime::from_double((double)stationTstatesAt4MHz / 4000000.0));
	mame_sched::service();
	if (m_impl->selectedDrive >= 0 && m_impl->selectedDrive < kNumDrives)
		m_impl->floppies[m_impl->selectedDrive].pump();
	m_impl->refreshTrackIfNeeded();
}

void MameWd1772::writeCommand(uint8_t value) { m_impl->dev.cmd_w(value); }
void MameWd1772::writeTrackReg(uint8_t value) { m_impl->dev.track_w(value); }
void MameWd1772::writeSectorReg(uint8_t value) { m_impl->dev.sector_w(value); }
void MameWd1772::writeDataReg(uint8_t value) { m_impl->dev.data_w(value); }
uint8_t MameWd1772::readStatus() { return m_impl->dev.status_r(); }
uint8_t MameWd1772::readTrackReg() const { return m_impl->dev.track_r(); }
uint8_t MameWd1772::readSectorReg() const { return m_impl->dev.sector_r(); }
uint8_t MameWd1772::readDataReg() { return m_impl->dev.data_r(); }
uint8_t MameWd1772::peekStatus() const { return m_impl->dev.peekStatus(); }

bool MameWd1772::drq() const { return m_impl->dev.drq_r() != 0; }
bool MameWd1772::intrq() const { return m_impl->dev.intrq_r() != 0; }

void MameWd1772::selectDrive(int index)
{
	m_impl->selectedDrive = (index >= 0 && index < kNumDrives) ? index : -1;
	m_impl->applyFloppySelection();
}

void MameWd1772::setSide(bool sideB) { m_impl->side = sideB; }
void MameWd1772::setDensity(bool doubleDensity) { m_impl->dev.dden_w(doubleDensity ? 0 : 1); } // dden_w: 0=MFM(double), 1=FM(single) -- see wd_fdc.cpp's own dden_w()

bool MameWd1772::insertDisk(int driveIndex, const std::string &path)
{
	if (driveIndex < 0 || driveIndex >= kNumDrives) return false;
	m_impl->diskLoaded[driveIndex] = m_impl->disks[driveIndex].load(path);
	m_impl->loadedDrive = -1; // force a track reload next tick(), even if track/side didn't change
	return m_impl->diskLoaded[driveIndex];
}
