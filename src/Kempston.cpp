#include "Kempston.h"
#include <SDL.h>
#include <cstdio>

bool Kempston::in(uint16_t port, uint8_t& value)
{
	if ((port & 0xFF) != 0x1F)
		return false;

	const Uint8* ks = SDL_GetKeyboardState(NULL);
	uint8_t result = 0;
	if (ks[SDL_SCANCODE_RIGHT]) result |= 0x01;
	if (ks[SDL_SCANCODE_LEFT])  result |= 0x02;
	if (ks[SDL_SCANCODE_DOWN])  result |= 0x04;
	if (ks[SDL_SCANCODE_UP])    result |= 0x08;
	if (ks[SDL_SCANCODE_LALT])  result |= 0x10;
	value = result;
	return true;
}

std::string Kempston::debugCommand(const std::vector<std::string>& toks)
{
	if (toks.empty() || toks[0] != "KEMPSTON")
		return std::string();

	uint8_t value = 0;
	in(0x1F, value);

	char out[128];
	snprintf(out, sizeof(out), "OK right=%d left=%d down=%d up=%d fire=%d",
	         (value & 0x01) ? 1 : 0, (value & 0x02) ? 1 : 0,
	         (value & 0x04) ? 1 : 0, (value & 0x08) ? 1 : 0,
	         (value & 0x10) ? 1 : 0);
	return out;
}
