// dsk_browser -- dwupanelowa (Midnight Commander-style) przeglądarka
// R/W dla obrazów .dsk CP/M i (tylko odczyt) Timex TOS, stylizowana na
// /home/pawelb/src/tnfsd_v2/tnfs_server/gopher_browser.py (ten sam
// układ: Tab przełącza panel, Insert/Ctrl-T zaznacza wiele pozycji, F5
// kopiuje między panelami, F3 podgląd, F2 odśwież, F10/q wyjście).
//
// 2026-11-21 REDESIGN (user-directed): obie strony startują jako
// zwykłe panele LOKALNE -- Enter na pliku ".dsk" w DOWOLNYM panelu
// OTWIERA jego zawartość w tym samym miejscu (auto-detekcja formatu
// CP/M vs TOS), zamiast wymagać ścieżki .dsk jako argumentu CLI z
// jednym panelem na trwałe przypisanym do obrazu.
//
// Format auto-detekcja: TOS ma OBOWIĄZKOWY pierwszy wpis katalogu
// (ścieżka 4, sektor 0) z K=0xFF i ext="DIR" (nazwa dysku -- patrz
// src/TosFs.h), sprawdzany jako pierwszy, silny sygnał. CP/M nie ma
// takiej gwarancji na konkretnej pozycji, więc jego sprawdzenie skanuje
// wszystkie 16 sektorów ścieżki 2 (w KOLEJNOŚCI FIZYCZNEJ, bez translate
// table -- wystarczy znaleźć JEDEN sensowny wpis, dokładnie ta sama
// obserwacja co w CpmFs's own class comment: odwiedzenie wszystkich 16
// sektorów w dowolnej kolejności znajdzie te samo wpisy).
//
// TOS "katalogi" (patrz src/TosFs.h) to nie nawigacja po ścieżkach --
// to zmiana filtra "numeru użytkownika" nad tym samym, jedynym
// katalogiem. Enter na wpisie <DIR> w panelu TOS wchodzi (push), Backspace
// wychodzi (pop); Backspace w korzeniu zamyka obraz z powrotem do
// widoku lokalnego (tak samo jak Backspace na CP/M, który jest płaski
// i zawsze "w korzeniu").
//
// CP/M ma pełny odczyt/zapis (CpmFs, już zweryfikowana żywym bootem).
// TOS jest TYLKO DO ODCZYTU -- TosFs nie implementuje zapisu (żaden
// realny sampel do zweryfikowania formatu zapisu, patrz jej własny
// komentarz klasy), F5 Lokalny->TOS i F8 na TOS zwracają błąd statusu
// zamiast cokolwiek próbować.
//
// Każdy writeFile()/deleteFile() w CpmFs sam robi DskImage::flush() --
// nie ma tu osobnego kroku "Zapisz", każda operacja od razu trwale
// trafia do pliku .dsk na dysku.

#include "../src/CpmFs.h"
#include "../src/TosFs.h"
#include "../src/DskImage.h"

#include <ncurses.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// ── Model danych ────────────────────────────────────────────────────────

struct Entry {
	std::string name;
	bool isDir = false;
	long size = 0;
	bool readOnly = false; // CP/M R/O attribute, or TOS "protected" attribute
	bool system = false;
};

// ── Uruchamianie zewnętrznego programu bez shella (fork/execvp) --
// zobacz gopher_browser.py's _view_file: nazwy plików (stąd: z obrazu
// .dsk albo z lokalnego katalogu) mogą zawierać znaki specjalne dla
// shella, więc system()/popen() byłyby tu wstrzyknięciem polecenia. ──
static void runSubprocess(const std::vector<std::string>& argv)
{
	std::vector<char*> cargv;
	for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
	cargv.push_back(nullptr);

	pid_t pid = fork();
	if (pid == 0) {
		execvp(cargv[0], cargv.data());
		_exit(127);
	} else if (pid > 0) {
		int status = 0;
		waitpid(pid, &status, 0);
	}
}

static bool programExists(const std::string& name)
{
	std::string cmd = "command -v " + name + " >/dev/null 2>&1";
	return system(cmd.c_str()) == 0;
}

static bool looksLikeDskFile(const std::string& name)
{
	if (name.size() < 4) return false;
	std::string ext = name.substr(name.size() - 4);
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	return ext == ".dsk";
}

// ── Panel bazowy (uproszczony odpowiednik Panel z gopher_browser.py) ────

class Panel
{
public:
	WINDOW* win;
	std::string title;
	std::vector<Entry> items;
	int cursor = 0, offset = 0;
	std::string path;
	std::string error;
	bool loading = false;
	std::vector<std::string> marked;

	explicit Panel(WINDOW* w, const std::string& t) : win(w), title(t) {}
	virtual ~Panel() = default;

	int height() const { int h, w; getmaxyx(win, h, w); (void)w; return h - 2; }
	int width() const { int h, w; getmaxyx(win, h, w); (void)h; return w - 2; }

	void move(int delta)
	{
		int n = (int)items.size();
		if (n == 0) return;
		cursor = std::max(0, std::min(n - 1, cursor + delta));
		int h = height();
		if (cursor < offset) offset = cursor;
		else if (cursor >= offset + h) offset = cursor - h + 1;
	}
	void goFirst() { cursor = 0; offset = 0; }
	void goLast()
	{
		cursor = std::max(0, (int)items.size() - 1);
		offset = std::max(0, cursor - height() + 1);
	}
	const Entry* selected() const
	{
		if (!items.empty() && cursor >= 0 && cursor < (int)items.size()) return &items[cursor];
		return nullptr;
	}
	bool isMarked(const std::string& name) const
	{
		return std::find(marked.begin(), marked.end(), name) != marked.end();
	}
	void toggleMark()
	{
		const Entry* e = selected();
		if (!e || e->name == "..") return;
		auto it = std::find(marked.begin(), marked.end(), e->name);
		if (it != marked.end()) marked.erase(it);
		else marked.push_back(e->name);
		move(1);
	}
	std::vector<Entry> getMarked() const
	{
		std::vector<Entry> out;
		for (auto& e : items) if (isMarked(e.name)) out.push_back(e);
		return out;
	}
	void pruneMarks()
	{
		std::vector<std::string> kept;
		for (auto& n : marked) {
			bool found = false;
			for (auto& e : items) if (e.name == n) { found = true; break; }
			if (found) kept.push_back(n);
		}
		marked = kept;
	}

	static std::string fmtSize(long size, bool isDir)
	{
		char buf[32];
		if (isDir) { snprintf(buf, sizeof(buf), "%10s", "<DIR>"); return buf; }
		if (size < 1024) snprintf(buf, sizeof(buf), "%10ld", size);
		else if (size < 1024 * 1024) snprintf(buf, sizeof(buf), "%9.1fK", size / 1024.0);
		else snprintf(buf, sizeof(buf), "%9.1fM", size / (1024.0 * 1024));
		return buf;
	}

