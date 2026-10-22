#include <iostream>
#include <iomanip>
#include <vector>

#pragma comment(lib, "SDL2.lib")
#pragma comment(lib, "SDL2main.lib")

#include "SDL.h"
#include "minzx.h"
#include "filemgr.h"
#include "debugserver.h"
#include "ZiFi.h"
#include "Multiface.h"
#include "DivIde.h"
#include "SDI.h"
#include "OsdMenu.h"
#include <cstdlib>
#include <cstring>
#include <strings.h>

bool isLittleEndian()
{
	uint16_t val16 = 0x1;
	uint8_t* ptr8 = (uint8_t*)(&val16);
	return (*ptr8 == 1);
}

// Agent/debug-driven testing support: instead of synthesizing SDL input
// events (fragile — depends on X11 window focus, which isn't reliable in
// headless/VNC-less sessions) or recompiling with one-off test hooks,
// poke memory and set PC directly from the command line. Lets any new
// peripheral be exercised with a tiny hand-assembled loop, no keyboard/
// window-manager path required at all.
//
//   --poke ADDR=B1,B2,B3,...   (hex address, comma-separated hex bytes; repeatable)
//   --pc ADDR                  (hex address; applied after all --poke args)
//
// Example: test the Kempston port without a working keyboard matrix —
//   --poke 8000=DB,1F,18,FC --pc 8000
// (IN A,(0x1F) ; JR 0x8000 — an infinite loop re-reading the port)
// Returns the --debug-port value, or -1 if not given.
static int applyDebugArgs(int argc, char* argv[], MinZX& minZX)
{
	int pcAddr = -1;
	int debugPort = -1;
	for (int i = 1; i < argc; i++)
	{
		if (0 == strcmp(argv[i], "--poke") && i + 1 < argc)
		{
			char* eq = strchr(argv[++i], '=');
			if (!eq) { std::cerr << "--poke expects ADDR=B1,B2,..." << std::endl; continue; }
			*eq = '\0';
			uint16_t addr = (uint16_t)strtoul(argv[i], nullptr, 16);
			char* tok = strtok(eq + 1, ",");
			while (tok)
			{
				minZX.poke8(addr++, (uint8_t)strtoul(tok, nullptr, 16));
				tok = strtok(nullptr, ",");
			}
		}
		else if (0 == strcmp(argv[i], "--pc") && i + 1 < argc)
		{
			pcAddr = (int)strtoul(argv[++i], nullptr, 16);
		}
		else if (0 == strcmp(argv[i], "--trace-port") && i + 1 < argc)
		{
			g_tracePort = (int)strtoul(argv[++i], nullptr, 16);
		}
		else if (0 == strcmp(argv[i], "--debug-port") && i + 1 < argc)
		{
			debugPort = atoi(argv[++i]);
		}
		else if (0 == strcmp(argv[i], "--out") && i + 1 < argc)
		{
			// PORT=VALUE, hex -- drives a port write directly, without the
			// Z80 executing an OUT instruction. For test bootstrap (e.g.
			// paging in a DOCK bank before execution starts), same spirit
			// as --poke/--pc below.
			char* eq = strchr(argv[++i], '=');
			if (!eq) { std::cerr << "--out expects PORT=VALUE" << std::endl; continue; }
			*eq = '\0';
			uint16_t port = (uint16_t)strtoul(argv[i], nullptr, 16);
			uint8_t value = (uint8_t)strtoul(eq + 1, nullptr, 16);
			minZX.poke_out(port, value);
		}
		else if (0 == strcmp(argv[i], "--dock-cartridge") && i + 1 < argc)
		{
			if (!minZX.loadDockCartridge(argv[++i])) {
				std::cerr << "FATAL: --dock-cartridge: failed to load " << argv[i] << std::endl;
				exit(1);
			}
		}
		else if (0 == strcmp(argv[i], "--zifi") && i + 1 < argc)
		{
			// HOST:PORT for the ZiFi peripheral's TCP connection — either a
			// real ESP-01/Wemos D1 bridged onto TCP via socat, or
			// tools/virtual_esp01's standalone daemon. JASE only ever
			// speaks one TCP byte-stream protocol; which of those is on
			// the other end is entirely outside this process.
			char* arg = argv[++i];
			char* colon = strrchr(arg, ':');
			if (!colon) { std::cerr << "--zifi expects HOST:PORT" << std::endl; continue; }
			*colon = '\0';
			minZX.getZiFi()->connectTo(arg, atoi(colon + 1));
		}
	}
	if (pcAddr >= 0)
		minZX.getCPU()->setRegPC((uint16_t)pcAddr);
	return debugPort;
}

