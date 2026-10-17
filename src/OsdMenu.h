#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <SDL.h>

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

	// Draws the overlay into `pixels` (texW x texH, ARGB8888, same
	// layout main.cpp's own pixel buffer uses) on top of whatever is
	// already there. No-op if closed -- call unconditionally from
	// main.cpp's render step, same spirit as debugServer.poll().
	// Non-const: ticks the transient status-message countdown.
	void render(uint8_t* pixels, int texW, int texH);

private:
	enum class Mode { Closed, TopMenu, FileBrowser };
	enum class Action { None, LoadSnapshot, LoadTape };

	MinZX* minZX_;
	Mode mode_ = Mode::Closed;

	int topSel_ = 0;
	static const int kTopItemCount = 5;

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
	static const int kFontScale = 2;

	void openBrowser(Action action);
	void rescan();
	void activateTop();
	void activateFile();
	bool matchesFilter(const std::string& name) const;
	void doSave(bool asZ80);
	void setStatus(const std::string& s, bool ok);

	void drawText(uint8_t* pixels, int texW, int texH, int x, int y, const std::string& text, uint32_t argb) const;
	void fillRect(uint8_t* pixels, int texW, int texH, int x, int y, int w, int h, uint32_t argb) const;
};