	void draw(bool active)
	{
		werase(win);
		int h, w; getmaxyx(win, h, w);

		wattron(win, COLOR_PAIR(1) | (active ? A_BOLD : 0));
		box(win, 0, 0);
		wattroff(win, COLOR_PAIR(1) | (active ? A_BOLD : 0));

		std::string t = " " + title + ": " + path + " ";
		if (!marked.empty()) t = " " + title + ": " + path + " [" + std::to_string(marked.size()) + " zazn.] ";
		if ((int)t.size() > w - 4) t = t.substr(0, w - 7) + "... ";
		wattron(win, COLOR_PAIR(3) | A_BOLD);
		mvwaddnstr(win, 0, 2, t.c_str(), w - 4);
		wattroff(win, COLOR_PAIR(3) | A_BOLD);

		if (loading) {
			wattron(win, COLOR_PAIR(4));
			mvwaddstr(win, h / 2, (w - 12) / 2, " Ladowanie... ");
			wattroff(win, COLOR_PAIR(4));
			wnoutrefresh(win);
			return;
		}
		if (!error.empty()) {
			wattron(win, COLOR_PAIR(5) | A_BOLD);
			mvwaddnstr(win, h / 2, 2, error.c_str(), w - 4);
			wattroff(win, COLOR_PAIR(5) | A_BOLD);
			wnoutrefresh(win);
			return;
		}

		int panelH = h - 2;
		for (int row = 0; row < panelH; row++) {
			int idx = offset + row;
			if (idx >= (int)items.size()) break;
			const Entry& e = items[idx];
			bool isCur = (idx == cursor) && active;
			bool isSel = (idx == cursor);
			bool marked_ = isMarked(e.name);

			int nameW = w - 24;
			if (nameW < 1) nameW = 1;
			std::string name = std::string(marked_ ? "*" : " ") + e.name + (e.isDir ? "/" : "");
			if ((int)name.size() > nameW) name = name.substr(0, nameW - 1) + "...";
			name.resize(nameW, ' ');

			std::string line = name + fmtSize(e.size, e.isDir);
			if ((int)line.size() > w - 2) line.resize(w - 2);

			int attr;
			if (marked_) attr = (isCur ? COLOR_PAIR(13) : COLOR_PAIR(12)) | A_BOLD;
			else if (isCur) attr = COLOR_PAIR(2) | A_BOLD;
			else if (isSel && !active) attr = COLOR_PAIR(6);
			else if (e.isDir) attr = COLOR_PAIR(3);
			else attr = COLOR_PAIR(0);

			wattron(win, attr);
			mvwaddnstr(win, row + 1, 1, line.c_str(), w - 2);
			wattroff(win, attr);
		}
		wnoutrefresh(win);
	}

	virtual void load() = 0;
	virtual bool enter() = 0; // true jesli wejscie zmienilo panel (katalog), false = plik (podglad)
	virtual void goUp() = 0;
};

// ── Panel lokalny ─────────────────────────────────────────────────────────

#include <dirent.h>
#include <sys/stat.h>

class LocalPanel : public Panel
{
public:
	explicit LocalPanel(WINDOW* w, const std::string& startPath = "") : Panel(w, "Lokalny")
	{
		if (!startPath.empty()) { path = startPath; return; }
		const char* home = getenv("HOME");
		path = home ? home : "/";
	}

	void load() override
	{
		error.clear();
		DIR* d = opendir(path.c_str());
		if (!d) { error = strerror(errno); items.clear(); return; }
		std::vector<Entry> entries;
		if (path != "/") entries.push_back({"..", true, 0, false, false});
		struct dirent* de;
		std::vector<std::string> names;
		while ((de = readdir(d)) != nullptr) {
			std::string n = de->d_name;
			if (n == "." || n == "..") continue;
			names.push_back(n);
		}
		closedir(d);
		std::sort(names.begin(), names.end(), [&](const std::string& a, const std::string& b) {
			std::string fa = path + "/" + a, fb = path + "/" + b;
			struct stat sa{}, sb{};
			bool da = (stat(fa.c_str(), &sa) == 0) && S_ISDIR(sa.st_mode);
			bool db = (stat(fb.c_str(), &sb) == 0) && S_ISDIR(sb.st_mode);
			if (da != db) return da > db;
			std::string la = a, lb = b;
			std::transform(la.begin(), la.end(), la.begin(), ::tolower);
			std::transform(lb.begin(), lb.end(), lb.begin(), ::tolower);
			return la < lb;
		});
		for (auto& n : names) {
			std::string full = path + "/" + n;
			struct stat st{};
			Entry e;
			e.name = n;
			if (stat(full.c_str(), &st) == 0) {
				e.isDir = S_ISDIR(st.st_mode);
				e.size = (long)st.st_size;
			}
			entries.push_back(e);
		}
		items = entries;
		pruneMarks();
	}

	// Enter na katalogu zmienia panel (true); Enter na ".dsk" NIE jest
	// obsłużone tutaj (App::run() sprawdza to przed wywołaniem enter(),
	// żeby mogło zamienić caly panel na DskPanel) -- zwykły plik zwraca
	// false (podgląd, jak dotąd).
	bool enter() override
	{
		const Entry* e = selected();
		if (!e) return false;
		if (e->name == "..") {
			size_t slash = path.find_last_of('/');
			std::string parent = (slash == 0) ? "/" : path.substr(0, slash);
			if (parent.empty()) parent = "/";
			if (parent == path) return false;
			path = parent;
			load();
			cursor = offset = 0;
			return true;
		}
		if (e->isDir) {
			path = (path == "/") ? ("/" + e->name) : (path + "/" + e->name);
			load();
			cursor = offset = 0;
			return true;
		}
		return false;
	}

	void goUp() override
	{
		size_t slash = path.find_last_of('/');
		std::string parent = (slash == 0) ? "/" : path.substr(0, slash);
		if (parent.empty()) parent = "/";
		if (parent != path) { path = parent; load(); cursor = offset = 0; }
	}

	// F7/F8 on the Local panel -- real POSIX mkdir/rmdir/unlink, same MC-
	// style uniformity DskPanel already has for CP/M/TOS (App::opMkdir()/
	// opDelete() dispatch here via dynamic_cast, no separate code path).
	bool createDirectory(const std::string& name)
	{
		std::string full = path + "/" + name;
		return ::mkdir(full.c_str(), 0755) == 0;
	}

	// rmdir() only removes an EMPTY directory (standard POSIX semantics,
	// same as MC itself -- no recursive delete here, out of scope).
	bool deleteEntry(const std::string& name, bool isDir)
	{
		std::string full = path + "/" + name;
		return isDir ? (::rmdir(full.c_str()) == 0) : (::unlink(full.c_str()) == 0);
	}
};

// ── Auto-detekcja formatu .dsk (CP/M vs TOS) ─────────────────────────────

enum class DiskFormat { Unknown, Cpm, Tos };

