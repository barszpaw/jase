# -----------------------------------------------------------------------------
# Wire-protocol primitives for JASE's Multiply bridge: command/file-type byte
# constants, the two read-framing helpers the rest of the package builds on,
# and the directory-entry record encoder. No session state, no file-format
# parsing -- just the vocabulary the wire uses. See session.py for the
# per-connection dispatch that uses these, and docs/Dandanator/
# dandanator_multiply_notes.md for the hardware/protocol background.
#
# Values reverse-engineered from the real Arduino firmware's command
# dispatch (Multiply_Arduino.ino's loop(), incCmdsFileTypes.h) and
# directory-listing code (libFileList.ino) -- not guessed. Those sources
# aren't vendored in this repo (CC BY-SA, github.com/mad3001/Multiply) but
# were read in full during development.
# -----------------------------------------------------------------------------

import struct

# Commands the real Arduino firmware's main dispatch loop recognizes. Only a
# subset is actually handled by this package -- see session.py's dispatch
# and _FIXED_PAYLOAD below for which.
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

# Multiply_Arduino.ino: const uint8_t MultiplyVersion[8] = {'M','U','L',
# 'T','v','1','.','2'}, the exact reply to CMD_ZX2INO_REQ_ID. Getting the
# BYTE COUNT right here matters, not just the content: real menu ROMs read
# a fixed-size identification string and busy-poll port 0x1F for every
# byte of it -- send fewer bytes than expected (an earlier version of this
# bridge sent a single placeholder byte) and the Z80 side hangs forever
# waiting for bytes that will never come, with no error and no timeout.
# Confirmed live: a real romset's "Launch Multiply" menu entry appeared to
# freeze after one nibble-pair -- it was correctly waiting for byte 2 of 8.
MULTIPLY_VERSION = bytes(b'MULTv1.2')

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
# v1 limitation: only chapter 0 is served -- see virtual_sdcard.py.
MAX_FILES = 253
FILES_PER_PAGE = 23  # MultMenu's FilesPerPag/MaxFilePage (labels_multiply.asm)


def encode_chapter_terminator(num_entries):
    """The FT_END_DIRECTORY record for a CMD_ZX2SD_LS_RELATIVE chapter-0
    reply -- NOT just a bare terminator. Taken DIRECTLY from the real
    Arduino source, mad3001/Multiply's Src/Multiply_Arduino/
    libFileList.ino (`FirstChapterScan()`/`ListDir()`'s "folder doesn't
    exist" fallback, which writes this same structure) -- a first attempt
    at this function was reverse-engineered from the CONSUMING side
    instead (MultMenu.asm's `Chapter0:` handler) and got the byte offsets
    wrong by one (derived 3/4/5/11, real firmware uses 2/3/4/10) because
    the Z80-side HL arithmetic is easy to mis-trace; always check the
    generating source first when it's available; see `incCmdsFileTypes.h`
    for the shared `tChaptersOffset`/`tPagesOffset`/`tFilesOffset`/
    `indexOffset` constants this mirrors 1:1.
    Byte 0 = FT_END_DIRECTORY (satisfied by zero-init, same as a plain
    terminator); byte 2 = total chapters; byte 3 = total pages
    (FILES_PER_PAGE entries per page); byte 4 = file count; bytes 10-11 =
    chapter 1's own "index" slot, which the real firmware sets to the
    literal sentinel 0xFFFF (`FakeIndex`) rather than a real seek offset
    -- chapter 1 always starts via `rewindDirectory()`, never a seek, so
    this value is never actually used as an index, just checked against
    the sentinel. Chapters 2-9 (bytes 12-27, in the same 9-chapter x
    2-byte table) are left at 0x0000 -- the real firmware only ever
    writes them when a folder has MORE than MAX_FILES (253) entries,
    which this server already truncates to, so they'd never be read
    anyway (LastChapter stays 1). Leaving this whole record out entirely
    (the original v1 bug) left the ROM's own chapter/index tables full of
    whatever was in RAM beforehand -- live-observed as a different
    garbage "chapter" number requested on every run, and a CD_IX sent
    with a wildly out-of-range index when picking a directory from the
    listing.
    """
    buf = bytearray(MSG_LEN)
    buf[0] = FT_END_DIRECTORY
    pages = max(1, (num_entries + FILES_PER_PAGE - 1) // FILES_PER_PAGE)
    buf[2] = 1
    buf[3] = pages & 0xFF
    buf[4] = min(num_entries, 255)
    struct.pack_into('<H', buf, 10, 0xFFFF)
    return bytes(buf)

SNA_48K_SIZE   = 49179
SNA_128K_SIZE  = 131103
SNA_128K_SIZE2 = 147487
SCR_SIZE       = 6912
SCR_BLKSIZE    = 432

# libSNA.ino/libZ80.ino shared block-stream geometry: one 16K RAM page is
# split into a SNABLKSIZE-CHUNKSIZE first part (sent as block c==1) plus a
# CHUNKSIZE tail (sent separately as block c==10) -- real hardware's own
# reason for this split (Arduino buffer size limits) doesn't apply here,
# but the WIRE FRAMING still has to match it exactly. See snapshot.py.
SNABLKSIZE = 16384
CHUNKSIZE  = 512

# Payload shape for every command the real firmware's dispatch table
# recognizes, whether or not session.py actually handles it -- correct
# consumption is mandatory even for unimplemented commands: this is an
# UNFRAMED byte stream (no message delimiters), so getting a payload length
# wrong desyncs every command after it for the rest of the connection, same
# risk the real firmware's own fixed dispatch table avoids by construction.
# None marks commands session.py reads its own payload for directly
# (variable-length cstrings, or ones sharing a handler).
FIXED_PAYLOAD = {
    CMD_ZX2INO_REQ_ID: 0,
    CMD_ZX2SD_SETZXTYPE: None,
    CMD_ZX2SD_OFREAD: None,
    CMD_ZX2SD_OFREAD_IX: None,
    CMD_ZX2SD_CD_ROOT: 0,
    CMD_ZX2SD_CD: None,
    CMD_ZX2SD_CD_IX: None,
    CMD_ZX2SD_GETDIR: 0,
    CMD_ZX2SD_LS_RELATIVE: None,
    CMD_ZX2SD_LS_ABSOLUTE: None,
    CMD_ZX2SD_SCR: None,
    CMD_ZX2SD_Z80_16K: None,
    CMD_ZX2SD_Z80_48K: None,
    CMD_ZX2SD_Z80_128K: None,
    CMD_ZX2SD_SCR_FROM_Z80: 1,
    CMD_ZX2SD_SNA_48K: None,
    CMD_ZX2SD_SNA_128K: None,
    CMD_ZX2SD_SCR_FROM_SNA: 1,
    CMD_ZX2SD_GETINFO: 2,
    CMD_ZX2SD_TAP: None,
    CMD_ZX2SD_SCRTAP: 1,
    CMD_ZX2SD_ROMSETBLK4B: 1,
}


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


def encode_entry(name, ft, index):
    name_field = name[:NAME_LEN]
    if name_field:
        name_field = name_field[0].upper() + name_field[1:].lower()
    name_bytes = name_field.encode('ascii', errors='replace').ljust(NAME_LEN, b'\x00')
    return bytes([ft]) + name_bytes + bytes([0x00]) + struct.pack('<H', index & 0xFFFF)
