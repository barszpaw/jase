#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

// The ROM/RAM/banking "device" (see project memory for the architecture
// discussion that led here). Real Timex SCLD hardware pages the full 64K
// address space in eight 8K windows via port 0xF4 (one bit per window) --
// this mirrors that exactly, not a simplified scheme, since the point is
// running real Timex DOCK-paging software unmodified. Plain 48K machines
// just never touch the paging API: one mapHome() call per page at init,
// and the page table never changes again -- behavior-identical to the
// flat 64K array this replaced.
class MemoryMap
{
public:
	static const int PAGE_SIZE = 0x2000; // 8K
	static const int NUM_PAGES = 8;      // 8 x 8K covers the full 64K

	struct Bank {
		uint8_t data[PAGE_SIZE];
		bool isRom;
	};

	// numRomBanks/numRamBanks: pool sizes only -- this class holds no
	// policy about what layout a machine wants. The caller sets up the
	// initial page table afterward via mapHome()/mapPage() (see MinZX::
	// setupInitialPaging(), one case per machine type).
	void init(int numRomBanks, int numRamBanks)
	{
		romBanks_.resize(numRomBanks);
		ramBanks_.resize(numRamBanks);
		for (auto& b : romBanks_) { memset(b.data, 0x00, PAGE_SIZE); b.isRom = true; }
		for (auto& b : ramBanks_) { memset(b.data, 0x00, PAGE_SIZE); b.isRom = false; }
	}

	void destroy()
	{
		romBanks_.clear();
		ramBanks_.clear();
	}

	// The sequential HOME layout real SCLD port 0xF4 bit=0 falls back to:
	// page<2 -> ROM bank `page`, page>=2 -> RAM bank `page-2`. Used by 48K
	// and TC2048 (TC2048's DOCK paging is defined relative to exactly this
	// layout). NOT used by 128K, whose page2-5 are fixed to specific RAM
	// banks rather than sequential ones -- its setupInitialPaging() calls
	// mapPage() directly instead.
	void mapHome(int page)
	{
		pageTable_[page] = (page < 2) ? &romBanks_[page] : &ramBanks_[page - 2];
	}

	void mapPage(int page, Bank* bank) { pageTable_[page] = bank; }

	uint8_t peek8(uint16_t addr) const
	{
		return pageTable_[addr >> 13]->data[addr & (PAGE_SIZE - 1)];
	}
	void poke8(uint16_t addr, uint8_t value)
	{
		Bank* b = pageTable_[addr >> 13];
		if (!b->isRom) b->data[addr & (PAGE_SIZE - 1)] = value;
	}

	Bank& romBank(int i) { return romBanks_[i]; }
	Bank& ramBank(int i) { return ramBanks_[i]; }
	int ramBankCount() const { return (int)ramBanks_.size(); }

private:
	std::vector<Bank> romBanks_;
	std::vector<Bank> ramBanks_;
	Bank* pageTable_[NUM_PAGES] = {};
};
