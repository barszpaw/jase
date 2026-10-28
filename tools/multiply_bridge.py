#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# Multiply bridge -- companion process for JASE's Multiply peripheral
# (src/Multiply.cpp), the Arduino+microSD addon for the ZX Dandanator! Mini
# (github.com/mad3001/Multiply). See docs/Dandanator/dandanator_multiply_notes.md
# for the full hardware/protocol research this is built from.
#
# Same split as tools/sdi_bridge/sdi_bridge.py: JASE's C++ side (Multiply.cpp)
# is a dumb byte-pipe -- it doesn't know what CMD_ZX2SD_* commands MEAN, only
# how to shuttle bytes to/from here over TCP. All the real logic (SD
# directory browsing, file-type classification, screen-preview streaming)
# lives in THIS process, written in Python rather than C++ for the same
# reason sdi_bridge.py exists: file-format-heavy logic doesn't belong in the
# hot Z80-emulation path.
#
# Wire protocol: UNFRAMED raw bytes, both directions, on one TCP connection
# -- a direct reflection of the real hardware's own wire protocol: the
# Dandanator's command-52 channel delivers one byte at a time with no
# message delimiters, and the real Arduino firmware tracks command/payload
# boundaries itself (via getBuffer_0's null-terminated reads and
# getBuffer_N's fixed-length reads -- see
# docs/Dandanator/reference_src/libSerialCmds.ino). This bridge does the
# same: recv_exact()/recv_cstring() below, and the per-command payload
# shapes in handle_client()'s dispatch, were reverse-engineered directly
# from the real firmware's own command loop (Multiply_Arduino.ino) and
# directory-listing code (libFileList.ino/libSDFiles.ino) -- not guessed.
# Those sources aren't vendored in this repo (CC BY-SA, github.com/
# mad3001/Multiply) but were read in full during development; see the
# per-function comments below for what each one is based on.
#
# v3 scope: identification, hardware-type notification, directory
# navigation (CD_ROOT/CD/CD_IX/GETDIR), directory listing (LS_RELATIVE,
# single chapter only -- see MAX_FILES), opening a file by name or index
# (OFREAD/OFREAD_IX), screen-preview streaming (CMD_ZX2SD_SCR, plain
# .SCR files only), and now actual game loading: CMD_ZX2SD_SNA_48K/128K,
# CMD_ZX2SD_Z80_16K/48K/128K (versions 1/2/3, compressed or not), and
# CMD_ZX2SD_TAP. These three share the real firmware's wire format
# exactly (SendSNA/SendZ80 in libSNA.ino/libZ80.ino both normalize to
# the SAME block stream: block 9 = 64-byte SNA-style header, blocks 1-8
# = 16K RAM pages, block 10 = the last 512 bytes of block 1's page sent
# separately -- see sna_header()/page_for_block() below) -- but this
# bridge does NOT replicate the real firmware's incremental, state-
# carried-across-calls RLE decompression (uncompress() in
# libZ80uncompress.ino, needed there only because of the Arduino's tiny
# RAM). Instead, a Z80 file's compressed blocks are fully decompressed
# into memory the moment block 9 is requested (see parse_z80()), and
# every later block (1-8, 10) just slices the result -- identical bytes
# on the wire, far less state to get wrong in translation. SNA 128K's
# RAMBLOCKS remapping (which of the 8 RAM banks lands in which of up to
# 9 file slots, depending on which bank is currently paged into
# 0xC000) is a direct translation of SendSNA's own table -- less
# battle-tested than the 48K path, flagged where it matters.
#
# Deliberately NOT implemented yet: SCR-extracted-from-Z80/SNA/TAP
# (CMD_ZX2SD_SCR_FROM_Z80/SNA/SCRTAP), GETINFO's per-type extended
# data, multi-chapter pagination for huge directories, ROMSET content,
# and the PC-tunnel romset-upload commands. Each of those is still
# payload-framed correctly even though unimplemented (consuming the
# right number of bytes is NOT optional -- see "_FIXED_PAYLOAD" --
# getting this wrong desyncs every command after it for the rest of the
# connection, same risk the real firmware's own fixed dispatch table
# avoids by construction).
# -----------------------------------------------------------------------------

import argparse
import os
import socket
import struct

VERSION = "0.2"

