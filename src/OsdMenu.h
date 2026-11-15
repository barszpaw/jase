#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <SDL.h>
#include "Gopher.h"
#include "Tnfs.h"

class MinZX;

// JASE-native on-screen menu/OSD -- inspired by fusex's ui/widget/ (SDL-
// only, own bitmap font drawn straight into the display buffer, a
// paginated directory browser) and pico-spec's OSDMain.cpp, but a fresh
// implementation against the same idea, not a port of either (both GPL).
//
// Owns its own small amount of state (which screen is showing, the file
// browser's cwd/cursor) and draws directly into the same ARGB8888 pixel
// buffer main.cpp hands to SDL_UpdateTexture -- there's no separate
// window/renderer/texture of its own. While open, it owns ALL keyboard
// input and main.cpp skips calling MinZX::update() (see main.cpp) so the
// emulated machine is effectively paused and the last rendered frame
// stays on screen underneath the overlay, exactly like a real emulator's
// pause-menu look.
class OsdMenu
{
public:
	explicit OsdMenu(MinZX* minZX);

	bool isOpen() const { return mode_ != Mode::Closed; }

	// Public entry point for an EXTERNALLY-triggered status message --
	// e.g. main.cpp's unsaved-FDD3000-disk exit guard -- as opposed to
	// setStatus() (private), which is this class's own internal
	// bookkeeping for actions IT just performed (Save/Load/etc).
	void showWarning(const std::string& msg) { setStatus(msg, false); }

	// The settings.json path peripheral/machine config screens read from
	// and "RESET > SAVE CONFIG" writes to -- whatever --settings named,
	// or "settings.json" (the repo-root template/reference file) if the
	// process was started without one. main.cpp calls this once, right
	// after parsing argv, before the main loop. A read-modify-write (not
	// a from-scratch rebuild) -- see saveConfig() -- so this file's own
	// "_description"/"_examples" fields survive every save.
	void setSettingsPath(const std::string& path) { settingsPath_ = path; }

	// F1: opens to the top-level menu if closed, closes outright if
	// already open (same effect as Esc at the top level). Reopening
	// always starts back at the top -- no "resume where I left off".
	void toggle();

	// F5 (main.cpp, only while closed -- see its own comment): jumps
	// straight into STORAGE, skipping TopMenu entirely. Used to be a
	// second "also opens the menu" binding identical to F1 (stale --
	// user, 2026-11-01: "miałby być tylko dla storage"), now genuinely
	// distinct from F1. F5's OTHER job (context-specific action inside
	// an open menu, e.g. "save a copy locally" in the remote file
	// browser) is unrelated and still lives entirely in handleKey().
	void openStorage();

	// One call per SDL_KEYDOWN while isOpen(). Up/Down move the cursor,
	// Enter activates, Esc goes back a level (or closes, from the top).
	void handleKey(SDL_Scancode sc);

	// One call per SDL_TEXTINPUT event while isOpen() -- only acts when a
	// text field (Mode::TextInput) is active; a no-op otherwise. `text` is
	// the UTF-8 chunk SDL hands over (usually one character).
	void handleTextInput(const char* text);

	// Draws the overlay into `pixels` (texW x texH, ARGB8888, same
	// layout main.cpp's own pixel buffer uses) on top of whatever is
	// already there. No-op if closed -- call unconditionally from
	// main.cpp's render step, same spirit as debugServer.poll().
	// Non-const: ticks the transient status-message countdown.
	void render(uint8_t* pixels, int texW, int texH);

