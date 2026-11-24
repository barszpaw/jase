#!/usr/bin/env python3
"""Builds a standard 48K .SNA snapshot for ONE game extracted from a real
Dandanator Mini flash dump -- a "known good reference" built directly from
the decoded GameHeader + decompressed GameBlocks (see dandanator_extract.py
and zx7.py), bypassing the real PIC/Dandanator command protocol entirely.

Research/debugging tool, not production code: lets JASE's own real .SNA
loader (FileMgr::loadSNA) run the game's captured state directly, so its
behaviour can be compared against what happens going through the normal
Dandanator menu+protocol path (which is known to crash for this romset --
see project memory). If the .SNA plays correctly, that isolates the bug
to the protocol/loading path, not the captured game state itself.

Block-to-RAM-page mapping is NOT stored explicitly per block -- inferred
from standard 48K layout order (block[0]->0x4000, block[1]->0x8000,
block[2]->0xC000), confirmed by each decompressing to a clean 16KB (or, for
whichever block is the designated GAME_CHUNK_SLOT, 16KB minus the chunk
size). The missing bytes are the game's own separately-compressed
'gameChunk' field: DandanatorMiniRamGameCompressor.compressSlot() builds
the chunk-slot's compressed data from
`Arrays.copyOfRange(data, 0, SLOT_SIZE - GAME_CHUNK_SIZE)` -- i.e. the
LAST GAME_CHUNK_SIZE (256) bytes of that page are cut before compression
and stored separately -- so gameChunk goes at the END of the page, found
empirically to decompress at its own literal absolute file offset (NOT
the slot*SLOT_SIZE+start formula used for ordinary blocks -- confirmed:
SlotZeroBase.uncompress()/copy() skip straight to the given offset in the
whole-file stream, no slot arithmetic involved, unlike GameBlock's own
populateGameSlots() which applies the slot formula itself before calling
the same two-arg skip-to-offset primitive).

Usage:
    python3 tools/dandanator_to_sna.py games/dandanator_01.rom "Bomb Jack" out.sna
"""
import sys
import struct

from dandanator_extract import DandanatorRomSet, GameRecord
from zx7 import decompress, Zx7Error

SLOT_SIZE = 16384
GAME_CHUNK_SLOT = 2  # DandanatorMiniConstants.GAME_CHUNK_SLOT -- 0-indexed block that's trimmed


def decompress_block(data: bytes, slot: int, start: int, size: int) -> bytes:
    offset = slot * SLOT_SIZE + start
    chunk = data[offset : offset + size + 512]  # pad -- zx7 stops itself at its own end marker
    return decompress(chunk)


def decompress_game_chunk(data: bytes, address: int, length: int) -> bytes:
    # Literal absolute file offset -- NOT the slot*SLOT_SIZE+start formula
    # ordinary GameBlocks use (see module docstring).
    chunk = data[address : address + length + 64]
    return decompress(chunk)


def build_ram(romset: DandanatorRomSet, rec: GameRecord) -> bytes:
    blocks = rec.valid_blocks
    if len(blocks) != 3:
        raise ValueError(f"expected 3 valid blocks for a 48K game, got {len(blocks)}: {blocks}")

    pages = []
    for i, blk in enumerate(blocks):
        decoded = decompress_block(romset.data, blk.init_slot, blk.start, blk.size)
        if i == GAME_CHUNK_SLOT and len(decoded) < SLOT_SIZE:
            gap = SLOT_SIZE - len(decoded)
            if rec.chunk_length > 0:
                chunk_data = decompress_game_chunk(romset.data, rec.chunk_address, rec.chunk_length)
                if len(chunk_data) != gap:
                    raise ValueError(
                        f"gameChunk decompressed to {len(chunk_data)} bytes, "
                        f"needed exactly {gap} to complete block[{i}]'s page")
                decoded = decoded + chunk_data
                print(f"  block[{i}]: completed {gap}-byte gap with decompressed gameChunk "
                      f"(address=0x{rec.chunk_address:04x} length={rec.chunk_length})")
            else:
                print(f"  NOTE: block[{i}] is {gap} bytes short and gameChunk.length is 0 -- zero-filling")
                decoded = decoded + b"\x00" * gap
        if len(decoded) != SLOT_SIZE:
            raise ValueError(f"block[{i}] decompressed to {len(decoded)} bytes, expected {SLOT_SIZE}")
        pages.append(decoded)
    return b"".join(pages)  # 0x4000-0x7FFF, 0x8000-0xBFFF, 0xC000-0xFFFF in order


def build_sna(romset: DandanatorRomSet, rec: GameRecord) -> bytes:
    # GameHeaderV6Serializer's field order is byte-for-byte the real 27-byte
    # .SNA header format (confirmed against FileMgr::loadSNA's own read
    # order) -- the raw bytes can be used verbatim, no re-encoding needed.
    header_bytes = romset.bank(0)[rec.offset : rec.offset + 27]
    ram = build_ram(romset, rec)
    sna = header_bytes + ram
    assert len(sna) == 49179, f"built {len(sna)} bytes, expected 49179 (standard 48K .SNA size)"
    return sna


def main() -> None:
    if len(sys.argv) < 4:
        print(__doc__)
        sys.exit(1)
    rom_path, game_name, out_path = sys.argv[1:4]

    romset = DandanatorRomSet.from_file(rom_path)
    rec = next((r for r in romset.iter_game_records() if game_name.lower() in r.name.lower()), None)
    if rec is None:
        names = [r.name for r in romset.iter_game_records()]
        sys.exit(f"no game matching {game_name!r} found. Available: {names}")

    print(f"Building .SNA for {rec.name!r} (bank0 offset 0x{rec.offset:04x})")
    print(f"  header: {rec.header.summary()}")
    for i, blk in enumerate(rec.valid_blocks):
        print(f"  block[{i}]: {blk.summary()}")

    sna = build_sna(romset, rec)
    with open(out_path, "wb") as f:
        f.write(sna)
    print(f"Wrote {len(sna)} bytes to {out_path}")
    print(f"PC will be recovered by the .SNA loader's standard 'pop PC off (SP)' "
          f"trick -- SP=0x{rec.header.sp:04x} (must land inside 0x4000-0xFFFF to work).")


if __name__ == "__main__":
    main()