static DiskFormat detectDiskFormat(DskImage& dsk)
{
	// TOS: wpis "nazwa dysku" ZAWSZE istnieje jako pierwszy wpis katalogu
	// (K=0xFF, ext="DIR") -- silny, specyficzny sygnał (patrz
	// src/TosFs.h). Na dysku jednostronnym katalog jest na sciezce 4; na
	// "gestym" (2-stronnym, np. 620/640KB) 4 LOGICZNE zarezerwowane
	// ściezki to tylko 2 FIZYCZNE ściezki (strony sie przeplatają) --
	// katalog wtedy jest na fizycznej sciezce 2/stronie 0, potwierdzone
	// zywo na realnym dwustronnym dysku TOS ("CPM tos a2 start cpm
	// 525.dsk"). TosFs::open() samo wybiera wlasciwa geometrie na
	// podstawie numSides().
	{
		int dirTrack = (dsk.numSides() == 2) ? 2 : 4;
		int len = 0;
		uint8_t* e = dsk.findSector(dirTrack, 0, 0, len);
		if (e && len >= 32 && e[0] == 0xFF &&
		    (e[9] & 0x7F) == 'D' && (e[10] & 0x7F) == 'I' && (e[11] & 0x7F) == 'R') {
			return DiskFormat::Tos;
		}
	}
	// CP/M: jednostronne 170KB jest jedyna zweryfikowana geometria (patrz
	// CpmFs.h's own class comment) -- dwustronne gestosci maja
	// NIEZNANY uklad katalogu/translate table, wiec nie ma tu nic
	// sensownego do sprawdzenia; odrzucone explicite, zamiast ryzykowac
	// falszywe dopasowanie na przypadkowych bajtach danych (zdarzyło się
	// realnie -- "CPM 525.dsk" pokazywało smieciowy listing zanim ten
	// guard istnial).
	if (dsk.numSides() != 1) return DiskFormat::Unknown;
	// Brak gwarantowanej pozycji, więc szukamy JEDNEGO sensownego wpisu
	// gdziekolwiek w 16 fizycznych sektorach ścieżki 2 (kolejność
	// nieważna -- patrz CpmFs.h's own class comment: wizyta wszystkich 16
	// sektorów w DOWOLNYM porządku i tak znajdzie te same wpisy).
	for (int sec = 0; sec < 16; sec++) {
		int len = 0;
		uint8_t* p = dsk.findSector(2, 0, sec, len);
		if (!p) continue;
		for (int off = 0; off + 32 <= len; off += 32) {
			uint8_t* e = p + off;
			if (e[0] == 0xE5) continue;
			if (e[0] > 0x1F) continue; // CP/M user number 0-15 (0-31 na niektorych systemach)
			bool printable = true;
			for (int i = 1; i <= 11; i++) {
				uint8_t c = e[i] & 0x7F;
				if (c < 0x20 || c > 0x7E) { printable = false; break; }
			}
			if (printable) return DiskFormat::Cpm;
		}
	}
	return DiskFormat::Unknown;
}

// ── Reczny wybor formatu (Alt+F / auto-pytanie) ──────────────────────────
//
// Dwie NIEZALEZNE decyzje: (1) jaki FILESYSTEM -- CP/M albo jeden z
// czterech TosFs::Preset; (2) jak wczytac KONTENER -- normalny .dsk
// CPCEMU/Extended (DskImage::load()), albo RAW -- goly, bezglowkowy zrzut
// sekwencyjny bez ZADNEGO formatu kontenera (DskImage::loadRaw(), patrz
// jej wlasny komentarz -- user-directed 2026-11-22, maja realne pliki
// .img w tym ukladzie, np. ~/src/TOStools/*.img). Zamiast duplikowac
// wszystkie 5 wyborow filesystemu w warianty RAW (user's own
// simplification: "checkbox wystarczy by zaznaczyc ze raw"), dialog ma
// JEDEN przelacznik RAW obok listy -- kazdy filesystem juz ma swoja
// wlasna geometrie (tracks/sides), ktora RAW po prostu uzywa zamiast
// czytac ja z (nieistniejacego w RAW) naglowka kontenera.
struct FormatOption {
	std::string label;
	bool isRaw = false;
	int rawTracks = 0, rawSides = 0;
	int fsChoice = -1; // 0=CP/M, 1..4=TosFs::Preset+1 (patrz DskPanel::openDiskManual), -1=Anuluj
};

// ── Panel obrazu .dsk (CP/M R/W, lub TOS tylko-do-odczytu) ───────────────
//
// TOS "katalogi" nie są nawigacją po ściezkach -- to zmiana wartości
// filtra userStack_.back() nad tym samym, jedynym katalogiem (patrz
// src/TosFs.h). atRoot() odróżnia "Backspace wyjdź z podkatalogu" od
// "Backspace zamknij obraz", obsłużone w App::run().
class DskPanel : public Panel
{
public:
	DskImage dsk;
	DiskFormat format = DiskFormat::Unknown;
	CpmFs cpmFs;
	TosFs tosFs;
	std::vector<int> userStack{0}; // tylko TOS -- stos numerow user/subdir

	explicit DskPanel(WINDOW* w, const std::string& dskPath) : Panel(w, "Dysk")
	{
		path = dskPath;
	}

	bool openDisk(std::string& errOut)
	{
		if (!dsk.load(path)) { errOut = "Nie udalo sie wczytac obrazu."; return false; }
		format = detectDiskFormat(dsk);
		if (format == DiskFormat::Cpm) {
			if (!cpmFs.open(dsk)) { errOut = "CpmFs::open nieudane."; return false; }
			title = "CP/M";
		} else if (format == DiskFormat::Tos) {
			if (!tosFs.open(dsk)) { errOut = "TosFs::open nieudane (geometria?)."; return false; }
			title = "TOS";
		} else {
			errOut = "Nierozpoznany format .dsk (ani CP/M, ani TOS).";
			return false;
		}
		return true;
	}

	// Rozne-niz-autodetekcja otwarcie (Alt+F / auto-pytanie w App) --
	// wymusza wybrany kontener (normalny .dsk albo RAW, patrz
	// FormatOption's own comment) i filesystem, ignorujac
	// detectDiskFormat() calkowicie.
	bool openDiskManual(const FormatOption& opt, std::string& errOut)
	{
		bool loaded = opt.isRaw
			? dsk.loadRaw(path, opt.rawTracks, opt.rawSides, 16, 256)
			: dsk.load(path);
		if (!loaded) { errOut = "Nie udalo sie wczytac obrazu (zla geometria dla RAW?)."; return false; }
		userStack = {0};
		if (opt.fsChoice == 0) {
			if (!cpmFs.open(dsk)) { errOut = "CpmFs::open nieudane (geometria?)."; return false; }
			format = DiskFormat::Cpm;
			title = opt.isRaw ? "CP/M (RAW)" : "CP/M";
			return true;
		}
		auto preset = static_cast<TosFs::Preset>(opt.fsChoice - 1);
		if (!tosFs.openWithPreset(dsk, preset)) {
			errOut = std::string("TosFs::openWithPreset nieudane dla ") + TosFs::presetName(preset) +
				" (zla liczba stron dla tej geometrii?).";
			return false;
		}
		format = DiskFormat::Tos;
		title = std::string("TOS (") + TosFs::presetName(preset) + (opt.isRaw ? ", RAW)" : ")");
		return true;
	}

	bool atRoot() const { return format != DiskFormat::Tos || userStack.size() <= 1; }
	// CP/M jest zawsze R/W (zweryfikowane). TOS jest R/W TYLKO na
	// zweryfikowanej 1-stronnej geometrii -- TosFs sama odmawia
	// delete/mkdir na 2-stronnej (patrz TosFs.h's own class comment),
	// zwraca tu to samo zamiast duplikowac regule.
	bool isReadOnly() const
	{
		if (format == DiskFormat::Tos) return !tosFs.isWritableGeometry();
		return false;
	}

