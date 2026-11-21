// dsk_browser -- dwupanelowa (Midnight Commander-style) przeglądarka
// R/W dla obrazów .dsk CP/M 2.2, stylizowana na
// /home/pawelb/src/tnfsd_v2/tnfs_server/gopher_browser.py (ten sam
// układ: lewy panel = lokalny filesystem, prawy panel = zdalny/obcy
// filesystem, Tab przełącza panel, Insert/Ctrl-T zaznacza wiele
// pozycji, F5 kopiuje między panelami, F3 podgląd, F2 odśwież, F10/q
// wyjście) -- zbudowana na już istniejącej, zweryfikowanej żywym
// bootem realnego CP/M bibliotece src/CpmFs.h/.cpp (patrz jej własny
// komentarz klasy co do DPB/translate table -- 2026-11-21).
//
// ZAKRES (2026-11-21): tylko CP/M -- TOS (inny filesystem na tych
// samych obrazach .dsk, patrz docs/FDD3000/tos_bez_tajemnic.html) NIE
// jest tu zaimplementowany; DskPanel poniżej jest napisana tak, by
// łatwo dało się dodać analogiczny "TosPanel" później, ale robienie
// tego teraz byłoby zgadywaniem bez realnego sample'a do zweryfikowania
// (ta sama zasada co przy CpmFs -- patrz jej komentarz).
//
// CP/M 2.2 nie ma podkatalogów -- DskPanel jest więc płaska (brak ".."),
// inaczej niż LocalPanel/GopherPanel w oryginale.
//
// Każdy writeFile()/deleteFile() w CpmFs sam robi DskImage::flush() --
// nie ma tu osobnego kroku "Zapisz", każda operacja od razu trwale
// trafia do pliku .dsk na dysku (ten sam model co fdd3000_station's
// SAVEDISK, tyle że bez kroku "Save As": ten browser operuje od razu
// na podanej ścieżce .dsk).
//
// Użycie: dsk_browser obraz.dsk

#include "../src/CpmFs.h"
#include "../src/DskImage.h"

#include <ncurses.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

// ── Model danych ────────────────────────────────────────────────────────

struct Entry {
	std::string name;
	bool isDir = false;
	long size = 0;
	bool readOnly = false;
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
	explicit LocalPanel(WINDOW* w) : Panel(w, "Lokalny")
	{
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
};

// ── Panel obrazu .dsk (CP/M, plaski -- bez podkatalogow) ─────────────────

class DskPanel : public Panel
{
public:
	CpmFs& fs;
	explicit DskPanel(WINDOW* w, CpmFs& fsRef, const std::string& dskPath)
		: Panel(w, "CP/M"), fs(fsRef)
	{
		path = dskPath;
	}

	void load() override
	{
		error.clear();
		items.clear();
		for (auto& d : fs.list()) {
			Entry e;
			e.name = d.name;
			e.isDir = false;
			e.size = (long)d.sizeBytes;
			e.readOnly = d.readOnly;
			e.system = d.system;
			items.push_back(e);
		}
		pruneMarks();
	}

