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
	explicit OsdMenu(MinZX* minZX) : minZX_(minZX) {}

	bool isOpen() const { return mode_ != Mode::Closed; }

	// F5: opens to the top-level menu if closed, closes outright if
	// already open (same effect as Esc at the top level). Reopening
	// always starts back at the top -- no "resume where I left off".
	void toggle();

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

private:
	enum class Mode { Closed, TopMenu, FileBrowser, NetworkMenu, RemoteList, ProtoPicker, TextInput, RemoteBrowser };
	enum class Action { None, LoadSnapshot, LoadTape };
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

	int topSel_ = 0;
	static const int kTopItemCount = 6; // Load Snapshot/Tape, Save SNA/Z80, Network, Close

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

	// bitluni's Font6x8 (see OsdFont.h) is native-pixel-accurate to real
	// pico-spec hardware's own ~320px-wide OSD framebuffer. JASE's buffer
	// is 576px wide (MinZX::SCREEN_W) -- drawn at 1:1 the same font looks
	// tiny/thin by comparison (confirmed against a real device photo:
	// text there fills noticeably more of the screen). 2x integer scaling
	// restores that same look/weight without touching the font data.
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
	void activateFile();
	bool matchesFilter(const std::string& name) const;
	void doSave(bool asZ80);
	void setStatus(const std::string& s, bool ok);

	void drawText(uint8_t* pixels, int texW, int texH, int x, int y, const std::string& text, uint32_t argb) const;
	void fillRect(uint8_t* pixels, int texW, int texH, int x, int y, int w, int h, uint32_t argb) const;
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
	int networkMenuItemCount() const { return 1; }

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
};
