#!/usr/bin/env python3
"""Experiment / research tool: reverse-engineer and extract games from a
real Dandanator Mini flash dump (512KB = 32 x 16KB banks).

Not part of the emulator -- this is a standalone analysis script. Its
record layout (GameHeader / GameBlock / GAME_STRUCT_SIZE=131) is ported
from the official romset generator's own serializer, not guessed from
raw bytes: github.com/teiram/dandanator-mini,
handlers/dandanatormini/v6/{GameHeaderV6Serializer,GameMapperV6,
V6Constants}.java and DandanatorMiniConstants.java. That source is the
authority for the format; this script mirrors its deserialize() logic
field-for-field in Python so the decoded output can be trusted.

Confirmed empirically against games/dandanator_01.rom (a real hardware
flash dump): bank 0's second half is a flat array of 131-byte
GAME_STRUCT records (one per menu entry), starting around offset
0x0E01. Each record is, in order:
  - GameHeader (31 bytes): a frozen Z80 register snapshot (I, HL', DE',
    BC', AF', HL, DE, BC, IY, IX, IFF, R, AF, SP, IM, border, saved
    stack word, port 0x7FFD, port 0x1FFD) -- i.e. this is a "freeze and
    resume" snapshot format, not a flat ROM image.
  - name (33 bytes, null-terminated, zero-padded)
  - hardwareMode, isCompressed, gameType, screenHold, activeRom (1 byte each)
  - launchCode (18 bytes): a small relocatable Z80 stub, presumably the
    code that pages in the right flash slot(s) and restores the
    snapshot above.
  - gameChunk address + length (2+2 bytes, little-endian)
  - 8x GameBlock (5 bytes each: initSlot, start address LE, size LE) --
    up to 8 RAM chunks, each sourced from its own flash slot/bank,
    describing how the game's RAM image is reassembled piece by piece.

Usage:
    python3 tools/dandanator_extract.py games/dandanator_01.rom [outdir]
"""
from __future__ import annotations

import os
import struct
import sys
from dataclasses import dataclass, field
from typing import BinaryIO, List, Optional

BANK_SIZE = 16384
NUM_BANKS = 32
GAME_STRUCT_SIZE = 131          # V6Constants.GAME_STRUCT_SIZE
GAME_STRUCT_OFFSET = 3585       # V6Constants.GAME_STRUCT_OFFSET (within bank 0)
SNA_HEADER_SIZE = 31            # V6Constants.SNA_HEADER_SIZE
GAME_LAUNCH_SIZE = 18           # V6Constants.GAME_LAUNCH_SIZE
GAMENAME_SIZE = 33              # DandanatorMiniConstants.GAMENAME_SIZE
FILLER_BYTE = 0xFF              # DandanatorMiniConstants.FILLER_BYTE
NUM_BLOCKS = 8

GAME_TYPES = {
    0: "ROM", 1: "RAM16", 2: "RAM48", 3: "RAM128",
    4: "RAM48_MLD", 5: "RAM128_MLD", 6: "DAN_SNAP", 7: "DAN_SNAP128",
}


def _read_u8(f: BinaryIO) -> int:
    return f.read(1)[0]


def _read_u16le(f: BinaryIO) -> int:
    return struct.unpack("<H", f.read(2))[0]


@dataclass
class GameHeader:
    """Mirrors GameHeaderV6Serializer.deserialize() field-for-field."""
    i_register: int
    hl_alt: int
    de_alt: int
    bc_alt: int
    af_alt: int
    hl: int
    de: int
    bc: int
    iy: int
    ix: int
    interrupt_enable: int
    r_register: int
    af: int
    sp: int
    interrupt_mode: int
    border_color: int
    saved_stack_data: int
    port_7ffd: int
    port_1ffd: int

    @classmethod
    def deserialize(cls, f: BinaryIO) -> "GameHeader":
        return cls(
            i_register=_read_u8(f),
            hl_alt=_read_u16le(f),
            de_alt=_read_u16le(f),
            bc_alt=_read_u16le(f),
            af_alt=_read_u16le(f),
            hl=_read_u16le(f),
            de=_read_u16le(f),
            bc=_read_u16le(f),
            iy=_read_u16le(f),
            ix=_read_u16le(f),
            interrupt_enable=_read_u8(f),
            r_register=_read_u8(f),
            af=_read_u16le(f),
            sp=_read_u16le(f),
            interrupt_mode=_read_u8(f),
            border_color=_read_u8(f),
            saved_stack_data=_read_u16le(f),
            port_7ffd=_read_u8(f),
            port_1ffd=_read_u8(f),
        )

    def summary(self) -> str:
        return (
            f"I={self.i_register:02x} R={self.r_register:02x} "
            f"AF={self.af:04x} HL={self.hl:04x} DE={self.de:04x} BC={self.bc:04x} "
            f"IX={self.ix:04x} IY={self.iy:04x} SP={self.sp:04x} "
            f"AF'={self.af_alt:04x} HL'={self.hl_alt:04x} DE'={self.de_alt:04x} BC'={self.bc_alt:04x} "
            f"IM={self.interrupt_mode} IFF={self.interrupt_enable} border={self.border_color} "
            f"7FFD={self.port_7ffd:02x} 1FFD={self.port_1ffd:02x} stack_word={self.saved_stack_data:04x}"
        )