	// Persistent on-screen indicators (2026-11-14, user-directed) -- a
	// disk-activity LED (bottom-right, lit while Fdd3000::
	// isRecentlyActive()) and/or a short FPS readout, each independently
	// toggleable (OPTIONS screen rows below, or --show-indicators/
	// --show-fps on the command line). Unlike render(), this draws EVERY
	// frame regardless of isOpen() -- real disk activity lights/FPS
	// counters are meant to be visible with the menu closed, same as
	// real Fuse's own always-on indicators. `fps` is the caller's own
	// current estimate (main.cpp already computes one for its console
	// print every ~2s; this just redraws the last value every frame).
	void renderHud(uint8_t* pixels, int texW, int texH, double fps);
	void setShowIndicators(bool on) { showIndicators_ = on; }
	void setShowFps(bool on) { showFps_ = on; }

private:
	enum class Mode { Closed, TopMenu, StorageMenu, FileBrowser, NetworkMenu, RemoteList, ProtoPicker, TextInput, RemoteBrowser, ZxKeyboard,
	                   OptionsMenu, MachinePicker, ResetMenu, PeriphDetail };
	enum class Action { None, LoadSnapshot, LoadTape,
	                     LoadMultiface1Rom, LoadDivideRom, LoadDivideDisk, LoadDandanatorRom, LoadSdi8255Rom,
	                     LoadFdd3000Rom, LoadFdd3000StationRom, LoadFdd3000Disk };
	enum class Proto { Gopher, Tnfs };

	MinZX* minZX_;
	Mode mode_ = Mode::Closed;

	// Snapshot of `pixels` taken the instant the menu opens (same idea as
	// pico-spec's VIDEO::SaveRect.save/restore) and restored at the start
	// of every render() call before drawing the current screen's box on
	// top. Without this, switching between screens of different sizes
	// (e.g. TopMenu's tall box -> NetworkMenu's 2-row one) left the
	// previous, larger box's pixels sitting underneath the new, smaller
	// one -- `pixels` is one persistent buffer across frames (that's what
	// keeps the paused Spectrum picture visible at all), so nothing else
	// ever clears the region a shrinking box vacates.
	std::vector<uint8_t> background_;
	bool haveBackground_ = false;

	// Top-level categories mirror pico-spec's own real OSD structure
	// (messages.h's MENU_MAIN: Volume/Storage/Audio/Video/Machine/Reset/
	// Options/Debug/Hardware/Network/ZX Keyboard/Help/About -- read for
	// this category breakdown only, no code copied, same policy as
	// everywhere else in this GPL-adjacent reading) -- kept only the
	// categories that correspond to something JASE actually has: no
	// Volume/Audio/Video (no mixer), Debug/Hardware (ESP32-specific, no
	// JASE analog), Help/About (not written yet). "Machine" is folded
	// into Options (see below) rather than kept as its own top entry,
	// since JASE's machine type is fixed at construction (MinZX::
	// setMachine() must run before init()) -- unlike pico-spec, which
	// really can hot-swap architecture, so there's nothing live for a
	// standalone "Machine" entry to DO here beyond what Options already
	// covers as a next-boot preference. ZX Keyboard IS kept, as a
	// read-only Symbol Shift legend -- even with a real SDL keyboard,
	// finding which PC key types which punctuation symbol isn't obvious
	// (Pawel's point: the keyboard view helps locate multi-symbol keys).
	//
	// STORAGE folds in every disk/ROM-cartridge peripheral as its own
	// sub-row, matching pico-spec's real Storage submenu shape exactly
	// (Tape>/Betadisk>/esxDOS>/MB-02+>/Z-Controller>/IDE-HDD>/Snapshot>
	// -- one row per storage-capable piece of hardware, not a separate
	// top-level "Peripherals" category). FDD3000 specifically has no
	// pico-spec analog at all (it's Fuse's own peripheral, not
	// pico-spec's) -- its drive A-D row shape instead matches Fuse's own
	// real `ui/options.dat` entries for it (Checkbox "FDD(3)000
	// interface" + per-drive Combo rows), found while reading Fuse's GPL
	// patch for the emulation itself (see Fdd3000.h) -- same "facts not
	// code" policy once again.
	int topSel_ = 0;
	static const int kTopItemCount = 6; // Storage, Options, Reset, Network, ZX Keyboard, Close

