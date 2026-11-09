#include "mfm_track_encoder.h"
#include "../DskImage.h"
#include <algorithm>
#include <cstdint>

namespace {

// Standard floppy ID/data-field CRC: CRC-CCITT (poly 0x1021), seeded
// 0xFFFF and accumulated over the 3 sync bytes + mark byte + field content --
// the universal IBM/WD convention every real FDC (including the real
// WD1772) and MAME's own wd_fdc.cpp both expect.
uint16_t crc16Update(uint16_t crc, uint8_t byte)
{
	crc ^= (uint16_t)byte << 8;
	for (int i = 0; i < 8; i++)
		crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
	return crc;
}

// Appends one MFM-encoded byte's 16 raw (clock+data interleaved) bits.
// Standard rule: a clock bit is 1 only when BOTH its neighbouring data
// bits are 0 (self-clocking, run-length-limited encoding) -- lastDataBit
// carries the previous byte's final data bit across the boundary so the
// very first clock bit of this byte is still computed correctly.
void emitByteMfm(std::vector<bool> &bits, uint8_t byte, bool &lastDataBit)
{
	for (int i = 7; i >= 0; i--) {
		bool dataBit = (byte >> i) & 1;
		bool clockBit = !lastDataBit && !dataBit;
		bits.push_back(clockBit);
		bits.push_back(dataBit);
		lastDataBit = dataBit;
	}
}

// Sync marks (0xA1/0xC2) use a deliberately-illegal clock pattern no
// real data byte can produce under the normal MFM rule -- that's what
// makes them unambiguously recognizable as sync marks rather than
// coincidental data. Real WD/IBM-standard raw 16-bit patterns: 0xA1 ->
// 0x4489, 0xC2 -> 0x5224 -- the exact values wd_fdc.cpp's live_run()
// itself searches for (confirmed by reading its source, see class
// header comment). Emitted as a fixed 16-bit pattern rather than via
// emitByteMfm()'s normal clock rule; lastDataBit is still updated
// afterward to the sync byte's own low bit so later continuity isn't
// broken.
void emitSyncRaw(std::vector<bool> &bits, uint16_t rawPattern, uint8_t impliedByte, bool &lastDataBit)
{
	for (int i = 15; i >= 0; i--)
		bits.push_back((rawPattern >> i) & 1);
	lastDataBit = impliedByte & 1;
}

void emitA1(std::vector<bool> &bits, bool &lastDataBit) { emitSyncRaw(bits, 0x4489, 0xA1, lastDataBit); }

} // namespace

MfmTrack encodeTrackMFM(DskImage &img, int track, int side, double bitCellSeconds)
{
	MfmTrack out;
	int count = img.sectorCount(track, side);
	if (count <= 0) return out; // unformatted/absent track, valid stays false

	std::vector<bool> bits;
	bool lastDataBit = false;

	auto gap = [&](int n, uint8_t fill) {
		for (int i = 0; i < n; i++) emitByteMfm(bits, fill, lastDataBit);
	};
	auto sync00 = [&](int n) {
		for (int i = 0; i < n; i++) emitByteMfm(bits, 0x00, lastDataBit);
	};

	gap(32, 0x4E); // GAP1 (post-index)

	int dataBytes = 0;
	for (int idx = 0; idx < count; idx++) {
		DskImage::SectorInfo si;
		if (!img.sectorInfo(track, side, idx, si)) continue;
		int len = si.length;
		dataBytes += len;
		uint8_t *data = img.sectorData(track, side, idx, len);

		// ---- ID field ----
		sync00(12);
		// Seed 0xFFFF, feed all 3 sync bytes as plain byte-level CRC-16
		// (poly 0x1021) -- confirmed correct directly against wd_fdc.cpp's
		// OWN source, not assumed from general floppy-format folklore
		// (which, tried first, pointed at seeding 0xCCCC and skipping the
		// sync bytes -- WRONG, verified computationally: crc16Update
		// chained for 3x 0xA1 from 0xFFFF does NOT equal 0xCCCC).
		// wd_fdc.cpp's live_run() hardcodes `cur_live.crc = 0x443b` the
		// instant it detects the FIRST sync mark (SEARCH_ADDRESS_MARK_
		// HEADER state) -- found live, 2026-11-08, that 0x443b is EXACTLY
		// crc16Update(0xFFFF, 0xA1): one byte-level 0xA1 from a 0xFFFF
		// seed. The other two sync bytes' decoded value then runs through
		// the SAME ongoing per-bit accumulation as any other byte, which
		// is mathematically identical to this byte-level chain.
		uint16_t idCrc = 0xFFFF;
		for (int i = 0; i < 3; i++) { emitA1(bits, lastDataBit); idCrc = crc16Update(idCrc, 0xA1); }
		uint8_t idam = 0xFE;
		emitByteMfm(bits, idam, lastDataBit); idCrc = crc16Update(idCrc, idam);
		uint8_t idField[4] = { si.c, si.h, si.r, si.n };
		for (uint8_t b : idField) { emitByteMfm(bits, b, lastDataBit); idCrc = crc16Update(idCrc, b); }
		emitByteMfm(bits, (uint8_t)(idCrc >> 8), lastDataBit);
		emitByteMfm(bits, (uint8_t)(idCrc & 0xFF), lastDataBit);

		gap(22, 0x4E); // GAP2
		if (!si.hasData) continue; // MAME dsk_format: ST1 bit 0x04 means no data field

		// ---- Data field ----
		sync00(12);
		uint16_t dataCrc = 0xFFFF; // see the ID field's own comment above
		for (int i = 0; i < 3; i++) { emitA1(bits, lastDataBit); dataCrc = crc16Update(dataCrc, 0xA1); }
		uint8_t dam = (si.st2 & 0x40) ? 0xF8 : 0xFB;
		emitByteMfm(bits, dam, lastDataBit); dataCrc = crc16Update(dataCrc, dam);
		for (int i = 0; i < len; i++) { uint8_t b = data ? data[i] : 0; emitByteMfm(bits, b, lastDataBit); dataCrc = crc16Update(dataCrc, b); }
		if ((si.st1 | si.st2) & 0x20) dataCrc ^= 0xFFFF; // preserve DSK's bad-data-CRC status
		emitByteMfm(bits, (uint8_t)(dataCrc >> 8), lastDataBit);
		emitByteMfm(bits, (uint8_t)(dataCrc & 0xFF), lastDataBit);

		gap(img.gap3Length(track, side), 0x4E); // track header's GAP3
	}

	// GAP4b: fill whatever's left of a plausible track length -- not
	// measured from real media (none available), just enough slack that
	// the PLL has clean, repetitive flux to free-run on before the next
	// revolution's GAP1.
	// MAME's dsk_format pads ordinary DD tracks to at least 100000
	// 2-us cells (300 rpm), while allowing larger layouts to keep their
	// declared sectors/gaps. Keep this track timing when a layout is short.
	int minimumCells = 149 * 16 + (count * 62 + dataBytes) * 16;
	int minimumBytes = std::max(0, (100000 - minimumCells) / 16);
	gap(minimumBytes, 0x4E);

	if (bits.size() < 100000) {
		gap((100000 - (int)bits.size() + 15) / 16, 0x4E);
	}
	out.transitions.reserve(bits.size() / 2);
	for (size_t i = 0; i < bits.size(); i++)
		if (bits[i]) out.transitions.push_back(attotime::from_double((double)i * bitCellSeconds));
	out.revolutionTime = attotime::from_double((double)bits.size() * bitCellSeconds);
	out.valid = true;
	return out;
}
