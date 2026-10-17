#include "OsdMenu.h"
#include "OsdFont.h"
#include "minzx.h"
#include "filemgr.h"
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <strings.h>

namespace fs = std::filesystem;

// std::min<int>(..., kFileRows) binds by reference -- needs an out-of-line
// definition for the static const int, not just the in-class initializer.
const int OsdMenu::kFileRows;

static bool hasExt(const std::string& name, const char* ext)
{
	size_t nlen = name.size(), elen = strlen(ext);
	return nlen > elen && 0 == strcasecmp(name.c_str() + nlen - elen, ext);
}

void OsdMenu::toggle()
{
	if (mode_ == Mode::Closed)
	{
		mode_ = Mode::TopMenu;
		topSel_ = 0;
	}
	else
	{
		mode_ = Mode::Closed;
	}
}

void OsdMenu::setStatus(const std::string& s, bool ok)
{
	status_ = s;
	statusOk_ = ok;
	statusFramesLeft_ = 100; // ~2s at the ~50fps main loop cadence
}

bool OsdMenu::matchesFilter(const std::string& name) const
{
	switch (browseAction_)
	{
		case Action::LoadSnapshot: return hasExt(name, ".sna") || hasExt(name, ".z80");
		case Action::LoadTape:     return hasExt(name, ".tap") || hasExt(name, ".tzx");
		default: return true;
	}
}

void OsdMenu::rescan()
{
	entries_.clear();
	std::error_code ec;
	fs::path dir(cwd_);
	if (dir.has_parent_path() || dir != dir.root_path())
		entries_.push_back({ "..", true });
	std::vector<Entry> dirs, files;
	for (auto& de : fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec))
	{
		std::string name = de.path().filename().string();
		if (name.empty() || name[0] == '.')
			continue;
		bool isDir = de.is_directory(ec);
		if (isDir)
			dirs.push_back({ name, true });
		else if (matchesFilter(name))
			files.push_back({ name, false });
	}
	std::sort(dirs.begin(), dirs.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
	std::sort(files.begin(), files.end(), [](const Entry& a, const Entry& b) { return a.name < b.name; });
	entries_.insert(entries_.end(), dirs.begin(), dirs.end());
	entries_.insert(entries_.end(), files.begin(), files.end());
	fileSel_ = 0;
	fileScroll_ = 0;
}

void OsdMenu::openBrowser(Action action)
{
	browseAction_ = action;
	cwd_ = ".";
	mode_ = Mode::FileBrowser;
	rescan();
}

void OsdMenu::doSave(bool asZ80)
{
	time_t t = time(nullptr);
	struct tm tmv;
	localtime_r(&t, &tmv);
	char name[64];
	snprintf(name, sizeof(name), "jase_%04d%02d%02d_%02d%02d%02d.%s",
		tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
		asZ80 ? "z80" : "sna");

	FileMgr fileMgr;
	bool ok = asZ80 ? fileMgr.saveZ80(name, minZX_) : fileMgr.saveSNA(name, minZX_);
	setStatus(ok ? (std::string("Saved: ") + name) : (std::string("Save FAILED: ") + name), ok);
}

void OsdMenu::activateTop()
{
	switch (topSel_)
	{
		case 0: openBrowser(Action::LoadSnapshot); break;
		case 1: openBrowser(Action::LoadTape); break;
		case 2: doSave(false); break;
		case 3: doSave(true); break;
		case 4: mode_ = Mode::Closed; break;
	}
}

void OsdMenu::activateFile()
{
	if (entries_.empty())
		return;
	const Entry e = entries_[fileSel_];

	if (e.name == "..")
	{
		fs::path dir(cwd_);
		cwd_ = dir.parent_path().empty() ? "." : dir.parent_path().string();
		rescan();
		return;
	}
	if (e.isDir)
	{
		cwd_ = (fs::path(cwd_) / e.name).string();
		rescan();
		return;
	}

	std::string fullPath = (fs::path(cwd_) / e.name).string();
	FileMgr fileMgr;
	bool ok = false;
	if (browseAction_ == Action::LoadSnapshot)
		ok = hasExt(e.name, ".z80") ? fileMgr.loadZ80(fullPath.c_str(), minZX_)
		                            : fileMgr.loadSNA(fullPath.c_str(), minZX_);
	else if (browseAction_ == Action::LoadTape)
		ok = hasExt(e.name, ".tzx") ? fileMgr.loadTZX(fullPath.c_str(), minZX_)
		                            : fileMgr.loadTAP(fullPath.c_str(), minZX_);

	if (ok)
		mode_ = Mode::Closed; // load succeeded -- resume play immediately, same as a real emulator's file menu
	else
		setStatus(std::string("Load FAILED: ") + e.name, false);
}

