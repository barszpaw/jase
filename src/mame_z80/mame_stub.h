// Minimal, purpose-built stand-in for the pieces of MAME's "emu.h" core
// framework that src/devices/cpu/z80/z80.cpp (BSD-3-Clause, Juergen
// Buchmueller et al.) needs to compile and run standalone, outside the
// real MAME device/scheduler/memory-space machinery. NOT a general MAME
// emulation layer -- scoped exactly to what z80.cpp/z80.h/z80.hxx
// reference, nothing more. Real MAME's own device_t/cpu_device provide
// save-states, a debugger state table, daisy-chained peripheral IRQ
// routing, wait-state-aware memory access objects, and device validity
// checking; FDD3000's station has none of that (no daisy chain, no real
// /WAIT line asserted by anything), so every one of those is stubbed to
// either a true no-op or the simplest behavior consistent with "no wait
// states, one flat 64K address space, no debugger attached".
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cassert>
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include <functional>
#include "mame_attotime.h"

using u8 = uint8_t;  using u16 = uint16_t; using u32 = uint32_t; using u64 = uint64_t;
using s8 = int8_t;   using s16 = int16_t;  using s32 = int32_t;  using s64 = int64_t;

// MAME's own register-pair union (emucore.h) -- little-endian layout to
// match ENDIANNESS_LITTLE below; z80.h/z80.cpp access both .w (full
// 16-bit) and .b.l/.b.h (low/high byte) views of the same storage.
union PAIR16
{
	u16 w;
	struct { u8 l, h; } b;
};

#define ATTR_COLD
#define DECLARE_DEVICE_TYPE(name, cls)
#define DEFINE_DEVICE_TYPE(name, cls, shortname, fullname)
#define NAME(x) x
#define LOGMASKED(mask, ...) ((void)0)
#define BIT(x, n) (((x) >> (n)) & 1)

using offs_t = uint32_t;

// MAME's bit-manipulation helpers (src/osd/eminline.h / bitswap.h) --
// trivial, self-contained reimplementations, not stubs (wd_fdc.cpp's own
// shift-register bit-order tricks genuinely depend on these computing
// the right values, unlike the no-op debugger/save-state stubs above).
template <typename T>
constexpr T make_bitmask(int bits)
{
	return bits >= (int)(sizeof(T) * 8) ? ~T(0) : (T(1) << bits) - 1;
}

template <unsigned NumBits, typename T, typename... Bits>
constexpr T bitswap(T val, Bits... bits)
{
	T result = 0;
	int positions[] = { (int)bits... };
	int n = (int)(sizeof...(bits));
	for (int i = 0; i < n; i++)
		result |= ((val >> positions[i]) & 1) << (n - 1 - i);
	return result;
}

// Constants real MAME derives from its own address-space/input-line
// enums -- only the numeric identity matters here, nothing reads these
// as real array indices into anything of ours.
constexpr int ENDIANNESS_LITTLE = 0;
constexpr int AS_PROGRAM = 0;
constexpr int AS_OPCODES = 1;
constexpr int AS_IO = 2;
constexpr int INPUT_LINE_NMI = 100;
constexpr int INPUT_LINE_IRQ0 = 0;
constexpr int ASSERT_LINE = 1;
constexpr int CLEAR_LINE = 0;
constexpr int STATE_GENPC = 1000;
constexpr int STATE_GENPCBASE = 1001;
constexpr int STATE_GENFLAGS = 1002;

inline void osd_printf_error(const char *fmt, ...) {}
[[noreturn]] inline void fatalerror(const char *fmt, ...) { std::abort(); }
inline void logerror(const char *fmt, ...) {}