# Commands the real Arduino firmware's main dispatch loop recognizes (see
# Multiply_Arduino.ino's loop() and incCmdsFileTypes.h). Only a subset is
# handled below -- see module docstring.
CMD_ZX2INO_REQ_ID      = 0x01
CMD_ZX2SD_SETZXTYPE    = 0x03
CMD_ZX2SD_OFREAD       = 0x08
CMD_ZX2SD_OFREAD_IX    = 0x09
CMD_ZX2SD_CD_ROOT      = 0x0A
CMD_ZX2SD_CD           = 0x0B
CMD_ZX2SD_CD_IX        = 0x0C
CMD_ZX2SD_GETDIR       = 0x0D
CMD_ZX2SD_LS_RELATIVE  = 0x0E
CMD_ZX2SD_LS_ABSOLUTE  = 0x0F
CMD_ZX2SD_SCR          = 0x10
CMD_ZX2SD_Z80_16K      = 0x11
CMD_ZX2SD_Z80_48K      = 0x12
CMD_ZX2SD_Z80_128K     = 0x13
CMD_ZX2SD_SCR_FROM_Z80 = 0x14
CMD_ZX2SD_SNA_48K      = 0x16
CMD_ZX2SD_SNA_128K     = 0x17
CMD_ZX2SD_SCR_FROM_SNA = 0x18
CMD_ZX2SD_GETINFO      = 0x1E
CMD_ZX2SD_TAP          = 0x20
CMD_ZX2SD_SCRTAP       = 0x23
CMD_ZX2SD_ROMSETBLK4B  = 0x30

# File-type bytes for directory-entry records (incCmdsFileTypes.h).
FT_END_DIRECTORY = 0x00
FT_DIRECTORY     = 0x0C
FT_SCR           = 0x10
FT_Z80_16K       = 0x11
FT_Z80_48K       = 0x12
FT_Z80_128K      = 0x13
FT_SNA_48K       = 0x16
FT_SNA_128K      = 0x17
FT_TAP           = 0x20
FT_ROMSET        = 0x30

# Per-entry record layout, reverse-engineered from ListChapter()/
# FirstChapterScan() in libFileList.ino: type(1) + name(30, case-folded,
# zero-padded) + 0x00 separator(1) + index as uint16 LE(2) = 34 bytes.
NAME_LEN = 30
MSG_LEN = NAME_LEN + 4
# One "chapter" (libFileList.ino: entriesPerPage=23 * pagesPerChapter=11).
# v1 limitation: only chapter 0 is served -- see handle_ls_relative().
MAX_FILES = 253

SNA_48K_SIZE   = 49179
SNA_128K_SIZE  = 131103
SNA_128K_SIZE2 = 147487
SCR_SIZE       = 6912
SCR_BLKSIZE    = 432

# libSNA.ino/libZ80.ino shared block-stream geometry: one 16K RAM page is
# split into a SNABLKSIZE-CHUNKSIZE first part (sent as block c==1) plus a
# CHUNKSIZE tail (sent separately as block c==10) -- real hardware's own
# reason for this split (Arduino buffer size limits) doesn't apply here,
# but the WIRE FRAMING still has to match it exactly.
SNABLKSIZE = 16384
CHUNKSIZE  = 512

# libZ80.ino's Z80_128k_page[8]: RAM bank (0-7, "block c"=bank+1) -> real
# Z80-file page number, for 128K snapshots.
Z80_128K_PAGE = (8, 5, 3, 4, 6, 7, 9, 10)

# libZ80.ino's Z802SNAHeader[25]: byte-for-byte remap table from a raw Z80
# header into the first 25 bytes of the SNA-style header this bridge sends.
Z80_TO_SNA_HEADER = (10, 19, 20, 17, 18, 15, 16, 22, 21, 4, 5, 13, 14, 2, 3,
                     23, 24, 25, 26, 27, 11, 1, 0, 8, 9)


class ClientState:
    def __init__(self, root):
        self.root = os.path.abspath(root)
        self.cwd = self.root
        self.hw_type = 3  # SETZXTYPE default assumption: 128K-capable
        self.entries = []        # last LS_RELATIVE/CD result: list of (name, is_dir, ft, full_path)
        self.open_path = None    # path opened by the most recent OFREAD/OFREAD_IX
        self.open_data = None    # that file's full contents, read once at OFREAD/OFREAD_IX time
        self.snapshot = None     # (header_bytes, pages dict{bank0-7: 16384 bytes}) -- built at block 9, reused for blocks 1/10/2-8
        self.tap_pos = 0         # byte offset of the NEXT TAP block header -- see handle_tap()