// Dispatches by extension: .tzx gets the richer per-block TZX parser,
// everything else (notably .tap) goes through the plain-TAP loader.
static bool loadTapeFile(FileMgr& fileMgr, const char* path, MinZX* minZX)
{
	size_t len = strlen(path);
	bool isTzx = len > 4 && (0 == strcasecmp(path + len - 4, ".tzx"));
	return isTzx ? fileMgr.loadTZX(path, minZX) : fileMgr.loadTAP(path, minZX);
}

static void printUsage(const char* argv0)
{
	std::cout <<
		"Usage: " << argv0 << " [options]\n"
		"\n"
		"Machine selection:\n"
		"  --machine NAME         48k (default), tc2048, or 128k\n"
		"  --rom PATH             override the ROM --machine picked (e.g. a\n"
		"                         standalone diagnostic ROM that replaces it\n"
		"                         outright)\n"
		"  --turbosound           two AY-3-8912 chips instead of one, selected\n"
		"                         via the NedoPC convention real clones use\n"
		"                         (write 0xFF/0xFE as the \"register\" to\n"
		"                         0xFFFD to pick chip A/B -- see TurboSound.h).\n"
		"                         Replaces the plain single-chip AY outright,\n"
		"                         not an addition to it.\n"
		"  --timex-hires          enables the Timex SCLD video mode register\n"
		"                         (port 0xFF, modes 0-2/6) on ANY --machine,\n"
		"                         not just tc2048 -- matches pico-spec's own\n"
		"                         Config::timex_video, a toggle independent\n"
		"                         of the machine model. DOCK paging (port\n"
		"                         0xF4) stays tc2048-only; hi-res itself\n"
		"                         only ever touches ordinary RAM banks 0/1.\n"
		"  --pc-keys              type Spectrum punctuation directly from\n"
		"                         the matching PC key, via SYMBOL SHIFT,\n"
		"                         instead of having to hold Symbol Shift\n"
		"                         yourself: , - . / ; = [ ] \\ ' unshifted;\n"
		"                         RIGHT SHIFT + key for * + : < > ? @ ^ _\n"
		"                         { | } ~ and $ (-> £). LEFT shift keeps\n"
		"                         its normal job (off by default --\n"
		"                         unmodified matrix matches real hardware).\n"
		"\n"
		"Media:\n"
		"  --tape PATH            stage a .tap/.tzx; LOAD \"\" loads it normally\n"
		"  --load-tape PATH       stage a .tap/.tzx AND resume instantly via\n"
		"                         the bundled mid-LOAD snapshot (48K ROM only --\n"
		"                         doesn't work on tc2048/128k, use --tape there)\n"
		"  --load-sna PATH        load a .sna snapshot directly\n"
		"  --load-z80 PATH        load a .z80 snapshot directly (v1/v2/v3,\n"
		"                         compressed or not; 48K/128K only -- the\n"
		"                         snapshot's machine must match --machine,\n"
		"                         since that can't change after startup)\n"
		"  --tape-fastload        restore the old instant-delivery shortcut\n"
		"                         at PC==0x0556 on top of the real tape\n"
		"                         engine (default off -- the pure pulse\n"
		"                         engine alone has to get a tape through)\n"
		"  --dock-cartridge PATH  load a TS2068 .dck cartridge into the DOCK\n"
		"                         bank pool (tc2048 only; doesn't page it in --\n"
		"                         pair with --out F4=MASK and --pc)\n"
		"\n"
		"Peripherals:\n"
		"  --zifi HOST:PORT       connect to a real ESP-01/Wemos D1 bridged\n"
		"                         over TCP, or tools/virtual_esp01's daemon\n"
		"  --multiface1           enable the Multiface ONE stand-in (red\n"
		"                         button = Alt+F10 window hotkey / NMI\n"
		"                         debug command; ports 0x9F/0x1F page its\n"
		"                         8K ROM+RAM in/out over 0x0000-0x3FFF).\n"
		"                         Off by default -- with no card \"installed\"\n"
		"                         NMI still reaches the CPU, it just has\n"
		"                         nothing to page in. Without a real ROM\n"
		"                         (see below) this just proves the paging/\n"
		"                         NMI mechanism works -- no actual menu.\n"
		"  --multiface1-rom PATH  load a real, legally-dumped Multiface ONE\n"
		"                         ROM (exactly 8192 bytes) instead of the\n"
		"                         built-in placeholder; implies --multiface1.\n"
		"                         Copyrighted like the system ROMs -- bring\n"
		"                         your own dump, none is bundled.\n"
		"  --divide               enable the DivIDE stand-in (esxDOS over a\n"
		"                         minimal ATA/IDE register interface --\n"
		"                         ports 0xA3/A7/AB/AF/B3/B7/BB/BF/E3, 8K\n"
		"                         ROM + 4x8K banked RAM over 0x0000-0x3FFF,\n"
		"                         paged in by PC, not NMI). Needs both\n"
		"                         --divide-rom and --divide-disk to do\n"
		"                         anything -- off by default, no ROM/disk\n"
		"                         bundled.\n"
		"  --divide-rom PATH      esxDOS ROM for the IDE variant (exactly\n"
		"                         8192 bytes, e.g. ESXIDE.BIN from an\n"
		"                         esxdos release archive); implies --divide.\n"
		"  --divide-disk PATH     the \"hard disk\" backing store -- a raw\n"
		"                         sector image or a standard .hdf (RS-IDE\n"
		"                         signature), sniffed automatically.\n"
		"  --sdi                  enable the SDI stand-in (ZX Spectrum <->\n"
		"                         Commodore 1541 IEC disk interface; see\n"
		"                         docs/VC1541/VC1541.md). ROM paging, port\n"
		"                         I/O, and IEC ATN/TALK/LISTEN/OPEN/CLOSE\n"
		"                         decode are done; LOAD's data phase isn't\n"
		"                         driven back onto the bus yet.\n"
		"  --sdi-rom PATH         the real SDI103J ROM (exactly 4096 bytes);\n"
		"                         implies --sdi.\n"
		"  --sdi-bridge HOST:PORT connect to tools/c64iec_fs/c64iec_fs_tcp.py\n"
		"                         (or an equivalent companion) for OPEN/\n"
		"                         CLOSE/SAVE; implies --sdi.\n"
		"\n"
		"Debugging:\n"
		"  --debug-port PORT      listen for the line-based debug protocol:\n"
		"                           REGS, MEM addr len, POKE addr b1,b2,...,\n"
		"                           PORT addr, OUT addr val, STATE, STEP [n],\n"
		"                           CONT, STOP, RESET, NMI, BREAK/UNBREAK addr,\n"
		"                           BREAKS, PRESSKEYS chord,chord,..., TYPE text,\n"
		"                           SAVESNA path, LOADZ80 path, SAVEZ80 path,\n"
		"                           SCREENSHOT path\n"
		"                         plus one per peripheral (each device owns its\n"
		"                         own command, see Peripheral.h): KEMPSTON, AY,\n"
		"                         ZIFI, MULTIFACE1, DIVIDE\n"
		"                         (see debugserver.cpp for exact syntax)\n"
		"  --poke ADDR=B1,B2,...  write bytes to memory before execution starts\n"
		"                         (hex address, comma-separated hex bytes)\n"
		"  --pc ADDR              set the initial PC (hex; applied after all\n"
		"                         --poke args)\n"
		"  --out PORT=VALUE       write a port before execution starts (hex)\n"
		"  --trace-port PORT      log every IN from this port when it changes\n"
		"\n"
		"Window hotkeys:\n"
		"  F1                     toggle the on-screen menu -- Storage (load/\n"
		"                         save snapshot, load tape), Machine (info),\n"
		"                         Reset, Network (Gopher/TNFS), ZX Keyboard\n"
		"                         (Symbol Shift legend). Matches pico-spec-\n"
		"                         drewpo's own HK_MAIN_MENU. This is JASE's\n"
		"                         own OSD, not a card -- see Peripherals.\n"
		"  F5                     also opens the menu when closed; once open,\n"
		"                         it's context-specific inside a remote file\n"
		"                         browser (Save a copy locally), matching\n"
		"                         pico-spec's own F2 Reload/F5 Save/F8 Forget\n"
		"                         file-browser convention (also F2 Reload,\n"
		"                         F8 Forget-connection in the Network menus).\n"
		"  F11                    RESET (full machine reset)\n"
		"  Alt+F10                NMI (see --multiface1 above; matches\n"
		"                         pico-spec-drewpo's own F10/Alt+F10 split)\n"
		"  Esc                    quit (or back out of the on-screen menu,\n"
		"                         if it's open)\n"
		"  L                      toggle SDL_LockTexture() vs\n"
		"                         SDL_UpdateTexture() for the video blit\n"
		"                         (perf comparison, not user-facing)\n"
		"\n"
		"  --help, -h             show this message and exit\n";
}

