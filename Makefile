# Linux/SDL2 build. The MinZX_SDL.sln/.vcxproj files are for the original
# Windows/Visual-Studio project this was forked from (see README.md) --
# unrelated to this Makefile and not needed to build on Linux.
CXX      ?= g++
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
INCLUDES := -Iinclude -Iinclude/z80cpp $(shell pkg-config --cflags sdl2) $(shell pkg-config --cflags nlohmann_json)
LIBS     := $(shell pkg-config --libs sdl2) -ldl

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

.PHONY: all clean

all: $(TARGET) $(PLUGIN_KEMPSTON)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

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

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)
	rm -f $(PLUGIN_KEMPSTON_OBJS) $(PLUGIN_KEMPSTON_OBJS:.o=.d) $(PLUGIN_KEMPSTON)