@dataclass
class GameBlock:
    """Mirrors the per-block read loop in GameMapperV6.fromRomSet()."""
    init_slot: int
    start: int
    size: int

    @classmethod
    def deserialize(cls, f: BinaryIO) -> "GameBlock":
        return cls(init_slot=_read_u8(f), start=_read_u16le(f), size=_read_u16le(f))

    @property
    def is_valid(self) -> bool:
        return self.init_slot < FILLER_BYTE

    def summary(self) -> str:
        return f"slot={self.init_slot:3d} start=0x{self.start:04x} size={self.size}"


@dataclass
class GameRecord:
    """One 131-byte GAME_STRUCT entry -- mirrors GameMapperV6.fromRomSet()."""
    offset: int
    header: GameHeader
    name: str
    hardware_mode: int
    is_compressed: bool
    game_type: int
    screen_hold: bool
    active_rom: int
    launch_code: bytes
    chunk_address: int
    chunk_length: int
    blocks: List[GameBlock] = field(default_factory=list)

    @classmethod
    def deserialize(cls, f: BinaryIO, offset: int) -> "GameRecord":
        header = GameHeader.deserialize(f)
        raw_name = f.read(GAMENAME_SIZE)
        name = raw_name.split(b"\x00", 1)[0].decode("ascii", errors="replace")
        hardware_mode = _read_u8(f)
        is_compressed = _read_u8(f) != 0
        game_type = _read_u8(f)
        screen_hold = _read_u8(f) != 0
        active_rom = _read_u8(f)
        launch_code = f.read(GAME_LAUNCH_SIZE)
        chunk_address = _read_u16le(f)
        chunk_length = _read_u16le(f)
        blocks = [GameBlock.deserialize(f) for _ in range(NUM_BLOCKS)]
        return cls(
            offset=offset, header=header, name=name, hardware_mode=hardware_mode,
            is_compressed=is_compressed, game_type=game_type, screen_hold=screen_hold,
            active_rom=active_rom, launch_code=launch_code, chunk_address=chunk_address,
            chunk_length=chunk_length, blocks=blocks,
        )

    @property
    def valid_blocks(self) -> List[GameBlock]:
        return [b for b in self.blocks if b.is_valid]

    @property
    def is_real_entry(self) -> bool:
        # An all-0xFF name byte or empty name means "unused slot" in the
        # flat 131-byte array past the last real game.
        return bool(self.name) and self.name[0] != "\xff"


class DandanatorRomSet:
    """Parses a 512KB flash dump into its 32 banks and bank 0's game directory."""

    def __init__(self, data: bytes):
        if len(data) != NUM_BANKS * BANK_SIZE:
            raise ValueError(f"expected {NUM_BANKS*BANK_SIZE} bytes, got {len(data)}")
        self.data = data

    @classmethod
    def from_file(cls, path: str) -> "DandanatorRomSet":
        with open(path, "rb") as f:
            return cls(f.read())

    def bank(self, index: int) -> bytes:
        return self.data[index * BANK_SIZE : (index + 1) * BANK_SIZE]

    def dump_banks(self, outdir: str) -> None:
        os.makedirs(outdir, exist_ok=True)
        for i in range(NUM_BANKS):
            with open(os.path.join(outdir, f"bank{i:02d}.bin"), "wb") as f:
                f.write(self.bank(i))

    def iter_game_records(self):
        """Walks bank 0's GAME_STRUCT array from GAME_STRUCT_OFFSET until
        records stop looking like real entries (flash past the last used
        slot is 0xFF-filled, so the name read decodes as empty/garbage)."""
        bank0 = self.bank(0)
        offset = GAME_STRUCT_OFFSET
        import io
        while offset + GAME_STRUCT_SIZE <= len(bank0):
            chunk = bank0[offset : offset + GAME_STRUCT_SIZE]
            if chunk[0] == FILLER_BYTE:
                break
            record = GameRecord.deserialize(io.BytesIO(chunk), offset)
            if not record.is_real_entry:
                break
            yield record
            offset += GAME_STRUCT_SIZE