// MAME's debugger hooks -- no debugger is attached in this standalone
// build, so every one of these is the "nothing is watching" case. Real
// MAME gates a handful of extra bookkeeping branches in z80.hxx behind
// debugger_enabled(); keeping it false just skips that bookkeeping.
inline bool debugger_enabled() { return false; }
inline void debugger_wait_hook() {}
inline void debugger_instruction_hook(u16) {}
inline int standard_irq_callback(int, u16) { return 0; }
struct FakeDebugger { void interrupt_hook(int, int) {} };
struct device_execute_interface
{
	struct DeviceRef { FakeDebugger *debug() { static FakeDebugger d; return &d; } };
	static DeviceRef device() { return DeviceRef{}; }
};
inline std::string string_format(const char *fmt, ...)
{
	char buf[256];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return std::string(buf);
}

// device_type / machine_config / address_space_config -- real MAME's are
// elaborate factory/registry types; here they're just inert tags passed
// through constructors we never use for actual construction logic.
using device_type = int;
constexpr device_type Z80 = 1;
constexpr device_type WD1772 = 2;
struct machine_config {};
struct address_space_config
{
	address_space_config(const char *, int /*endianness*/, int /*datawidth*/, int /*addrwidth*/, int /*addrshift*/ = 0) {}
};
using space_config_vector = std::vector<std::pair<int, const address_space_config *>>;

namespace util { class disasm_interface {}; }
class validity_checker {};
struct device_state_entry
{
	int idx = 0;
	int index() const { return idx; }
};

// Every one of these fires on a real bus-observation line (refresh/M1,
// halt, bus-ack, IRQ-ack, "no memory request" refresh activity) that
// nothing in the FDD3000 station glue needs to watch -- isunset()
// returning true keeps z80.cpp/z80.hxx on their "nobody's listening,
// skip the extra work" paths, and operator() is a no-op for the few
// call sites (halt_cb) that fire unconditionally anyway.
struct NullCallback
{
	template <typename T> NullCallback(T &) {}
	bool isunset() const { return true; }
	template <typename... Args> void operator()(Args &&...) {}
	NullCallback &bind() { return *this; } // real MAME: devcb_write*::bind() for driver-side wiring; nothing wires these here
};
using devcb_write_line = NullCallback;
using devcb_write8 = NullCallback;

// Read-direction callback (enmf_rd_callback() on wd_fdc_device_base) --
// a different shape than the write callbacks above: constructible with
// an optional default-return value, and called with NO arguments,
// returning the "read" value instead of accepting one. Nothing wires a
// real ENMF line into this station, so the stubbed read is always 0.
struct NullReadCallback
{
	template <typename T> NullReadCallback(T &) {}
	template <typename T> NullReadCallback(T &, int) {}
	bool isunset() const { return true; }
	int operator()() const { return 0; }
	NullReadCallback &bind() { return *this; }
};
using devcb_read_line = NullReadCallback;

// The actual Z80<->station wiring -- same global-callback shape Z80free
// already uses elsewhere in this codebase (one CPU instance per process,
// see Fdd3000.cpp's own s_activeForZ80free comment for why that's fine
// here too). Implemented in z80mame_wrapper.cpp, pointing at the SAME
// station I/O decode fdd3000_station/main.cpp already has for Z80free.
extern "C" {
	uint8_t mamez80_mem_read(uint16_t addr);
	void    mamez80_mem_write(uint16_t addr, uint8_t value);
	uint8_t mamez80_io_read(uint16_t port);
	void    mamez80_io_write(uint16_t port, uint8_t value);
}

// Stand-ins for MAME's memory_access<...>::cache/::specific templates --
// real ones negotiate wait-states/bus contention through an address
// space; FDD3000's station asserts no /WAIT line at all, so every access
// just completes immediately against the plain callbacks above.
struct MameMemAccess
{
	u8 read_byte(u16 addr) { return mamez80_mem_read(addr); }
	u8 read_interruptible(u16 addr) { return mamez80_mem_read(addr); }
	void write_interruptible(u16 addr, u8 v) { mamez80_mem_write(addr, v); }
};
struct MameIoAccess
{
	u8 read_interruptible(u16 port) { return mamez80_io_read(port); }
	void write_interruptible(u16 port, u8 v) { mamez80_io_write(port, v); }
};