void OsdMenu::handleKey(SDL_Scancode sc)
{
	if (mode_ == Mode::Closed)
		return;

	if (sc == SDL_SCANCODE_ESCAPE)
	{
		if (mode_ == Mode::FileBrowser)
			mode_ = Mode::TopMenu;
		else
			mode_ = Mode::Closed;
		return;
	}

	if (mode_ == Mode::TopMenu)
	{
		if (sc == SDL_SCANCODE_UP)
			topSel_ = (topSel_ - 1 + kTopItemCount) % kTopItemCount;
		else if (sc == SDL_SCANCODE_DOWN)
			topSel_ = (topSel_ + 1) % kTopItemCount;
		else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
			activateTop();
	}
	else if (mode_ == Mode::FileBrowser)
	{
		if (entries_.empty())
			return;
		if (sc == SDL_SCANCODE_UP)
		{
			if (fileSel_ > 0) fileSel_--;
			if (fileSel_ < fileScroll_) fileScroll_ = fileSel_;
		}
		else if (sc == SDL_SCANCODE_DOWN)
		{
			if (fileSel_ + 1 < (int)entries_.size()) fileSel_++;
			if (fileSel_ >= fileScroll_ + kFileRows) fileScroll_ = fileSel_ - kFileRows + 1;
		}
		else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
			activateFile();
	}
}

// --- drawing -----------------------------------------------------------

void OsdMenu::fillRect(uint8_t* pixels, int texW, int texH, int x, int y, int w, int h, uint32_t argb) const
{
	uint8_t a = (argb >> 24) & 0xFF, r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
	int x0 = std::max(0, x), y0 = std::max(0, y);
	int x1 = std::min(texW, x + w), y1 = std::min(texH, y + h);
	for (int yy = y0; yy < y1; yy++)
	{
		uint8_t* row = pixels + (size_t)texW * 4 * yy;
		for (int xx = x0; xx < x1; xx++)
		{
			uint8_t* px = row + xx * 4;
			px[0] = b; px[1] = g; px[2] = r; px[3] = a;
		}
	}
}

// bitluni's Font6x8 (see OsdFont.h) is 6 pixels wide, firstChar=32 --
// codes outside [32, 32+96) (e.g. high-bit/control bytes from a filename
// in some other codepage) fall back to '?', same as Font::valid() would
// refuse them in pico-spec's own OSD. Each source pixel is blown up into
// a kFontScale x kFontScale block (see OsdMenu.h for why).
void OsdMenu::drawText(uint8_t* pixels, int texW, int texH, int x, int y, const std::string& text, uint32_t argb) const
{
	uint8_t a = (argb >> 24) & 0xFF, r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
	int cx = x;
	for (unsigned char ch : text)
	{
		int rel = (int)ch - kOsdFontFirstChar;
		if (rel < 0 || rel >= kOsdFontCharCount) rel = '?' - kOsdFontFirstChar;
		const uint8_t* glyph = kOsdFont6x8[rel];
		for (int row = 0; row < 8; row++)
		{
			uint8_t bits = glyph[row];
			for (int col = 0; col < 6; col++)
			{
				if (!(bits & (1 << (5 - col))))
					continue;
				int px0 = cx + col * kFontScale;
				int py0 = y + row * kFontScale;
				for (int dy = 0; dy < kFontScale; dy++)
				{
					int yy = py0 + dy;
					if (yy < 0 || yy >= texH) continue;
					uint8_t* rowPtr = pixels + (size_t)texW * 4 * yy;
					for (int dx = 0; dx < kFontScale; dx++)
					{
						int xx = px0 + dx;
						if (xx < 0 || xx >= texW) continue;
						uint8_t* px = rowPtr + xx * 4;
						px[0] = b; px[1] = g; px[2] = r; px[3] = a;
					}
				}
			}
		}
		cx += 7 * kFontScale; // 6px glyph + 1px gap, matches pico-spec's own letter spacing
	}
}

