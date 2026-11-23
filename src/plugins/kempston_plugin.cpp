#include "../Kempston.h"

// C-linkage entry points the core dlopen()s -- see plugin_api.h for the
// contract. This file (plus Kempston.cpp/.h) is compiled into
// plugins/libkempston.so instead of the core `jase` binary.
extern "C" Peripheral* jase_plugin_create()
{
	return new Kempston();
}

extern "C" void jase_plugin_destroy(Peripheral* p)
{
	delete p;
}