// A no-op "address space handle" -- real MAME's space(id) returns a live
// address_space to bind a cache/specific accessor against; since
// MameMemAccess/MameIoAccess already talk straight to the global
// callbacks with no setup step needed, binding is just a no-op.
struct FakeSpace
{
	template <typename T> void cache(T &) {}
	template <typename T> void specific(T &) {}
};

// Chainable no-op matching state_add(...)'s real fluent interface
// (.callimport().noshow()...) -- the debugger/save-state table this
// would normally populate is never consulted by anything we drive.
struct FakeStateEntry
{
	FakeStateEntry &callimport() { return *this; }
	FakeStateEntry &callexport() { return *this; }
	FakeStateEntry &noshow() { return *this; }
	FakeStateEntry &formatstr(const char *) { return *this; }
	FakeStateEntry &mask(u64) { return *this; }
};

class device_t; // forward decl, defined below

// Minimal z80_daisy_chain_interface -- FDD3000's station has no daisy-
// chained Z80 peripheral chips (PIO/CTC/SIO), so "no chain present" is
// simply always true, same as a Z80 wired with nothing on its IEI/IEO
// lines on real hardware. device_z80daisy_interface itself is never
// instantiated here -- it only needs to exist as a pointer type for
// daisy_get_irq_device()'s (always-null) return value to typecheck.
struct device_z80daisy_interface { int z80daisy_irq_ack() { return 0; } };
struct z80_daisy_chain_interface
{
	z80_daisy_chain_interface(const machine_config &, device_t &) {}
	bool daisy_chain_present() const { return false; }
	int daisy_update_irq_state() { return 0; }
	device_z80daisy_interface *daisy_get_irq_device() { return nullptr; }
	void daisy_call_reti_device() {}
};

// device_t / cpu_device -- the actual stub base z80_device derives from.
// Provides exactly the member functions z80.cpp/z80.h call unqualified
// (tag(), space(), save_item(), state_add(), set_icountptr(),
// has_space()/has_configured_map(), total_cycles(),
// access_to_be_redone()) as no-ops or the simplest value consistent with
// "one flat address space, no wait states, no debugger."
// Minimal timer/scheduler stand-in -- real MAME's device_scheduler runs a
// priority queue of emu_timer objects driven by the CPU cores' own
// cycle-by-cycle progress; nothing here needs that generality. The
// station's own driving loop (fdd3000_station/main.cpp) already advances
// a single notion of "simulated time" once per step (it has to, to pace
// itself to a real clock rate) -- mame_sched::advance()/service() just
// hook INTO that existing loop rather than running a separate one.
// `inline` globals (C++17) avoid needing a dedicated .cpp for a single
// header-only stub shared by two translation units (z80.cpp, wd_fdc.cpp).
class emu_timer
{
public:
	void adjust(attotime delay, int param = 0);
	bool enabled() const { return m_armed; }
	attotime m_expiry;
	int m_param = 0;
	bool m_armed = false;
	std::function<void(int)> m_callback;
};

namespace mame_sched {
	// Function-local statics (Meyer's singleton), NOT plain `inline`
	// globals -- found live, 2026-11-08, via a long READ SECTOR
	// investigation: `static Fdd35Machine g_machine;` in main.cpp is
	// ITSELF a global requiring dynamic initialization (it owns a
	// wd1772_device, whose constructor calls timer_alloc()), and C++17
	// gives NO guarantee about initialization order between globals in
	// DIFFERENT translation units (the classic "static initialization
	// order fiasco"). With `g_now`/`g_timers` as plain `inline`
	// variables, if g_machine's constructor chain ran BEFORE g_now/
	// g_timers's own (non-trivial in C++17 -- std::vector's default
	// ctor isn't constexpr pre-C++20) dynamic initialization, the
	// timers pushed during g_machine's construction got silently wiped
	// the instant g_timers was later (re)constructed empty -- `service()`
	// then saw an empty vector forever, no timer ever fired, READ
	// SECTOR's live_run() state machine never started. This bug was
	// invisible in every standalone test (test_wd1772.cpp etc.) because
	// those construct MameWd1772 as a LOCAL variable inside main() --
	// i.e. well after all global/static initialization has already run,
	// never hitting the ordering issue at all. Function-local statics
	// are guaranteed initialized on first use, independent of any
	// cross-TU global ordering, which is immune to this by construction.
	inline attotime &g_now() { static attotime t; return t; }
	inline std::vector<emu_timer *> &g_timers() { static std::vector<emu_timer *> v; return v; }
	inline void advance(attotime by) { g_now() += by; }
	inline void service()
	{
		for (emu_timer *t : g_timers()) {
			if (t->m_armed && g_now() >= t->m_expiry) {
				t->m_armed = false;
				t->m_callback(t->m_param);
			}
		}
	}
}

