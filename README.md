# JASE — Just Another Spectrum Emulator

A minimal ZX Spectrum emulator built for one specific job: a fast,
flash-free dev loop for testing [pico-spec](https://github.com/barszpaw/pico-spec)'s
network peripherals (Gopher, TNFS, FTP, ...) on Linux, without needing a
real RP2350 board for every iteration. Not trying to compete with
Fuse/ZEsarUX as a general-purpose emulator.

Built on [dcrespo3d/MinZX_SDL](https://github.com/dcrespo3d/MinZX_SDL)
(itself on [jsanchezv/z80cpp](https://github.com/jsanchezv/z80cpp)) — the
one genuinely portable, cleanly-decoupled Z80 core in the whole
ESPectrum/pico-zxspectrum/pico-spec lineage (`class MinZX : public
Z80operations`, flat memory/port arrays, zero external singletons).
pico-spec's own Z80 opcode engine was tried first and ruled out: it calls
`Ports::`/`VIDEO::`/`MemESP::`/`Config::` directly by name throughout,
not through a swappable interface — a deliberate, reasonable choice for
embedded performance, but it means the engine can't be lifted out without
dragging in most of the emulator.

## Status

- Boots a real 48K ROM (confirmed: the actual `© 1982 Sinclair Research
  Ltd` boot screen renders correctly via SDL2/OpenGL).
- Kempston joystick: arrow keys + left Alt (fire), standard bit layout,
  ported from pico-spec's `Ports.cpp`/`ESPectrum.cpp`.
- No text keyboard matrix yet (port 0xFE is stubbed) — BASIC can't be
  typed into interactively yet.
- No sound yet.
- No network peripherals yet (the actual point of this project) — next
  up.

## Debugging without a human at the keyboard

This project is explicitly also built for an AI agent to drive and
verify, not just a human. Command-line flags inject state directly
instead of relying on synthesizing SDL input events (fragile — depends on
X11 window focus, unreliable in headless sessions):

```
--poke ADDR=B1,B2,B3,...   poke raw bytes into memory (hex, repeatable)
--pc ADDR                  set the initial program counter (hex)
--trace-port ADDR          print "PORT xxxx = yy" whenever that port's
                           read value changes (hex)
```

Example — exercise the Kempston port without a working keyboard matrix,
with a tiny hand-assembled infinite loop (`IN A,(0x1F) ; JR` back to
itself):

```
./jase --poke 8000=DB,1F,18,FC --pc 8000 --trace-port 1F
```

Planned next step: a GDB remote-serial-protocol stub (see fusex's
`debugger/gdbserver.c` for a reference, ~1366 lines) instead of growing
more ad-hoc flags — real memory/register access, breakpoints, and
single-step, speakable by actual `gdb`/VS Code or a scripted session.

## Build

Plain `g++` + SDL2, no Visual Studio project needed on Linux:

```
g++ -std=c++17 -include cstring -I src -I include/z80cpp -I include \
  $(pkg-config --cflags sdl2) \
  src/main.cpp src/minzx.cpp src/filemgr.cpp src/z80cpp/z80.cpp \
  $(pkg-config --libs sdl2) \
  -o jase
```

Needs a real 48K ROM at `zx48.rom` in the working directory (not included
— copyrighted; not ours to distribute).