def recv_exact(conn, n):
    buf = b''
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            raise ConnectionError("peer closed mid-read")
        buf += chunk
    return buf


def recv_cstring(conn, maxlen=255):
    """getBuffer_0()'s wire format: bytes until a 0x00 terminator."""
    buf = bytearray()
    while True:
        b = recv_exact(conn, 1)
        if b == b'\x00':
            break
        buf += b
        if len(buf) >= maxlen:
            break
    return bytes(buf)


def classify(path, is_dir):
    """FT_* byte for this entry, or None to hide it -- a deliberately
    simplified stand-in for SetFileType() in libSDFiles.ino (which
    inspects file headers/CRCs/sizes in detail; this only looks at
    extension + size)."""
    name = os.path.basename(path)
    if name.startswith('.'):
        return None
    if is_dir:
        return FT_DIRECTORY
    ext = os.path.splitext(name)[1].upper()
    try:
        size = os.path.getsize(path)
    except OSError:
        return None
    if ext == '.SCR' and size == SCR_SIZE:
        return FT_SCR
    if ext == '.SNA':
        if size == SNA_48K_SIZE:
            return FT_SNA_48K
        if size in (SNA_128K_SIZE, SNA_128K_SIZE2):
            return FT_SNA_128K
        return None
    if ext == '.Z80':
        # Real SetFileType() inspects the header to pick 16K/48K/128K and
        # compressed-vs-not; not implemented here (loading isn't
        # implemented yet either, see module docstring) -- shown as 48K.
        return FT_Z80_48K
    if ext == '.TAP':
        return FT_TAP
    return None


def list_dir(state):
    """Rebuilds state.entries for state.cwd: ".." first (unless at root),
    then directories, then files -- matching ListChapter()'s own
    directories-then-files ordering. The index assigned here (just the
    position in this list) is OUR OWN invention, not the real SD card's
    FAT32 directory index (SdFat's dirIndex()) -- fine since nothing
    outside this process ever interprets it, it only round-trips back to
    us via OFREAD_IX/CD_IX/GETINFO."""
    entries = []
    if state.cwd != state.root:
        entries.append(("..", True, FT_DIRECTORY, os.path.dirname(state.cwd)))
    try:
        names = sorted(os.listdir(state.cwd), key=str.lower)
    except OSError:
        names = []
    dirs, files = [], []
    for name in names:
        full = os.path.join(state.cwd, name)
        is_dir = os.path.isdir(full)
        ft = classify(full, is_dir)
        if ft is None:
            continue
        (dirs if is_dir else files).append((name, is_dir, ft, full))
    entries.extend(dirs)
    entries.extend(files)
    state.entries = entries[:MAX_FILES]


def encode_entry(name, ft, index):
    name_field = name[:NAME_LEN]
    if name_field:
        name_field = name_field[0].upper() + name_field[1:].lower()
    name_bytes = name_field.encode('ascii', errors='replace').ljust(NAME_LEN, b'\x00')
    return bytes([ft]) + name_bytes + bytes([0x00]) + struct.pack('<H', index & 0xFFFF)


def handle_req_id(conn):
    conn.sendall(bytes([0x01]))


def handle_setzxtype(conn, state):
    state.hw_type = recv_exact(conn, 1)[0]
    print(f"[SETZXTYPE] hw_type={state.hw_type}")


def handle_cd_root(state):
    state.cwd = state.root
    print(f"[CD_ROOT] -> {state.cwd}")


def handle_cd(conn, state):
    name = recv_cstring(conn).decode('ascii', errors='replace')
    target = os.path.dirname(state.cwd) if name == '..' else os.path.join(state.cwd, name)
    target = os.path.abspath(target)
    if target.startswith(state.root) and os.path.isdir(target):
        state.cwd = target
        print(f"[CD] {name!r} -> {state.cwd}")
    else:
        print(f"[CD] {name!r} not a directory (or escapes root), ignored")


