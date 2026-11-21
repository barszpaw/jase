// Converts a headerless "raw" disk image (plain sequential track-major/
// side-minor dump, no container, no recorded interleave -- see
// DskImage::loadRaw()'s own comment and ~/src/TOStools's RawImageAsDSK,
// the project this convention comes from) into a real CPCEMU/Extended DSK
// file that JASE itself (--fdd3000-disk-a/b/c/d) and dsk_browser can both
// open directly, with no --raw/--format switches needed anywhere else --
// JASE's own disk-insertion path (Wd1772Fsm::insertDisk()) only ever calls
// DskImage::load(), never loadRaw(), so converting once up front is the
// simplest way to make a raw image usable in the real emulator.
//
// All real disks in this project use 16 sectors/track, 256 bytes/sector
// (see DskImage.h/CpmFs.h), so --format only needs to pick (tracks, sides);
// "amstrad-cpc" is listed separately from "tos-timex" for familiarity with
// ~/src/TOStools's own --format enum even though both resolve to the same
// 40-track/1-side/170KB geometry.
//
// Usage: raw2dsk --format FORMAT input.img output.dsk
#include "DskImage.h"
#include "TosFs.h"
#include <cstdio>
#include <cstring>
#include <string>

namespace {

bool formatGeometry(const std::string& format, int& tracks, int& sides)
{
	if (format == "amstrad-cpc") { tracks = 40; sides = 1; return true; }
	TosFs::Preset preset;
	if (format == "tos-timex") preset = TosFs::Preset::TosTimex;
	else if (format == "tos-timex-640") preset = TosFs::Preset::TosTimex640;
	else if (format == "tos-timex-320") preset = TosFs::Preset::TosTimex320;
	else if (format == "tos-timex-320-alt") preset = TosFs::Preset::TosTimex320Alt;
	else return false;
	TosFs::presetGeometry(preset, tracks, sides);
	return true;
}

} // namespace

int main(int argc, char** argv)
{
	std::string format, input, output;
	for (int i = 1; i < argc; i++) {
		if (0 == strcmp(argv[i], "--format") && i + 1 < argc) { format = argv[++i]; continue; }
		if (input.empty()) input = argv[i];
		else if (output.empty()) output = argv[i];
	}
	if (format.empty() || input.empty() || output.empty()) {
		fprintf(stderr,
			"Usage: raw2dsk --format {amstrad-cpc,tos-timex,tos-timex-640,tos-timex-320,tos-timex-320-alt} input.img output.dsk\n");
		return 1;
	}
	int tracks, sides;
	if (!formatGeometry(format, tracks, sides)) {
		fprintf(stderr, "raw2dsk: unknown --format '%s'\n", format.c_str());
		return 1;
	}
	DskImage img;
	if (!img.loadRaw(input, tracks, sides, 16, 256)) {
		fprintf(stderr, "raw2dsk: failed to load %s as raw %s (%d tracks, %d sides, 16x256)\n",
			input.c_str(), format.c_str(), tracks, sides);
		return 1;
	}
	if (!img.exportAsContainer(output)) {
		fprintf(stderr, "raw2dsk: failed to write %s\n", output.c_str());
		return 1;
	}
	printf("raw2dsk: wrote %s (%s, %d tracks, %d sides)\n", output.c_str(), format.c_str(), tracks, sides);
	return 0;
}