int main(int argc, char *argv[])
{
	for (int i = 1; i < argc; i++)
		if (0 == strcmp(argv[i], "--help") || 0 == strcmp(argv[i], "-h"))
		{
			printUsage(argv[0]);
			return 0;
		}

	if (isLittleEndian()) std::cout << "Running on little endian machine" << std::endl;
	else                  std::cout << "Running on big endian machine"    << std::endl;

	// SDL must be initialized before MinZX::init(), which opens the audio
	// device for the beeper (SDL_OpenAudioDevice needs SDL_INIT_AUDIO up
	// front — it silently fails if called before SDL_Init()).
	SDL_Init(SDL_INIT_EVERYTHING);

	MinZX minZX;
	OsdMenu osdMenu(&minZX);

	// --machine must be applied before init() -- it picks the ROM that
	// loadROM() reads during init().
	for (int i = 1; i < argc; i++)
	{
		if (0 == strcmp(argv[i], "--machine") && i + 1 < argc)
		{
			const char* m = argv[++i];
			if (!minZX.setMachine(m))
				std::cerr << "--machine: unknown value '" << m << "' (expected 48k, tc2048, 128k)" << std::endl;
		}
		else if (0 == strcmp(argv[i], "--rom") && i + 1 < argc)
		{
			minZX.setRomOverride(argv[++i]);
		}
		else if (0 == strcmp(argv[i], "--turbosound"))
		{
			minZX.setTurboSound(true);
		}
		else if (0 == strcmp(argv[i], "--timex-hires"))
		{
			minZX.setTimexVideo(true);
		}
		else if (0 == strcmp(argv[i], "--pc-keys"))
		{
			minZX.setPcKeys(true);
		}
	}

	minZX.init();

	int debugPort = applyDebugArgs(argc, argv, minZX);
	DebugServer debugServer;
	if (debugPort >= 0 && !debugServer.init(debugPort))
		std::cerr << "DebugServer: failed to listen on port " << debugPort << std::endl;

	FileMgr fileMgr;

	// Explicit, named media flags — preferred over the positional-argument
	// fallback below (kept for convenience/backward compat with existing
	// test scripts). Space-separated like the rest of this program's flags
	// (--poke ADDR=..., not --poke=ADDR=...).
	//   --tape PATH       stage the tape (fast-load hook armed), wait for
	//                     LOAD "" to be typed/entered normally.
	//   --load-tape PATH  stage AND resume straight into the ROM's
	//                     LD-BYTES call via a bundled .sna snapshot
	//                     (assets/tape_load_point.sna) — pico-spec style:
	//                     no keyboard/BASIC path at all, instant. A
	//                     register-only version of this was tried first
	//                     and reset the machine (no valid stack beneath
	//                     the jump) — see project memory for why the full
	//                     snapshot (real stack + memory, not just
	//                     registers) is what actually makes this work.
	//   --load-sna PATH   snapshots have no "wait for a command" concept
	//                     (loadSNA already pokes state + sets PC directly),
	//                     so there's only one form.
	const char* tapePath = nullptr;
	const char* autoTapePath = nullptr;
	const char* snaPath = nullptr;
	const char* z80Path = nullptr;
	for (int i = 1; i < argc; i++)
	{
		if (0 == strcmp(argv[i], "--tape") && i + 1 < argc)
			tapePath = argv[++i];
		else if (0 == strcmp(argv[i], "--load-tape") && i + 1 < argc)
			autoTapePath = argv[++i];
		else if (0 == strcmp(argv[i], "--load-sna") && i + 1 < argc)
			snaPath = argv[++i];
		else if (0 == strcmp(argv[i], "--load-z80") && i + 1 < argc)
			z80Path = argv[++i];
		else if (0 == strcmp(argv[i], "--tape-fastload"))
			minZX.setTapeFastload(true);
		else if (0 == strcmp(argv[i], "--multiface1"))
			minZX.getMultiface()->setEnabled(true);
		else if (0 == strcmp(argv[i], "--multiface1-rom") && i + 1 < argc) {
			// Implies --multiface1 -- supplying a real ROM dump only
			// makes sense if you also want the card active.
			minZX.getMultiface()->setEnabled(true);
			if (!minZX.getMultiface()->loadRom(argv[++i]))
				std::cerr << "--multiface1-rom: failed to load, falling back to the built-in placeholder" << std::endl;
		}
		else if (0 == strcmp(argv[i], "--divide"))
			minZX.getDivIde()->setEnabled(true);
		else if (0 == strcmp(argv[i], "--divide-rom") && i + 1 < argc) {
			minZX.getDivIde()->setEnabled(true);
			if (!minZX.getDivIde()->loadRom(argv[++i])) {
				std::cerr << "FATAL: --divide-rom: failed to load " << argv[i] << std::endl;
				exit(1);
			}
		}
		else if (0 == strcmp(argv[i], "--divide-disk") && i + 1 < argc) {
			minZX.getDivIde()->setEnabled(true);
			if (!minZX.getDivIde()->insertDisk(argv[++i])) {
				std::cerr << "FATAL: --divide-disk: failed to load " << argv[i] << std::endl;
				exit(1);
			}
		}
		else if (0 == strcmp(argv[i], "--sdi"))
			minZX.getSDI()->setEnabled(true);
		else if (0 == strcmp(argv[i], "--sdi-rom") && i + 1 < argc) {
			minZX.getSDI()->setEnabled(true);
			if (!minZX.getSDI()->loadRom(argv[++i])) {
				std::cerr << "FATAL: --sdi-rom: failed to load " << argv[i] << std::endl;
				exit(1);
			}
		}
		else if (0 == strcmp(argv[i], "--sdi-bridge") && i + 1 < argc) {
			// HOST:PORT for tools/c64iec_fs/c64iec_fs_tcp.py (or an
			// equivalent companion speaking the same wire protocol) --
			// same "just a TCP byte pipe, backend is this process's own
			// business" split as --zifi.
			char* arg = argv[++i];
			char* colon = strrchr(arg, ':');
			if (!colon) { std::cerr << "--sdi-bridge expects HOST:PORT" << std::endl; continue; }
			*colon = '\0';
			minZX.getSDI()->connectBridge(arg, atoi(colon + 1));
		}
	}

	// FileMgr's load* functions already print exactly which path failed
	// (see filemgr.cpp's WARN calls) -- what was missing was main.cpp ever
	// checking the bool they return. Silently continuing past a failed
	// load left the machine running with whatever was already in memory
	// (usually nothing), indistinguishable from a real hang.
	if (autoTapePath)
	{
		if (!loadTapeFile(fileMgr, autoTapePath, &minZX)) {
			std::cerr << "FATAL: --load-tape: failed to load " << autoTapePath << std::endl;
			exit(1);
		}
		fileMgr.loadSNA(MinZX::resolveAssetPath("assets/tape_load_point.sna").c_str(), &minZX);
	}
	else
	{
		// --tape and --load-sna are independent and can combine (e.g. to
		// resume a captured "mid LD-BYTES call" snapshot with a tape
		// staged so the fast-load hook is armed when it lands on 0x0556)
		// — NOT an if/else chain like --load-tape's own exclusive path.
		if (tapePath && !loadTapeFile(fileMgr, tapePath, &minZX)) {
			std::cerr << "FATAL: --tape: failed to load " << tapePath << std::endl;
			exit(1);
		}
		if (snaPath && !fileMgr.loadSNA(snaPath, &minZX)) {
			std::cerr << "FATAL: --load-sna: failed to load " << snaPath << std::endl;
			exit(1);
		}
		if (z80Path && !fileMgr.loadZ80(z80Path, &minZX)) {
			std::cerr << "FATAL: --load-z80: failed to load " << z80Path << std::endl;
			exit(1);
		}
		if (!tapePath && !snaPath && !z80Path && argc > 1 && argv[1][0] != '-')
		{
			const char* path = argv[1];
			size_t len = strlen(path);
			bool isTape = len > 4 && (0 == strcasecmp(path + len - 4, ".tap") || 0 == strcasecmp(path + len - 4, ".tzx"));
			bool isZ80 = len > 4 && 0 == strcasecmp(path + len - 4, ".z80");
			bool ok = isTape ? loadTapeFile(fileMgr, path, &minZX)
			        : isZ80 ? fileMgr.loadZ80(path, &minZX)
			        : fileMgr.loadSNA(path, &minZX);
			if (!ok) {
				std::cerr << "FATAL: failed to load " << path << std::endl;
				exit(1);
			}
		}
	}

	unsigned vsyncFlag = 0;
//	unsigned vsyncFlag = SDL_RENDERER_PRESENTVSYNC;

	SDL_Window* window = SDL_CreateWindow
		(
		"SDL2",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
//		960, 720,
		1280, 960,
//		2560, 1920,
		SDL_WINDOW_SHOWN
		);

	SDL_Renderer* renderer = SDL_CreateRenderer
		(
		window,
		-1,
		SDL_RENDERER_ACCELERATED | vsyncFlag
		);

	SDL_RendererInfo info = {};
	if (0 == SDL_GetRendererInfo(renderer, &info))
	{
		std::cout << "Renderer name: " << info.name << std::endl;
		std::cout << "Texture formats: " << std::endl;
		// info.texture_formats is a fixed 16-entry array (SDL2's own
		// SDL_RendererInfo layout) -- num_texture_formats SHOULD never
		// exceed that, but under SDL_VIDEODRIVER=dummy (no real
		// acceleration, used for headless debug-protocol testing) it's
		// been observed garbage/huge, walking this loop straight past
		// the array into an intermittent segfault.
		Uint32 count = info.num_texture_formats;
		if (count > SDL_arraysize(info.texture_formats))
			count = SDL_arraysize(info.texture_formats);
		for (Uint32 i = 0; i < count; i++)
			std::cout << SDL_GetPixelFormatName(info.texture_formats[i]) << std::endl;
	}
	else
	{
		std::cerr << "SDL_GetRendererInfo failed: " << SDL_GetError() << std::endl;
	}

	const unsigned int texWidth = MinZX::SCREEN_W;
	const unsigned int texHeight = MinZX::SCREEN_H;
	SDL_Texture* texture = SDL_CreateTexture
		(
		renderer,
		SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING,
		texWidth, texHeight
		);

	std::vector< unsigned char > pixels(texWidth * texHeight * 4, 0);

	SDL_Event event;
	bool running = true;
	bool useLocktexture = false;

	unsigned int frames = 0;
	Uint64 start = SDL_GetPerformanceCounter();
	
	while (running)
	{

		SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
		SDL_RenderClear(renderer);

		while (SDL_PollEvent(&event))
		{
			if (SDL_QUIT == event.type)
			{
				running = false;
				break;
			}

			// F1 is pico-spec's own HK_MAIN_MENU binding -- the one key
			// that always opens/closes the menu outright.
			if (SDL_KEYDOWN == event.type && SDL_SCANCODE_F1 == event.key.keysym.scancode)
			{
				osdMenu.toggle();
				continue;
			}
			// F5 ALSO opens the menu when closed (common "quick menu"
			// convention elsewhere), but once open it's a context-specific
			// action key (pico-spec's own convention: F2 Reload/F5 Save/
			// F8 Forget inside a file browser) -- so it must NOT be
			// intercepted here while the menu is already open, or it could
			// never reach osdMenu.handleKey() to do anything else.
			if (SDL_KEYDOWN == event.type && SDL_SCANCODE_F5 == event.key.keysym.scancode && !osdMenu.isOpen())
			{
				osdMenu.toggle();
				continue;
			}

			// While the menu is open it owns all keyboard input -- Esc backs
			// out of it instead of quitting, arrows/Enter navigate, and none
			// of the hotkeys below (L/F11/Alt+F10) fire. The Spectrum's own
			// keyboard matrix keeps reading SDL_GetKeyboardState() live
			// regardless (see minzx.cpp), but that's harmless: main.cpp
			// skips calling minZX.update() for every frame the menu is open
			// (see below), so nothing ever acts on it.
			if (osdMenu.isOpen())
			{
				if (SDL_KEYDOWN == event.type)
					osdMenu.handleKey(event.key.keysym.scancode);
				else if (SDL_TEXTINPUT == event.type)
					osdMenu.handleTextInput(event.text.text);
				continue;
			}

			if (SDL_KEYDOWN == event.type && SDL_SCANCODE_ESCAPE == event.key.keysym.scancode)
			{
				running = false;
				break;
			}
			if (SDL_KEYDOWN == event.type && SDL_SCANCODE_L == event.key.keysym.scancode)
			{
				useLocktexture = !useLocktexture;
				std::cout << "Using " << (useLocktexture ? "SDL_LockTexture() + memcpy()" : "SDL_UpdateTexture()") << std::endl;
			}
			// F11 = RESET, Alt+F10 = NMI -- matches pico-spec-drewpo's own
			// hotkey convention (Config.cpp: HK_HARD_RESET on plain F11,
			// HK_NMI on Alt+F10; plain F10 there is volume, not reset) so
			// muscle memory carries over between the two emulators.
			if (SDL_KEYDOWN == event.type && SDL_SCANCODE_F11 == event.key.keysym.scancode)
			{
				minZX.reset();
				std::cout << "RESET" << std::endl;
			}
			if (SDL_KEYDOWN == event.type && SDL_SCANCODE_F10 == event.key.keysym.scancode
				&& (event.key.keysym.mod & KMOD_ALT))
			{
				minZX.triggerNMI();
				std::cout << "NMI" << std::endl;
			}
		}

		// splat down some random pixels
		if (!osdMenu.isOpen())
		{
			for (unsigned int i = 0; i < 1000; i++)
			{
				const unsigned int x = rand() % texWidth;
				const unsigned int y = rand() % texHeight;

				const unsigned int offset = (texWidth * 4 * y) + x * 4;
				pixels[offset + 0] = rand() % 256;        // b
				pixels[offset + 1] = rand() % 256;        // g
				pixels[offset + 2] = rand() % 256;        // r
				pixels[offset + 3] = SDL_ALPHA_OPAQUE;    // a
			}
		}

		// Menu open == emulation paused: skip minZX.update() so the last
		// rendered Spectrum frame stays in `pixels` underneath the overlay,
		// same "frozen picture behind the menu" look fusex/pico-spec have.
		if (debugServer.isRunning() && !osdMenu.isOpen())
			minZX.update(&pixels[0]);
		debugServer.poll(minZX);

		osdMenu.render(&pixels[0], texWidth, texHeight);

		if (useLocktexture)
		{
			unsigned char* lockedPixels = nullptr;
			int pitch = 0;
			SDL_LockTexture
				(
				texture,
				NULL,
				reinterpret_cast< void** >(&lockedPixels),
				&pitch
				);
			std::memcpy(lockedPixels, pixels.data(), pixels.size());
			SDL_UnlockTexture(texture);
		}
		else
		{
			SDL_UpdateTexture
				(
				texture,
				NULL,
				pixels.data(),
				texWidth * 4
				);
		}

		SDL_RenderCopy(renderer, texture, NULL, NULL);
		SDL_RenderPresent(renderer);

		// Pace the loop off the audio queue's real depth, not a fixed
		// wall-clock delay — a fixed SDL_Delay(17) drifts from the audio
		// device's exact 44100 Hz consumption rate (observed ~47-48 FPS in
		// practice, not 50), so the queue slowly starves and the beeper
		// crackles. Let ~3 frames build up, then block until it's down to
		// ~2 — audio becomes the clock, video rides along with it (same
		// principle pico-spec uses via its blocking I2S/PWM write).
		const Uint32 samplesPerFrame = (Uint32)MinZX::kAudioSamplesPerFrame;
		while (minZX.getQueuedAudioBytes() > samplesPerFrame * 3)
			SDL_Delay(1);
		// Menu open means minZX.update() isn't running, so the audio queue
		// drains and the loop above never blocks -- without this it busy-
		// spins the render loop as fast as the GPU allows for no reason.
		if (osdMenu.isOpen())
			SDL_Delay(10);

		frames++;
		const Uint64 end = SDL_GetPerformanceCounter();
		const static Uint64 freq = SDL_GetPerformanceFrequency();
		const double seconds = (end - start) / static_cast< double >(freq);
		if (seconds > 2.0)
		{
			std::cout
				<< frames << " frames in "
				<< std::setprecision(1) << std::fixed << seconds << " seconds = "
				<< std::setprecision(1) << std::fixed << frames / seconds << " FPS ("
				<< std::setprecision(3) << std::fixed << (seconds * 1000.0) / frames << " ms/frame)"
				<< std::endl;
			start = end;
			frames = 0;
		}
	}

	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();

	return 0;
}
