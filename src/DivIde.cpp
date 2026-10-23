#include "DivIde.h"
#include <cstring>

DivIde::DivIde()
{
	memset(rom_.data, 0xFF, MemoryMap::PAGE_SIZE);
	rom_.isRom = true;
	for (int i = 0; i < 4; i++) {
		memset(ram_[i].data, 0x00, MemoryMap::PAGE_SIZE);
		ram_[i].isRom = false;
	}
}

DivIde::~DivIde()
{
	if (disk_) fclose(disk_);
}

bool DivIde::loadRom(const char* path)
{
	FILE* pf = fopen(path, "rb");
	if (pf == NULL) {
		fprintf(stderr, "DivIde::loadRom: cannot open %s\n", path);
		return false;
	}
	uint8_t buf[MemoryMap::PAGE_SIZE];
	size_t n = fread(buf, 1, sizeof(buf), pf);
	fclose(pf);
	if (n != sizeof(buf)) {
		fprintf(stderr, "DivIde::loadRom: %s is %zu bytes, expected exactly %d\n",
		        path, n, MemoryMap::PAGE_SIZE);
		return false;
	}
	memcpy(rom_.data, buf, sizeof(buf));
	printf("DivIde::loadRom: loaded esxDOS (IDE) ROM from %s\n", path);
	return true;
}

// Sniffs the "RS-IDE" + 0x1A signature (the standard .hdf format fusex/
// ZEsarUX/pico-spec all read and write -- confirmed against libspectrum's
// ide.c) to find where sector data actually starts; falls back to
// treating the whole file as a headerless raw sector image (byte 0 =
// sector 0) if that signature isn't present. Either of Pawel's own
// esxdos.hdf / esxdos.raw works without having to say which is which.
bool DivIde::insertDisk(const char* path)
{
	if (disk_) { fclose(disk_); disk_ = nullptr; }

	disk_ = fopen(path, "r+b");
	if (!disk_) disk_ = fopen(path, "rb"); // read-only media still usable
	if (!disk_) {
		fprintf(stderr, "DivIde::insertDisk: cannot open %s\n", path);
		return false;
	}

	uint8_t header[11];
	size_t n = fread(header, 1, sizeof(header), disk_);
	if (n == sizeof(header) && memcmp(header, "RS-IDE", 6) == 0 && header[6] == 0x1A) {
		diskDataOffset_ = header[9] | (header[10] << 8);
		printf("DivIde::insertDisk: %s is a valid .hdf, data at offset %ld\n", path, diskDataOffset_);
	} else {
		diskDataOffset_ = 0;
		printf("DivIde::insertDisk: %s has no RS-IDE signature, treating as a raw sector image\n", path);
	}

	fseek(disk_, 0, SEEK_END);
	long fileSize = ftell(disk_);
	diskSectors_ = (uint32_t)((fileSize - diskDataOffset_) / 512);
	printf("DivIde::insertDisk: %u sectors (%lu MB) available\n",
	       diskSectors_, (unsigned long)((uint64_t)diskSectors_ * 512 / (1024 * 1024)));
	return true;
}

uint32_t DivIde::lba() const
{
	// LBA28: SECTOR=bits0-7, CYLINDER_LOW=bits8-15, CYLINDER_HIGH=bits16-23,
	// DRIVE/HEAD bits0-3=bits24-27. esxDOS only ever uses LBA (bit6 of
	// DRIVE/HEAD set) -- CHS addressing is not modelled, see class comment.
	return (uint32_t)sectorReg_
	     | ((uint32_t)cylLow_ << 8)
	     | ((uint32_t)cylHigh_ << 16)
	     | (((uint32_t)driveHead_ & 0x0F) << 24);
}

