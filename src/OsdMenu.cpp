#include "OsdMenu.h"
#include "OsdFont.h"
#include "minzx.h"
#include "filemgr.h"
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <strings.h>
#include <fstream>
#include <unistd.h>

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
		close();
	}
}

void OsdMenu::setStatus(const std::string& s, bool ok)
{
	status_ = s;
	statusOk_ = ok;
	statusFramesLeft_ = 100; // ~2s at the ~50fps main loop cadence
}

// ---- local filesystem browser (Load Snapshot / Load Tape) -------------

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
		case 4: activateNetwork(); break;
		case 5: close(); break;
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
		close(); // load succeeded -- resume play immediately, same as a real emulator's file menu
	else
		setStatus(std::string("Load FAILED: ") + e.name, false);
}

// ---- NETWORK -> REMOTE (Gopher/TNFS) -----------------------------------

static std::string remotesFilePath()
{
	const char* home = getenv("HOME");
	return std::string(home ? home : ".") + "/.jase_remotes.tsv";
}

void OsdMenu::loadRemotes()
{
	remotes_.clear();
	std::ifstream f(remotesFilePath());
	std::string line;
	while (std::getline(f, line))
	{
		std::vector<std::string> cols;
		size_t start = 0;
		for (size_t i = 0; i <= line.size(); i++)
			if (i == line.size() || line[i] == '\t') { cols.push_back(line.substr(start, i - start)); start = i + 1; }
		if (cols.size() < 4)
			continue;
		RemoteConfig rc;
		rc.proto = atoi(cols[0].c_str());
		rc.host = cols[1];
		rc.port = (uint16_t)atoi(cols[2].c_str());
		rc.path = cols[3];
		rc.alias = cols.size() > 4 ? cols[4] : "";
		remotes_.push_back(rc);
	}
}

void OsdMenu::saveRemotes() const
{
	std::ofstream f(remotesFilePath(), std::ios::trunc);
	for (const auto& rc : remotes_)
		f << rc.proto << '\t' << rc.host << '\t' << rc.port << '\t' << rc.path << '\t' << rc.alias << '\n';
}

void OsdMenu::activateNetwork()
{
	// Only one row for now ("REMOTE") -- see networkMenuItemCount().
	mode_ = Mode::NetworkMenu;
	netSel_ = 0;
}

bool OsdMenu::connectActive(const RemoteConfig& rc)
{
	activeProto_ = (rc.proto == 0) ? Proto::Gopher : Proto::Tnfs;
	return (activeProto_ == Proto::Gopher) ? gopher_.connect(rc.host, rc.port)
	                                        : tnfs_.connect(rc.host, rc.port);
}

bool OsdMenu::remoteList(std::vector<Entry>& out)
{
	out.clear();
	if (activeProto_ == Proto::Gopher)
	{
		bool addUp = gopher_.cwdPath() != "/";
		std::vector<Gopher::Entry> ge;
		if (!gopher_.list(ge)) return false;
		if (addUp) out.push_back({ "..", true });
		for (const auto& e : ge) out.push_back({ e.display, e.isDir });
	}
	else
	{
		bool addUp = tnfs_.cwdPath() != "/";
		std::vector<Tnfs::Entry> te;
		if (!tnfs_.list(te)) return false;
		if (addUp) out.push_back({ "..", true });
		for (const auto& e : te) out.push_back({ e.name, e.isDir });
	}
	return true;
}

bool OsdMenu::remoteCwd(const std::string& name)
{
	return (activeProto_ == Proto::Gopher) ? gopher_.cwd(name) : tnfs_.cwd(name);
}

bool OsdMenu::remoteCwdUp()
{
	return (activeProto_ == Proto::Gopher) ? gopher_.cwdUp() : tnfs_.cwd("..");
}

std::string OsdMenu::remoteCwdPath() const
{
	return (activeProto_ == Proto::Gopher) ? gopher_.cwdPath() : tnfs_.cwdPath();
}

void OsdMenu::remoteDisconnect()
{
	if (activeProto_ == Proto::Gopher) gopher_.disconnect(); else tnfs_.disconnect();
}

