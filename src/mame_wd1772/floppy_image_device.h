// Minimal stand-in for MAME's real floppy_image_device (src/devices/
// imagedev/floppy.h/.cpp -- real disk insert/eject, physical geometry,
// MAME's own format-reader framework) -- scoped to EXACTLY the 14
// methods wd_fdc.cpp actually calls on a floppy_image_device* (grepped
// directly from its source, see mfm_track_encoder.h's own comment for
// the broader rationale). Backed by a single pre-synthesized MfmTrack
// (see mfm_track_encoder.h) rather than real flux/geometry -- one
// instance covers one (track,side) at a time, reloaded by the station
// glue whenever the WD1772's own track/side registers change, the same
// granularity real media switches track under a moving head.
//
// write_hooked confirmed false for real wd1772 (ready_hooked=false in
// its own MAME constructor, see project memory) -- so ready_r()/the
// ready callback are genuinely dead code for THIS chip; implemented
// anyway for API completeness but never exercised. Index pulses DO
// matter (seek/read logic can wait on one) -- pump() fires one every
// simulated revolution.
#pragma once
#include <functional>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "mame_attotime.h"
#include "mame_stub.h" // mame_sched::g_now
#include "mfm_track_encoder.h"

class floppy_image_device
{
public:
	using IndexFn = std::function<void(floppy_image_device *, int)>;
	using ReadyFn = std::function<void(floppy_image_device *, int)>;

	// Real MAME's index_pulse_cb/ready_cb are named delegate types
	// constructed either empty (clearing a previous hookup) or bound to
	// a member function -- same two call shapes wd_fdc.cpp's
	// set_floppy() uses (see its own comment).
	struct index_pulse_cb {
		index_pulse_cb() = default;
		template <typename C>
		index_pulse_cb(void (C::*method)(floppy_image_device *, int), C *obj)
			: fn([obj, method](floppy_image_device *f, int s) { (obj->*method)(f, s); }) {}
		IndexFn fn;
	};
	struct ready_cb {
		ready_cb() = default;
		template <typename C>
		ready_cb(void (C::*method)(floppy_image_device *, int), C *obj)
			: fn([obj, method](floppy_image_device *f, int s) { (obj->*method)(f, s); }) {}
		ReadyFn fn;
	};

	void setup_index_pulse_cb(index_pulse_cb cb) { m_indexCb = cb.fn; }
	void setup_ready_cb(ready_cb cb) { m_readyCb = cb.fn; }

	// Station glue calls this whenever the encoded track changes (track/
	// side register write) -- replaces the loaded flux data wholesale,
	// same as a real head seeking to a new track.
	void loadTrack(const MfmTrack &track) { m_track = track; m_lastReturnedIdx = -1; }

	attotime get_next_transition(const attotime &from)
	{
		if (!m_track.valid || m_track.transitions.empty()) return attotime::never;
		// Linear scan is fine here -- tracks are a few thousand
		// transitions, called a handful of times per emulated byte, not
		// a hot path that needs a binary search.
		attotime wrapBase = attotime::zero;
		attotime f = from;
		// Unwrap `from` into [0, revolutionTime) plus a whole-revolution
		// base, so the search below only ever deals with one revolution's
		// worth of transitions -- the disk is continuously spinning, so a
		// real head sees the SAME transitions again every revolution.
		if (m_track.revolutionTime.as_double() > 0.0) {
			double rev = m_track.revolutionTime.as_double();
			double rel = f.as_double();
			double whole = std::floor(rel / rev);
			wrapBase = attotime::from_double(whole * rev);
			f = attotime::from_double(rel - whole * rev);
		}
		attotime result = attotime::never;
		for (const attotime &t : m_track.transitions)
			if (t > f) { result = wrapBase + t; break; }
		// Wrapped past the last transition this revolution -- the next
		// one is the first transition of the NEXT revolution.
		if (result.is_never() && !m_track.transitions.empty())
			result = wrapBase + m_track.revolutionTime + m_track.transitions.front();
		return result;
	}