def write_report(romset: DandanatorRomSet, records: List[GameRecord], outdir: str) -> str:
    report_path = os.path.join(outdir, "directory_report.txt")
    with open(report_path, "w") as rep:
        rep.write(f"Dandanator game directory ({len(records)} entries)\n")
        rep.write(f"GAME_STRUCT_SIZE={GAME_STRUCT_SIZE}  GAME_STRUCT_OFFSET=0x{GAME_STRUCT_OFFSET:04x}\n\n")
        for rec in records:
            rep.write(f"=== {rec.name!r} @ bank0 offset 0x{rec.offset:04x} ===\n")
            rep.write(f"  type={GAME_TYPES.get(rec.game_type, rec.game_type)} "
                      f"compressed={rec.is_compressed} screenHold={rec.screen_hold} "
                      f"hwMode={rec.hardware_mode} activeRom={rec.active_rom}\n")
            rep.write(f"  header: {rec.header.summary()}\n")
            rep.write(f"  launchCode ({GAME_LAUNCH_SIZE}B): {rec.launch_code.hex()}\n")
            rep.write(f"  gameChunk: address=0x{rec.chunk_address:04x} length={rec.chunk_length}\n")
            for i, blk in enumerate(rec.blocks):
                marker = "" if blk.is_valid else "  (unused)"
                rep.write(f"  block[{i}]: {blk.summary()}{marker}\n")
            rep.write("\n")
    return report_path


def extract_game_payloads(romset: DandanatorRomSet, records: List[GameRecord], outdir: str) -> None:
    """For each game, concatenates its valid GameBlocks' raw flash-slot
    bytes into a single file named after the game -- a first-order
    'unpacked' view, even though block payloads may still be compressed
    (is_compressed) and block.start/size describe the RAM placement,
    not necessarily a ready-to-run flat image."""
    games_dir = os.path.join(outdir, "games")
    os.makedirs(games_dir, exist_ok=True)
    for rec in records:
        safe_name = "".join(c if c.isalnum() or c in " .-_" else "_" for c in rec.name).strip() or "unnamed"
        blocks = rec.valid_blocks
        if not blocks:
            continue
        out_path = os.path.join(games_dir, f"{safe_name}.blocks.bin")
        with open(out_path, "wb") as out:
            for blk in blocks:
                if blk.init_slot < NUM_BANKS:
                    out.write(romset.bank(blk.init_slot))
                else:
                    out.write(b"\x00" * BANK_SIZE)  # slot refers to something outside this flash image


def main() -> None:
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    rom_path = sys.argv[1]
    out_dir = sys.argv[2] if len(sys.argv) > 2 else os.path.splitext(rom_path)[0] + "_extracted"

    romset = DandanatorRomSet.from_file(rom_path)
    romset.dump_banks(out_dir)
    print(f"Wrote {NUM_BANKS} bank files ({BANK_SIZE} bytes each) to {out_dir}/")

    records = list(romset.iter_game_records())
    print(f"\nParsed {len(records)} directory entries from bank 0:")
    for rec in records:
        blocks_desc = ", ".join(f"slot{b.init_slot}" for b in rec.valid_blocks) or "(no blocks)"
        print(f"  0x{rec.offset:04x}  {rec.name:<24} type={GAME_TYPES.get(rec.game_type, rec.game_type):<10} blocks=[{blocks_desc}]")

    report_path = write_report(romset, records, out_dir)
    print(f"\nFull field-decoded report written to {report_path}")

    extract_game_payloads(romset, records, out_dir)
    print(f"Per-game block payloads written to {out_dir}/games/")


if __name__ == "__main__":
    main()