inline void emu_timer::adjust(attotime delay, int param)
{
	m_expiry = mame_sched::g_now() + delay;
	m_param = param;
	m_armed = !delay.is_never();
}

#define FUNC(x) &x
#define TIMER_CALLBACK_MEMBER(name) void name(int param)

struct FakeMachine {
	attotime time() const { return mame_sched::g_now(); }
	bool side_effects_disabled() const { return false; } // no debugger memory-peek path here
};

class device_t
{
public:
	device_t(const machine_config &, device_type, const char *tag, device_t *owner, u32 clock)
		: m_tag(tag), m_clockHz(clock) {}
	virtual ~device_t() = default;
	const char *tag() const { return m_tag; }
	FakeSpace space(int) { return FakeSpace{}; }
	template <typename... Args> void save_item(Args &&...) {}
	template <typename... Args> FakeStateEntry state_add(Args &&...) { return FakeStateEntry{}; }
	void set_icountptr(int &) {}
	bool has_space(int) const { return false; }
	bool has_configured_map(int) const { return false; }
	u64 total_cycles() const { return 1; }
	bool access_to_be_redone() const { return false; }
	virtual void device_validity_check(validity_checker &) const {}
	virtual void device_start() {}
	virtual void device_reset() {}
	virtual void device_pre_save() {}
	virtual void device_post_load() {}
	virtual void state_import(const device_state_entry &) {}
	virtual void state_export(const device_state_entry &) {}
	virtual void state_string_export(const device_state_entry &, std::string &) const {}

	FakeMachine machine() const { return FakeMachine{}; }
	attotime clocks_to_attotime(uint64_t clocks) const
	{
		return attotime::from_double(m_clockHz > 0 ? (double)clocks / (double)m_clockHz : 0.0);
	}
	template <typename C>
	emu_timer *timer_alloc(void (C::*method)(int), C *obj)
	{
		emu_timer *t = new emu_timer(); // one-time allocation per device lifetime, intentionally never freed
		t->m_callback = [obj, method](int param) { (obj->*method)(param); };
		mame_sched::g_timers().push_back(t);
		return t;
	}
private:
	const char *m_tag;
	u32 m_clockHz;
};

class device_memory_interface
{
public:
	using space_config_vector = ::space_config_vector;
	virtual space_config_vector memory_space_config() const = 0;
	virtual ~device_memory_interface() = default;
};

class cpu_device : public device_t, public device_memory_interface
{
public:
	cpu_device(const machine_config &mconfig, device_type type, const char *tag, device_t *owner, u32 clock)
		: device_t(mconfig, type, tag, owner, clock) {}
	virtual void execute_run() = 0;
	virtual void execute_set_input(int inputnum, int state) = 0;
	virtual bool cpu_is_interruptible() const { return true; }
	virtual u32 execute_min_cycles() const noexcept { return 1; }
	virtual u32 execute_max_cycles() const noexcept { return 16; }
	virtual u32 execute_default_irq_vector(int) const noexcept { return 0xff; }
	virtual bool execute_input_edge_triggered(int) const noexcept { return false; }
};
