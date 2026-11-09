// Synthesizes a standard IBM-MFM flux-transition stream for one track,
// built from DskImage's already-parsed sector table -- lets MAME's real,
// unmodified wd_fdc.cpp (BSD-3-Clause) decode it through its own
// bit-level live_run()/PLL engine exactly as it would a real disk, while
// the actual sector CONTENT still comes from the same CPCEMU DSK parser
// (DskImage) the rest of this project already trusts. Not a byte-for-
// byte real disk capture -- gap lengths/IAM/interleave are plausible
// IBM-standard defaults, not measured from real FDD3000 media (none
// available) -- but the sync marks, ID fields, CRC16, and data framing
// are built to the real, documented format so wd_fdc.cpp's own ID-search
// and CRC-check logic exercises the same code paths it would on a real
// disk, not a shortcut.
#pragma once
#include <cstdint>
#include <vector>
#include "mame_attotime.h"

class DskImage;

// One MFM-encoded track, as a sorted list of flux-transition times
// (0 .. revolutionTime, then wraps). get_next_transition() and friends
// in the floppy_image_device stub query this directly.
struct MfmTrack
{
	std::vector<attotime> transitions; // sorted, within [0, revolutionTime)
	attotime revolutionTime;
	bool valid = false; // false if the track doesn't exist in the image (unformatted)
};

// bitCellSeconds: duration of ONE RAW bit (half an MFM data-bit cell) --
// see mfm_track_encoder.cpp's own comment for why the exact value is a
// free parameter here (nothing depends on matching real rotational
// speed, only on internal self-consistency between encoder and PLL).
MfmTrack encodeTrackMFM(DskImage &img, int track, int side, double bitCellSeconds);
