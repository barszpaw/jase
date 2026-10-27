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
INCLUDES := -Iinclude -Iinclude/z80cpp $(shell pkg-config --cflags sdl2)
LIBS     := $(shell pkg-config --libs sdl2)

SRCS := $(wildcard src/*.cpp) src/z80cpp/z80.cpp
OBJS := $(SRCS:.cpp=.o)
DEPS := $(OBJS:.o=.d)

TARGET := jase

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) $(TARGET)
