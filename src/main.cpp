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
			if (!minZX.loadDockCartridge(argv[++i]))
				std::cerr << "--dock-cartridge: failed to load " << argv[i] << std::endl;
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
		"\n"
		"Media:\n"
		"  --tape PATH            stage a .tap/.tzx; LOAD \"\" loads it normally\n"
		"  --load-tape PATH       stage a .tap/.tzx AND resume instantly via\n"
		"                         the bundled mid-LOAD snapshot (48K ROM only --\n"
		"                         doesn't work on tc2048/128k, use --tape there)\n"
		"  --load-sna PATH        load a .sna snapshot directly\n"
		"  --tape-fastload        restore the old instant-delivery shortcut\n"
		"                         at PC==0x0556 on top of the real tape\n"
		"                         engine (default off -- the pure pulse\n"
		"                         engine alone has to get a tape through)\n"
		"  --dock-cartridge PATH  load a TS2068 .dck cartridge into the DOCK\n"
		"                         bank pool (tc2048 only; doesn't page it in --\n"
		"                         pair with --out F4=MASK and --pc)\n"
		"\n"
		"Debugging:\n"
		"  --debug-port PORT      listen for the line-based debug protocol\n"
		"                         (REGS/MEM/POKE/STEP/BREAK/PRESSKEYS/TYPE/\n"
		"                         SCREENSHOT/STATE/... -- see debugserver.cpp)\n"
		"  --poke ADDR=B1,B2,...  write bytes to memory before execution starts\n"
		"                         (hex address, comma-separated hex bytes)\n"
		"  --pc ADDR              set the initial PC (hex; applied after all\n"
		"                         --poke args)\n"
		"  --out PORT=VALUE       write a port before execution starts (hex)\n"
		"  --trace-port PORT      log every IN from this port when it changes\n"
		"\n"
		"Networking (ZiFi peripheral):\n"
		"  --zifi HOST:PORT       connect to a real ESP-01/Wemos D1 bridged\n"
		"                         over TCP, or tools/virtual_esp01's daemon\n"
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
	for (int i = 1; i < argc; i++)
	{
		if (0 == strcmp(argv[i], "--tape") && i + 1 < argc)
			tapePath = argv[++i];
		else if (0 == strcmp(argv[i], "--load-tape") && i + 1 < argc)
			autoTapePath = argv[++i];
		else if (0 == strcmp(argv[i], "--load-sna") && i + 1 < argc)
			snaPath = argv[++i];
		else if (0 == strcmp(argv[i], "--tape-fastload"))
			minZX.setTapeFastload(true);
	}

	if (autoTapePath)
	{
		loadTapeFile(fileMgr, autoTapePath, &minZX);
		fileMgr.loadSNA("assets/tape_load_point.sna", &minZX);
	}
	else
	{
		// --tape and --load-sna are independent and can combine (e.g. to
		// resume a captured "mid LD-BYTES call" snapshot with a tape
		// staged so the fast-load hook is armed when it lands on 0x0556)
		// — NOT an if/else chain like --load-tape's own exclusive path.
		if (tapePath)
			loadTapeFile(fileMgr, tapePath, &minZX);
		if (snaPath)
			fileMgr.loadSNA(snaPath, &minZX);
		if (!tapePath && !snaPath && argc > 1 && argv[1][0] != '-')
		{
			const char* path = argv[1];
			size_t len = strlen(path);
			bool isTape = len > 4 && (0 == strcasecmp(path + len - 4, ".tap") || 0 == strcasecmp(path + len - 4, ".tzx"));
			if (isTape)
				loadTapeFile(fileMgr, path, &minZX);
			else
				fileMgr.loadSNA(path, &minZX);
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

	SDL_RendererInfo info;
	SDL_GetRendererInfo(renderer, &info);
	std::cout << "Renderer name: " << info.name << std::endl;
	std::cout << "Texture formats: " << std::endl;
	for (Uint32 i = 0; i < info.num_texture_formats; i++)
	{
		std::cout << SDL_GetPixelFormatName(info.texture_formats[i]) << std::endl;
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
			if ((SDL_QUIT == event.type) ||
				(SDL_KEYDOWN == event.type && SDL_SCANCODE_ESCAPE == event.key.keysym.scancode))
			{
				running = false;
				break;
			}
			if (SDL_KEYDOWN == event.type && SDL_SCANCODE_L == event.key.keysym.scancode)
			{
				useLocktexture = !useLocktexture;
				std::cout << "Using " << (useLocktexture ? "SDL_LockTexture() + memcpy()" : "SDL_UpdateTexture()") << std::endl;
			}
		}

		// splat down some random pixels
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

		if (debugServer.isRunning())
			minZX.update(&pixels[0]);
		debugServer.poll(minZX);

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