	void load() override
	{
		error.clear();
		items.clear();
		// ".." zawsze widoczny -- Enter na nim zamyka caly obraz (w korzeniu)
		// albo wychodzi z podkatalogu TOS (patrz App::run()'s own special-case
		// check, obok Backspace robiacego to samo). Dodane po uzywaniu na
		// zywo: samo Backspace bywa niewiarygodne miedzy terminalami/klientami,
		// widoczny wpis jest odkrywalny i nie zalezy od tego, jaki kod klawisza
		// terminal faktycznie wysyla.
		items.push_back({"..", true, 0, false, false});
		if (format == DiskFormat::Cpm) {
			for (auto& d : cpmFs.list()) {
				Entry e;
				e.name = d.name;
				e.size = (long)d.sizeBytes;
				e.readOnly = d.readOnly;
				e.system = d.system;
				items.push_back(e);
			}
		} else if (format == DiskFormat::Tos) {
			for (auto& d : tosFs.list(userStack.back())) {
				Entry e;
				e.name = d.name;
				e.isDir = d.isDir;
				e.size = (long)d.sizeBytes;
				e.readOnly = d.protected_;
				items.push_back(e);
			}
		}
		pruneMarks();
	}

	// TOS: Enter na <DIR> zmienia filtr (true). CP/M: zawsze plaskie (false).
	bool enter() override
	{
		if (format != DiskFormat::Tos) return false;
		const Entry* e = selected();
		if (!e || !e->isDir) return false;
		// Odszukaj D (numer wlasny podkatalogu) tego wpisu ponownie --
		// Entry nie przechowuje go, wiec pytamy TosFs o liste jeszcze raz.
		for (auto& d : tosFs.list(userStack.back())) {
			if (d.name == e->name && d.isDir) {
				userStack.push_back(d.dirNumber);
				load();
				cursor = offset = 0;
				return true;
			}
		}
		return false;
	}

	// Wyjscie z podkatalogu TOS (nie uzywane dla CP/M -- atRoot() jest
	// tam zawsze true, App::run() od razu zamyka obraz).
	void goUp() override
	{
		if (format == DiskFormat::Tos && userStack.size() > 1) {
			userStack.pop_back();
			load();
			cursor = offset = 0;
		}
	}

	bool readFile(const std::string& name, std::vector<uint8_t>& out)
	{
		if (format == DiskFormat::Cpm) return cpmFs.readFile(name, out);
		if (format == DiskFormat::Tos) return tosFs.readFile(name, userStack.back(), out);
		return false;
	}

	bool writeFile(const std::string& name, const std::vector<uint8_t>& data)
	{
		if (format == DiskFormat::Cpm) return cpmFs.writeFile(name, data);
		if (format == DiskFormat::Tos) return tosFs.writeFile(name, userStack.back(), data);
		return false;
	}

	bool deleteFile(const std::string& name)
	{
		if (format == DiskFormat::Cpm) return cpmFs.deleteFile(name);
		if (format == DiskFormat::Tos) return tosFs.deleteFile(name, userStack.back());
		return false;
	}

	// F7: mkdir. Tylko TOS ma podkatalogi -- CP/M 2.2 nie, wiec wywolujacy
	// (App::opMkdir()) musi sam sprawdzic format przed pokazaniem dialogu.
	bool createDirectory(const std::string& name)
	{
		if (format != DiskFormat::Tos) return false;
		int newD = 0;
		if (!tosFs.createDirectory(name, userStack.back(), newD)) return false;
		load();
		return true;
	}
};

// ── Stopka F-klawiszy / status ───────────────────────────────────────────

static void drawFkeys(int screenH, int screenW)
{
	move(screenH - 1, 0);
	clrtoeol();
	struct FKey { const char* key; const char* label; };
	static const FKey keys[] = {
		{"Ins", "Zaznacz"}, {"F2", "Odswiez"}, {"F3", "Podglad"}, {"F5", "Kopiuj"},
		{"F7", "Mkdir"}, {"F8", "Usun"}, {"Tab", "Panel"}, {"F10", "Wyjscie"},
	};
	int n = (int)(sizeof(keys) / sizeof(keys[0]));
	int x = 0;
	for (int i = 0; i < n && x < screenW - 1; i++) {
		attron(COLOR_PAIR(7) | A_BOLD);
		mvaddnstr(screenH - 1, x, keys[i].key, screenW - 1 - x);
		attroff(COLOR_PAIR(7) | A_BOLD);
		x += (int)strlen(keys[i].key);
		attron(COLOR_PAIR(8));
		mvaddnstr(screenH - 1, x, keys[i].label, screenW - 1 - x);
		attroff(COLOR_PAIR(8));
		x += (int)strlen(keys[i].label) + 1;
	}
}

static void drawStatus(int screenH, int screenW, const std::string& msg, bool isError)
{
	move(screenH - 2, 0);
	clrtoeol();
	int attr = isError ? (COLOR_PAIR(5) | A_BOLD) : COLOR_PAIR(4);
	attron(attr);
	mvaddnstr(screenH - 2, 0, msg.c_str(), screenW - 1);
	attroff(attr);
}

// ── Dialogi (prosty input / confirm, modelowane na gopher_browser.py) ───

static bool dialogConfirm(const std::string& question)
{
	int sh, sw; getmaxyx(stdscr, sh, sw);
	int dw = std::min(sw - 4, std::max(50, (int)question.size() + 10));
	int dh = 5;
	int dy = (sh - dh) / 2, dx = (sw - dw) / 2;
	WINDOW* win = newwin(dh, dw, dy, dx);
	wattron(win, COLOR_PAIR(1));
	box(win, 0, 0);
	wattroff(win, COLOR_PAIR(1));
	wattron(win, COLOR_PAIR(5) | A_BOLD);
	mvwaddnstr(win, 1, 2, question.c_str(), dw - 4);
	wattroff(win, COLOR_PAIR(5) | A_BOLD);
	wattron(win, COLOR_PAIR(4));
	mvwaddstr(win, 3, (dw - 18) / 2, " [Enter/T]ak  [N]ie ");
	wattroff(win, COLOR_PAIR(4));
	wrefresh(win);
	int ch = wgetch(win);
	delwin(win);
	return ch == '\n' || ch == '\r' || ch == KEY_ENTER || ch == 't' || ch == 'T' || ch == 'y' || ch == 'Y';
}

// Zwraca std::string przy Enter, "" + ok=false przy Esc.
static std::string dialogInput(const std::string& prompt, const std::string& def, bool& ok)
{
	int sh, sw; getmaxyx(stdscr, sh, sw);
	int dw = std::min(sw - 4, std::max(60, (int)prompt.size() + 20));
	int dh = 5;
	int dy = (sh - dh) / 2, dx = (sw - dw) / 2;
	WINDOW* win = newwin(dh, dw, dy, dx);
	wattron(win, COLOR_PAIR(1));
	box(win, 0, 0);
	wattroff(win, COLOR_PAIR(1));
	wattron(win, COLOR_PAIR(3) | A_BOLD);
	mvwaddnstr(win, 1, 2, prompt.c_str(), dw - 4);
	wattroff(win, COLOR_PAIR(3) | A_BOLD);

	std::string text = def;
	curs_set(1);
	keypad(win, TRUE);
	for (;;) {
		int inpW = dw - 4;
		std::string display = text;
		if ((int)display.size() > inpW) display = display.substr(display.size() - inpW);
		wattron(win, COLOR_PAIR(6));
		mvwaddstr(win, 3, 2, std::string(inpW, ' ').c_str());
		mvwaddnstr(win, 3, 2, display.c_str(), inpW);
		wattroff(win, COLOR_PAIR(6));
		wmove(win, 3, 2 + std::min((int)text.size(), inpW));
		wrefresh(win);

		int ch = wgetch(win);
		if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
			curs_set(0);
			delwin(win);
			ok = true;
			return text;
		} else if (ch == 27) {
			curs_set(0);
			delwin(win);
			ok = false;
			return "";
		} else if (ch == KEY_BACKSPACE || ch == 127 || ch == 8) {
			if (!text.empty()) text.pop_back();
		} else if (ch >= 32 && ch < 256) {
			text.push_back((char)ch);
		}
	}
}