	int idx_r() const { return m_indexLevel; }
	void mon_w(int) {}                 // motor on/off -- always "spinning" here, no spin-up delay modeled
	int ready_r() const { return 0; }  // dead code for wd1772 (ready_hooked=false), see class comment
	void ss_w(int) {}                  // side select -- station glue already picks the right MfmTrack directly
	void stp_w(int) {}                 // step pulse -- station's own track register is the source of truth
	bool wpt_r() const { return false; }
	// Station glue calls this alongside loadTrack() whenever WD1772's own
	// track register changes -- real wd_fdc.cpp polls this during RESTORE
	// (seek to track 0) to know when to stop stepping.
	void setTrackNumber(int t) { m_currentTrackNum = t; }
	// Real hardware's /TRK00 line is active LOW (0 = head at track 0) --
	// wd_fdc.cpp reads it that way: `if(!floppy->trk00_r()) status |=
	// S_TR00`. Found live, 2026-11-08: this used to return the LOGICAL
	// "is at track 0" value (true when at track 0), the opposite
	// polarity -- status bit S_TR00 then NEVER set, even genuinely at
	// track 0, so any boot/driver code polling it (confirmed: FDD35's
	// own boot ROM does exactly this after RESTORE, at 0x3A-0x3E) spun
	// forever. RESTORE itself still "completed" (BUSY cleared normally,
	// unaffected by this bit), which is why this bug was invisible to
	// the standalone RESTORE-then-READ-SECTOR behavioral test -- it only
	// shows up when real firmware polls S_TR00 specifically, as this ROM
	// does and MAME's own generic `wd_fdc.cpp` test rig doesn't.
	bool trk00_r() const { return m_currentTrackNum != 0; }
	void dir_w(int) {}
	// ABSOLUTE time of the next index pulse, NOT a duration -- real
	// MAME's own floppy_image_device::time_next_index() returns an
	// absolute attotime, and live_run() compares it directly against
	// other absolute times (`etime > limit`) with no offset applied.
	// Found live, 2026-11-08: an earlier version returned a relative
	// duration-until-next-index, which made EVERY live_run() call's
	// `limit` parameter read as "a few milliseconds" while `ctime` was
	// already seconds in -- pll_get_next_bit()'s `etime > limit` check
	// then failed on literally the very first bit, every single call, so
	// read_one_bit() always returned "need more time" and the PLL never
	// actually read a single bit. This is why READ SECTOR failed fast
	// (RNF) while RESTORE (which doesn't go through live_run()'s bit
	// decode at all, just trk00_r()/step pulses) worked fine -- the two
	// commands exercise completely different code paths.
	attotime time_next_index() const
	{
		if (m_track.revolutionTime.as_double() <= 0.0) return attotime::never;
		double rev = m_track.revolutionTime.as_double();
		double rel = mame_sched::g_now().as_double();
		double whole = std::floor(rel / rev);
		double untilNext = (whole + 1) * rev - rel;
		return attotime::from_double(rel + untilNext);
	}

	// Write support not modeled yet (same simplification JASE's own
	// Wd1772 already documents: writes land only in DskImage's in-memory
	// copy) -- these are accepted but no-ops.
	void write_start(const attotime &) {}
	void write_end(const attotime &) {}
	void write_flush(const attotime &) {}
	void write_flux_change(const attotime &) {}

	// Called once per station step (alongside mame_sched::service()) --
	// fires a brief index pulse once per simulated revolution. Real
	// hardware's pulse is a few hundred microseconds; the exact width
	// doesn't matter here since wd_fdc.cpp's index_callback() only acts
	// on the rising edge (state=1) and ignores state=0 for this chip.
	void pump()
	{
		if (m_track.revolutionTime.as_double() <= 0.0) return;
		double rev = m_track.revolutionTime.as_double();
		double rel = mame_sched::g_now().as_double();
		long long whole = (long long)std::floor(rel / rev);
		if (whole != m_lastRevolution) {
			m_lastRevolution = whole;
			if (m_indexCb) m_indexCb(this, 1);
			if (m_indexCb) m_indexCb(this, 0);
		}
	}

private:
	MfmTrack m_track;
	IndexFn m_indexCb;
	ReadyFn m_readyCb;
	int m_indexLevel = 0;
	long long m_lastRevolution = -1;
	int m_lastReturnedIdx = -1;
	int m_currentTrackNum = -1;
};
