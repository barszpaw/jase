#include "OsdMenu.h"
#include "OsdFont.h"
#include "minzx.h"
#include "filemgr.h"
#include "ZiFi.h"
#include "Multiface.h"
#include "DivIde.h"
#include "Dandanator.h"
#include "Multiply.h"
#include "SDI_8255.h"
#include "Fdd3000.h"
#include <nlohmann/json.hpp>
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
// ordered_json, not plain json -- saveConfig() read-modify-writes the
// user's own settings.json, and plain nlohmann::json re-serializes
// object keys alphabetically (its underlying std::map), which would
// needlessly reshuffle every key on the very first save even where
// nothing actually changed. ordered_json preserves insertion order
// (existing keys keep their original position; saveConfig()'s own new
// assignments land in the order it makes them), keeping a save's diff
// limited to what actually changed.
using json = nlohmann::ordered_json;

// std::min<int>(..., kFileRows) binds by reference -- needs an out-of-line
// definition for the static const int, not just the in-class initializer.
const int OsdMenu::kFileRows;

OsdMenu::OsdMenu(MinZX* minZX) : minZX_(minZX)
{
	// Seed the next-boot shadow preferences from MinZX's actual current
	// state (see OsdMenu.h's own comment) -- so a value already set via
	// --machine/--turbosound shows correctly the first time OPTIONS is
	// opened, not just after the user picks it through the menu.
	switch (minZX_->getMachine()) {
		case MachineType::TimexTC2048:  prefMachineIdx_ = 1; break;
		case MachineType::Spectrum128K: prefMachineIdx_ = 2; break;
		default:                        prefMachineIdx_ = 0; break; // Spectrum48K
	}
	prefTurboSound_ = minZX_->isTurboSound();
}

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

void OsdMenu::openStorage()
{
	// Only meaningful from Closed -- main.cpp's own F5 handler already
	// guards on !isOpen(), same as toggle()'s F1 caller does.
	mode_ = Mode::StorageMenu;
	storageSel_ = 0;
	topSel_ = 0; // so Esc-ing back out of Storage lands on TopMenu's first row, not a stale cursor
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
		case Action::LoadMultiface1Rom:
		case Action::LoadDandanatorRom:
		case Action::LoadSdi8255Rom:
		case Action::LoadFdd3000Rom:
		case Action::LoadFdd3000StationRom:
		case Action::LoadDivideRom:  return hasExt(name, ".rom") || hasExt(name, ".bin");
		case Action::LoadFdd3000Disk: return hasExt(name, ".dsk");
		case Action::LoadDivideDisk: return hasExt(name, ".hdf") || hasExt(name, ".img") || hasExt(name, ".raw");
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
	// Must be an ABSOLUTE path, not textual ".": fs::path(".").parent_path()
	// is empty, so activateFile()'s ".." handling (which falls back to "."
	// on an empty parent_path()) immediately bounced right back to the
	// starting directory on every "go up" -- never able to climb past it,
	// confirmed as the bug Pawel hit. fs::current_path() resolves to the
	// real absolute directory, whose parent_path() walks correctly all
	// the way up to the filesystem root.
	cwd_ = fs::current_path().string();
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
		case 0: mode_ = Mode::StorageMenu; storageSel_ = 0; break;
		case 1: mode_ = Mode::OptionsMenu; optionsSel_ = 0; break;
		case 2: mode_ = Mode::ResetMenu; resetSel_ = 0; break;
		case 3: activateNetwork(); break;
		case 4: mode_ = Mode::ZxKeyboard; break;
		case 5: close(); break;
	}
}

void OsdMenu::activateStorage()
{
	switch (storageSel_)
	{
		case 0: openBrowser(Action::LoadSnapshot); break;
		case 1: openBrowser(Action::LoadTape); break;
		case 2: doSave(false); break;
		case 3: doSave(true); break;
		case 4: openPeriphDetail(PeriphId::Multiface1); break;
		case 5: openPeriphDetail(PeriphId::DivIde); break;
		case 6: openPeriphDetail(PeriphId::Dandanator); break;
		case 7: openPeriphDetail(PeriphId::Sdi8255); break;
		case 8: openPeriphDetail(PeriphId::Fdd3000); break;
	}
}

// ---- OPTIONS / MACHINE PICKER / RESET ----------------------------------

static const char* kMachineNames[3] = { "48K", "TC2048", "128K" };
static const char* kMachineSettingsValues[3] = { "48k", "tc2048", "128k" };

void OsdMenu::activateOptions()
{
	switch (optionsSel_)
	{
		case 0: mode_ = Mode::MachinePicker; machinePickerSel_ = prefMachineIdx_; break;
		case 1: prefTurboSound_ = !prefTurboSound_; break; // next-boot only, see OsdMenu.h
		case 2: minZX_->setTimexVideo(!minZX_->isTimexVideo()); break;    // live
		case 3: minZX_->setPcKeys(!minZX_->isPcKeys()); break;             // live
		case 4: minZX_->setTapeFastload(!minZX_->isTapeFastload()); break; // live
	}
}

