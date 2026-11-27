#include "debugserver.h"
#include "minzx.h"
#include "filemgr.h"
#include "z80.h"

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>
#include <cctype>

static std::vector<std::string> splitWs(const std::string& s)
{
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

static std::vector<std::string> splitOn(const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::istringstream iss(s);
    std::string tok;
    while (std::getline(iss, tok, sep)) out.push_back(tok);
    return out;
}

// Name -> scancode for the PRESSKEYS command: single letters/digits map
// directly, a handful of named keys (RETURN/ENTER, SPACE, ALT/SYMSHIFT,
// SHIFT) cover the rest of what the keyboard matrix actually exposes (see
// minzx.cpp's kKeyMap for the full matrix this feeds into).
static SDL_Scancode scancodeFromName(std::string name)
{
    for (char& c : name) c = (char)toupper((unsigned char)c);
    if (name.size() == 1) {
        char c = name[0];
        if (c >= 'A' && c <= 'Z') return (SDL_Scancode)(SDL_SCANCODE_A + (c - 'A'));
        if (c == '0') return SDL_SCANCODE_0;
        if (c >= '1' && c <= '9') return (SDL_Scancode)(SDL_SCANCODE_1 + (c - '1'));
    }
    if (name == "RETURN" || name == "ENTER") return SDL_SCANCODE_RETURN;
    if (name == "SPACE") return SDL_SCANCODE_SPACE;
    if (name == "ALT" || name == "SYMSHIFT" || name == "SYM") return SDL_SCANCODE_LALT;
    if (name == "SHIFT" || name == "CAPSSHIFT") return SDL_SCANCODE_LSHIFT;
    return SDL_SCANCODE_UNKNOWN;
}

// Char -> chord for the TYPE command: lets a caller send plain text ("AT
// cmds" as printed on a PC keyboard) instead of manually working out
// which real Spectrum key combo produces each character — Pawel's point
// after watching a 33-key PRESSKEYS sequence get hand-assembled for one
// AT+CIPSTART line: "wygodnie naciskać klawisze tak jak są opisane na
// klawiaturze PC... a translacja zrobi sobie mapowanie na klawisze
// spectrum" (comfortable to press keys as labeled on a PC keyboard, let
// translation handle the Spectrum mapping).
//
// Two real Spectrum facts this depends on, both confirmed by testing in
// this same session before adding this, not guessed: unshifted letters
// give LOWERCASE by default (CAPS SHIFT needed for uppercase — matches
// both the BASIC ROM editor and espat-zx's own z88dk-based key reading,
// since both ultimately go through the same ROM-level keyboard-to-ASCII
// convention), and the standard SYMBOL SHIFT table printed on every real
// Spectrum keycap (+ on K, = on L, , on N, . on M, " on P, etc.).
//
// NOTE: this is deliberately NOT keyword-aware (see TYPEBASIC below for
// that). TYPE's own use case is typing literal characters into a context
// that has no K/L token substitution at all (e.g. an already-running
// INPUT/INPUT$ prompt) — every char here must stay a plain literal key.
static bool charToChord(char ch, std::vector<SDL_Scancode>& chord)
{
    chord.clear();
    if (ch >= 'a' && ch <= 'z') {
        chord.push_back((SDL_Scancode)(SDL_SCANCODE_A + (ch - 'a')));
        return true;
    }
    if (ch >= 'A' && ch <= 'Z') {
        chord.push_back(SDL_SCANCODE_LSHIFT);
        chord.push_back((SDL_Scancode)(SDL_SCANCODE_A + (ch - 'A')));
        return true;
    }
    if (ch == '0') { chord.push_back(SDL_SCANCODE_0); return true; }
    if (ch >= '1' && ch <= '9') { chord.push_back((SDL_Scancode)(SDL_SCANCODE_1 + (ch - '1'))); return true; }
    if (ch == ' ') { chord.push_back(SDL_SCANCODE_SPACE); return true; }
    if (ch == '\r' || ch == '\n') { chord.push_back(SDL_SCANCODE_RETURN); return true; }

    // Symbol-shift (real Spectrum keycap legends, not an invented table).
    struct SymEntry { char ch; SDL_Scancode sc; };
    static const SymEntry kSymbols[] = {
        {'!', SDL_SCANCODE_1}, {'@', SDL_SCANCODE_2}, {'#', SDL_SCANCODE_3},
        {'$', SDL_SCANCODE_4}, {'%', SDL_SCANCODE_5}, {'&', SDL_SCANCODE_6},
        {'\'', SDL_SCANCODE_7}, {'(', SDL_SCANCODE_8}, {')', SDL_SCANCODE_9},
        {'_', SDL_SCANCODE_0},
        {'<', SDL_SCANCODE_Q}, {'>', SDL_SCANCODE_W},
        {':', SDL_SCANCODE_Z}, {'?', SDL_SCANCODE_C}, {'/', SDL_SCANCODE_V},
        {'*', SDL_SCANCODE_B}, {',', SDL_SCANCODE_N}, {'.', SDL_SCANCODE_M},
        {'-', SDL_SCANCODE_J}, {'+', SDL_SCANCODE_K}, {'=', SDL_SCANCODE_L},
        {'"', SDL_SCANCODE_P}, {';', SDL_SCANCODE_O},
    };
    for (const auto& e : kSymbols) {
        if (e.ch == ch) {
            chord.push_back(SDL_SCANCODE_LALT);
            chord.push_back(e.sc);
            return true;
        }
    }
    return false;
}

// --- TYPEBASIC: K/L/E cursor-mode-aware BASIC line entry -------------
//
// A real Spectrum (48K/Timex) BASIC editor never types keywords letter by
// letter: a bare letter key at the START OF A STATEMENT (cursor shows K)
// produces a WHOLE KEYWORD token (P -> "PRINT "), then the cursor drops
// to L (plain literal letters/digits) for the rest of the line. SYMBOL
// SHIFT symbols/words (AND, TO, THEN, ...) and EXTENDED-mode functions/
// colour words (SIN, INK, CAT, FORMAT, ...) need their own real-hardware
// gesture (SS+key, or CAPS+SYMBOL SHIFT together then a key) — these are
// NOT auto-triggered by a plain English word either on real hardware or
// here; write them as {KEYWORD} (case-insensitive) in the TYPEBASIC text.
// Full source for every table below: the zx-spectrum-basic skill's
// editor-and-keys.md / keyboard-tokens.md (this project's own TOS CAT*/
// FORMAT* keyboard-entry testing independently confirmed the digit-row
// CAT=E+SS+9 and FORMAT=E+SS+0 entries this session).
struct KeywordEntry { const char* text; SDL_Scancode key; };

// K-mode: bare letter key at start-of-statement -> whole keyword, cursor
// then drops to L. GO TO/GO SUB keep their real embedded space; GOTO/
// GOSUB accepted too as a convenience alias.
static const KeywordEntry kKmodeKeywords[] = {
    {"PLOT", SDL_SCANCODE_Q}, {"DRAW", SDL_SCANCODE_W}, {"REM", SDL_SCANCODE_E},
    {"RUN", SDL_SCANCODE_R}, {"RAND", SDL_SCANCODE_T}, {"RANDOMIZE", SDL_SCANCODE_T},
    {"RETURN", SDL_SCANCODE_Y}, {"IF", SDL_SCANCODE_U}, {"INPUT", SDL_SCANCODE_I},
    {"POKE", SDL_SCANCODE_O}, {"PRINT", SDL_SCANCODE_P},
    {"NEW", SDL_SCANCODE_A}, {"SAVE", SDL_SCANCODE_S}, {"DIM", SDL_SCANCODE_D},
    {"FOR", SDL_SCANCODE_F}, {"GO TO", SDL_SCANCODE_G}, {"GOTO", SDL_SCANCODE_G},
    {"GO SUB", SDL_SCANCODE_H}, {"GOSUB", SDL_SCANCODE_H},
    {"LOAD", SDL_SCANCODE_J}, {"LIST", SDL_SCANCODE_K}, {"LET", SDL_SCANCODE_L},
    {"COPY", SDL_SCANCODE_Z}, {"CLEAR", SDL_SCANCODE_X}, {"CONT", SDL_SCANCODE_C},
    {"CLS", SDL_SCANCODE_V}, {"BORDER", SDL_SCANCODE_B}, {"NEXT", SDL_SCANCODE_N},
    {"PAUSE", SDL_SCANCODE_M},
};

// SYMBOL SHIFT + key, available at any cursor position (own gesture,
// doesn't change K/L state) — except THEN, which real hardware resets
// back to K for (same as ':').
static const KeywordEntry kSymShiftWords[] = {
    {"AND", SDL_SCANCODE_Y}, {"OR", SDL_SCANCODE_U}, {"AT", SDL_SCANCODE_I},
    {"STOP", SDL_SCANCODE_A}, {"NOT", SDL_SCANCODE_S}, {"STEP", SDL_SCANCODE_D},
    {"TO", SDL_SCANCODE_F}, {"THEN", SDL_SCANCODE_G},
};

// EXTENDED mode (CAPS SHIFT+SYMBOL SHIFT together, release, then the key
// alone) — green functions above the letter keys.
static const KeywordEntry kExtendedFunctions[] = {
    {"SIN", SDL_SCANCODE_Q}, {"COS", SDL_SCANCODE_W}, {"TAN", SDL_SCANCODE_E},
    {"INT", SDL_SCANCODE_R}, {"RND", SDL_SCANCODE_T}, {"STR$", SDL_SCANCODE_Y},
    {"CHR$", SDL_SCANCODE_U}, {"CODE", SDL_SCANCODE_I}, {"PEEK", SDL_SCANCODE_O},
    {"TAB", SDL_SCANCODE_P},
    {"READ", SDL_SCANCODE_A}, {"RESTORE", SDL_SCANCODE_S}, {"DATA", SDL_SCANCODE_D},
    {"SGN", SDL_SCANCODE_F}, {"ABS", SDL_SCANCODE_G}, {"SQR", SDL_SCANCODE_H},
    {"VAL", SDL_SCANCODE_J}, {"LEN", SDL_SCANCODE_K}, {"USR", SDL_SCANCODE_L},
    {"LN", SDL_SCANCODE_Z}, {"EXP", SDL_SCANCODE_X}, {"LPRINT", SDL_SCANCODE_C},
    {"LLIST", SDL_SCANCODE_V}, {"BIN", SDL_SCANCODE_B}, {"INKEY$", SDL_SCANCODE_N},
    {"PI", SDL_SCANCODE_M},
};

// EXTENDED mode, then SYMBOL SHIFT + key — red words below the letter keys.
static const KeywordEntry kExtendedSymShift[] = {
    {"ASN", SDL_SCANCODE_Q}, {"ACS", SDL_SCANCODE_W}, {"ATN", SDL_SCANCODE_E},
    {"VERIFY", SDL_SCANCODE_R}, {"MERGE", SDL_SCANCODE_T},
    {"IN", SDL_SCANCODE_I}, {"OUT", SDL_SCANCODE_O},
    {"CIRCLE", SDL_SCANCODE_H}, {"VAL$", SDL_SCANCODE_J}, {"SCREEN$", SDL_SCANCODE_K},
    {"ATTR", SDL_SCANCODE_L}, {"BEEP", SDL_SCANCODE_Z}, {"INK", SDL_SCANCODE_X},
    {"PAPER", SDL_SCANCODE_C}, {"FLASH", SDL_SCANCODE_V}, {"BRIGHT", SDL_SCANCODE_B},
    {"OVER", SDL_SCANCODE_N}, {"INVERSE", SDL_SCANCODE_M},
};

// EXTENDED mode, then SYMBOL SHIFT + digit — number row's red tokens.
// CAT (key 9) and FORMAT (key 0) independently confirmed against real TOS
// this session; the rest follow the same documented table shape.
static const KeywordEntry kExtendedDigitRow[] = {
    {"DEF FN", SDL_SCANCODE_1}, {"FN", SDL_SCANCODE_2}, {"LINE", SDL_SCANCODE_3},
    {"OPEN #", SDL_SCANCODE_4}, {"CLOSE #", SDL_SCANCODE_5}, {"MOVE", SDL_SCANCODE_6},
    {"ERASE", SDL_SCANCODE_7}, {"POINT", SDL_SCANCODE_8}, {"CAT", SDL_SCANCODE_9},
    {"FORMAT", SDL_SCANCODE_0},
};

static const SDL_Scancode* findKeyword(const KeywordEntry* table, size_t count, const std::string& word)
{
    for (size_t i = 0; i < count; i++)
        if (word == table[i].text) return &table[i].key;
    return nullptr;
}
#define KWLOOKUP(table, word) findKeyword(table, sizeof(table) / sizeof(table[0]), word)

// Tokenizes `text` into a chord sequence implementing the state machine
// above. Returns false with `err` set on an unmappable/invalid input
// instead of silently producing garbage (every ambiguous case — a
// keyword word outside start-of-statement position, an unknown {..}
// escape, an unterminated quote/escape — is a caller mistake worth
// surfacing rather than guessing).
static bool typeBasicToChords(const std::string& text, std::vector<std::vector<SDL_Scancode>>& chords, std::string& err)
{
    bool kEligible = true; // cursor shows K: next bare letter = whole keyword
    bool inQuotes = false;
    size_t i = 0, n = text.size();
    while (i < n) {
        char c = text[i];

        if (c == '"') {
            chords.push_back({SDL_SCANCODE_LALT, SDL_SCANCODE_P});
            inQuotes = !inQuotes;
            kEligible = false;
            i++;
            continue;
        }
        if (inQuotes) {
            // Quoted content is always literal — never keyword-matched,
            // exactly like typing a real filename/string on hardware.
            std::vector<SDL_Scancode> chord;
            if (!charToChord(c, chord)) { err = "unmappable character in string"; return false; }
            chords.push_back(chord);
            i++;
            continue;
        }
        if (c == '{') {
            size_t end = text.find('}', i);
            if (end == std::string::npos) { err = "unterminated { escape"; return false; }
            std::string word = text.substr(i + 1, end - i - 1);
            for (char& wc : word) wc = (char)toupper((unsigned char)wc);
            if (const SDL_Scancode* key = KWLOOKUP(kKmodeKeywords, word)) {
                if (!kEligible) { err = "{" + word + "} needs start-of-statement position"; return false; }
                chords.push_back({*key});
                kEligible = false;
            } else if (word == "THEN") {
                chords.push_back({SDL_SCANCODE_LALT, *KWLOOKUP(kSymShiftWords, word)});
                kEligible = true; // THEN starts a new logical statement
            } else if (const SDL_Scancode* key = KWLOOKUP(kSymShiftWords, word)) {
                chords.push_back({SDL_SCANCODE_LALT, *key});
            } else if (const SDL_Scancode* key = KWLOOKUP(kExtendedFunctions, word)) {
                chords.push_back({SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LALT});
                chords.push_back({*key});
                kEligible = false;
            } else if (const SDL_Scancode* key = KWLOOKUP(kExtendedSymShift, word)) {
                chords.push_back({SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LALT});
                chords.push_back({SDL_SCANCODE_LALT, *key});
                kEligible = false;
            } else if (const SDL_Scancode* key = KWLOOKUP(kExtendedDigitRow, word)) {
                chords.push_back({SDL_SCANCODE_LSHIFT, SDL_SCANCODE_LALT});
                chords.push_back({SDL_SCANCODE_LALT, *key});
                kEligible = false;
            } else {
                err = "unknown {" + word + "} keyword";
                return false;
            }
            i = end + 1;
            continue;
        }
        if (c == '\n' || c == '\r') {
            if (c == '\r' && i + 1 < n && text[i + 1] == '\n') i++;
            chords.push_back({SDL_SCANCODE_RETURN});
            kEligible = true;
            i++;
            continue;
        }
        if (c == ':') {
            chords.push_back({SDL_SCANCODE_LALT, SDL_SCANCODE_Z});
            kEligible = true;
            i++;
            continue;
        }
        if (c == ' ') { chords.push_back({SDL_SCANCODE_SPACE}); i++; continue; }
        if (c == '0') { chords.push_back({SDL_SCANCODE_0}); i++; continue; }
        if (c >= '1' && c <= '9') {
            chords.push_back({(SDL_Scancode)(SDL_SCANCODE_1 + (c - '1'))});
            i++;
            continue;
        }
        if (kEligible && isupper((unsigned char)c)) {
            // Longest exact match against the K-mode table starting here
            // (only K-mode keywords auto-trigger from plain text — that's
            // also the only case with no alternate gesture on real
            // hardware; everything else needs the {..} escape above).
            const KeywordEntry* best = nullptr;
            size_t bestLen = 0;
            for (const auto& e : kKmodeKeywords) {
                size_t len = strlen(e.text);
                if (text.compare(i, len, e.text) == 0 && len > bestLen) { best = &e; bestLen = len; }
            }
            if (best) {
                chords.push_back({best->key});
                i += bestLen;
                kEligible = false;
                continue;
            }
        }
        std::vector<SDL_Scancode> chord;
        if (!charToChord(c, chord)) { err = std::string("unmappable character '") + c + "'"; return false; }
        chords.push_back(chord);
        kEligible = false;
        i++;
    }
    if (inQuotes) { err = "unterminated quote"; return false; }
    return true;
}

bool DebugServer::init(int port)
{
    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0) return false;

    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);

    if (bind(listenFd_, (sockaddr*)&addr, sizeof(addr)) < 0) {
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    if (listen(listenFd_, 1) < 0) {
        close(listenFd_);
        listenFd_ = -1;
        return false;
    }
    fcntl(listenFd_, F_SETFL, O_NONBLOCK);
    printf("DebugServer: listening on port %d\n", port);
    return true;
}