	bool enter() override { return false; } // plaski -- Enter na pliku = podglad (obsluzone w App)
	void goUp() override {}                  // nic nad plaska lista
};

// ── Stopka F-klawiszy / status ───────────────────────────────────────────

static void drawFkeys(int screenH, int screenW)
{
	move(screenH - 1, 0);
	clrtoeol();
	struct FKey { const char* key; const char* label; };
	static const FKey keys[] = {
		{"Ins", "Zaznacz"}, {"F2", "Odswiez"}, {"F3", "Podglad"},
		{"F5", "Kopiuj"}, {"F8", "Usun(dsk)"}, {"Tab", "Panel"}, {"F10", "Wyjscie"},
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

// ── Aplikacja ─────────────────────────────────────────────────────────────

class App
{
public:
	Panel* panels[2];
	int active = 0;
	std::string statusMsg;
	bool statusError = false;

	App(Panel* left, Panel* right) { panels[0] = left; panels[1] = right; }

	Panel* activePanel() { return panels[active]; }
	Panel* otherPanel() { return panels[1 - active]; }

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
			if (!dp->fs.readFile(e->name, data)) {
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
	// do obrazu CP/M, Dsk->Lokalny = odczyt z obrazu. Inne kombinacje
	// (Dsk<->Dsk, Lokalny<->Lokalny) nie sa tu sensowne -- ten browser
	// laczy jeden realny .dsk z jednym katalogiem hosta, nie dwa obrazy
	// naraz, w przeciwienstwie do gopher_browser.py, ktory moglby miec
	// dwa zdalne panele teoretycznie (tu architektura to wyklucza: jest
	// tylko jeden CpmFs na caly proces).
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
		status("F5 kopiuje tylko miedzy panelem Lokalnym i CP/M.", true);
	}

	void copyDskToLocal(DskPanel* src, LocalPanel* dst)
	{
		// Pary (nazwa_na_obrazie, nazwa_docelowa_lokalna) -- odczyt z CP/M
		// idzie ZAWSZE po pierwszej, zapis lokalny po drugiej, nigdy nie
		// mieszane (wczesniejsza wersja nadpisywala jedyna nazwe przy
		// zmianie nazwy docelowej, gubiac informacje potrzebna do
		// readFile() -- naprawione).
		std::vector<std::pair<std::string, std::string>> pairs;
		std::vector<Entry> marked = src->getMarked();
		if (!marked.empty()) {
			std::string label = std::to_string(marked.size()) + " zaznaczonych pozycji";
			if (!dialogConfirm("Pobrac " + label + "?")) return;
			for (auto& e : marked) pairs.push_back({e.name, e.name}); // bez zmiany nazwy
		} else {
			const Entry* e = src->selected();
			if (!e || e->name == "..") return;
			bool ok = false;
			std::string dstName = dialogInput("Pobierz \"" + e->name + "\" jako:", e->name, ok);
			if (!ok || dstName.empty()) return;
			pairs.push_back({e->name, dstName});
		}

		int done = 0;
		std::vector<std::string> errors;
		for (auto& [origName, localName] : pairs) {
			std::vector<uint8_t> data;
			if (!src->fs.readFile(origName, data)) { errors.push_back(origName + ": blad odczytu"); continue; }
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
		std::vector<Entry> toCopy = src->getMarked();
		if (toCopy.empty()) {
			const Entry* e = src->selected();
			if (!e || e->name == ".." || e->isDir) {
				if (e && e->isDir) status("Kopiowanie katalogow na CP/M nie jest wspierane (brak podkatalogow w CP/M).", true);
				return;
			}
			toCopy.push_back(*e);
		} else {
			// Odfiltruj katalogi z zaznaczenia -- CP/M nie ma podkatalogow.
			std::vector<Entry> files;
			for (auto& e : toCopy) if (!e.isDir) files.push_back(e);
			if (files.size() != toCopy.size())
				status("Katalogi w zaznaczeniu zostaly pominiete (brak podkatalogow w CP/M).");
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
				std::string suggested = CpmFs_suggestName(e.name);
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
			if (!dst->fs.writeFile(dstName, data)) {
				errors.push_back(dstName + ": zapis nieudany (nazwa juz istnieje, dysk pelny, lub nieobslugiwana gestosc)");
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
	// tak to wymusi przy zapisie -- to tylko podglad w dialogu, zebysmy
	// nie prosili o nazwe, ktora i tak zostanie uciêta po cichu).
	static std::string CpmFs_suggestName(const std::string& localName)
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

	void opDelete()
	{
		auto* dp = dynamic_cast<DskPanel*>(activePanel());
		if (!dp) { status("F8 usuwa tylko z obrazu CP/M (panel aktywny musi byc CP/M).", true); return; }
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
		for (auto& e : toDelete) if (dp->fs.deleteFile(e.name)) done++;
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
				if (!activePanel()->enter()) opView();
			}
			else if (ch == KEY_BACKSPACE || ch == 127 || ch == 263) activePanel()->goUp();
			else if (ch == 'r' || ch == KEY_F(2)) loadActive();
			else if (ch == KEY_F(3)) opView();
			else if (ch == KEY_F(5)) opCopy();
			else if (ch == KEY_F(8)) opDelete();
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
	if (argc < 2) {
		fprintf(stderr, "uzycie: %s obraz.dsk\n", argv[0]);
		return 1;
	}
	std::string dskPath = argv[1];

	DskImage dsk;
	if (!dsk.load(dskPath)) {
		fprintf(stderr, "dsk_browser: nie udalo sie wczytac %s\n", dskPath.c_str());
		return 1;
	}
	CpmFs fs;
	if (!fs.open(dsk)) {
		fprintf(stderr, "dsk_browser: %s nie wyglada na obraz CP/M (CpmFs::open nieudane)\n", dskPath.c_str());
		return 1;
	}

	initscr();
	initColors();
	keypad(stdscr, TRUE);
	noecho();
	cbreak();

	int sh, sw; getmaxyx(stdscr, sh, sw);
	int pw = sw / 2;
	WINDOW* lwin = newwin(sh - 2, pw, 0, 0);
	WINDOW* rwin = newwin(sh - 2, sw - pw, 0, pw);

	LocalPanel left(lwin);
	DskPanel right(rwin, fs, dskPath);

	App app(&left, &right);
	app.run();

	delwin(lwin);
	delwin(rwin);
	endwin();
	return 0;
}