// Colour scheme lifted straight from pico-spec's own OSD (OSDMenu.cpp's
// PrintRow()/WindowDraw(), see project notes): black border, title row
// white-on-black, body rows black-on-white, focused row black-on-bright-
// cyan. zxColor(c,bright) there indexes the same 16-colour ZX Spectrum
// palette JASE's own zxColor()/speColors[] (minzx.cpp) already renders
// the emulated screen with -- these are just that same palette's RGB
// values (0xB0/0xFF per channel) written out directly, since OsdMenu
// doesn't have access to minzx.cpp's file-local speColors[] table.
static const uint32_t kOsdBlack     = 0xFF000000;
static const uint32_t kOsdWhiteBri  = 0xFFFFFFFF;
static const uint32_t kOsdCyanBri   = 0xFF00FFFF;
static const uint32_t kOsdGreenBri  = 0xFF00FF00;
static const uint32_t kOsdRedBri    = 0xFFFF0000;

void OsdMenu::render(uint8_t* pixels, int texW, int texH)
{
	if (mode_ == Mode::Closed)
		return;

	if (statusFramesLeft_ > 0)
		statusFramesLeft_--;

	const int rowH = 8 * kFontScale + 4; // glyph height + a little breathing room
	const int titleH = rowH;
	const int border = kFontScale; // thicker outline at 2x, matches the bolder look overall
	int visibleBodyRows = (mode_ == Mode::TopMenu) ? kTopItemCount : kFileRows;
	// Near full-width, small margins each side -- matches the real
	// pico-spec OSD (a photo of the actual device showed its menu box
	// spanning almost the whole screen, not a small centered dialog like
	// this used to be).
	int boxW = texW - 40;
	int boxH = titleH + visibleBodyRows * rowH + 4;
	int boxX = texW / 2 - boxW / 2, boxY = 8;

	// Black border, like OSD::WindowDraw()'s vga.rect() -- an outline, not
	// a filled background (the body/title fills below cover the interior
	// completely anyway).
	fillRect(pixels, texW, texH, boxX - border, boxY - border, boxW + 2 * border, boxH + 2 * border, kOsdBlack);

	// Title bar: white text on black.
	fillRect(pixels, texW, texH, boxX, boxY, boxW, titleH, kOsdBlack);
	const char* title = "JASE MENU";
	if (mode_ == Mode::FileBrowser)
		title = (browseAction_ == Action::LoadTape) ? "LOAD TAPE" : "LOAD SNAPSHOT";
	drawText(pixels, texW, texH, boxX + 2 * kFontScale, boxY + kFontScale, title, kOsdWhiteBri);

	int textX = boxX + 2 * kFontScale, rowY0 = boxY + titleH;

	if (mode_ == Mode::TopMenu)
	{
		static const char* kItems[kTopItemCount] = {
			"LOAD SNAPSHOT (.SNA/.Z80)",
			"LOAD TAPE (.TAP/.TZX)",
			"SAVE SNAPSHOT AS .SNA",
			"SAVE SNAPSHOT AS .Z80",
			"CLOSE MENU",
		};
		for (int i = 0; i < kTopItemCount; i++)
		{
			int rowY = rowY0 + i * rowH;
			bool sel = (i == topSel_);
			fillRect(pixels, texW, texH, boxX, rowY - 1, boxW, rowH, sel ? kOsdCyanBri : kOsdWhiteBri);
			drawText(pixels, texW, texH, textX, rowY, kItems[i], kOsdBlack);
		}

		if (statusFramesLeft_ > 0)
		{
			int rowY = rowY0 + kTopItemCount * rowH;
			fillRect(pixels, texW, texH, boxX, rowY - 1, boxW, rowH, kOsdWhiteBri);
			drawText(pixels, texW, texH, textX, rowY, status_, statusOk_ ? kOsdGreenBri : kOsdRedBri);
		}
	}
	else // FileBrowser
	{
		int visible = std::min((int)entries_.size() - fileScroll_, kFileRows);
		for (int row = 0; row < kFileRows; row++)
		{
			int rowY = rowY0 + row * rowH;
			if (row >= visible)
			{
				fillRect(pixels, texW, texH, boxX, rowY - 1, boxW, rowH, kOsdWhiteBri);
				continue;
			}
			int idx = fileScroll_ + row;
			const Entry& e = entries_[idx];
			std::string label = e.isDir ? ("[" + e.name + "]") : e.name;
			size_t maxChars = (size_t)(boxW - 4 * kFontScale) / (7 * kFontScale);
			if (label.size() > maxChars) label = label.substr(0, maxChars);
			bool sel = (idx == fileSel_);
			fillRect(pixels, texW, texH, boxX, rowY - 1, boxW, rowH, sel ? kOsdCyanBri : kOsdWhiteBri);
			drawText(pixels, texW, texH, textX, rowY, label, kOsdBlack);
		}
	}
}