void OsdMenu::activateMachinePicker()
{
	prefMachineIdx_ = machinePickerSel_;
	mode_ = Mode::OptionsMenu;
}

void OsdMenu::activateReset()
{
	switch (resetSel_)
	{
		case 0: minZX_->reset(); close(); break;
		case 1: saveConfig(); break; // stays in ResetMenu -- see its own comment
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

	// Peripheral ROM/disk pickers: load into the peripheral, enable it if
	// it's a ROM (same "implies enable" convention main.cpp's own CLI
	// flags use), and return to that peripheral's detail screen instead
	// of closing the whole menu -- there's usually more to set (e.g.
	// FDD3000's other 3 drives) right after picking one file.
	if (browseAction_ == Action::LoadMultiface1Rom || browseAction_ == Action::LoadDivideRom ||
	    browseAction_ == Action::LoadDivideDisk || browseAction_ == Action::LoadDandanatorRom ||
	    browseAction_ == Action::LoadSdi8255Rom || browseAction_ == Action::LoadFdd3000Rom ||
	    browseAction_ == Action::LoadFdd3000StationRom || browseAction_ == Action::LoadFdd3000Disk)
	{
		bool ok = false;
		switch (browseAction_)
		{
			case Action::LoadMultiface1Rom:
				ok = minZX_->getMultiface()->loadRom(fullPath.c_str());
				if (ok) minZX_->getMultiface()->setEnabled(true);
				break;
			case Action::LoadDivideRom:
				ok = minZX_->getDivIde()->loadRom(fullPath.c_str());
				if (ok) minZX_->getDivIde()->setEnabled(true);
				break;
			case Action::LoadDivideDisk:
				ok = minZX_->getDivIde()->insertDisk(fullPath.c_str());
				break;
			case Action::LoadDandanatorRom:
				ok = minZX_->getDandanator()->loadRom(fullPath.c_str());
				if (ok) minZX_->getDandanator()->setEnabled(true);
				break;
			case Action::LoadSdi8255Rom:
				ok = minZX_->getSDI8255()->loadRom(fullPath.c_str());
				if (ok) minZX_->getSDI8255()->setEnabled(true);
				break;
			case Action::LoadFdd3000Rom:
				ok = minZX_->getFdd3000()->loadInterfaceRom(fullPath.c_str());
				if (ok) minZX_->getFdd3000()->setEnabled(true);
				break;
			case Action::LoadFdd3000StationRom:
				ok = minZX_->getFdd3000()->loadStationBootRom(fullPath.c_str());
				break;
			case Action::LoadFdd3000Disk:
				ok = minZX_->getFdd3000()->insertDisk(fdd3000DriveSlot_, fullPath.c_str());
				break;
			default: break;
		}
		setStatus(ok ? (std::string("Loaded: ") + e.name) : (std::string("Load FAILED: ") + e.name), ok);
		mode_ = Mode::PeriphDetail;
		return;
	}

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

// ---- Peripheral detail screens (Storage > */Network > ZiFi) -----------

static std::string pathOrNone(const std::string& p) { return p.empty() ? std::string("(not set)") : p; }
static std::string rawHostPort(const std::string& h, int p) { return h.empty() ? std::string() : (h + ":" + std::to_string(p)); }
static std::string hostPortOrNone(const std::string& h, int p) { std::string r = rawHostPort(h, p); return r.empty() ? std::string("(not set)") : r; }

void OsdMenu::openPeriphDetail(PeriphId id)
{
	detailPeriphId_ = id;
	periphDetailSel_ = 0;
	mode_ = Mode::PeriphDetail;
}

int OsdMenu::periphDetailRowCount(PeriphId id) const
{
	switch (id)
	{
		case PeriphId::Multiface1: return 2; // Enabled, ROM
		case PeriphId::DivIde:     return 3; // Enabled, ROM, Disk
		case PeriphId::Dandanator: return 4; // Enabled, ROM, Multiply Enabled, Multiply Bridge
		case PeriphId::Sdi8255:    return 3; // Enabled, ROM, Bridge
		case PeriphId::Fdd3000:    return 8; // Enabled, ROM, Station ROM, Disk A-D, Reset FDD3000
		case PeriphId::ZiFi:       return 2; // Host:Port, Reconnect
	}
	return 0;
}

void OsdMenu::periphDetailRows(PeriphId id, std::vector<std::string>& rows) const
{
	rows.clear();
	switch (id)
	{
		case PeriphId::Multiface1:
		{
			Multiface* p = minZX_->getMultiface();
			rows.push_back(std::string("ENABLED: ") + (p->isEnabled() ? "YES" : "NO"));
			rows.push_back("ROM: " + pathOrNone(p->getRomPath()));
			break;
		}
		case PeriphId::DivIde:
		{
			DivIde* p = minZX_->getDivIde();
			rows.push_back(std::string("ENABLED: ") + (p->isEnabled() ? "YES" : "NO"));
			rows.push_back("ROM: " + pathOrNone(p->getRomPath()));
			rows.push_back("DISK: " + pathOrNone(p->getDiskPath()));
			break;
		}
		case PeriphId::Dandanator:
		{
			Dandanator* p = minZX_->getDandanator();
			Multiply* m = minZX_->getMultiply();
			rows.push_back(std::string("ENABLED: ") + (p->isEnabled() ? "YES" : "NO"));
			rows.push_back("ROM: " + pathOrNone(p->getRomPath()));
			rows.push_back(std::string("MULTIPLY: ") + (m->isEnabled() ? "YES" : "NO"));
			rows.push_back("MULTIPLY BRIDGE: " + hostPortOrNone(m->getBridgeHost(), m->getBridgePort()));
			break;
		}
		case PeriphId::Sdi8255:
		{
			SDI_8255* p = minZX_->getSDI8255();
			rows.push_back(std::string("ENABLED: ") + (p->isEnabled() ? "YES" : "NO"));
			rows.push_back("ROM: " + pathOrNone(p->getRomPath()));
			rows.push_back("BRIDGE: " + hostPortOrNone(p->getBridgeHost(), p->getBridgePort()));
			break;
		}
		case PeriphId::Fdd3000:
		{
			Fdd3000* p = minZX_->getFdd3000();
			rows.push_back(std::string("ENABLED: ") + (p->isEnabled() ? "YES" : "NO"));
			rows.push_back("ROM: " + pathOrNone(p->getRomPath()));
			// SEPARATE real chip from the interface ROM above -- see
			// Fdd3000::loadStationBootRom()'s own comment.
			rows.push_back("STATION ROM: " + pathOrNone(p->getStationRomPath()));
			static const char* kDriveLetter[4] = { "A", "B", "C", "D" };
			for (int i = 0; i < 4; i++)
				rows.push_back(std::string("DISK ") + kDriveLetter[i] + ": " + pathOrNone(p->getDiskPath(i)));
			// FDD3000's OWN physical reset button, separate from F11/
			// MinZX::reset() -- see Fdd3000::stationReset()'s own
			// comment: the real workflow is "swap which disk image is in
			// drive A, THEN press this", not the main machine reset. Also
			// bound directly to F8 (main.cpp) since this menu path is
			// long for something meant to be pressed often -- shown here
			// so it's discoverable from inside the menu too.
			rows.push_back("RESET FDD3000 (F8)");
			break;
		}
		case PeriphId::ZiFi:
		{
			ZiFi* z = minZX_->getZiFi();
			rows.push_back("HOST:PORT: " + hostPortOrNone(z->getLastHost(), z->getLastPort()));
			rows.push_back("RECONNECT");
			break;
		}
	}
}

void OsdMenu::openPeriphFileBrowser(Action action, int driveSlot)
{
	fdd3000DriveSlot_ = driveSlot;
	openBrowser(action);
}

void OsdMenu::activatePeriphDetailRow(PeriphId id, int row)
{
	switch (id)
	{
		case PeriphId::Multiface1:
		{
			Multiface* p = minZX_->getMultiface();
			if (row == 0) p->setEnabled(!p->isEnabled());
			else if (row == 1) openPeriphFileBrowser(Action::LoadMultiface1Rom);
			break;
		}
		case PeriphId::DivIde:
		{
			DivIde* p = minZX_->getDivIde();
			if (row == 0) p->setEnabled(!p->isEnabled());
			else if (row == 1) openPeriphFileBrowser(Action::LoadDivideRom);
			else if (row == 2) openPeriphFileBrowser(Action::LoadDivideDisk);
			break;
		}
		case PeriphId::Dandanator:
		{
			Dandanator* p = minZX_->getDandanator();
			Multiply* m = minZX_->getMultiply();
			if (row == 0) p->setEnabled(!p->isEnabled());
			else if (row == 1) openPeriphFileBrowser(Action::LoadDandanatorRom);
			else if (row == 2) m->setEnabled(!m->isEnabled());
			else if (row == 3) startHostPortField("MULTIPLY BRIDGE HOST:PORT", rawHostPort(m->getBridgeHost(), m->getBridgePort()));
			break;
		}
		case PeriphId::Sdi8255:
		{
			SDI_8255* p = minZX_->getSDI8255();
			if (row == 0) p->setEnabled(!p->isEnabled());
			else if (row == 1) openPeriphFileBrowser(Action::LoadSdi8255Rom);
			else if (row == 2) startHostPortField("SDI8255 BRIDGE HOST:PORT", rawHostPort(p->getBridgeHost(), p->getBridgePort()));
			break;
		}
		case PeriphId::Fdd3000:
		{
			Fdd3000* p = minZX_->getFdd3000();
			if (row == 0) p->setEnabled(!p->isEnabled());
			else if (row == 1) openPeriphFileBrowser(Action::LoadFdd3000Rom);
			else if (row == 2) openPeriphFileBrowser(Action::LoadFdd3000StationRom);
			else if (row >= 3 && row <= 6) openPeriphFileBrowser(Action::LoadFdd3000Disk, row - 3);
			else if (row == 7) { p->stationReset(); setStatus("FDD3000 station reset", true); }
			break;
		}
		case PeriphId::ZiFi:
		{
			ZiFi* z = minZX_->getZiFi();
			if (row == 0) startHostPortField("ZIFI HOST:PORT", rawHostPort(z->getLastHost(), z->getLastPort()));
			else if (row == 1) reconnectZiFi();
			break;
		}
	}
}

void OsdMenu::startHostPortField(const std::string& label, const std::string& initial)
{
	textPurpose_ = TextPurpose::PeriphHostPort;
	textLabel_ = label;
	textBuf_ = initial;
	mode_ = Mode::TextInput;
}

// ---- RESET > SAVE CONFIG: settings.json read-modify-write --------------

static json jopt(const std::string& s) { return s.empty() ? json(nullptr) : json(s); }
static json joptPort(int v) { return v == 0 ? json(nullptr) : json(v); }

void OsdMenu::saveConfig()
{
	json j;
	std::ifstream in(settingsPath_);
	if (in) { try { in >> j; } catch (...) { j = json::object(); } }
	in.close();

	j["machine"]["machine"] = kMachineSettingsValues[prefMachineIdx_];
	j["machine"]["turbosound"] = prefTurboSound_;
	j["machine"]["timexHires"] = minZX_->isTimexVideo();
	j["machine"]["pcKeys"] = minZX_->isPcKeys();
	j["media"]["tapeFastload"] = minZX_->isTapeFastload();

	Multiface* mf = minZX_->getMultiface();
	j["peripherals"]["multiface1"]["enabled"] = mf->isEnabled();
	j["peripherals"]["multiface1"]["rom"] = jopt(mf->getRomPath());

	DivIde* di = minZX_->getDivIde();
	j["peripherals"]["divide"]["enabled"] = di->isEnabled();
	j["peripherals"]["divide"]["rom"] = jopt(di->getRomPath());
	j["peripherals"]["divide"]["disk"] = jopt(di->getDiskPath());

	Dandanator* da = minZX_->getDandanator();
	j["peripherals"]["dandanator"]["enabled"] = da->isEnabled();
	j["peripherals"]["dandanator"]["rom"] = jopt(da->getRomPath());

	Multiply* mu = minZX_->getMultiply();
	j["peripherals"]["multiply"]["enabled"] = mu->isEnabled();
	j["peripherals"]["multiply"]["bridgeHost"] = jopt(mu->getBridgeHost());
	j["peripherals"]["multiply"]["bridgePort"] = joptPort(mu->getBridgePort());

	SDI_8255* sdi = minZX_->getSDI8255();
	j["peripherals"]["sdi8255"]["enabled"] = sdi->isEnabled();
	j["peripherals"]["sdi8255"]["rom"] = jopt(sdi->getRomPath());
	j["peripherals"]["sdi8255"]["bridgeHost"] = jopt(sdi->getBridgeHost());
	j["peripherals"]["sdi8255"]["bridgePort"] = joptPort(sdi->getBridgePort());

	Fdd3000* fd = minZX_->getFdd3000();
	j["peripherals"]["fdd3000"]["enabled"] = fd->isEnabled();
	j["peripherals"]["fdd3000"]["rom"] = jopt(fd->getRomPath());
	j["peripherals"]["fdd3000"]["stationRom"] = jopt(fd->getStationRomPath());
	static const char* kDiskKeys[4] = { "diskA", "diskB", "diskC", "diskD" };
	for (int i = 0; i < 4; i++)
		j["peripherals"]["fdd3000"][kDiskKeys[i]] = jopt(fd->getDiskPath(i));

	ZiFi* zf = minZX_->getZiFi();
	j["peripherals"]["zifi"]["host"] = jopt(zf->getLastHost());
	j["peripherals"]["zifi"]["port"] = joptPort(zf->getLastPort());

	std::ofstream out(settingsPath_, std::ios::trunc);
	if (!out) { setStatus("Save FAILED: " + settingsPath_, false); return; }
	out << j.dump(2) << "\n";
	setStatus("Saved to " + settingsPath_, true);
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
	mode_ = Mode::NetworkMenu;
	netSel_ = 0;
}

void OsdMenu::reconnectZiFi()
{
	ZiFi* zifi = minZX_->getZiFi();
	if (zifi && zifi->reconnect())
		setStatus("ZIFI: RECONNECTED", true);
	else
		setStatus("ZIFI: RECONNECT FAILED", false);
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

void OsdMenu::forgetSelectedRemote()
{
	int n = (int)remotes_.size();
	if (remoteSel_ < 0 || remoteSel_ >= n)
		return; // on the "[ADD REMOTE]" row, or the list is empty
	remotes_.erase(remotes_.begin() + remoteSel_);
	saveRemotes();
	if (remoteSel_ > (int)remotes_.size())
		remoteSel_ = (int)remotes_.size();
}

void OsdMenu::reloadRemoteBrowser()
{
	if (!remoteList(entries_))
	{
		setStatus("Reload FAILED", false);
		return;
	}
	if (entries_.empty())
		fileSel_ = 0;
	else if (fileSel_ >= (int)entries_.size())
		fileSel_ = (int)entries_.size() - 1;
	if (fileScroll_ > fileSel_)
		fileScroll_ = fileSel_;
	setStatus("Reloaded", true);
}

void OsdMenu::saveSelectedRemoteFile()
{
	if (entries_.empty())
		return;
	const Entry e = entries_[fileSel_];
	if (e.name == "..")
		return;
	if (e.isDir)
	{
		setStatus("Can't save a directory", false);
		return;
	}

	std::string realName = (activeProto_ == Proto::Gopher) ? gopher_.downloadBasename(e.name) : e.name;
	std::string destPath = "./" + realName;
	bool ok = (activeProto_ == Proto::Gopher) ? gopher_.getToFile(e.name, destPath)
	                                           : tnfs_.getToFile(e.name, destPath);
	setStatus(ok ? (std::string("Saved: ") + realName) : (std::string("Save FAILED: ") + realName), ok);
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
	textPurpose_ = TextPurpose::AddRemote;
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
	if (textPurpose_ == TextPurpose::PeriphHostPort)
	{
		size_t colon = textBuf_.rfind(':');
		if (colon == std::string::npos || colon == 0 || colon == textBuf_.size() - 1)
		{
			setStatus("Expected HOST:PORT", false);
			return;
		}
		std::string host = textBuf_.substr(0, colon);
		int port = atoi(textBuf_.substr(colon + 1).c_str());
		bool ok = false;
		switch (detailPeriphId_)
		{
			case PeriphId::ZiFi:       ok = minZX_->getZiFi()->connectTo(host.c_str(), port); break;
			case PeriphId::Dandanator: ok = minZX_->getMultiply()->connectTo(host.c_str(), port); break;
			case PeriphId::Sdi8255:    ok = minZX_->getSDI8255()->connectBridge(host.c_str(), port); break;
			default: break;
		}
		setStatus(ok ? "Connected" : "Connect FAILED", ok);
		mode_ = Mode::PeriphDetail;
		return;
	}

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
			case Mode::StorageMenu:   mode_ = Mode::TopMenu; break;
			case Mode::FileBrowser:
				// Peripheral ROM/disk pickers return to that peripheral's
				// own detail screen, same as a successful load does (see
				// activateFile()) -- everything else (Snapshot/Tape) goes
				// back to StorageMenu as before.
				mode_ = (browseAction_ == Action::LoadSnapshot || browseAction_ == Action::LoadTape)
				      ? Mode::StorageMenu : Mode::PeriphDetail;
				break;
			case Mode::NetworkMenu:   mode_ = Mode::TopMenu; break;
			case Mode::RemoteList:    mode_ = Mode::NetworkMenu; break;
			case Mode::ProtoPicker:   mode_ = Mode::RemoteList; break;
			case Mode::TextInput:
				// AddRemote abandons the whole wizard back to RemoteList;
				// a peripheral HOST:PORT field just goes back to that
				// peripheral's own detail screen (see startHostPortField()).
				mode_ = (textPurpose_ == TextPurpose::PeriphHostPort) ? Mode::PeriphDetail : Mode::RemoteList;
				break;
			case Mode::RemoteBrowser: remoteDisconnect(); mode_ = Mode::RemoteList; break;
			case Mode::ZxKeyboard:    mode_ = Mode::TopMenu; break;
			case Mode::OptionsMenu:   mode_ = Mode::TopMenu; break;
			case Mode::MachinePicker: mode_ = Mode::OptionsMenu; break;
			case Mode::ResetMenu:     mode_ = Mode::TopMenu; break;
			case Mode::PeriphDetail:
				mode_ = (detailPeriphId_ == PeriphId::ZiFi) ? Mode::NetworkMenu : Mode::StorageMenu;
				break;
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
	else if (mode_ == Mode::StorageMenu)
	{
		if (sc == SDL_SCANCODE_UP)
			storageSel_ = (storageSel_ - 1 + kStorageItemCount) % kStorageItemCount;
		else if (sc == SDL_SCANCODE_DOWN)
			storageSel_ = (storageSel_ + 1) % kStorageItemCount;
		else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
			activateStorage();
	}
	else if (mode_ == Mode::ZxKeyboard)
	{
		if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
			mode_ = Mode::TopMenu; // read-only -- Enter and Esc do the same thing
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
			else if (netSel_ == 1) openPeriphDetail(PeriphId::ZiFi);
		}
	}
	else if (mode_ == Mode::OptionsMenu)
	{
		if (sc == SDL_SCANCODE_UP)
			optionsSel_ = (optionsSel_ - 1 + kOptionsItemCount) % kOptionsItemCount;
		else if (sc == SDL_SCANCODE_DOWN)
			optionsSel_ = (optionsSel_ + 1) % kOptionsItemCount;
		else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
			activateOptions();
	}
	else if (mode_ == Mode::MachinePicker)
	{
		if (sc == SDL_SCANCODE_UP)
			machinePickerSel_ = (machinePickerSel_ - 1 + 3) % 3;
		else if (sc == SDL_SCANCODE_DOWN)
			machinePickerSel_ = (machinePickerSel_ + 1) % 3;
		else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
			activateMachinePicker();
	}
	else if (mode_ == Mode::ResetMenu)
	{
		if (sc == SDL_SCANCODE_UP)
			resetSel_ = (resetSel_ - 1 + kResetItemCount) % kResetItemCount;
		else if (sc == SDL_SCANCODE_DOWN)
			resetSel_ = (resetSel_ + 1) % kResetItemCount;
		else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
			activateReset();
	}
	else if (mode_ == Mode::PeriphDetail)
	{
		int n = periphDetailRowCount(detailPeriphId_);
		if (sc == SDL_SCANCODE_UP)
			periphDetailSel_ = (periphDetailSel_ - 1 + n) % n;
		else if (sc == SDL_SCANCODE_DOWN)
			periphDetailSel_ = (periphDetailSel_ + 1) % n;
		else if (sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_KP_ENTER)
			activatePeriphDetailRow(detailPeriphId_, periphDetailSel_);
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
		else if (sc == SDL_SCANCODE_F8)
			forgetSelectedRemote();
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
		if (mode_ == Mode::RemoteBrowser && sc == SDL_SCANCODE_F2)
		{
			reloadRemoteBrowser(); // doesn't need a non-empty selection, unlike everything below
			return;
		}
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
		else if (mode_ == Mode::RemoteBrowser && sc == SDL_SCANCODE_F5)
			saveSelectedRemoteFile();
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

// mago1 (see OsdFont.h) is PROPORTIONAL -- every glyph has its own pixel
// width (OsdGlyph::width), unlike the earlier fixed-6px bitluni font.
// Codes outside [32, 32+95) fall back to '?'.
int OsdMenu::glyphWidth(unsigned char ch) const
{
	int rel = (int)ch - kOsdFontFirstChar;
	if (rel < 0 || rel >= kOsdFontCharCount) rel = '?' - kOsdFontFirstChar;
	return kOsdFont[rel].width;
}

int OsdMenu::textWidth(const std::string& text) const
{
	int w = 0;
	for (unsigned char ch : text)
		w += glyphWidth(ch) * kFontScale + kFontScale; // glyph + 1px inter-glyph gap
	return w;
}

std::string OsdMenu::truncateToWidth(const std::string& s, int maxWidthPx) const
{
	int w = 0;
	for (size_t i = 0; i < s.size(); i++)
	{
		w += glyphWidth((unsigned char)s[i]) * kFontScale + kFontScale;
		if (w > maxWidthPx)
			return s.substr(0, i);
	}
	return s;
}

void OsdMenu::drawText(uint8_t* pixels, int texW, int texH, int x, int y, const std::string& text, uint32_t argb) const
{
	uint8_t a = (argb >> 24) & 0xFF, r = (argb >> 16) & 0xFF, g = (argb >> 8) & 0xFF, b = argb & 0xFF;
	int cx = x;
	for (unsigned char ch : text)
	{
		int rel = (int)ch - kOsdFontFirstChar;
		if (rel < 0 || rel >= kOsdFontCharCount) rel = '?' - kOsdFontFirstChar;
		const OsdGlyph& glyph = kOsdFont[rel];
		for (int row = 0; row < kOsdFontHeight; row++)
		{
			uint8_t bits = glyph.rows[row];
			for (int col = 0; col < glyph.width; col++)
			{
				if (!(bits & (1 << (glyph.width - 1 - col))))
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
		cx += glyph.width * kFontScale + kFontScale; // glyph width + 1px gap
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

// No inter-row gap at all -- row height is exactly the font's own cell
// height, nothing added. Kept as a named constant (not just deleted
// from the rowH formulas) so a future tweak has one obvious place to
// change, rather than re-deriving which "+N" to resurrect.
static const int kRowGap = 0;

void OsdMenu::renderSimpleList(uint8_t* pixels, int texW, int texH, const char* title,
                                const std::vector<std::string>& items, int selIndex,
                                const std::string& statusLine, bool statusOk) const
{
	const int rowH = kOsdFontHeight * kFontScale + kRowGap;
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
	int maxWidthPx = boxW - 4 * kFontScale;
	for (size_t i = 0; i < items.size(); i++)
	{
		int rowY = rowY0 + (int)i * rowH;
		bool sel = ((int)i == selIndex);
		fillRect(pixels, texW, texH, boxX, rowY - 1, boxW, rowH, sel ? kOsdCyanBri : kOsdWhiteBri);
		drawText(pixels, texW, texH, textX, rowY, truncateToWidth(items[i], maxWidthPx), kOsdBlack);
	}
	if (extraRows)
	{
		int rowY = rowY0 + (int)items.size() * rowH;
		fillRect(pixels, texW, texH, boxX, rowY - 1, boxW, rowH, kOsdWhiteBri);
		drawText(pixels, texW, texH, textX, rowY, truncateToWidth(statusLine, maxWidthPx), statusOk ? kOsdGreenBri : kOsdRedBri);
	}
}

// Symbol Shift legend verified against the official "Recreated ZX
// Spectrum Keyboard Technical Document" (QWERTY Key List, Symbol Shift
// column) -- see OsdMenu.h. On JASE's own keyboard matrix (minzx.cpp),
// Symbol Shift is (left or right) Alt. "GBP" spells out the pound sign
// since it isn't in mago1's ASCII range either. The six codepoints
// mago1's "Eye Candy" variant otherwise repurposes as decorative icons
// (^ _ { | } ~) are hand-drawn overrides in bake_mago_font.py instead,
// so they're safe to use literally here (and anywhere else real text
// might contain them -- see that script for the actual bug this fixed).
void OsdMenu::renderZxKeyboard(uint8_t* pixels, int texW, int texH) const
{
	std::vector<std::string> lines = {
		"SYMBOL SHIFT (ALT) + KEY =",
		"1=! 2=@ 3=# 4=$ 5=% 6=& 7=' 8=( 9=) 0=_",
		"Q=<= W=<> E=>= R=< T=> Y=[ U=] O=; P=\"",
		"A=~ S=| D=\\ F={ G=} H=^ J=- K=+ L==",
		"Z=: X=GBP C=? V=/ B=* N=, M=.",
	};
	renderSimpleList(pixels, texW, texH, "ZX KEYBOARD", lines, -1);
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
		renderSimpleList(pixels, texW, texH, "NETWORK", { "REMOTE >", "ZIFI >" }, netSel_,
			statusFramesLeft_ > 0 ? status_ : "", statusOk_);
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
		const char* title = remotes_.empty() ? "REMOTE" : "REMOTE (F8=FORGET)";
		renderSimpleList(pixels, texW, texH, title, items, remoteSel_,
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
		const char* title = (textPurpose_ == TextPurpose::PeriphHostPort) ? "CONNECT" : "ADD REMOTE";
		renderSimpleList(pixels, texW, texH, title, { line }, -1);
		return;
	}
	if (mode_ == Mode::TopMenu)
	{
		static const char* kItems[kTopItemCount] = {
			"STORAGE >", "OPTIONS >", "RESET >", "NETWORK >", "ZX KEYBOARD", "CLOSE MENU",
		};
		renderSimpleList(pixels, texW, texH, "JASE MENU",
			std::vector<std::string>(kItems, kItems + kTopItemCount), topSel_,
			statusFramesLeft_ > 0 ? status_ : "", statusOk_);
		return;
	}
	if (mode_ == Mode::StorageMenu)
	{
		static const char* kItems[kStorageItemCount] = {
			"LOAD SNAPSHOT (.SNA/.Z80)",
			"LOAD TAPE (.TAP/.TZX)",
			"SAVE SNAPSHOT AS .SNA",
			"SAVE SNAPSHOT AS .Z80",
			"MULTIFACE ONE >",
			"DIVIDE >",
			"DANDANATOR >",
			"SDI8255 >",
			"FDD3000 >",
		};
		renderSimpleList(pixels, texW, texH, "STORAGE",
			std::vector<std::string>(kItems, kItems + kStorageItemCount), storageSel_,
			statusFramesLeft_ > 0 ? status_ : "", statusOk_);
		return;
	}
	if (mode_ == Mode::OptionsMenu)
	{
		std::vector<std::string> items = {
			std::string("PREFERRED MACHINE: ") + kMachineNames[prefMachineIdx_] + " >",
			std::string("TURBOSOUND (RESTART): ") + (prefTurboSound_ ? "ON" : "OFF"),
			std::string("TIMEX HIRES: ") + (minZX_->isTimexVideo() ? "ON" : "OFF"),
			std::string("PC KEYS: ") + (minZX_->isPcKeys() ? "ON" : "OFF"),
			std::string("TAPE FASTLOAD: ") + (minZX_->isTapeFastload() ? "ON" : "OFF"),
		};
		renderSimpleList(pixels, texW, texH, "OPTIONS", items, optionsSel_,
			statusFramesLeft_ > 0 ? status_ : "", statusOk_);
		return;
	}
	if (mode_ == Mode::MachinePicker)
	{
		renderSimpleList(pixels, texW, texH, "PREFERRED MACHINE (RESTART)",
			{ kMachineNames[0], kMachineNames[1], kMachineNames[2] }, machinePickerSel_);
		return;
	}
	if (mode_ == Mode::ResetMenu)
	{
		renderSimpleList(pixels, texW, texH, "RESET",
			{ "RESET (F11)", "SAVE CONFIG" }, resetSel_,
			statusFramesLeft_ > 0 ? status_ : "", statusOk_);
		return;
	}
	if (mode_ == Mode::PeriphDetail)
	{
		std::vector<std::string> items;
		periphDetailRows(detailPeriphId_, items);
		static const char* kTitles[] = { "MULTIFACE ONE", "DIVIDE", "DANDANATOR", "SDI8255", "FDD3000", "ZIFI" };
		renderSimpleList(pixels, texW, texH, kTitles[(int)detailPeriphId_], items, periphDetailSel_,
			statusFramesLeft_ > 0 ? status_ : "", statusOk_);
		return;
	}
	if (mode_ == Mode::ZxKeyboard)
	{
		renderZxKeyboard(pixels, texW, texH);
		return;
	}

	// FileBrowser or RemoteBrowser -- same Entry{name,isDir} shape, same
	// scroll/cursor fields, laid out with the fixed-row scroller instead
	// of renderSimpleList() (which sizes its box to fit every item, no
	// good for a directory that might have hundreds of entries). Row
	// spacing, the "name ... <DIR>" right-aligned marker, the separate
	// path/breadcrumb row, and the always-on hotkey legend all match a
	// real pico-spec device photo of its own TNFS "Open File" browser.
	const int rowH = kOsdFontHeight * kFontScale + kRowGap;
	const int titleH = rowH;
	const int border = kFontScale;
	bool showStatus = statusFramesLeft_ > 0;
	int boxW = texW - 40;
	int boxH = titleH /*title*/ + titleH /*path*/ + kFileRows * rowH + titleH /*legend*/ + (showStatus ? rowH : 0) + 4;
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
	int maxWidthPx = boxW - 4 * kFontScale;

	// Path/breadcrumb row -- a second header line, same style as the
	// title (continues the black bar rather than a separate box).
	std::string path = truncateToWidth((mode_ == Mode::RemoteBrowser) ? remoteCwdPath() : cwd_, maxWidthPx);
	fillRect(pixels, texW, texH, boxX, rowY0, boxW, titleH, kOsdBlack);
	drawText(pixels, texW, texH, textX, rowY0 + kFontScale, path, kOsdWhiteBri);
	rowY0 += titleH;

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
		bool sel = (idx == fileSel_);
		fillRect(pixels, texW, texH, boxX, rowY - 1, boxW, rowH, sel ? kOsdCyanBri : kOsdWhiteBri);

		static const char* kDirMarker = "<DIR>";
		std::string label = e.name;
		if (e.isDir && e.name != "..")
		{
			int markerW = textWidth(kDirMarker);
			int avail = maxWidthPx > markerW + kFontScale ? maxWidthPx - markerW - kFontScale : maxWidthPx;
			label = truncateToWidth(label, avail);
			int markerX = boxX + boxW - 2 * kFontScale - markerW;
			drawText(pixels, texW, texH, markerX, rowY, kDirMarker, kOsdBlack);
		}
		else
		{
			label = truncateToWidth(label, maxWidthPx);
		}
		drawText(pixels, texW, texH, textX, rowY, label, kOsdBlack);
	}
	rowY0 += kFileRows * rowH;

	// Hotkey legend -- always visible (not transient like status_), and
	// only lists keys this build actually implements (see handleKey()):
	// real pico-spec screens also show F7 Upload/F8 Del here, which JASE
	// doesn't support yet, so they're left off rather than shown as dead
	// keys.
	fillRect(pixels, texW, texH, boxX, rowY0, boxW, titleH, kOsdBlack);
	const char* legend = (mode_ == Mode::RemoteBrowser) ? "ENT=LOAD  F2=RELOAD  F5=SAVE A COPY"
	                                                     : "ENT=LOAD";
	drawText(pixels, texW, texH, textX, rowY0 + kFontScale, legend, kOsdWhiteBri);
	rowY0 += titleH;

	if (showStatus)
	{
		fillRect(pixels, texW, texH, boxX, rowY0 - 1, boxW, rowH, kOsdWhiteBri);
		drawText(pixels, texW, texH, textX, rowY0, truncateToWidth(status_, maxWidthPx), statusOk_ ? kOsdGreenBri : kOsdRedBri);
	}
}