// Lista 5 filesystemow (CP/M + 4 TosFs::Preset) plus jeden "[ ] RAW"
// checkbox-wiersz (Space lub Enter na nim: toggle, NIE zamyka dialogu) i
// "Anuluj" -- uzyte przez format-override dialog (Alt+F / auto-pytanie
// gdy auto-detekcja zawiedzie, patrz App::chooseAndOpenDisk()). Zwraca
// FormatOption z fsChoice=-1 przy Esc/Anuluj.
static FormatOption dialogChooseFormatOption(const std::string& title_)
{
	struct Row { std::string label, desc; int fsChoice, tracks, sides; };
	std::vector<Row> rows;
	rows.push_back({"CP/M", "Wymusza CP/M (jedyna zweryfikowana geometria, 170KB).", 0, 40, 1});
	for (int p = 0; p < 4; p++) {
		auto preset = static_cast<TosFs::Preset>(p);
		int tracks, sides;
		TosFs::presetGeometry(preset, tracks, sides);
		rows.push_back({TosFs::presetName(preset), TosFs::presetDescription(preset), p + 1, tracks, sides});
	}

	bool rawToggle = false;
	int cursor = 0;
	const int kToggleRow = (int)rows.size();
	const int kCancelRow = kToggleRow + 1;
	const int kTotalRows = kCancelRow + 1;

	int sh, sw; getmaxyx(stdscr, sh, sw);
	size_t maxLabelLen = title_.size();
	for (auto& r : rows) maxLabelLen = std::max(maxLabelLen, r.label.size());
	int dw = std::min(sw - 4, std::max(60, (int)maxLabelLen + 8));
	int dh = std::min(sh - 4, kTotalRows + 4); // border+title, rows, 1 desc line, border
	int dy = (sh - dh) / 2, dx = (sw - dw) / 2;
	WINDOW* win = newwin(dh, dw, dy, dx);
	keypad(win, TRUE);

	auto drawRow = [&](int row, int logicalIdx, const std::string& text) {
		int attr = (logicalIdx == cursor) ? (COLOR_PAIR(2) | A_BOLD) : COLOR_PAIR(0);
		wattron(win, attr);
		std::string line = " " + text;
		line.resize(dw - 2, ' ');
		mvwaddnstr(win, row, 1, line.c_str(), dw - 2);
		wattroff(win, attr);
	};

	for (;;) {
		werase(win);
		wattron(win, COLOR_PAIR(1));
		box(win, 0, 0);
		wattroff(win, COLOR_PAIR(1));
		wattron(win, COLOR_PAIR(3) | A_BOLD);
		mvwaddnstr(win, 0, 2, (" " + title_ + " ").c_str(), dw - 4);
		wattroff(win, COLOR_PAIR(3) | A_BOLD);

		int row = 1;
		for (size_t i = 0; i < rows.size() && row < dh - 2; i++, row++) drawRow(row, (int)i, rows[i].label);
		if (row < dh - 2) {
			drawRow(row, kToggleRow, std::string("[") + (rawToggle ? "X" : " ") + "] RAW (bez naglowka kontenera)");
			row++;
		}
		if (row < dh - 2) drawRow(row, kCancelRow, "Anuluj");

		std::string desc;
		if (cursor >= 0 && cursor < (int)rows.size()) desc = rows[cursor].desc;
		else if (cursor == kToggleRow) desc = "Golutki, sekwencyjny zrzut bez formatu kontenera (16 sek/sciezke, 256B/sektor) -- jak ~/src/TOStools's own --raw.";
		wattron(win, COLOR_PAIR(6));
		mvwaddnstr(win, dh - 2, 2, desc.c_str(), dw - 4);
		wattroff(win, COLOR_PAIR(6));
		wrefresh(win);

		int ch = wgetch(win);
		if (ch == KEY_UP) cursor = std::max(0, cursor - 1);
		else if (ch == KEY_DOWN) cursor = std::min(kTotalRows - 1, cursor + 1);
		else if (ch == ' ' && cursor == kToggleRow) rawToggle = !rawToggle;
		else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
			if (cursor == kToggleRow) { rawToggle = !rawToggle; continue; }
			delwin(win);
			FormatOption result;
			if (cursor == kCancelRow) { result.fsChoice = -1; return result; }
			result.label = rows[cursor].label;
			result.fsChoice = rows[cursor].fsChoice;
			result.isRaw = rawToggle;
			result.rawTracks = rows[cursor].tracks;
			result.rawSides = rows[cursor].sides;
			return result;
		} else if (ch == 27) {
			delwin(win);
			FormatOption result;
			result.fsChoice = -1;
			return result;
		}
	}
}

// ── Aplikacja ─────────────────────────────────────────────────────────────

class App
{
public:
	std::unique_ptr<Panel> panels[2];
	int active = 0;
	std::string statusMsg;
	bool statusError = false;

	App(std::unique_ptr<Panel> left, std::unique_ptr<Panel> right)
	{
		panels[0] = std::move(left);
		panels[1] = std::move(right);
	}

	Panel* activePanel() { return panels[active].get(); }
	Panel* otherPanel() { return panels[1 - active].get(); }

	void status(const std::string& msg, bool err = false)
	{
		statusMsg = msg;
		statusError = err;
	}

	void redraw()
	{
		erase();
		wnoutrefresh(stdscr);
		for (int i = 0; i < 2; i++) panels[i]->draw(i == active);
		int sh, sw; getmaxyx(stdscr, sh, sw);
		drawFkeys(sh, sw);
		drawStatus(sh, sw, statusMsg, statusError);
		doupdate();
	}

	void loadActive()
	{
		Panel* p = activePanel();
		p->loading = true;
		redraw();
		p->loading = false;
		p->load();
		redraw();
	}

	void resizeLayout()
	{
		int sh, sw; getmaxyx(stdscr, sh, sw);
		int pw = std::max(1, sw / 2);
		int ph = std::max(1, sh - 2);
		wresize(panels[0]->win, ph, pw);
		mvwin(panels[0]->win, 0, 0);
		wresize(panels[1]->win, ph, sw - pw);
		mvwin(panels[1]->win, 0, pw);
	}

	static void viewFile(const std::string& path)
	{
		std::vector<std::string> cmd;
		if (programExists("less")) cmd = {"less", "-S", path};
		else cmd = {"more", path};
		endwin();
		runSubprocess(cmd);
		doupdate();
	}

