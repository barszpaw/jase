#include "Wd1772Fsm.h"
#include <cstdio>

void Wd1772Fsm::logTrace(const char* what) const
{
	if (!traceEnabled_) return;
	fprintf(stderr,
	        "WD1772FSMTRACE %-4s drive=%d side=%s dens=%s track=%02x sector=%02x cmd=%02x "
	        "status=%02x drq=%d intrq=%d\n",
	        what, selectedDrive_, side_ ? "B" : "A", density_ ? "MFM" : "FM",
	        trackRegister, sectorRegister, currentCommand,
	        statusRegister, drqFlag_ ? 1 : 0, intrqFlag_ ? 1 : 0);
}