def handle_cd_ix(conn, state):
    ix = struct.unpack('<H', recv_exact(conn, 2))[0]
    if ix >= len(state.entries):
        print(f"[CD_IX] index {ix} out of range, ignored")
        return
    name, is_dir, ft, full = state.entries[ix]
    if is_dir:
        state.cwd = os.path.abspath(full)
        print(f"[CD_IX] {ix} ({name!r}) -> {state.cwd}")
    else:
        print(f"[CD_IX] {ix} ({name!r}) is not a directory, ignored")


def handle_getdir(conn, state):
    name = os.path.basename(state.cwd) or '/'
    conn.sendall(name.encode('ascii', errors='replace')[:255] + b'\x00')


def handle_ls_relative(conn, state):
    chapter = struct.unpack('<H', recv_exact(conn, 2))[0]
    if chapter not in (0, 0xFFFF):
        # v1 limitation -- see MAX_FILES/module docstring. Reply with an
        # immediate end-of-directory marker rather than hang the Z80 side
        # waiting for entries that will never come.
        print(f"[LS_RELATIVE] chapter {chapter} not supported (v1: chapter 0 only)")
        conn.sendall(encode_entry("", FT_END_DIRECTORY, 0))
        return
    list_dir(state)
    print(f"[LS_RELATIVE] {state.cwd}: {len(state.entries)} entries")
    for index, (name, is_dir, ft, full) in enumerate(state.entries):
        conn.sendall(encode_entry(name, ft, index))
    conn.sendall(encode_entry("", FT_END_DIRECTORY, 0))


def _resolve_ofread(state, path_or_name):
    target = os.path.abspath(os.path.join(state.cwd, path_or_name))
    if not target.startswith(state.root) or not os.path.isfile(target):
        return None
    return target


def _open(state, path):
    state.open_path = path
    state.snapshot = None   # invalidate any Z80/SNA parse from a previous file
    state.tap_pos = 0
    if path:
        try:
            with open(path, 'rb') as f:
                state.open_data = f.read()
            return
        except OSError:
            pass
    state.open_path = None
    state.open_data = None


def handle_ofread(conn, state):
    name = recv_cstring(conn).decode('ascii', errors='replace')
    target = _resolve_ofread(state, name)
    _open(state, target)
    print(f"[OFREAD] {name!r} -> {'OK ' + target if target else 'FAIL'}")
    # Real firmware's reply here (OpenFile()'s b[off]/b[off+1], consumed
    # by the Z80 side as SUCCESS/FAIL + file type) isn't replicated yet --
    # OFREAD's only observable effect currently is which file a
    # subsequent SCR/SNA/Z80/TAP command reads from.


def handle_ofread_ix(conn, state):
    ix = struct.unpack('<H', recv_exact(conn, 2))[0]
    if ix < len(state.entries) and not state.entries[ix][1]:
        _open(state, state.entries[ix][3])
        print(f"[OFREAD_IX] {ix} -> OK {state.open_path}")
    else:
        _open(state, None)
        print(f"[OFREAD_IX] {ix} -> FAIL")


def handle_scr(conn, state):
    block = recv_exact(conn, 1)[0]
    if not state.open_data:
        print("[SCR] no file open, nothing sent")
        return
    if block == 0:
        conn.sendall(state.open_data[:SCR_SIZE])
    else:
        start = (block - 1) * SCR_BLKSIZE
        conn.sendall(state.open_data[start:start + SCR_BLKSIZE])
    print(f"[SCR] block={block} from {state.open_path}")


# -----------------------------------------------------------------------------
# Z80 snapshot parsing -- a direct translation of SendZ80()'s case 9 (header
# remap) and cases 1-8 (RAM block location/decompression) in libZ80.ino, plus
# uncompress() in libZ80uncompress.ino. See module docstring for why this
# fully decompresses up front instead of replicating the real firmware's
# incremental/stateful streaming.
# -----------------------------------------------------------------------------

def z80_rle_decompress(data, out_len):
    """ED ED <count> <value> = repeat value `count` times; anything else is
    literal. Stops once `out_len` bytes are produced (matches a bounded
    16384-byte RAM page -- a real Z80 v1 file's compressed stream is
    terminated by a literal 00 ED ED 00 marker AFTER the page's own
    content, which this stops short of reaching)."""
    out = bytearray()
    i = 0
    n = len(data)
    while len(out) < out_len and i < n:
        b = data[i]
        if b == 0xED and i + 1 < n and data[i + 1] == 0xED and i + 3 < n:
            count = data[i + 2]
            value = data[i + 3]
            out.extend([value] * count)
            i += 4
        else:
            out.append(b)
            i += 1
    return bytes(out[:out_len])