bool OsdMenu::remoteLoadFile(const std::string& name)
{
	// Gopher's display text is a human description, not necessarily a
	// filename -- the real name with extension lives in the entry's
	// selector (Gopher::downloadBasename resolves that). TNFS entries
	// already are real filesystem names.
	std::string realName = (activeProto_ == Proto::Gopher) ? gopher_.downloadBasename(name) : name;

	bool isSna = hasExt(realName, ".sna");
	bool isZ80 = hasExt(realName, ".z80");
	bool isTap = hasExt(realName, ".tap");
	bool isTzx = hasExt(realName, ".tzx");
	if (!isSna && !isZ80 && !isTap && !isTzx)
	{
		setStatus(std::string("Unsupported: ") + realName, false);
		return false;
	}

	std::string tmpPath = "/tmp/jase_netload_" + std::to_string((long)getpid()) +
		(isSna ? ".sna" : isZ80 ? ".z80" : isTap ? ".tap" : ".tzx");
	bool dlOk = (activeProto_ == Proto::Gopher) ? gopher_.getToFile(name, tmpPath)
	                                             : tnfs_.getToFile(name, tmpPath);
	if (!dlOk)
	{
		setStatus(std::string("Download FAILED: ") + realName, false);
		return false;
	}

	FileMgr fileMgr;
	bool ok = false;
	if (isSna) ok = fileMgr.loadSNA(tmpPath.c_str(), minZX_);
	else if (isZ80) ok = fileMgr.loadZ80(tmpPath.c_str(), minZX_);
	else if (isTap) ok = fileMgr.loadTAP(tmpPath.c_str(), minZX_);
	else if (isTzx) ok = fileMgr.loadTZX(tmpPath.c_str(), minZX_);
	unlink(tmpPath.c_str()); // loaders read everything up front -- not needed after
	if (!ok)
		setStatus(std::string("Load FAILED: ") + realName, false);
	return ok;
}

void OsdMenu::activateRemote()
{
	int n = (int)remotes_.size();
	if (remoteSel_ == n) { beginAddRemote(); return; } // trailing "[ADD REMOTE]" row
	if (remoteSel_ < 0 || remoteSel_ >= n) return;

	const RemoteConfig rc = remotes_[remoteSel_];
	if (!connectActive(rc))
	{
		setStatus(std::string("Connect FAILED: ") + rc.host, false);
		mode_ = Mode::RemoteList;
		return;
	}
	if (!rc.path.empty())
		remoteCwd(rc.path);
	if (!remoteList(entries_))
	{
		setStatus("Listing FAILED", false);
		remoteDisconnect();
		mode_ = Mode::RemoteList;
		return;
	}
	fileSel_ = 0;
	fileScroll_ = 0;
	mode_ = Mode::RemoteBrowser;
}

void OsdMenu::activateRemoteFile()
{
	if (entries_.empty())
		return;
	const Entry e = entries_[fileSel_];

	if (e.name == "..")
	{
		remoteCwdUp();
		if (!remoteList(entries_)) setStatus("Listing FAILED", false);
		fileSel_ = 0; fileScroll_ = 0;
		return;
	}
	if (e.isDir)
	{
		if (!remoteCwd(e.name)) { setStatus("CWD FAILED", false); return; }
		if (!remoteList(entries_)) setStatus("Listing FAILED", false);
		fileSel_ = 0; fileScroll_ = 0;
		return;
	}

	if (remoteLoadFile(e.name))
	{
		remoteDisconnect();
		close(); // load succeeded -- resume play immediately
	}
	// on failure remoteLoadFile() already set a status message; stay put
}

void OsdMenu::beginAddRemote()
{
	mode_ = Mode::ProtoPicker;
	protoSel_ = 0;
}

void OsdMenu::activateProto()
{
	pendingProto_ = (protoSel_ == 0) ? Proto::Gopher : Proto::Tnfs;
	pendingHost_.clear(); pendingPort_.clear(); pendingPath_.clear(); pendingAlias_.clear();
	startTextField(TextField::Host);
}

void OsdMenu::startTextField(TextField f)
{
	pendingField_ = f;
	mode_ = Mode::TextInput;
	switch (f)
	{
		case TextField::Host:
			textLabel_ = "HOST"; textBuf_ = pendingHost_;
			break;
		case TextField::Port:
			textLabel_ = "PORT";
			textBuf_ = pendingPort_.empty() ? (pendingProto_ == Proto::Gopher ? "70" : "16384") : pendingPort_;
			break;
		case TextField::Path:
			textLabel_ = "START PATH (OPTIONAL)"; textBuf_ = pendingPath_;
			break;
		case TextField::Alias:
			textLabel_ = "ALIAS (OPTIONAL)"; textBuf_ = pendingAlias_;
			break;
	}
}