	int storageSel_ = 0;
	static const int kStorageItemCount = 9; // Load Snapshot/Tape, Save SNA/Z80, then one row per disk/cartridge peripheral

	// ---- OPTIONS (next-boot machine preference + live-applicable toggles) --

	// Preferred machine/turbosound are next-BOOT preferences, never
	// applied live (MinZX::setMachine()/setTurboSound() must run before
	// init() -- see their own comments) -- shadowed here, seeded from
	// MinZX's actual current state at construction (so a value passed
	// via --machine/--turbosound already shows correctly before the user
	// ever touches this menu), only ever changed by the picker/toggle
	// below, and written out by saveConfig() -- never applied to the
	// running machine. Timex Hires/PC Keys/Tape Fastload DO have live
	// setters (MinZX::setTimexVideo()/setPcKeys()/setTapeFastload(), see
	// their own comments -- callable any time) and need no shadow: the
	// Options screen toggles them immediately AND reads them back
	// straight from MinZX for both display and saveConfig().
	int optionsSel_ = 0;
	static const int kOptionsItemCount = 7; // Preferred Machine, Turbosound, Timex Hires, PC Keys, Tape Fastload, Show Indicators, Show FPS
	int prefMachineIdx_ = 0; // 0=48k, 1=tc2048, 2=128k
	bool prefTurboSound_ = false;
	int machinePickerSel_ = 0;

	// ---- persistent HUD (2026-11-14) -- see renderHud()'s own comment ----
	bool showIndicators_ = true;  // disk-activity LED, on by default (non-intrusive, matches real hardware)
	bool showFps_ = false;        // short FPS readout, off by default (debug/performance info, not everyone wants it)

	int resetSel_ = 0;
	static const int kResetItemCount = 2; // Reset, Save Config

	// ---- peripheral detail screens (reached from Storage or Network) ----

	// One id per peripheral with a detail screen; ZiFi (5) is reached
	// from NETWORK, not STORAGE (it's not a disk/cartridge device -- see
	// class comment), everything else (0-4) from STORAGE.
	enum class PeriphId { Multiface1, DivIde, Dandanator, Sdi8255, Fdd3000, ZiFi };
	PeriphId detailPeriphId_ = PeriphId::Multiface1;
	int periphDetailSel_ = 0;
	int fdd3000DriveSlot_ = 0; // which of A-D the file browser is currently targeting, see openPeriphFileBrowser()

	void openPeriphDetail(PeriphId id);
	int periphDetailRowCount(PeriphId id) const;
	void periphDetailRows(PeriphId id, std::vector<std::string>& rows) const;
	void activatePeriphDetailRow(PeriphId id, int row);
	void openPeriphFileBrowser(Action action, int driveSlot = 0);

	void activateOptions();   // Enter in OptionsMenu
	void activateMachinePicker(); // Enter in MachinePicker
	void activateReset();     // Enter in ResetMenu

	// Settings.json path (see setSettingsPath()) and the read-modify-
	// write that persists ALL current config into it: machine.{machine,
	// turbosound,timexHires,pcKeys}, media.tapeFastload, and one
	// peripherals.<name> object per peripheral (enabled + whatever
	// paths/bridge targets it has -- read back from each peripheral's
	// own getRomPath()/getDiskPath()/getBridgeHost()/getBridgePort(), see
	// those classes). A plain nlohmann::json read+merge+dump(2), not a
	// from-scratch rebuild, so unrelated keys (_description, _examples)
	// survive untouched. Sets status_ with the result either way.
	std::string settingsPath_ = "settings.json";
	void saveConfig();

	Action browseAction_ = Action::None;
	std::string cwd_ = ".";
	struct Entry { std::string name; bool isDir; };
	std::vector<Entry> entries_;
	int fileSel_ = 0;
	int fileScroll_ = 0;
	static const int kFileRows = 10; // visible rows in the browser box -- fits texH=240 at kFontScale=2