def parse_z80(data, req_type):
    """Returns (sna_header: 64 bytes, pages: dict{bank 0-7 -> 16384 bytes}).
    `req_type` is the CMD_ZX2SD_Z80_* the Z80 side asked for (16K/48K/128K)
    -- only used the same way SendZ80() uses it, to pick the 48K-style
    fixed page assignment (4000/8000/C000 -> banks 5/2/0) vs. the 128K
    per-file page table."""
    # getFileChunk(&myFile, Z80Header, 0, Z80MAXHEADERSIZE) -- unconditional
    # 87-byte prefix read regardless of the real header's size.
    hdr = data[:87].ljust(87, b'\x00')

    sna = bytearray(64)
    for i, src in enumerate(Z80_TO_SNA_HEADER):
        sna[i] = hdr[src]
    sna[19] = 0 if hdr[27] == 0 else 4
    sna[20] = (hdr[11] & 0x7F) | (0x80 if (hdr[12] & 1) == 1 else 0)
    sna[25] = hdr[29] & 0x03
    sna[26] = (hdr[12] & 0x0E) >> 1
    sna[29] = 0x30
    sna[30] = 0x04

    is_v1 = (hdr[6] != 0) or (hdr[7] != 0)
    if is_v1:
        sna[27], sna[28] = hdr[6], hdr[7]
        sna[31] = 3
        z80_version = 2 if (hdr[12] & 32) else 1  # 2=v1 compressed, 1=v1 uncompressed
    else:
        z80_version = hdr[30]  # the raw byte: 23 (v2), 54 or 55 (v3)
        sna[27], sna[28] = hdr[32], hdr[33]
        if hdr[30] == 23:
            sna[31] = 8 if hdr[34] >= 3 else 3
        elif hdr[30] == 55:
            sna[30] = hdr[86]
            sna[31] = 8 if hdr[34] >= 4 else 3
        elif hdr[30] == 54:
            sna[31] = 8 if hdr[34] >= 4 else 3
        if sna[31] == 8:
            sna[29] = hdr[35]
        if (hdr[37] & 0x80) and sna[31] == 3:
            sna[31] = 1
    for c in range(18):
        sna[32 + c] = hdr[38 + c] if z80_version > 22 else 0
    for c in range(49, 63):
        sna[c] = 255
    sna[63] = 255

    pages = {}
    if is_v1:
        body = data[30:]
        if z80_version == 2:  # compressed
            raw = z80_rle_decompress(body, 49152)
        else:
            raw = body[:49152].ljust(49152, b'\x00')
        pages[5], pages[2], pages[0] = raw[0:16384], raw[16384:32768], raw[32768:49152]
    else:
        add_len = data[30] | (data[31] << 8)
        pos = 32 + add_len
        page_to_bank = {8: 5, 4: 2, 5: 0}  # 48K/16K fixed layout: page8/4/5 -> bank5/2/0
        if req_type == CMD_ZX2SD_Z80_128K:
            page_to_bank = {p: b for b, p in enumerate(Z80_128K_PAGE)}
        n = len(data)
        while pos + 3 <= n:
            block_len = data[pos] | (data[pos + 1] << 8)
            page_num = data[pos + 2]
            pos += 3
            if block_len == 0xFFFF:
                block_data = data[pos:pos + 16384].ljust(16384, b'\x00')
                pos += 16384
            else:
                block_data = z80_rle_decompress(data[pos:pos + block_len], 16384)
                pos += block_len
            bank = page_to_bank.get(page_num)
            if bank is not None:
                pages[bank] = block_data
    return bytes(sna), pages


# -----------------------------------------------------------------------------
# SNA parsing -- direct translation of SendSNA() in libSNA.ino.
# -----------------------------------------------------------------------------