void DebugServer::reply(const std::string& msg)
{
    if (clientFd_ < 0) return;
    std::string out = msg + "\n";
    ssize_t n = ::send(clientFd_, out.data(), out.size(), 0);
    (void)n; // best-effort; a dropped reply just looks like a timeout to the client
}

void DebugServer::poll(MinZX& minZX)
{
    if (listenFd_ < 0) return;

    // A breakpoint hit during the update() just run (by main.cpp, before
    // this poll() call) always stops free-run — same contract as a real
    // debugger. The client discovers this via STATE, no push needed.
    if (minZX.justHitBreakpoint()) {
        running_ = false;
        printf("DebugServer: breakpoint hit at 0x%04x\n", minZX.lastBreakpointPC());
    }

    // Accept a new client if we don't have one — one at a time is enough
    // for agent-driven debugging.
    if (clientFd_ < 0) {
        int fd = accept(listenFd_, nullptr, nullptr);
        if (fd >= 0) {
            fcntl(fd, F_SETFL, O_NONBLOCK);
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            clientFd_ = fd;
            rxBuf_.clear();
            printf("DebugServer: client connected\n");
        }
    }
    if (clientFd_ < 0) return;

    char buf[1024];
    ssize_t n = recv(clientFd_, buf, sizeof(buf), 0);
    if (n == 0) {
        // Clean disconnect.
        close(clientFd_);
        clientFd_ = -1;
        printf("DebugServer: client disconnected\n");
        return;
    }
    if (n < 0) return; // EAGAIN/EWOULDBLOCK — no data waiting, normal

    rxBuf_.append(buf, (size_t)n);
    size_t pos;
    while ((pos = rxBuf_.find('\n')) != std::string::npos) {
        std::string line = rxBuf_.substr(0, pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        rxBuf_.erase(0, pos + 1);
        if (!line.empty()) handleLine(line, minZX);
    }
}

void DebugServer::handleLine(const std::string& line, MinZX& minZX)
{
    std::vector<std::string> toks = splitWs(line);
    if (toks.empty()) return;
    const std::string& cmd = toks[0];

    Z80* z80 = minZX.getCPU();
    char out[512];

    if (cmd == "REGS") {
        snprintf(out, sizeof(out),
            "OK AF=%04x BC=%04x DE=%04x HL=%04x AFx=%04x BCx=%04x DEx=%04x HLx=%04x "
            "IX=%04x IY=%04x SP=%04x PC=%04x I=%02x R=%02x IFF1=%d IFF2=%d IM=%d",
            z80->getRegAF(), z80->getRegBC(), z80->getRegDE(), z80->getRegHL(),
            z80->getRegAFx(), z80->getRegBCx(), z80->getRegDEx(), z80->getRegHLx(),
            z80->getRegIX(), z80->getRegIY(), z80->getRegSP(), z80->getRegPC(),
            z80->getRegI(), z80->getRegR(),
            z80->isIFF1() ? 1 : 0, z80->isIFF2() ? 1 : 0, (int)z80->getIM());
        reply(out);
    }
    else if (cmd == "SETREG" && toks.size() >= 3) {
        // Direct CPU-state control (GDB-style "set $pc = 0x3e80" instead
        // of typing through the keyboard/BASIC editor) -- the honest,
        // supported way to e.g. jump straight to a USR address for a
        // deterministic test, instead of hijacking a RET's return
        // address on the stack by hand via POKE/MEM (done once, worked,
        // but is a hack a real command should replace). AF/BC/DE/HL/
        // AFx/BCx/DEx/HLx/IX/IY/SP/PC -- the same set REGS reports.
        uint16_t val = (uint16_t)strtoul(toks[2].c_str(), nullptr, 16);
        std::string reg = toks[1];
        for (char& c : reg) c = (char)toupper((unsigned char)c);
        bool ok = true;
        if (reg == "AF") z80->setRegAF(val);
        else if (reg == "BC") z80->setRegBC(val);
        else if (reg == "DE") z80->setRegDE(val);
        else if (reg == "HL") z80->setRegHL(val);
        else if (reg == "AFX") z80->setRegAFx(val);
        else if (reg == "BCX") z80->setRegBCx(val);
        else if (reg == "DEX") z80->setRegDEx(val);
        else if (reg == "HLX") z80->setRegHLx(val);
        else if (reg == "IX") z80->setRegIX(val);
        else if (reg == "IY") z80->setRegIY(val);
        else if (reg == "SP") z80->setRegSP(val);
        else if (reg == "PC") z80->setRegPC(val);
        else ok = false;
        if (ok) reply("OK");
        else reply("ERR unknown register");
    }
    else if (cmd == "MEM" && toks.size() >= 3) {
        uint16_t addr = (uint16_t)strtoul(toks[1].c_str(), nullptr, 16);
        long len = strtol(toks[2].c_str(), nullptr, 10);
        if (len < 0) len = 0;
        if (len > 4096) len = 4096; // one 1024-byte recv buffer's worth of hex, several times over — plenty for a debug peek
        std::string resp = "OK ";
        char hex[3];
        for (long i = 0; i < len; i++) {
            snprintf(hex, sizeof(hex), "%02x", minZX.peek8((uint16_t)(addr + i)));
            resp += hex;
        }
        reply(resp);
    }
    else if (cmd == "POKE" && toks.size() >= 3) {
        uint16_t addr = (uint16_t)strtoul(toks[1].c_str(), nullptr, 16);
        std::istringstream bs(toks[2]);
        std::string b;
        int count = 0;
        while (std::getline(bs, b, ',')) {
            minZX.poke8((uint16_t)(addr + count), (uint8_t)strtoul(b.c_str(), nullptr, 16));
            count++;
        }
        reply("OK");
    }
    else if (cmd == "PORT" && toks.size() >= 2) {
        uint16_t addr = (uint16_t)strtoul(toks[1].c_str(), nullptr, 16);
        uint8_t val = minZX.inPort(addr);
        snprintf(out, sizeof(out), "OK %02x", val);
        reply(out);
    }
    else if (cmd == "OUT" && toks.size() >= 3) {
        uint16_t addr = (uint16_t)strtoul(toks[1].c_str(), nullptr, 16);
        uint8_t val = (uint8_t)strtoul(toks[2].c_str(), nullptr, 16);
        minZX.outPort(addr, val);
        reply("OK");
    }
    else if (cmd == "INSTRTRACE") {
        // See MinZX.h's setInstrTraceEnabled() comment. Optional third
        // token is a hex "LOW-HIGH" range (e.g. "INSTRTRACE ON 3000-3fff");
        // bare "INSTRTRACE" just reports current state without changing it.
        if (toks.size() >= 2) {
            if (toks[1] == "ON") minZX.setInstrTraceEnabled(true);
            else if (toks[1] == "OFF") minZX.setInstrTraceEnabled(false);
            else { reply("ERR expected ON or OFF"); return; }
            if (toks.size() >= 3) {
                size_t dash = toks[2].find('-');
                if (dash != std::string::npos) {
                    uint16_t lo = (uint16_t)strtoul(toks[2].substr(0, dash).c_str(), nullptr, 16);
                    uint16_t hi = (uint16_t)strtoul(toks[2].substr(dash + 1).c_str(), nullptr, 16);
                    minZX.setInstrTraceRange(lo, hi);
                }
            }
        }
        snprintf(out, sizeof(out), "OK instrtrace=%d", minZX.isInstrTraceEnabled() ? 1 : 0);
        reply(out);
    }
    else if (cmd == "LIVEDISASM") {
        // Live/JIT disassembler (see include/z80cpp/LiveDisasm.h/z80.h) --
        // a Z80-CORE-level feature, not peripheral-specific: operates on
        // THIS process's own host CPU (minZX.getCPU()) directly, so any
        // peripheral's ROM window gets covered automatically whenever
        // it's actually paged in and executing, with no per-peripheral
        // wiring needed. Off by default -- must be turned ON explicitly.
        // "LIVEDISASM DUMP <path>" writes the current map to a file;
        // "LIVEDISASM CLEAR" resets it; bare "LIVEDISASM" reports state.
        if (toks.size() >= 2 && toks[1] == "ON") {
            z80->setLiveDisasmEnabled(true);
        } else if (toks.size() >= 2 && toks[1] == "OFF") {
            z80->setLiveDisasmEnabled(false);
        } else if (toks.size() >= 2 && toks[1] == "CLEAR") {
            z80->liveDisasmClear();
            reply("OK cleared");
            return;
        } else if (toks.size() >= 3 && toks[1] == "DUMP") {
            FILE* pf = fopen(toks[2].c_str(), "w");
            if (!pf) { reply("ERR cannot open output path"); return; }
            std::string dump = z80->liveDisasmDump();
            fwrite(dump.data(), 1, dump.size(), pf);
            fclose(pf);
            snprintf(out, sizeof(out), "OK wrote %zu addresses to %s", z80->liveDisasmSize(), toks[2].c_str());
            reply(out);
            return;
        }
        snprintf(out, sizeof(out), "OK livedisasm=%d size=%zu",
                 z80->isLiveDisasmEnabled() ? 1 : 0, z80->liveDisasmSize());
        reply(out);
    }
    else if (cmd == "STATE") {
        char tapeInfo[160];
        snprintf(tapeInfo, sizeof(tapeInfo), " tapepos=%zu/%zu tapephase=%d tapebyte=%zu tapepilot=%u",
                 minZX.getTapePos(), minZX.getTapeBlockCount(), minZX.getTapePhase(),
                 minZX.getTapeByteIdx(), (unsigned)minZX.getTapePilotPulsesLeft());
        char base[352];
        if (!running_)
            snprintf(base, sizeof(base), "OK border=%u tstates=%u running=0 pc=%04x 7ffd=%02x rambank=%d locked=%d",
                     (unsigned)minZX.getBorderColor(), (unsigned)minZX.getTStates(),
                     minZX.getCPU()->getRegPC(), (unsigned)minZX.get7FFDRaw(),
                     minZX.get7FFDRamBank(), minZX.get7FFDLocked() ? 1 : 0);
        else
            snprintf(base, sizeof(base), "OK border=%u tstates=%u running=1 7ffd=%02x rambank=%d locked=%d",
                     (unsigned)minZX.getBorderColor(), (unsigned)minZX.getTStates(),
                     (unsigned)minZX.get7FFDRaw(), minZX.get7FFDRamBank(), minZX.get7FFDLocked() ? 1 : 0);
        snprintf(out, sizeof(out), "%s%s", base, tapeInfo);
        reply(out);
    }
    else if (cmd == "STEP") {
        int n = toks.size() >= 2 ? atoi(toks[1].c_str()) : 1;
        if (n < 1) n = 1;
        minZX.step(n);
        running_ = false; // stepping always leaves you stopped, like a real debugger
        reply("OK");
    }
    else if (cmd == "CONT") {
        running_ = true;
        reply("OK");
    }
    else if (cmd == "STOP") {
        running_ = false;
        reply("OK");
    }
    else if (cmd == "RESET") {
        // A plain Z80 reset (what F9/Dandanator-button triggers) -- NOT
        // the same as F11 anymore, see HARDRESET below.
        minZX.reset();
        reply("OK");
    }
    else if (cmd == "HARDRESET") {
        // Same as the window's F11 hotkey -- see MinZX::hardReset()'s own
        // comment for what distinguishes this from plain RESET above.
        minZX.hardReset();
        reply("OK");
    }
    else if (cmd == "NMI") {
        // Same as the window's Alt+F10 hotkey -- see MinZX::triggerNMI()/
        // multifaceArmOnTrigger() for what actually happens on the Z80 side.
        minZX.triggerNMI();
        reply("OK");
    }
    else if (cmd == "BREAK" && toks.size() >= 2) {
        uint16_t addr = (uint16_t)strtoul(toks[1].c_str(), nullptr, 16);
        minZX.addBreakpoint(addr);
        reply("OK");
    }
    else if (cmd == "UNBREAK" && toks.size() >= 2) {
        uint16_t addr = (uint16_t)strtoul(toks[1].c_str(), nullptr, 16);
        minZX.removeBreakpoint(addr);
        reply("OK");
    }
    else if (cmd == "BREAKS") {
        std::vector<uint16_t> bps = minZX.listBreakpoints();
        std::string resp = "OK";
        char addr[8];
        for (uint16_t bp : bps) {
            snprintf(addr, sizeof(addr), " %04x", bp);
            resp += addr;
        }
        reply(resp);
    }
    else if (cmd == "BREAKIF") {
        // Conditional breakpoint (see BreakExpr.h) -- an arbitrary
        // boolean expression over registers/memory, checked once per
        // instruction alongside the plain PC breakpoints above.
        // "BREAKIF" with no expression reports the current one (if
        // any) instead of arming/disarming -- same query-vs-set split
        // SETREG doesn't need but this does, since an empty line is
        // ambiguous between "no condition" and "clear the condition"
        // otherwise. Use "BREAKIF CLEAR" to explicitly disarm.
        if (line.size() <= 8) {
            if (minZX.hasConditionalBreakpoint())
                reply("OK " + minZX.conditionalBreakpointText());
            else
                reply("OK (none)");
        } else {
            std::string expr = line.substr(8);
            if (expr == "CLEAR") {
                minZX.clearConditionalBreakpoint();
                reply("OK");
            } else {
                std::string error;
                if (minZX.setConditionalBreakpoint(expr, &error))
                    reply("OK");
                else
                    reply("ERR " + error);
            }
        }
    }
    else if (cmd == "PRESSKEYS" && toks.size() >= 2) {
        // Comma-separated chords, '+' within a chord for simultaneous
        // keys: "J,ALT+P,ALT+P,RETURN" types LOAD "" + ENTER. Pacing
        // (hold/release timing) is the emulator's job, not the caller's —
        // see MinZX::pressKeys. Call STATE/REGS first if you need to know
        // the ROM has finished booting before queuing keys; this command
        // doesn't add a boot-settle delay itself (startAutoTypeLoadCommand,
        // used by --load-tape, does — see project memory for why).
        std::vector<std::vector<SDL_Scancode>> chords;
        bool bad = false;
        for (const std::string& chordStr : splitOn(toks[1], ',')) {
            std::vector<SDL_Scancode> chord;
            for (const std::string& keyName : splitOn(chordStr, '+')) {
                SDL_Scancode sc = scancodeFromName(keyName);
                if (sc == SDL_SCANCODE_UNKNOWN) { bad = true; break; }
                chord.push_back(sc);
            }
            if (bad) break;
            chords.push_back(chord);
        }
        if (bad || chords.empty())
            reply("ERR bad key name");
        else {
            minZX.pressKeys(chords);
            reply("OK");
        }
    }
    else if (cmd == "TYPE" && line.size() > 5) {
        // Plain text, auto-translated to the real Spectrum key combos —
        // see charToChord. Takes everything after "TYPE " verbatim from
        // the raw line (not space-split tokens), so literal spaces in
        // the text survive. "\n"/"\r\n" in the text become RETURN.
        std::string text = line.substr(5);
        std::vector<std::vector<SDL_Scancode>> chords;
        bool bad = false;
        for (char ch : text) {
            std::vector<SDL_Scancode> chord;
            if (!charToChord(ch, chord)) { bad = true; break; }
            chords.push_back(chord);
        }
        if (bad || chords.empty())
            reply("ERR unmappable character");
        else {
            minZX.pressKeys(chords);
            reply("OK");
        }
    }
    else if (cmd == "TYPEBASIC" && line.size() > 10) {
        // Like TYPE, but K/L/E cursor-mode aware — for composing an actual
        // BASIC program/immediate line (CAT *, FORMAT *"d" TO "name", a
        // PRINT statement, ...), where a bare letter means a whole keyword
        // at the start of a statement. See typeBasicToChords above for the
        // state machine and the {KEYWORD} escape syntax it needs for
        // SYMBOL SHIFT/EXTENDED-mode words. Always starts fresh in K mode
        // (one full line/command per call, matching how this is actually
        // used — not mid-line continuation across multiple TYPEBASIC calls).
        std::string text = line.substr(10);
        std::vector<std::vector<SDL_Scancode>> chords;
        std::string err;
        if (!typeBasicToChords(text, chords, err) || chords.empty())
            reply("ERR " + (err.empty() ? "unmappable input" : err));
        else {
            minZX.pressKeys(chords);
            reply("OK");
        }
    }
    else if (cmd == "SAVESNA" && toks.size() >= 2) {
        // Captures the FULL machine state (registers + stack + memory),
        // not just registers — see project memory for why that distinction
        // mattered (a register-only "jump into LD-BYTES" attempt landed on
        // a garbage stack and the machine reset itself). Used to build the
        // bundled "mid natural LOAD "" call" snapshot --load-tape replays.
        FileMgr fileMgr;
        if (fileMgr.saveSNA(toks[1].c_str(), &minZX))
            reply("OK");
        else
            reply("ERR saveSNA failed");
    }
    else if (cmd == "SAVEZ80" && toks.size() >= 2) {
        FileMgr fileMgr;
        if (fileMgr.saveZ80(toks[1].c_str(), &minZX))
            reply("OK");
        else
            reply("ERR saveZ80 failed");
    }
    else if (cmd == "LOADZ80" && toks.size() >= 2) {
        FileMgr fileMgr;
        if (fileMgr.loadZ80(toks[1].c_str(), &minZX))
            reply("OK");
        else
            reply("ERR loadZ80 failed");
    }
    else if (cmd == "LOADSNA" && toks.size() >= 2) {
        FileMgr fileMgr;
        if (fileMgr.loadSNA(toks[1].c_str(), &minZX))
            reply("OK");
        else
            reply("ERR loadSNA failed");
    }
    else if (cmd == "SCREENSHOT" && toks.size() >= 2) {
        // Dumps the exact buffer generateScreen() wrote this frame -- unlike
        // reading $4000 over MEM and re-rendering host-side (dump_screen.py),
        // this verifies the actual renderer, including Timex modes.
        if (minZX.saveScreenshotBMP(toks[1].c_str()))
            reply("OK");
        else
            reply("ERR saveScreenshotBMP failed");
    }
    else {
        // Layer 3 extensions: each expansion device (Kempston now, AY-PSG/
        // ZiFi/DivSD/Spectranext later — see Peripheral.h) owns its own
        // command name and decodes it independently; nothing here needs
        // to change when a new device is added.
        std::string periphReply;
        for (Peripheral* p : minZX.getPeripherals()) {
            periphReply = p->debugCommand(toks);
            if (!periphReply.empty())
                break;
        }
        if (!periphReply.empty())
            reply(periphReply);
        else
            reply("ERR unknown command");
    }
}

void DebugServer::shutdown()
{
    if (clientFd_ >= 0) { close(clientFd_); clientFd_ = -1; }
    if (listenFd_ >= 0) { close(listenFd_); listenFd_ = -1; }
}
