# Linux/SDL2 build. The MinZX_SDL.sln/.vcxproj files are for the original
# Windows/Visual-Studio project this was forked from (see README.md) --
# unrelated to this Makefile and not needed to build on Linux.
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall
INCLUDES := -Iinclude -Iinclude/z80cpp $(shell pkg-config --cflags sdl2)
LIBS     := $(shell pkg-config --libs sdl2)

SRCS := $(wildcard src/*.cpp) src/z80cpp/z80.cpp
OBJS := $(SRCS:.cpp=.o)

TARGET := jase

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