def sna_ramblocks(paged_bank):
    """RAMBLOCKS[0..7]: for file "slot" index 0-7 (block c=1..8), which
    bank (0-7, or 2/8 meaning a slot shared with the always-present
    bank-2 "RAM2" copy) occupies it -- see SendSNA()'s own comment table.
    `paged_bank` is the bank (0-7) currently paged into 0xC000 (bottom 3
    bits of port #7FFD) -- irrelevant for 48K (always bank 0)."""
    c = paged_bank
    r = [0] * 8
    r[0] = 0
    r[1] = 1
    r[2] = 2 if c == 0 else 3
    r[3] = 2 if c == 1 else (3 if c < 1 else 4)
    r[4] = 2 if c == 3 else (4 if c < 2 else 5)
    r[5] = 2 if c == 4 else (5 if (c < 4 and c != 2) else 6)
    r[6] = 2 if c == 6 else (6 if (c < 5 and c != 2) else 7)
    r[7] = 2 if c == 7 else (7 if (c != 5 and c != 2) else 8)
    return r


def parse_sna(data, req_type):
    """Returns (sna_header: 64 bytes, pages: dict{bank 0-7 -> 16384 bytes}).
    Same return shape as parse_z80() so both can share send_snapshot_block()
    below."""
    is_128k = (req_type == CMD_ZX2SD_SNA_128K)

    sna = bytearray(64)
    sna[0:27] = data[:27]
    if is_128k:
        # 128K SNA stores PC right after the 48K-sized body (header + 3
        # fixed 16K blocks), then the #7FFD port value one byte later.
        sna[27], sna[28] = data[49179], data[49180]
        port7ffd = data[49181] if len(data) > 49181 else 0
    else:
        sna[27], sna[28] = 0, 0              # PC=0 for 48K -- real PC is on the stack
        port7ffd = 0x30                      # 48K ROM, RAM0 paged, locked
    sna[29] = port7ffd
    sna[30] = 0x04                           # #1FFD -- SNA has no 2A/2B/3 info, always 48K ROM paged
    sna[31] = 8 if is_128k else 3            # gametype: 8=128K, 3=48K
    # bytes 32-62: AY regs (SNA has none) + reserved, all zero/255 either way
    for c in range(49, 63):
        sna[c] = 255
    sna[63] = 255

    pages = {}
    if is_128k:
        for bank in sna_ramblocks(port7ffd & 7):
            if bank > 7:
                continue  # a slot sharing data with an already-covered bank -- nothing new to read
            pos = 27 + bank * 16384 + (4 if bank > 2 else 0)
            pages[bank] = data[pos:pos + 16384].ljust(16384, b'\x00')
    else:
        # 48K body is just banks 5,2,0 back to back, no remapping needed.
        pages[5] = data[27:27 + 16384]
        pages[2] = data[27 + 16384:27 + 32768]
        pages[0] = data[27 + 32768:27 + 49152]
    return bytes(sna), pages


def page_order_for(is_128k):
    """block c (1-8) -> bank (0-7), for whichever snapshot size is in
    play -- SendSNA's RAMBLOCKS/SendZ80's Z80_128k_page tables both exist
    only to locate each bank WITHIN THE FILE; parse_sna()/parse_z80()
    already resolved that into a plain bank->data dict, so the OUTPUT
    side (what block c means to the Z80) just needs this. 128K: block c
    IS bank c-1 by definition (SendZ80's own comment: "Send RAM block
    c=1..8 for ram page=0..7"). 48K/16K: only 3 blocks exist, fixed order
    5,2,0 (Spectrum's real RAM layout at 0x4000/0x8000/0xC000)."""
    return tuple(range(8)) if is_128k else (5, 2, 0)


def send_snapshot_block(conn, state, req_type, is_z80):
    kind = 'Z80' if is_z80 else 'SNA'
    c = recv_exact(conn, 1)[0]
    if c == 9:
        data = state.open_data or b''
        header, pages = (parse_z80 if is_z80 else parse_sna)(data, req_type)
        is_128k = req_type in (CMD_ZX2SD_SNA_128K, CMD_ZX2SD_Z80_128K)
        state.snapshot = (pages, is_128k)
        conn.sendall(header)
        print(f"[{kind}] header sent ({len(pages)} RAM pages parsed)")
        return

    if not state.snapshot:
        print(f"[{kind}] block {c} requested with no header sent first, ignored")
        return
    pages, is_128k = state.snapshot
    order = page_order_for(is_128k)

    if c == 10:
        # Chunk = the last CHUNKSIZE bytes of whichever page block 1 sent.
        data = pages.get(order[0], b'\x00' * SNABLKSIZE)
        conn.sendall(data[SNABLKSIZE - CHUNKSIZE:SNABLKSIZE])
    elif 1 <= c <= 8:
        if c - 1 >= len(order):
            conn.sendall(b'')  # no such block at this snapshot size (e.g. c=4 on a 48K snapshot)
        else:
            data = pages.get(order[c - 1], b'\x00' * SNABLKSIZE)
            conn.sendall(data[:SNABLKSIZE - CHUNKSIZE] if c == 1 else data)
    print(f"[{kind}] block={c}")