	// Enter na ".dsk" w panelu lokalnym zamienia TEN panel na DskPanel
	// (zachowuje WINDOW*, wiec layout sie nie zmienia). Niepowodzenie
	// (format nierozpoznany / wczytanie nieudane) zostawia panel lokalny
	// bez zmian, z komunikatem bledu w statusie.
	void openDisk(int slot, const std::string& dskPath)
	{
		WINDOW* win = panels[slot]->win;
		auto dp = std::make_unique<DskPanel>(win, dskPath);
		std::string err;
		if (!dp->openDisk(err)) {
			// Auto-detekcja zawiodla -- zapytaj o format recznie, zamiast
			// tylko pokazac blad (user-directed, 2026-11-22: "zrobmy okno
			// modalne gdzie nasz browser pyta sie o format jesli nie
			// rozpoznal").
			chooseAndOpenDisk(slot, dskPath);
			return;
		}
		finishOpenDisk(slot, std::move(dp));
	}

	void finishOpenDisk(int slot, std::unique_ptr<DskPanel> dp)
	{
		panels[slot] = std::move(dp);
		panels[slot]->loading = true;
		redraw();
		panels[slot]->loading = false;
		panels[slot]->load();
		// Panel's own title (CP/M:/TOS:) already shows in its border --
		// no need for a redundant status-line message, which otherwise
		// stayed on screen distractingly until the next action. Still
		// clear out any STALE error from a previous failed attempt,
		// though, so it doesn't keep looking like this one failed too.
		status("");
		redraw();
	}

	// Alt+F (dowolny panel) i fallback automatyczny gdy auto-detekcja w
	// openDisk() nie rozpozna formatu -- pokazuje liste do wyboru (CP/M +
	// 4 presety TosFs, patrz src/TosFs.h) i otwiera obraz z WYMUSZONYM
	// formatem, ignorujac detectDiskFormat() calkowicie.
	void chooseAndOpenDisk(int slot, const std::string& dskPath)
	{
		FormatOption opt = dialogChooseFormatOption("Wybierz format: " + dskPath);
		redraw();
		if (opt.fsChoice < 0) return; // Esc lub "Anuluj"

		WINDOW* win = panels[slot]->win;
		auto dp = std::make_unique<DskPanel>(win, dskPath);
		std::string err;
		if (!dp->openDiskManual(opt, err)) {
			status("Nie udalo sie otworzyc " + dskPath + " jako " + opt.label +
				(opt.isRaw ? " (RAW)" : "") + ": " + err, true);
			return;
		}
		finishOpenDisk(slot, std::move(dp));
	}

	// Alt+F: na panelu Lokalnym z wybranym ".dsk" dziala jak Enter ale z
	// recznym wyborem formatu; na panelu DskPanel otwiera TEN SAM obraz
	// na nowo z innym formatem (naprawia zly auto-detect bez zamykania
	// najpierw).
	void opChooseFormat()
	{
		if (auto* lp = dynamic_cast<LocalPanel*>(activePanel())) {
			const Entry* e = lp->selected();
			if (!e || e->isDir || e->name == "..") {
				status("Alt+F: wybierz plik .dsk w panelu lokalnym (albo badz juz w panelu dysku).", true);
				return;
			}
			chooseAndOpenDisk(active, lp->path + "/" + e->name);
			return;
		}
		if (auto* dp = dynamic_cast<DskPanel*>(activePanel())) {
			chooseAndOpenDisk(active, dp->path);
			return;
		}
	}

	// Backspace na korzeniu DskPanel (CP/M: zawsze; TOS: tylko gdy nie
	// jest zagniezdzony w podkatalogu) zamyka obraz i wraca do widoku
	// lokalnego katalogu, ktory go zawieral.
	// Wspolna obsluga ".." (Enter na widocznym wpisie ".." w DskPanel) i
	// Backspace: w korzeniu zamyka caly obraz, inaczej (TOS w
	// podkatalogu) wychodzi jeden poziom wyzej.
	void goUpOrCloseDisk(int slot)
	{
		auto* dp = dynamic_cast<DskPanel*>(panels[slot].get());
		if (!dp) { panels[slot]->goUp(); return; }
		if (dp->atRoot()) closeDisk(slot);
		else dp->goUp();
	}

	void closeDisk(int slot)
	{
		auto* dp = dynamic_cast<DskPanel*>(panels[slot].get());
		if (!dp) return;
		size_t lastSlash = dp->path.find_last_of('/');
		std::string parentDir = (lastSlash == std::string::npos) ? "." : dp->path.substr(0, lastSlash);
		WINDOW* win = dp->win;
		auto lp = std::make_unique<LocalPanel>(win, parentDir);
		panels[slot] = std::move(lp);
		panels[slot]->load();
		redraw();
	}

	void opView()
	{
		const Entry* e = activePanel()->selected();
		if (!e || e->isDir || e->name == "..") return;
		Panel* p = activePanel();
		if (auto* lp = dynamic_cast<LocalPanel*>(p)) {
			viewFile(lp->path + "/" + e->name);
			redraw();
			return;
		}
		if (auto* dp = dynamic_cast<DskPanel*>(p)) {
			std::vector<uint8_t> data;
			if (!dp->readFile(e->name, data)) {
				status("Nie udalo sie odczytac " + e->name + " z obrazu.", true);
				return;
			}
			char tmpl[] = "/tmp/dsk_browser_view_XXXXXX";
			int fd = mkstemp(tmpl);
			if (fd < 0) { status("Nie udalo sie utworzyc pliku tymczasowego.", true); return; }
			FILE* f = fdopen(fd, "wb");
			if (f) {
				fwrite(data.data(), 1, data.size(), f);
				fclose(f);
			}
			viewFile(tmpl);
			unlink(tmpl);
			redraw();
		}
	}

	void opToggleMark()
	{
		activePanel()->toggleMark();
		size_t n = activePanel()->marked.size();
		if (n) status("Zaznaczono: " + std::to_string(n) + " pozycji");
		else status("");
	}

	// F5: kopiuj. Kierunek wynika z typow paneli -- Lokalny->Dsk = zapis
	// na obraz (tylko CP/M -- TOS odrzuca), Dsk->Lokalny = odczyt z
	// obrazu (oba formaty).
	void opCopy()
	{
		Panel* src = activePanel();
		Panel* dst = otherPanel();
		auto* srcLocal = dynamic_cast<LocalPanel*>(src);
		auto* srcDsk = dynamic_cast<DskPanel*>(src);
		auto* dstLocal = dynamic_cast<LocalPanel*>(dst);
		auto* dstDsk = dynamic_cast<DskPanel*>(dst);

		if (srcDsk && dstLocal) { copyDskToLocal(srcDsk, dstLocal); return; }
		if (srcLocal && dstDsk) { copyLocalToDsk(srcLocal, dstDsk); return; }
		status("F5 kopiuje tylko miedzy panelem Lokalnym i panelem obrazu .dsk.", true);
	}

