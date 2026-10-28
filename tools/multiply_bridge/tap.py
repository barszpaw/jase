# -----------------------------------------------------------------------------
# TapPlayer -- block-by-block TAP streaming, a direct translation of
# SendTAP() in libTAP.ino. The Z80 side's own request carries
# [flag(1), length(2 LE), start(2 LE)]; the reply is
# [status(1), length(2 LE), start(2 LE)] followed by `length` data bytes.
# Deliberately pure (no socket I/O) -- see virtual_sdcard.py's module
# docstring for why that split matters here.
# -----------------------------------------------------------------------------

import struct


class TapPlayer:
    def __init__(self, data):
        self.data = data
        self.pos = 0  # byte offset of the NEXT block header

    def next_block(self, zx_flag, zx_length, zx_start):
        """Returns (status, send_length, send_start, data_bytes). Advances
        self.pos to the following block, auto-looping back to the start of
        the file at EOF ("required for some multiload games", per the real
        firmware's own comment).

        Note on a deliberate deviation from the real firmware: SendTAP()
        tracks the next block's position by seeking past however many
        bytes it decided to skip (ZXSkip) plus the checksum byte, landing
        on the next block by construction. This instead computes self.pos
        directly from the current block's own on-disk size
        (2 + tap_length + 2, independent of any truncation below) --
        same destination, no need to carry a "bytes skipped" value through
        to a seek call that doesn't exist here."""
        pos = self.pos
        if pos + 3 > len(self.data):
            pos = 0

        tap_length = struct.unpack('<H', self.data[pos:pos + 2])[0] - 2  # TAP length includes flag+checksum
        tap_flag = self.data[pos + 2]
        block_start = pos + 3  # skip [length(2)][flag(1)] -- flag itself isn't sent as data
        self.pos = pos + 2 + (tap_length + 2)

        if tap_flag != zx_flag:
            return 0xFF, 0, 0, b''

        if zx_length >= tap_length:
            status, send_length = (0x00, tap_length) if zx_length == tap_length else (0x02, tap_length)
        else:
            status, send_length = 0x01, zx_length

        send_start = zx_start
        if send_start < 0x4000:
            send_length -= (0x4000 - send_start)
            block_start += (0x4000 - send_start)
            send_start = 0x4000
        if (0xFFFF - send_length + 1) < send_start:
            send_length -= send_start - (0xFFFF - send_length + 1)

        return status, send_length, send_start, self.data[block_start:block_start + send_length]
