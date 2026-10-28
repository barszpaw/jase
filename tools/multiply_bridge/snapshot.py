# -----------------------------------------------------------------------------
# Snapshot -- SNA and Z80 snapshot loading, normalized to the real firmware's
# own shared block-stream format (SendSNA/SendZ80 in libSNA.ino/libZ80.ino
# both produce the SAME wire shape: block 9 = 64-byte SNA-style header,
# blocks 1-8 = 16K RAM pages, block 10 = the last CHUNKSIZE bytes of block
# 1's page, sent separately). Deliberately pure (no socket I/O) -- see
# virtual_sdcard.py's module docstring for why that split matters here.
#
# Z80's header remap table (Z802SNAHeader) and 128K page table
# (Z80_128k_page), and SNA 128K's RAMBLOCKS bank-remapping, are direct
# translations of the real firmware (libSNA.ino/libZ80.ino, CC BY-SA,
# github.com/mad3001/Multiply, not vendored, read in full during
# development). Unlike the real firmware, decompression happens fully in
# memory the moment the header (block 9) is parsed, rather than
# incrementally with state carried across calls (uncompress() in
# libZ80uncompress.ino) -- identical bytes end up on the wire either way;
# the incremental version exists there only because of the Arduino's tiny
# RAM, which doesn't apply to a bridge process running on a real computer.
# SNA 128K's RAMBLOCKS path is less battle-tested than everything else
# here -- flagged inline where it matters.
# -----------------------------------------------------------------------------

from protocol import CMD_ZX2SD_SNA_128K, CMD_ZX2SD_Z80_128K, SNABLKSIZE, CHUNKSIZE

# libZ80.ino's Z80_128k_page[8]: RAM bank (0-7, "block c"=bank+1) -> real
# Z80-file page number, for 128K snapshots.
Z80_128K_PAGE = (8, 5, 3, 4, 6, 7, 9, 10)

# libZ80.ino's Z802SNAHeader[25]: byte-for-byte remap table from a raw Z80
# header into the first 25 bytes of the SNA-style header this bridge sends.
Z80_TO_SNA_HEADER = (10, 19, 20, 17, 18, 15, 16, 22, 21, 4, 5, 13, 14, 2, 3,
                     23, 24, 25, 26, 27, 11, 1, 0, 8, 9)


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
    Same return shape as parse_z80() so Snapshot.load() can treat both
    uniformly."""
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


class Snapshot:
    """A fully-parsed SNA or Z80 file, ready to serve block()s 1-10 on
    request -- see module docstring for the shared wire format."""

    def __init__(self, header, pages, is_128k):
        self.header = header
        self.pages = pages
        self.order = page_order_for(is_128k)

    @classmethod
    def load(cls, data, req_type, is_z80):
        header, pages = (parse_z80 if is_z80 else parse_sna)(data, req_type)
        is_128k = req_type in (CMD_ZX2SD_SNA_128K, CMD_ZX2SD_Z80_128K)
        return cls(header, pages, is_128k)

    def block(self, c):
        """The bytes for block c (1-10, see SendSNA/SendZ80's own
        numbering) -- 9 is the header itself, handled by callers via
        self.header directly rather than through here."""
        if c == 10:
            data = self.pages.get(self.order[0], b'\x00' * SNABLKSIZE)
            return data[SNABLKSIZE - CHUNKSIZE:SNABLKSIZE]
        if 1 <= c <= 8:
            if c - 1 >= len(self.order):
                return b''  # no such block at this snapshot size (e.g. c=4 on a 48K snapshot)
            data = self.pages.get(self.order[c - 1], b'\x00' * SNABLKSIZE)
            return data[:SNABLKSIZE - CHUNKSIZE] if c == 1 else data
        return b''