	void copyDskToLocal(DskPanel* src, LocalPanel* dst)
	{
		// Pary (nazwa_na_obrazie, nazwa_docelowa_lokalna) -- odczyt z
		// obrazu idzie ZAWSZE po pierwszej, zapis lokalny po drugiej,
		// nigdy nie mieszane.
		std::vector<std::pair<std::string, std::string>> pairs;
		std::vector<Entry> marked = src->getMarked();
		if (!marked.empty()) {
			std::vector<Entry> files;
			for (auto& e : marked) if (!e.isDir) files.push_back(e);
			if (files.size() != marked.size())
				status("Podkatalogi TOS w zaznaczeniu zostaly pominiete (kopiowanie nie jest rekurencyjne).");
			marked = files;
			if (marked.empty()) { redraw(); return; }
			std::string label = std::to_string(marked.size()) + " zaznaczonych pozycji";
			if (!dialogConfirm("Pobrac " + label + "?")) return;
			for (auto& e : marked) pairs.push_back({e.name, e.name}); // bez zmiany nazwy
		} else {
			const Entry* e = src->selected();
			if (!e || e->name == ".." || e->isDir) return;
			bool ok = false;
			std::string dstName = dialogInput("Pobierz \"" + e->name + "\" jako:", e->name, ok);
			if (!ok || dstName.empty()) return;
			pairs.push_back({e->name, dstName});
		}

		int done = 0;
		std::vector<std::string> errors;
		for (auto& [origName, localName] : pairs) {
			std::vector<uint8_t> data;
			if (!src->readFile(origName, data)) { errors.push_back(origName + ": blad odczytu"); continue; }
			std::string fullPath = dst->path + "/" + localName;
			FILE* f = fopen(fullPath.c_str(), "wb");
			if (!f) { errors.push_back(localName + ": " + strerror(errno)); continue; }
			fwrite(data.data(), 1, data.size(), f);
			fclose(f);
			done++;
		}
		src->marked.clear();
		if (!errors.empty()) status("Pobrano " + std::to_string(done) + "/" + std::to_string(pairs.size()) +
			". Bledy: " + errors[0], true);
		else status("Pobrano " + std::to_string(done) + " plik(ow).");
		dst->load();
		redraw();
	}

	void copyLocalToDsk(LocalPanel* src, DskPanel* dst)
	{
		// TosFs::writeFile() (2026-11-27) builds proper O/L/R/A fields the
		// same way CpmFs::writeFile() already did -- dst->writeFile() below
		// already dispatches to either, gated by TosFs::isWritableGeometry()
		// itself (1-sided geometry only, same discipline as CpmFs's own
		// blocks16bit refusal), so no format-specific gate is needed here
		// anymore.
		std::vector<Entry> toCopy = src->getMarked();
		if (toCopy.empty()) {
			const Entry* e = src->selected();
			if (!e || e->name == ".." || e->isDir) {
				if (e && e->isDir) status("Kopiowanie katalogow na obraz nie jest wspierane.", true);
				return;
			}
			toCopy.push_back(*e);
		} else {
			std::vector<Entry> files;
			for (auto& e : toCopy) if (!e.isDir) files.push_back(e);
			if (files.size() != toCopy.size())
				status("Katalogi w zaznaczeniu zostaly pominiete.");
			toCopy = files;
			if (toCopy.empty()) { redraw(); return; }
			std::string label = std::to_string(toCopy.size()) + " zaznaczonych pozycji";
			if (!dialogConfirm("Wyslac na obraz " + label + "?")) return;
		}

		bool renamePrompt = (toCopy.size() == 1 && src->marked.empty());
		int done = 0;
		std::vector<std::string> errors;
		for (auto& e : toCopy) {
			std::string dstName = e.name;
			if (renamePrompt) {
				std::string suggested = suggestCpmName(e.name);
				bool ok = false;
				dstName = dialogInput("Zapisz \"" + e.name + "\" na obrazie jako:", suggested, ok);
				if (!ok || dstName.empty()) return;
			}
			std::string fullPath = src->path + "/" + e.name;
			FILE* f = fopen(fullPath.c_str(), "rb");
			if (!f) { errors.push_back(e.name + ": " + strerror(errno)); continue; }
			std::vector<uint8_t> data;
			fseek(f, 0, SEEK_END);
			long sz = ftell(f);
			fseek(f, 0, SEEK_SET);
			if (sz > 0) {
				data.resize((size_t)sz);
				size_t got = fread(data.data(), 1, data.size(), f);
				data.resize(got);
			}
			fclose(f);
			if (!dst->writeFile(dstName, data)) {
				errors.push_back(dstName + ": zapis nieudany (nazwa juz istnieje, dysk pelny, nieobslugiwana gestosc, lub obraz tylko-do-odczytu)");
				continue;
			}
			done++;
		}
		src->marked.clear();
		if (!errors.empty()) status("Zapisano " + std::to_string(done) + "/" + std::to_string(toCopy.size()) +
			". Bledy: " + errors[0], true);
		else status("Zapisano " + std::to_string(done) + " plik(ow) na obrazie.");
		dst->load();
		redraw();
	}

	// Sugestia nazwy 8.3 dla pliku lokalnego (CpmFs::normalizeName() i
	// tak to wymusi przy zapisie -- to tylko podglad w dialogu).
	static std::string suggestCpmName(const std::string& localName)
	{
		std::string base = localName, ext;
		size_t dot = localName.find_last_of('.');
		if (dot != std::string::npos) { base = localName.substr(0, dot); ext = localName.substr(dot + 1); }
		std::string out;
		for (size_t i = 0; i < base.size() && i < 8; i++) out += (char)toupper((unsigned char)base[i]);
		if (!ext.empty()) {
			out += ".";
			for (size_t i = 0; i < ext.size() && i < 3; i++) out += (char)toupper((unsigned char)ext[i]);
		}
		return out;
	}

	// F7: mkdir -- tylko TOS ma podkatalogi w ogole; CP/M 2.2 nie, wiec
	// zawsze blokowane tam niezaleznie od isReadOnly().
	void opMkdir()
	{
		Panel* p = activePanel();
		if (auto* lp = dynamic_cast<LocalPanel*>(p)) {
			bool ok = false;
			std::string name = dialogInput("Nazwa nowego katalogu:", "", ok);
			if (!ok || name.empty()) return;
			if (!lp->createDirectory(name)) {
				status("Nie udalo sie utworzyc katalogu \"" + name + "\": " + strerror(errno), true);
				return;
			}
			status("Utworzono katalog \"" + name + "\".");
			lp->load();
			redraw();
			return;
		}
		auto* dp = dynamic_cast<DskPanel*>(p);
		if (!dp) { status("F7 tworzy katalog w panelu Lokalnym lub podkatalog TOS.", true); return; }
		if (dp->format != DiskFormat::Tos) { status("Podkatalogi istnieja tylko na TOS (CP/M 2.2 ich nie ma).", true); return; }
		if (dp->isReadOnly()) { status("TOS jest tylko do odczytu w tej przegladarce.", true); return; }
		bool ok = false;
		std::string name = dialogInput("Nazwa nowego podkatalogu:", "", ok);
		if (!ok || name.empty()) return;
		if (!dp->createDirectory(name)) {
			status("Nie udalo sie utworzyc podkatalogu \"" + name + "\" (juz istnieje lub katalog pelny).", true);
			return;
		}
		status("Utworzono podkatalog \"" + name + "\".");
		redraw();
	}

