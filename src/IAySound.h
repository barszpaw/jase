#pragma once
#include <cstdint>

// Minimal interface MinZX::ay_ needs regardless of whether it's pointing
// at a single-chip AyPsg or a two-chip TurboSound -- just enough to pull
// audio out once per frame (flushAudioFrame()). Everything else (port
// decode, debug command) is peripheral-specific and reached through the
// normal Peripheral list, not through this pointer.
class IAySound
{
public:
	virtual ~IAySound() {}
	virtual void generate(uint8_t* buffer, int count, int sampleRate) = 0;
};