	std::string status_;
	bool statusOk_ = false;
	int statusFramesLeft_ = 0;

	// bitluni's Font6x8 (see OsdFont.h), scaled by this integer factor
	// when drawn (drawText()/renderSimpleList() both honour it). Kept as
	// a single knob rather than a hardcoded 1x/2x choice.
	static const int kFontScale = 1;

	// Every path that closes the menu (Esc from the top, a successful
	// load, "CLOSE MENU") goes through this instead of a raw mode_
	// assignment, so haveBackground_ is reliably invalidated -- see its
	// comment above for why a stale snapshot would otherwise leak into
	// the next time the menu opens.
	void close() { mode_ = Mode::Closed; haveBackground_ = false; }

	void openBrowser(Action action);
	void rescan();
	void activateTop();
	void activateStorage(); // Enter in StorageMenu
	// "ZX KEYBOARD" row -- not pico-spec's bitmap keyboard overlay image
	// (that's a copyrighted/hand-drawn asset, not something to copy), but
	// a text table of the same information that actually matters day to
	// day: which PC key, held with (left) Alt == Symbol Shift on this
	// keyboard matrix (see minzx.cpp's keyTable), types which punctuation
	// symbol. Verified against the official "Recreated ZX Spectrum
	// Keyboard Technical Document" (QWERTY Key List, Symbol Shift column).
	void renderZxKeyboard(uint8_t* pixels, int texW, int texH) const;
	void activateFile();
	bool matchesFilter(const std::string& name) const;
	void doSave(bool asZ80);
	void setStatus(const std::string& s, bool ok);

	void drawText(uint8_t* pixels, int texW, int texH, int x, int y, const std::string& text, uint32_t argb) const;
	void fillRect(uint8_t* pixels, int texW, int texH, int x, int y, int w, int h, uint32_t argb) const;
	// mago1 (OsdFont.h) is PROPORTIONAL, not fixed-width like the earlier
	// bitluni font -- every text layout that used to divide available
	// pixels by a constant per-char step now has to measure real glyph
	// widths instead. glyphWidth() is the one place that knows the font's
	// own [-> width] lookup (with the same out-of-range '?' fallback
	// drawText() uses); textWidth()/truncateToWidth() are built on it.
	int glyphWidth(unsigned char ch) const;
	int textWidth(const std::string& text) const; // includes the 1px inter-glyph gap drawText() advances by
	std::string truncateToWidth(const std::string& s, int maxWidthPx) const;
	// Shared renderer for the simple "N items, pick one" screens
	// (NetworkMenu/RemoteList/ProtoPicker/TextInput) -- no scrolling,
	// sized to fit all of `items` (they're expected to stay short).
	// selIndex < 0 draws no row highlighted (the TextInput field-edit view).
	// statusLine, if non-empty, is appended as one extra row in green/red
	// (statusOk) instead of black -- same convention as the top menu's
	// transient save/load feedback.
	void renderSimpleList(uint8_t* pixels, int texW, int texH, const char* title,
	                       const std::vector<std::string>& items, int selIndex,
	                       const std::string& statusLine = "", bool statusOk = true) const;

	// ---- NETWORK -> REMOTE (Gopher/TNFS), matching pico-spec's OSD shape ----

	// One saved connection (host/port/protocol/start path/display alias).
	// Persisted as a tab-separated line in ~/.jase_remotes.tsv -- plain
	// text, no NVS/flash involved (unlike pico-spec's Config::Remote).
	struct RemoteConfig { int proto; std::string host; uint16_t port; std::string path; std::string alias; };
	std::vector<RemoteConfig> remotes_;
	int netSel_ = 0;     // NetworkMenu cursor (only one row for now: "REMOTE")
	int remoteSel_ = 0;  // RemoteList cursor
	int protoSel_ = 0;   // ProtoPicker cursor