	void opDelete()
	{
		Panel* p = activePanel();
		if (auto* lp = dynamic_cast<LocalPanel*>(p)) {
			std::vector<Entry> toDelete = lp->getMarked();
			if (toDelete.empty()) {
				const Entry* e = lp->selected();
				if (!e || e->name == "..") return;
				toDelete.push_back(*e);
			}
			std::string label = toDelete.size() == 1 ? toDelete[0].name
				: std::to_string(toDelete.size()) + " zaznaczonych pozycji";
			if (!dialogConfirm("Usunac " + label + " lokalnie?")) return;
			int done = 0;
			std::vector<std::string> errors;
			for (auto& e : toDelete) {
				if (lp->deleteEntry(e.name, e.isDir)) done++;
				else errors.push_back(e.name + ": " + strerror(errno));
			}
			lp->marked.clear();
			if (!errors.empty()) status("Usunieto " + std::to_string(done) + "/" + std::to_string(toDelete.size()) +
				". Bledy: " + errors[0], true);
			else status("Usunieto " + std::to_string(done) + "/" + std::to_string(toDelete.size()) + " pozycji.");
			lp->load();
			redraw();
			return;
		}
		auto* dp = dynamic_cast<DskPanel*>(p);
		if (!dp) { status("F8 usuwa z panelu Lokalnego lub obrazu .dsk.", true); return; }
		if (dp->isReadOnly()) { status("TOS jest tylko do odczytu w tej przegladarce.", true); return; }
		std::vector<Entry> toDelete = dp->getMarked();
		if (toDelete.empty()) {
			const Entry* e = dp->selected();
			if (!e) return;
			toDelete.push_back(*e);
		}
		std::string label = toDelete.size() == 1 ? toDelete[0].name
			: std::to_string(toDelete.size()) + " zaznaczonych pozycji";
		if (!dialogConfirm("Usunac " + label + " z obrazu?")) return;
		int done = 0;
		for (auto& e : toDelete) if (dp->deleteFile(e.name)) done++;
		dp->marked.clear();
		status("Usunieto " + std::to_string(done) + "/" + std::to_string(toDelete.size()) + " plik(ow).");
		dp->load();
		redraw();
	}

	void run()
	{
		curs_set(0);
		panels[0]->load();
		panels[1]->load();
		redraw();

		for (;;) {
			redraw();
			int ch = getch();

			if (ch == 'q' || ch == KEY_F(10)) break;
			else if (ch == '\t') active = 1 - active;
			else if (ch == KEY_IC || ch == 20) opToggleMark(); // Insert lub Ctrl-T
			else if (ch == KEY_UP) activePanel()->move(-1);
			else if (ch == KEY_DOWN) activePanel()->move(1);
			else if (ch == KEY_PPAGE) activePanel()->move(-activePanel()->height());
			else if (ch == KEY_NPAGE) activePanel()->move(activePanel()->height());
			else if (ch == KEY_HOME) activePanel()->goFirst();
			else if (ch == KEY_END) activePanel()->goLast();
			else if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
				auto* lp = dynamic_cast<LocalPanel*>(activePanel());
				const Entry* e = activePanel()->selected();
				if (e && e->name == ".." && dynamic_cast<DskPanel*>(activePanel())) {
					// ".." w DskPanel -- zawsze widoczny wpis (patrz
					// DskPanel::load()), ta sama akcja co Backspace, ale
					// odkrywalna/klikalna przez Enter niezaleznie od tego,
					// jaki kod klawisza Backspace faktycznie wysyla dany
					// terminal/klient.
					goUpOrCloseDisk(active);
				} else if (lp && e && !e->isDir && e->name != ".." && looksLikeDskFile(e->name)) {
					openDisk(active, lp->path + "/" + e->name);
				} else if (!activePanel()->enter()) {
					opView();
				}
			}
			else if (ch == KEY_BACKSPACE || ch == 127 || ch == 263) {
				goUpOrCloseDisk(active);
			}
			else if (ch == 'r' || ch == KEY_F(2)) loadActive();
			else if (ch == KEY_F(3)) opView();
			else if (ch == KEY_F(5)) opCopy();
			else if (ch == KEY_F(7)) opMkdir();
			else if (ch == KEY_F(8)) opDelete();
			else if (ch == 27) {
				// Mozliwy Alt+<klawisz> -- terminal wysyla ESC (0x1B),
				// natychmiast potem drugi bajt, oba w jednym zapisie.
				// Krotki nodelay() odbiera ten drugi bajt jesli istnieje
				// (Alt+F), inaczej (samotny Escape, nieuzywany tutaj)
				// getch() zwraca ERR i nic sie nie dzieje.
				nodelay(stdscr, TRUE);
				int ch2 = getch();
				nodelay(stdscr, FALSE);
				if (ch2 == 'f' || ch2 == 'F') opChooseFormat();
			}
			else if (ch == KEY_RESIZE) resizeLayout();
		}
	}
};

static void initColors()
{
	start_color();
	use_default_colors();
	init_pair(1, COLOR_CYAN, -1);
	init_pair(2, COLOR_BLACK, COLOR_CYAN);
	init_pair(3, COLOR_CYAN, -1);
	init_pair(4, COLOR_BLACK, COLOR_GREEN);
	init_pair(5, COLOR_RED, -1);
	init_pair(6, COLOR_BLACK, COLOR_WHITE);
	init_pair(7, COLOR_BLACK, COLOR_CYAN);
	init_pair(8, COLOR_BLACK, COLOR_WHITE);
	init_pair(9, COLOR_GREEN, -1);
	init_pair(12, COLOR_YELLOW, -1);
	init_pair(13, COLOR_YELLOW, COLOR_CYAN);
}

int main(int argc, char** argv)
{
	// Argument .dsk opcjonalny teraz -- jesli podany, prawy panel od
	// razu otwiera sie na nim (wygoda), ale oba panele zaczynaja jako
	// lokalne i kazdy plik ".dsk" w DOWOLNYM panelu da sie otworzyc
	// przez Enter.
	std::string initialDsk = argc > 1 ? argv[1] : "";

	initscr();
	initColors();
	keypad(stdscr, TRUE);
	noecho();
	cbreak();

	int sh, sw; getmaxyx(stdscr, sh, sw);
	int pw = sw / 2;
	WINDOW* lwin = newwin(sh - 2, pw, 0, 0);
	WINDOW* rwin = newwin(sh - 2, sw - pw, 0, pw);

	auto left = std::make_unique<LocalPanel>(lwin);
	std::unique_ptr<Panel> right;
	if (!initialDsk.empty()) {
		auto dp = std::make_unique<DskPanel>(rwin, initialDsk);
		std::string err;
		if (dp->openDisk(err)) {
			right = std::move(dp);
		} else {
			endwin();
			fprintf(stderr, "dsk_browser: nie udalo sie otworzyc %s: %s\n", initialDsk.c_str(), err.c_str());
			return 1;
		}
	} else {
		right = std::make_unique<LocalPanel>(rwin);
	}

	App app(std::move(left), std::move(right));
	app.run();

	// Oba panele moglys byc wymieniane w trakcie dzialania (Lokalny <->
	// DskPanel), ale WINDOW* jest przekazywany dalej, nigdy tworzony na
	// nowo -- te dwa wskazniki (lwin/rwin, nie app.panels[i]->win per se,
	// choc aktualnie zawsze te same) sa jedynymi, ktore trzeba zwolnic.
	delwin(lwin);
	delwin(rwin);
	endwin();
	return 0;
}