// Status register bits actually used here: RDY (0x40, "drive" always
// ready once constructed), DRQ (0x08, data buffer has a byte ready to
// transfer either direction), ERR (0x01, unrecognised command).
void DivIde::executeCommand(uint8_t cmd)
{
	switch (cmd) {
		case 0x20: case 0x21: // READ SECTOR (with/without retry -- same here)
			memset(dataBuf_, 0, sizeof(dataBuf_));
			if (disk_) {
				fseek(disk_, diskDataOffset_ + (long)lba() * 512, SEEK_SET);
				size_t got = fread(dataBuf_, 1, sizeof(dataBuf_), disk_);
				(void)got; // short/missing read past end-of-disk just reads zeros, not an error worth surfacing here
			}
			dataPos_ = 0;
			writePending_ = false;
			status_ = 0x48; // RDY|DRQ
			break;

		case 0x30: case 0x31: // WRITE SECTOR (with/without retry)
			dataPos_ = 0;
			writePending_ = true;
			status_ = 0x48; // RDY|DRQ -- ready to receive bytes
			break;

		case 0x91: // INITIALIZE DRIVE PARAMETERS -- esxDOS doesn't rely on
			// the result, real hardware recommends against honouring it
			// (see fusex/ZEsarUX comments); we just acknowledge.
			status_ = 0x40;
			break;

		case 0xEC: { // IDENTIFY DRIVE -- minimal canned structure, just
			// enough for esxDOS to accept the "drive" and read its LBA
			// capacity (word49 bit9=LBA supported, words60-61=total
			// sectors, the only fields esxDOS's own driver reads).
			memset(dataBuf_, 0, sizeof(dataBuf_));
			auto setWord = [&](int w, uint16_t v) {
				dataBuf_[w * 2] = (uint8_t)v;
				dataBuf_[w * 2 + 1] = (uint8_t)(v >> 8);
			};
			setWord(49, 0x0200); // bit9: LBA supported
			setWord(60, (uint16_t)diskSectors_);
			setWord(61, (uint16_t)(diskSectors_ >> 16));
			dataPos_ = 0;
			writePending_ = false;
			status_ = 0x48;
			break;
		}

		default:
			status_ = 0x41; // RDY|ERR
			break;
	}
}

bool DivIde::in(uint16_t port, uint8_t& value)
{
	if (!enabled_) return false;
	switch (port & 0xFF) {
		case 0xA3: // DATA
			if (dataPos_ < sizeof(dataBuf_)) {
				value = dataBuf_[dataPos_++];
				if (dataPos_ == sizeof(dataBuf_)) status_ = 0x40; // drained
			} else {
				value = 0xFF;
			}
			return true;
		case 0xA7: value = errorFeature_; return true; // ERROR
		case 0xAB: value = sectorCount_;  return true;
		case 0xAF: value = sectorReg_;    return true;
		case 0xB3: value = cylLow_;       return true;
		case 0xB7: value = cylHigh_;      return true;
		case 0xBB: value = driveHead_;    return true;
		case 0xBF: value = status_;       return true; // STATUS
		default: return false;
	}
}

bool DivIde::out(uint16_t port, uint8_t value)
{
	if (!enabled_) return false;
	switch (port & 0xFF) {
		case 0xE3: // CONTROL -- see class comment for CONMEM/MAPRAM/bank
			conmem_ = (value & 0x80) != 0;
			ramBank_ = value & 0x03;
			return true;
		case 0xA3: // DATA, only meaningful mid-way through a WRITE SECTOR
			if (writePending_ && dataPos_ < sizeof(dataBuf_)) {
				dataBuf_[dataPos_++] = value;
				if (dataPos_ == sizeof(dataBuf_)) {
					if (disk_) {
						fseek(disk_, diskDataOffset_ + (long)lba() * 512, SEEK_SET);
						fwrite(dataBuf_, 1, sizeof(dataBuf_), disk_);
						fflush(disk_);
					}
					writePending_ = false;
					status_ = 0x40;
				}
			}
			return true;
		case 0xA7: errorFeature_ = value; return true; // FEATURE
		case 0xAB: sectorCount_ = value;  return true;
		case 0xAF: sectorReg_ = value;    return true;
		case 0xB3: cylLow_ = value;       return true;
		case 0xB7: cylHigh_ = value;      return true;
		case 0xBB: driveHead_ = value;    return true;
		case 0xBF: executeCommand(value); return true; // COMMAND
		default: return false;
	}
}