	void loadRemotes();
	void saveRemotes() const;
	int networkMenuItemCount() const { return 2; } // "REMOTE", "ZIFI"

	// Re-dials ZiFi's last --zifi target -- lets killing/restarting the
	// bridge or virtual_esp01 mock on the other end be recovered from
	// without restarting JASE. Sets status_/statusOk_ with the result.
	// Exposed both as ZiFi's own detail-screen "RECONNECT" row and kept
	// as a standalone method (not folded into activatePeriphDetailRow())
	// since it's also reachable with zero setup, unlike every other
	// peripheral's rows.
	void reconnectZiFi();

	// Add-Remote wizard: ProtoPicker picks pendingProto_, then TextInput
	// walks Host -> Port -> Path -> Alias (pendingField_ tracks which),
	// storing into pending*_ as each is confirmed; Alias confirms -> saved
	// to remotes_ + persisted -> back to RemoteList. Esc at any point
	// (including from ProtoPicker) discards the whole in-progress entry.
	enum class TextField { Host, Port, Path, Alias };
	TextField pendingField_ = TextField::Host;
	Proto pendingProto_ = Proto::Gopher;
	std::string pendingHost_, pendingPort_, pendingPath_, pendingAlias_;
	std::string textBuf_;
	std::string textLabel_;
	void beginAddRemote();
	void advanceTextField(); // called on Enter in TextInput mode
	void startTextField(TextField f);

	// Mode::TextInput is also reused for every peripheral/ZiFi "HOST:PORT"
	// field (Multiply's bridge, SDI8255's bridge, ZiFi itself) -- a single
	// one-shot field (not the 4-step Add-Remote wizard above), entered
	// via startHostPortField() and completed by the SAME advanceTextField()
	// Enter handler, branching on textPurpose_ at the top. Esc from this
	// purpose returns to PeriphDetail, not RemoteList (see handleKey()).
	enum class TextPurpose { AddRemote, PeriphHostPort };
	TextPurpose textPurpose_ = TextPurpose::AddRemote;
	void startHostPortField(const std::string& label, const std::string& initial);

	// The live connection, if any -- at most one of these is meaningful
	// at a time, selected by activeProto_. Concrete members (not a
	// polymorphic base + pointers): there are exactly two protocols in
	// scope and both are cheap to keep around unconditionally.
	Proto activeProto_ = Proto::Gopher;
	Gopher gopher_;
	Tnfs tnfs_;
	bool connectActive(const RemoteConfig& rc);
	bool remoteList(std::vector<Entry>& out);
	bool remoteCwd(const std::string& name); // "" / ".." handled by the active client
	bool remoteCwdUp();
	std::string remoteCwdPath() const;
	// Downloads `name` to a temp file, dispatches to the right FileMgr
	// loader by extension (.sna/.z80 -> snapshot, .tap/.tzx -> tape), same
	// convention as main.cpp's positional-argument dispatch. Returns false
	// (with a status message already set) for an unrecognized extension
	// or a download/load failure.
	bool remoteLoadFile(const std::string& name);
	void remoteDisconnect();

	void activateNetwork();  // Enter in NetworkMenu
	void activateRemote();   // Enter in RemoteList
	void activateProto();    // Enter in ProtoPicker
	void activateRemoteFile(); // Enter in RemoteBrowser

	// F8 in RemoteList: delete the selected saved connection (no
	// confirmation dialog -- re-adding one is cheap, unlike pico-spec's
	// msgDialog Yes/No, which this doesn't have a widget for).
	void forgetSelectedRemote();
	// F2 in RemoteBrowser: re-fetch the current directory's listing.
	void reloadRemoteBrowser();
	// F5 in RemoteBrowser: download the selected FILE (not a directory)
	// to JASE's own working directory under its real filename, without
	// loading it -- pico-spec's "save a copy locally" action, distinct
	// from Enter (which loads it straight into the emulator).
	void saveSelectedRemoteFile();
};
