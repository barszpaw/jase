// Standalone disk-write round-trip test (2026-11-14): proves Write Sector
// actually persists to the real .dsk file via DskImage::flush(), not just
// the in-memory copy (see CLAUDE.md/project memory for the gap this
// closed). Three stages:
//   1. Write a known byte pattern to a real sector via WD1772Fsm's own
//      command interface (WRITE SECTOR, 0xA0).
//   2. Read it straight back (in-memory correctness -- proves the FSM's
//      own write path still works).
//   3. Load a FRESH, SEPARATE Wd1772Fsm/DskImage from the SAME file path
//      and read the same sector again -- this can only see the pattern
//      if flush() genuinely wrote it to disk, since this second instance
//      never touched the first one's in-memory state at all.
//
// Usage: ALWAYS pass a DISPOSABLE COPY of a real .dsk image, never an
// original -- this test genuinely overwrites track 1/sector 1 with a
// synthetic pattern.
//   cp some_real.dsk /tmp/disposable_copy.dsk
//   make write-test && tools/wd1772_write_test /tmp/disposable_copy.dsk
#include "Wd1772Fsm.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>

// STATUS_* bit values are private to Wd1772Fsm (register-level detail,
// not a public contract) -- this external test uses the raw hex values
// directly instead, same numbers confirmed from Wd1772Fsm.h itself.
static const uint8_t ST_BUSY = 0x01, ST_CRC_ERROR = 0x08, ST_RNF = 0x10, ST_WRITE_PROT = 0x40;

static bool runCommand(Wd1772Fsm& wd, uint8_t cmd, int maxTicks = 2000000)
{
	wd.writeCommand(cmd);
	for (int i = 0; i < maxTicks; i++) {
		wd.tick(16);
		if (!(wd.readStatus() & ST_BUSY)) return true;
	}
	return false;
}

int main(int argc, char** argv)
{
	if (argc < 2) {
		fprintf(stderr, "usage: %s <path-to-DISPOSABLE-.dsk-copy>\n", argv[0]);
		fprintf(stderr, "  (this genuinely overwrites track 1/sector 1 -- never pass an original)\n");
		return 1;
	}
	const char* path = argv[1];
	const int track = 1, sector = 1; // a real, present sector on TDU-01.dsk-shaped disks
	uint8_t pattern[256];
	for (int i = 0; i < 256; i++) pattern[i] = (uint8_t)(0xA5 ^ i); // distinctive, not all-zero/all-FF

	// ---- Stage 1+2: write then read back, same live instance ----
	{
		Wd1772Fsm wd;
		if (!wd.insertDisk(0, path)) { fprintf(stderr, "FAIL: insertDisk failed\n"); return 1; }
		wd.selectDrive(0);

		if (!runCommand(wd, 0x00)) { fprintf(stderr, "FAIL: RESTORE never completed\n"); return 1; } // Type I RESTORE -> track 0
		if (wd.readStatus() & ST_CRC_ERROR) { fprintf(stderr, "FAIL: RESTORE CRC error\n"); return 1; }

		wd.writeTrackReg((uint8_t)track);
		wd.writeSectorReg((uint8_t)sector);
		wd.writeCommand(0xA0); // WRITE SECTOR, single sector, no delay flags
		int fed = 0;
		for (int i = 0; i < 2000000 && fed < 256; i++) {
			wd.tick(16);
			if (wd.drq()) { wd.writeDataReg(pattern[fed]); fed++; }
			if (!(wd.readStatus() & ST_BUSY) && fed < 256) break; // gave up early (RNF etc)
		}
		if (fed != 256) { fprintf(stderr, "FAIL: only fed %d/256 write bytes before BUSY cleared\n", fed); return 1; }
		for (int i = 0; i < 1000 && (wd.readStatus() & ST_BUSY); i++) wd.tick(16);
		uint8_t st = wd.readStatus();
		if (st & (ST_CRC_ERROR | ST_RNF | ST_WRITE_PROT)) {
			fprintf(stderr, "FAIL: WRITE SECTOR status=0x%02x after completion\n", st);
			return 1;
		}
		printf("OK stage 1: WRITE SECTOR completed cleanly (status=0x%02x)\n", st);

		// Read it straight back, same live FSM/DskImage instance.
		wd.writeTrackReg((uint8_t)track);
		wd.writeSectorReg((uint8_t)sector);
		wd.writeCommand(0x80); // READ SECTOR
		uint8_t readBack[256] = {};
		int got = 0;
		for (int i = 0; i < 2000000 && got < 256; i++) {
			wd.tick(16);
			if (wd.drq()) { readBack[got++] = wd.readDataReg(); }
			if (!(wd.readStatus() & ST_BUSY) && got < 256) break;
		}
		if (got != 256) { fprintf(stderr, "FAIL: only read %d/256 bytes back in-memory\n", got); return 1; }
		if (memcmp(readBack, pattern, 256) != 0) { fprintf(stderr, "FAIL: in-memory read-back mismatch\n"); return 1; }
		printf("OK stage 2: in-memory read-back matches the written pattern exactly\n");
	}
	// First Wd1772Fsm/DskImage instance is now destroyed -- its own
	// in-memory copy is gone. Anything found below can ONLY have come
	// from the real file on disk.

	// ---- Stage 3: fresh instance, same file path, prove persistence ----
	{
		Wd1772Fsm wd2;
		if (!wd2.insertDisk(0, path)) { fprintf(stderr, "FAIL: second insertDisk failed\n"); return 1; }
		wd2.selectDrive(0);
		if (!runCommand(wd2, 0x00)) { fprintf(stderr, "FAIL: second RESTORE never completed\n"); return 1; }

		wd2.writeTrackReg((uint8_t)track);
		wd2.writeSectorReg((uint8_t)sector);
		wd2.writeCommand(0x80); // READ SECTOR
		uint8_t readBack2[256] = {};
		int got2 = 0;
		for (int i = 0; i < 2000000 && got2 < 256; i++) {
			wd2.tick(16);
			if (wd2.drq()) { readBack2[got2++] = wd2.readDataReg(); }
			if (!(wd2.readStatus() & ST_BUSY) && got2 < 256) break;
		}
		if (got2 != 256) { fprintf(stderr, "FAIL: only read %d/256 bytes back from a fresh instance\n", got2); return 1; }
		if (memcmp(readBack2, pattern, 256) != 0) {
			fprintf(stderr, "FAIL: fresh-instance read does NOT match -- persistence to disk did not happen\n");
			return 1;
		}
		printf("OK stage 3: a FRESH Wd1772Fsm/DskImage, loading the SAME file from disk, reads back the\n"
		       "            exact written pattern -- DskImage::flush() genuinely persisted the write.\n");
	}

	printf("PASS: write -> persist -> reload -> read round-trip verified end to end.\n");
	return 0;
}
