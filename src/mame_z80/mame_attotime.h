// Minimal stand-in for MAME's real attotime (src/emu/attotime.h, a 96-bit
// seconds+attoseconds fixed-point time type) -- scoped to exactly the
// operations fdc_pll.cpp/wd_fdc.cpp actually use (from_double/from_msec,
// zero/never, is_never(), as_double(), seconds(), +/-/compare, *int,
// /int). A real 96-bit split representation isn't needed here: nothing
// in this station's timing model requires sub-nanosecond precision over
// astronomical time spans the way MAME's real one does (built for
// systems that run for real hours with multiple independent crystals) --
// a plain double of seconds has more than enough precision for
// microsecond-scale floppy bit-cell timing over a session that runs for
// minutes.
#pragma once
#include <cmath>
#include <cstdio>
#include <string>

class attotime
{
public:
	attotime() : m_seconds(0.0) {}
	explicit attotime(double seconds) : m_seconds(seconds) {}

	static const attotime zero;
	static const attotime never;

	static attotime from_double(double seconds) { return attotime(seconds); }
	static attotime from_msec(int64_t msec) { return attotime(msec / 1000.0); }
	static attotime from_usec(int64_t usec) { return attotime(usec / 1000000.0); }
	static attotime from_hz(double hz) { return attotime(hz > 0.0 ? 1.0 / hz : 0.0); }

	bool is_never() const { return m_seconds >= kNeverSeconds; }
	double as_double() const { return m_seconds; }
	// MAME's real seconds()/attoseconds() split always keeps attoseconds
	// non-negative, so a time like -0.3s decomposes as seconds=-1 (floor,
	// not truncation) -- matches the one place wd_fdc.cpp/fdc_pll.cpp
	// actually consult this (a pure sign check on a time delta).
	int64_t seconds() const { return (int64_t)std::floor(m_seconds); }
	std::string to_string() const
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%.9f", m_seconds); // debug-only (logerror is a no-op); never parsed back
		return std::string(buf);
	}

	attotime operator+(const attotime &o) const { return attotime(m_seconds + o.m_seconds); }
	attotime operator-(const attotime &o) const { return attotime(m_seconds - o.m_seconds); }
	attotime operator*(int v) const { return attotime(m_seconds * v); }
	attotime operator/(int v) const { return attotime(m_seconds / v); }
	attotime &operator+=(const attotime &o) { m_seconds += o.m_seconds; return *this; }
	attotime &operator-=(const attotime &o) { m_seconds -= o.m_seconds; return *this; }

	bool operator<(const attotime &o) const { return m_seconds < o.m_seconds; }
	bool operator>(const attotime &o) const { return m_seconds > o.m_seconds; }
	bool operator<=(const attotime &o) const { return m_seconds <= o.m_seconds; }
	bool operator>=(const attotime &o) const { return m_seconds >= o.m_seconds; }
	bool operator==(const attotime &o) const { return m_seconds == o.m_seconds; }
	bool operator!=(const attotime &o) const { return m_seconds != o.m_seconds; }

private:
	static constexpr double kNeverSeconds = 1e17; // sentinel, see is_never()
	double m_seconds;
};

inline attotime operator*(int v, const attotime &t) { return t * v; }