void OsdMenu::advanceTextField()
{
	switch (pendingField_)
	{
		case TextField::Host:
			if (textBuf_.empty())
				return; // required -- stay in this field
			pendingHost_ = textBuf_;
			startTextField(TextField::Port);
			return;
		case TextField::Port:
			pendingPort_ = textBuf_;
			startTextField(TextField::Path);
			return;
		case TextField::Path:
			pendingPath_ = textBuf_;
			startTextField(TextField::Alias);
			return;
		case TextField::Alias:
		{
			pendingAlias_ = textBuf_;
			RemoteConfig rc;
			rc.proto = (pendingProto_ == Proto::Gopher) ? 0 : 1;
			rc.host = pendingHost_;
			rc.port = (uint16_t)atoi(pendingPort_.c_str());
			if (rc.port == 0) rc.port = (pendingProto_ == Proto::Gopher) ? 70 : 16384;
			rc.path = pendingPath_;
			rc.alias = pendingAlias_;
			remotes_.push_back(rc);
			saveRemotes();
			remoteSel_ = (int)remotes_.size() - 1;
			mode_ = Mode::RemoteList;
			return;
		}
	}
}

// ---- input --------------------------------------------------------------

void OsdMenu::handleKey(SDL_Scancode sc)
{
	if (mode_ == Mode::Closed)
		return;

	if (sc == SDL_SCANCODE_ESCAPE)
	{
		switch (mode_)
		{
			case Mode::FileBrowser:   mode_ = Mode::TopMenu; break;
			case Mode::NetworkMenu:   mode_ = Mode::TopMenu; break;
			case Mode::RemoteList:    mode_ = Mode::NetworkMenu; break;
			case Mode::ProtoPicker:   mode_ = Mode::RemoteList; break;
			case Mode::TextInput:     mode_ = Mode::RemoteList; break; // abandons the whole add-remote flow
			case Mode::RemoteBrowser: remoteDisconnect(); mode_ = Mode::RemoteList; break;
			default:                  close(); break; // TopMenu
		}
		return;
	}

	if (mode_ == Mode::TextInput)
	{
		if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
			advanceTextField();
		else if (sc == SDL_SCANCODE_BACKSPACE && !textBuf_.empty())
			textBuf_.pop_back();
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
	else if (mode_ == Mode::NetworkMenu)
	{
		int n = networkMenuItemCount();
		if (sc == SDL_SCANCODE_UP)
			netSel_ = (netSel_ - 1 + n) % n;
		else if (sc == SDL_SCANCODE_DOWN)
			netSel_ = (netSel_ + 1) % n;
		else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
		{
			if (netSel_ == 0) { loadRemotes(); mode_ = Mode::RemoteList; remoteSel_ = 0; }
		}
	}
	else if (mode_ == Mode::RemoteList)
	{
		int n = (int)remotes_.size() + 1; // + "[ADD REMOTE]"
		if (sc == SDL_SCANCODE_UP)
			remoteSel_ = (remoteSel_ - 1 + n) % n;
		else if (sc == SDL_SCANCODE_DOWN)
			remoteSel_ = (remoteSel_ + 1) % n;
		else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
			activateRemote();
	}
	else if (mode_ == Mode::ProtoPicker)
	{
		if (sc == SDL_SCANCODE_UP)
			protoSel_ = (protoSel_ - 1 + 2) % 2;
		else if (sc == SDL_SCANCODE_DOWN)
			protoSel_ = (protoSel_ + 1) % 2;
		else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
			activateProto();
	}
	else if (mode_ == Mode::FileBrowser || mode_ == Mode::RemoteBrowser)
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
		{
			if (mode_ == Mode::FileBrowser) activateFile();
			else activateRemoteFile();
		}
	}
}