def handle_sna(conn, state, req_type):
    send_snapshot_block(conn, state, req_type, is_z80=False)


def handle_z80(conn, state, req_type):
    send_snapshot_block(conn, state, req_type, is_z80=True)


# -----------------------------------------------------------------------------
# TAP -- direct translation of SendTAP() in libTAP.ino. The Z80 side sends
# its own [flag(1), length(2 LE), start(2 LE)] as this command's 5-byte
# payload; the reply is [status(1), length(2 LE), start(2 LE)] followed by
# `length` data bytes, then the file position advances to the next block
# (auto-looping back to the start at EOF -- "required for some multiload
# games", per the real comment).
# -----------------------------------------------------------------------------

def handle_tap(conn, state):
    payload = recv_exact(conn, 5)
    zx_flag = payload[0]
    zx_length = struct.unpack('<H', payload[1:3])[0]
    zx_start = struct.unpack('<H', payload[3:5])[0]

    data = state.open_data or b''
    pos = state.tap_pos
    if pos + 3 > len(data):
        pos = 0  # loop back to the start of the TAP

    tap_length = struct.unpack('<H', data[pos:pos + 2])[0] - 2
    tap_flag = data[pos + 2]
    block_start = pos + 3  # skip [length(2)][flag(1)] -- flag itself isn't sent as data
    next_pos = pos + 2 + (tap_length + 2)  # length field covers flag+data+checksum

    if tap_flag != zx_flag:
        conn.sendall(bytes([0xFF]) + struct.pack('<H', 0) + struct.pack('<H', 0))
        state.tap_pos = next_pos
        print(f"[TAP] flag mismatch (zx={zx_flag:#x} tap={tap_flag:#x}), skipped block")
        return

    skip = 0
    if zx_length == tap_length:
        status, send_length = 0x00, tap_length
    elif zx_length < tap_length:
        status, send_length = 0x01, zx_length
        skip = tap_length - zx_length
    else:
        status, send_length = 0x02, tap_length

    send_start = zx_start
    if send_start < 0x4000:
        send_length -= (0x4000 - send_start)
        block_start += (0x4000 - send_start)
        send_start = 0x4000
    if (0xFFFF - send_length + 1) < send_start:
        extra_skip = send_start - (0xFFFF - send_length + 1)
        skip += extra_skip
        send_length -= extra_skip

    conn.sendall(bytes([status]) + struct.pack('<H', send_length & 0xFFFF) + struct.pack('<H', send_start))
    conn.sendall(data[block_start:block_start + send_length])
    state.tap_pos = next_pos
    print(f"[TAP] status={status} start={send_start:#06x} length={send_length} (next_pos={next_pos})")


# Payload shape for every command the real firmware's dispatch table
# recognizes (Multiply_Arduino.ino), whether or not handled above --
# correct consumption is mandatory even for unimplemented commands, see
# module docstring. None = a 1-byte block-number arg like SCR's (kept
# generic since several content-streaming commands share that shape).
_FIXED_PAYLOAD = {
    CMD_ZX2INO_REQ_ID: 0,
    CMD_ZX2SD_SETZXTYPE: None,  # handled directly (reads its own 1 byte)
    CMD_ZX2SD_OFREAD: None,     # cstring, handled directly
    CMD_ZX2SD_OFREAD_IX: None,  # handled directly (2 bytes)
    CMD_ZX2SD_CD_ROOT: 0,
    CMD_ZX2SD_CD: None,         # cstring, handled directly
    CMD_ZX2SD_CD_IX: None,      # handled directly (2 bytes)
    CMD_ZX2SD_GETDIR: 0,
    CMD_ZX2SD_LS_RELATIVE: None,  # handled directly (2 bytes)
    CMD_ZX2SD_LS_ABSOLUTE: None,  # cstring -- NOT handled, consumed+ignored
    CMD_ZX2SD_SCR: None,        # handled directly (1 byte)
    CMD_ZX2SD_Z80_16K: None,    # handled directly (1-byte block number)
    CMD_ZX2SD_Z80_48K: None,
    CMD_ZX2SD_Z80_128K: None,
    CMD_ZX2SD_SCR_FROM_Z80: 1,
    CMD_ZX2SD_SNA_48K: None,
    CMD_ZX2SD_SNA_128K: None,
    CMD_ZX2SD_SCR_FROM_SNA: 1,
    CMD_ZX2SD_GETINFO: 2,
    CMD_ZX2SD_TAP: None,        # handled directly (5-byte flag/length/start)
    CMD_ZX2SD_SCRTAP: 1,
    CMD_ZX2SD_ROMSETBLK4B: 1,
}