// PC-driven automap -- confirmed against fusex's z80_ops.c (divide_early/
// divide_late CHECK blocks): page in on the RST vectors, IM1 interrupt
// vector, NMI vector, and two 48K ROM tape entry points DivIDE
// intercepts for its own fast-load, plus the whole 0x3D00-0x3DFF range
// (an earlier, coarser check fusex applies before the main one); page
// out on 0x1FF8-0x1FFF. CONMEM (set via the control register) overrides
// all of this and forces paging in regardless of PC.
//
// The one-step-deferred timing (see trapAfter_/unmapAfter_ in DivIde.h)
// is the part that's easy to get wrong and silently look like it mostly
// works: apply whatever the PREVIOUS call decided FIRST, then decide
// what THIS pc wants for the call after that. That ordering is what
// makes a trap address's own opcode still come from the pre-trap
// mapping, matching pico-spec-drewpo's DivMMC::preOpcFetch()/
// postOpcFetch() split (same facts, no code copied, GPL).
void DivIde::onInstructionFetch(uint16_t pc, MemoryMap& memory, uint32_t tstates)
{
	(void)tstates;
	if (!enabled_) return;

	if (trapAfter_) { trapAfter_ = false; automapActive_ = true; }
	if (unmapAfter_) { unmapAfter_ = false; automapActive_ = false; }

	if ((pc & 0xFF00) == 0x3D00) {
		// Instant map, not deferred -- matches fusex's divide_early
		// CHECK, which runs (and takes effect) before the later,
		// deferred divide_late CHECK even gets a chance to.
		automapActive_ = true;
	} else if (!automapActive_ &&
	           (pc == 0x0000 || pc == 0x0008 || pc == 0x0038 ||
	            pc == 0x0066 || pc == 0x04C6 || pc == 0x0562)) {
		trapAfter_ = true;
	} else if (automapActive_ && (pc & 0xFFF8) == 0x1FF8) {
		unmapAfter_ = true;
	}

	// Deliberately doesn't restore pages 0/1 itself on page-out -- what
	// belongs there once ROMCS releases is a MACHINE-level question
	// (plain 48K: the one system ROM pair, always; 128K: whichever ROM/
	// RAM the 0x7FFD latch currently says, which esxDOS's own boot code
	// writes to directly and can change WHILE DivIDE is active) that
	// only MinZX can answer correctly -- see MinZX::reapplyMachinePaging(),
	// triggered off ROMCS's falling edge in the per-instruction loop
	// right after this runs. Confirmed this conflict is real, not
	// theoretical: esxDOS's own boot code writes port 0x7FFD (harmless
	// no-op on 48K, where that port doesn't exist; a real 128K ROM-bank
	// switch on 128K) as part of its generic startup housekeeping.
	bool shouldPage = conmem_ || automapActive_;
	if (shouldPage) {
		active_ = true;
		memory.mapPage(0, &rom_);
		memory.mapPage(1, &ram_[ramBank_]);
		memory.setRomcsAsserted(true);
	} else if (active_) {
		active_ = false;
		memory.setRomcsAsserted(false);
	}
}

std::string DivIde::debugCommand(const std::vector<std::string>& toks)
{
	if (toks.empty() || toks[0] != "DIVIDE")
		return std::string();

	char out[160];
	snprintf(out, sizeof(out), "OK enabled=%d active=%d conmem=%d rambank=%d lba=%u disk=%d sectors=%u",
	         enabled_ ? 1 : 0, active_ ? 1 : 0, conmem_ ? 1 : 0, ramBank_,
	         lba(), disk_ ? 1 : 0, diskSectors_);
	return out;
}