void OsdMenu::handleTextInput(const char* text)
{
	if (mode_ != Mode::TextInput || !text)
		return;
	if (textBuf_.size() < 120)
		textBuf_ += text;
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

void OsdMenu::renderSimpleList(uint8_t* pixels, int texW, int texH, const char* title,
                                const std::vector<std::string>& items, int selIndex,
                                const std::string& statusLine, bool statusOk) const
{
	const int rowH = 8 * kFontScale + 4;
	const int titleH = rowH;
	const int border = kFontScale;
	int extraRows = statusLine.empty() ? 0 : 1;
	int boxW = texW - 40;
	int boxH = titleH + (int)(items.size() + extraRows) * rowH + 4;
	int boxX = texW / 2 - boxW / 2, boxY = 8;

	fillRect(pixels, texW, texH, boxX - border, boxY - border, boxW + 2 * border, boxH + 2 * border, kOsdBlack);
	fillRect(pixels, texW, texH, boxX, boxY, boxW, titleH, kOsdBlack);
	drawText(pixels, texW, texH, boxX + 2 * kFontScale, boxY + kFontScale, title, kOsdWhiteBri);

	int textX = boxX + 2 * kFontScale, rowY0 = boxY + titleH;
	size_t maxChars = (size_t)(boxW - 4 * kFontScale) / (7 * kFontScale);
	for (size_t i = 0; i < items.size(); i++)
	{
		int rowY = rowY0 + (int)i * rowH;
		bool sel = ((int)i == selIndex);
		fillRect(pixels, texW, texH, boxX, rowY - 1, boxW, rowH, sel ? kOsdCyanBri : kOsdWhiteBri);
		std::string label = items[i];
		if (label.size() > maxChars) label = label.substr(0, maxChars);
		drawText(pixels, texW, texH, textX, rowY, label, kOsdBlack);
	}
	if (extraRows)
	{
		int rowY = rowY0 + (int)items.size() * rowH;
		fillRect(pixels, texW, texH, boxX, rowY - 1, boxW, rowH, kOsdWhiteBri);
		std::string label = statusLine;
		if (label.size() > maxChars) label = label.substr(0, maxChars);
		drawText(pixels, texW, texH, textX, rowY, label, statusOk ? kOsdGreenBri : kOsdRedBri);
	}
}

void OsdMenu::render(uint8_t* pixels, int texW, int texH)
{
	if (mode_ == Mode::Closed)
		return;

	size_t bufSize = (size_t)texW * texH * 4;
	if (!haveBackground_)
	{
		background_.assign(pixels, pixels + bufSize);
		haveBackground_ = true;
	}
	else
	{
		memcpy(pixels, background_.data(), bufSize);
	}

	if (statusFramesLeft_ > 0)
		statusFramesLeft_--;

	if (mode_ == Mode::NetworkMenu)
	{
		renderSimpleList(pixels, texW, texH, "NETWORK", { "REMOTE" }, netSel_);
		return;
	}
	if (mode_ == Mode::RemoteList)
	{
		std::vector<std::string> items;
		for (const auto& rc : remotes_)
		{
			if (!rc.alias.empty())
				items.push_back(rc.alias);
			else
			{
				char b[128];
				snprintf(b, sizeof(b), "%s:%u (%s)", rc.host.c_str(), (unsigned)rc.port,
					rc.proto == 0 ? "gopher" : "tnfs");
				items.push_back(b);
			}
		}
		items.push_back("[ADD REMOTE]");
		renderSimpleList(pixels, texW, texH, "REMOTE", items, remoteSel_,
			statusFramesLeft_ > 0 ? status_ : "", statusOk_);
		return;
	}
	if (mode_ == Mode::ProtoPicker)
	{
		renderSimpleList(pixels, texW, texH, "ADD REMOTE: PROTOCOL", { "GOPHER", "TNFS" }, protoSel_);
		return;
	}
	if (mode_ == Mode::TextInput)
	{
		std::string line = textLabel_ + ": " + textBuf_ + "_";
		renderSimpleList(pixels, texW, texH, "ADD REMOTE", { line }, -1);
		return;
	}

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
	else if (mode_ == Mode::RemoteBrowser)
		title = (activeProto_ == Proto::Gopher) ? "GOPHER" : "TNFS";
	drawText(pixels, texW, texH, boxX + 2 * kFontScale, boxY + kFontScale, title, kOsdWhiteBri);

	int textX = boxX + 2 * kFontScale, rowY0 = boxY + titleH;

	if (mode_ == Mode::TopMenu)
	{
		static const char* kItems[kTopItemCount] = {
			"LOAD SNAPSHOT (.SNA/.Z80)",
			"LOAD TAPE (.TAP/.TZX)",
			"SAVE SNAPSHOT AS .SNA",
			"SAVE SNAPSHOT AS .Z80",
			"NETWORK",
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
	else // FileBrowser or RemoteBrowser -- same Entry{name,isDir} shape, same scroll/cursor fields
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
