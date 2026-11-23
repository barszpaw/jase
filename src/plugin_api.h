#pragma once
#include "Peripheral.h"

// POC plugin ABI (2026-10-28): a peripheral can ship as its own .so instead
// of being linked into the core `jase` binary, so it can be released/
// updated independently (see project memory for why — SDI release cadence
// vs. core release cadence). Each plugin .so exports these two C-linkage
// symbols; core dlopen()s the file and dlsym()s them by name.
//
// NOT a stable cross-compiler ABI: Peripheral is a plain C++ virtual-method
// interface, so plugin and core must be built with the same compiler/flags
// (true here — same Makefile, same toolchain, every plugin built from
// source alongside core). A real stable ABI would need a C struct of
// function pointers instead of C++ vtables; out of scope for this POC.
extern "C" {
	typedef Peripheral* (*jase_plugin_create_fn)();
	typedef void (*jase_plugin_destroy_fn)(Peripheral*);
}

#define JASE_PLUGIN_CREATE_SYM  "jase_plugin_create"
#define JASE_PLUGIN_DESTROY_SYM "jase_plugin_destroy"