def handle_client(conn, state):
    while True:
        cmd = recv_exact(conn, 1)[0]
        print(f"[RECV] cmd=0x{cmd:02x}")

        if cmd == CMD_ZX2INO_REQ_ID:
            handle_req_id(conn)
        elif cmd == CMD_ZX2SD_SETZXTYPE:
            handle_setzxtype(conn, state)
        elif cmd == CMD_ZX2SD_OFREAD:
            handle_ofread(conn, state)
        elif cmd == CMD_ZX2SD_OFREAD_IX:
            handle_ofread_ix(conn, state)
        elif cmd == CMD_ZX2SD_CD_ROOT:
            handle_cd_root(state)
        elif cmd == CMD_ZX2SD_CD:
            handle_cd(conn, state)
        elif cmd == CMD_ZX2SD_CD_IX:
            handle_cd_ix(conn, state)
        elif cmd == CMD_ZX2SD_GETDIR:
            handle_getdir(conn, state)
        elif cmd == CMD_ZX2SD_LS_RELATIVE:
            handle_ls_relative(conn, state)
        elif cmd == CMD_ZX2SD_SCR:
            handle_scr(conn, state)
        elif cmd in (CMD_ZX2SD_SNA_48K, CMD_ZX2SD_SNA_128K):
            handle_sna(conn, state, cmd)
        elif cmd in (CMD_ZX2SD_Z80_16K, CMD_ZX2SD_Z80_48K, CMD_ZX2SD_Z80_128K):
            handle_z80(conn, state, cmd)
        elif cmd == CMD_ZX2SD_TAP:
            handle_tap(conn, state)
        elif cmd in _FIXED_PAYLOAD and _FIXED_PAYLOAD[cmd] is not None:
            # Known command, not (yet) implemented -- still have to
            # consume its exact payload so the NEXT command byte doesn't
            # desync (see module docstring).
            n = _FIXED_PAYLOAD[cmd]
            if n:
                recv_exact(conn, n)
            print(f"[?] cmd 0x{cmd:02x} recognized but not implemented yet -- payload consumed, no reply")
        elif cmd == CMD_ZX2SD_LS_ABSOLUTE:
            recv_cstring(conn)
            print(f"[?] cmd 0x{cmd:02x} (LS_ABSOLUTE) recognized but not implemented yet -- payload consumed, no reply")
        else:
            # Truly unknown command byte -- no way to know its payload
            # shape, so there's nothing safe to do except log it. If the
            # real menu ROM ever sends one of these, every command after
            # it on this connection will likely desync.
            print(f"[!] UNKNOWN cmd 0x{cmd:02x} -- cannot determine payload length, connection may desync from here")


def main() -> None:
    parser = argparse.ArgumentParser(description="Multiply bridge for JASE's Multiply peripheral")
    parser.add_argument('--tcp-port', type=int, default=9701)
    parser.add_argument('--basedir', required=True, help="directory to serve as the virtual SD card root")
    args = parser.parse_args()

    print(f"=== Multiply Bridge v{VERSION} ===")
    print(f"SD card root: {os.path.abspath(args.basedir)}")

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.tcp_port))
    srv.listen(1)
    print(f"Listening on 127.0.0.1:{args.tcp_port}. Waiting for JASE's Multiply peripheral... (Ctrl+C to stop)")

    while True:
        conn, addr = srv.accept()
        print(f"\n*** Client connected from {addr}.")
        state = ClientState(args.basedir)
        try:
            handle_client(conn, state)
        except ConnectionError:
            pass
        finally:
            print("*** Client disconnected.")
            conn.close()


if __name__ == "__main__":
    main()
