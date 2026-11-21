# Linux/SDL2 build. The MinZX_SDL.sln/.vcxproj files are for the original
# Windows/Visual-Studio project this was forked from (see README.md) --
# unrelated to this Makefile and not needed to build on Linux.
CXX      ?= g++
CC       ?= gcc
# -MMD -MP: emit a per-.o .d file listing every header it actually
# included, so editing a .h (e.g. SDI_8255.h) correctly triggers a
# rebuild of every .cpp that includes it, not just the .cpp with the
# same basename. Without this, incremental `make` after a header-only
# change silently links stale .o files against the new ones -- real
# case: SDI_8255.h lost three fields but main.o (which calls its inline
# setEnabled()) wasn't recompiled, so the stale/fresh object files
# disagreed about the class layout and setEnabled() stopped taking
# effect -- `make clean` was the only thing that fixed it. -MP adds a
# phony target per header so a deleted/renamed header doesn't break the
# build with a "no rule to make target" error.
CXXFLAGS ?= -std=c++17 -O2 -Wall -MMD -MP
# nlohmann_json (settings.json loading, main.cpp): `sudo apt install
# nlohmann-json3-dev` -- header-only, this just confirms it's present and
# adds its -I if the package ever relocates it off the default system path.
INCLUDES := -Iinclude -Iinclude/z80cpp -Isrc/z80free $(shell pkg-config --cflags sdl2) $(shell pkg-config --cflags nlohmann_json)
LIBS     := $(shell pkg-config --libs sdl2) -ldl

# src/mame_z80/, src/mame_wd1772/ (BSD-3-Clause MAME ports): kept in the
# tree for reference/future differential testing (see project memory),
# but NOT built into either target below -- the station's real core
# pairing (src/z80cpp/, src/Wd1772Fsm.h/.cpp) needs neither. CFLAGS/the .c
# rule below stay in case anything ever needs a plain-C build again.
CFLAGS   ?= -std=c99 -O2 -Wall -MMD -MP

# src/Kempston.cpp is excluded here: it ships as plugins/libkempston.so
# instead (POC, 2026-10-28, see src/plugin_api.h) -- a peripheral that can
# be released independently of core. It's still compiled, just into the
# plugin target below instead of into $(TARGET).
SRCS := $(filter-out src/Kempston.cpp,$(wildcard src/*.cpp)) src/z80cpp/z80.cpp
OBJS := $(SRCS:.cpp=.o)
DEPS := $(OBJS:.o=.d)

TARGET := jase

PLUGIN_KEMPSTON := plugins/libkempston.so
PLUGIN_KEMPSTON_SRCS := src/Kempston.cpp src/plugins/kempston_plugin.cpp
PLUGIN_KEMPSTON_OBJS := $(PLUGIN_KEMPSTON_SRCS:.cpp=.plugin.o)

# fdd3000_station (2026-11-11 unification -- see FddStation.h's own class
# comment): the standalone-process driver for the SAME FddStation class
# Fdd3000 itself uses in-process. No SDL/MinZX dependency.
STATION_TARGET := fdd3000_station/fdd3000_station
STATION_INCLUDES := -Isrc -Iinclude/z80cpp
STATION_OBJS := fdd3000_station/main.o src/FddStation.o src/Wd1772Fsm.o src/DskImage.o src/z80cpp/z80.o

# wd1772_write_test (2026-11-14 -- see tools/wd1772_write_test.cpp's own
# comment): standalone Write-Sector-persists-to-disk round-trip test, no
# SDL/MinZX dependency, same two core objects the station target uses.
WRITE_TEST_TARGET := tools/wd1772_write_test
WRITE_TEST_OBJS := tools/wd1772_write_test.o src/Wd1772Fsm.o src/DskImage.o

# dsk_browser (2026-11-21 -- see its own file comment): standalone
# dual-pane R/W browser for CP/M .dsk images, built on CpmFs/DskImage.
# No SDL/MinZX dependency; needs libncurses-dev (ncurses pkg-config module).
DSK_BROWSER_TARGET := tools/dsk_browser
DSK_BROWSER_OBJS := tools/dsk_browser.o src/CpmFs.o src/TosFs.o src/DskImage.o
DSK_BROWSER_LIBS := $(shell pkg-config --libs ncurses)

.PHONY: all clean station write-test dsk-browser

all: $(TARGET) $(PLUGIN_KEMPSTON)

station: $(STATION_TARGET)

write-test: $(WRITE_TEST_TARGET)

dsk-browser: $(DSK_BROWSER_TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LIBS)

$(STATION_TARGET): $(STATION_OBJS)
	$(CXX) $(STATION_OBJS) -o $@ -lpthread

$(WRITE_TEST_TARGET): $(WRITE_TEST_OBJS)
	$(CXX) $(WRITE_TEST_OBJS) -o $@

$(DSK_BROWSER_TARGET): $(DSK_BROWSER_OBJS)
	$(CXX) $(DSK_BROWSER_OBJS) -o $@ $(DSK_BROWSER_LIBS)

fdd3000_station/main.o: fdd3000_station/main.cpp
	$(CXX) $(CXXFLAGS) $(STATION_INCLUDES) -c $< -o $@

tools/wd1772_write_test.o: tools/wd1772_write_test.cpp
	$(CXX) $(CXXFLAGS) -Isrc -c $< -o $@

tools/dsk_browser.o: tools/dsk_browser.cpp
	$(CXX) $(CXXFLAGS) -Isrc -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -Isrc/z80free -c $< -o $@

# Plugin objects are built with -fPIC into their own .plugin.o (so they
# don't collide with the core's plain .o of the same source, e.g.
# src/Kempston.plugin.o vs. a hypothetical src/Kempston.o).
%.plugin.o: %.cpp
	$(CXX) $(CXXFLAGS) -fPIC $(INCLUDES) -c $< -o $@

$(PLUGIN_KEMPSTON): $(PLUGIN_KEMPSTON_OBJS)
	@mkdir -p plugins
	$(CXX) -shared $(PLUGIN_KEMPSTON_OBJS) -o $@ $(shell pkg-config --libs sdl2)

-include $(DEPS)
-include $(PLUGIN_KEMPSTON_OBJS:.o=.d)
-include fdd3000_station/main.d

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)
	rm -f $(PLUGIN_KEMPSTON_OBJS) $(PLUGIN_KEMPSTON_OBJS:.o=.d) $(PLUGIN_KEMPSTON)
	rm -f fdd3000_station/main.o fdd3000_station/main.d $(STATION_TARGET)
	rm -f tools/wd1772_write_test.o tools/wd1772_write_test.d $(WRITE_TEST_TARGET)
	rm -f tools/dsk_browser.o tools/dsk_browser.d $(DSK_BROWSER_TARGET)
