"""ZX7 decompressor (Einar Saukas' format), ported from the Dandanator Mini
generator's own Java implementation (UncompressedByteArrayReader.java,
github.com/teiram/dandanator-mini) so decoded game blocks can be trusted
against the same reference the romset was built with.
"""
from __future__ import annotations


class Zx7Error(Exception):
    pass


def decompress(data: bytes, max_size: int = 65535) -> bytes:
    out = bytearray()
    pos = 0
    bitmask = 0
    bitvalue = 0

    def read_byte() -> int:
        nonlocal pos
        if pos >= len(data):
            raise Zx7Error(f"input exhausted at output length {len(out)}")
        b = data[pos]
        pos += 1
        return b

    def read_bit() -> bool:
        nonlocal bitmask, bitvalue
        bitmask >>= 1
        if bitmask == 0:
            bitmask = 128
            bitvalue = read_byte()
        return (bitvalue & bitmask) != 0

    def read_elias_gamma() -> int:
        # Matches the Java original exactly: keep consuming leading-zero
        # bits until a 1-bit terminates the prefix, THEN check the count
        # -- not an early-exit inside the loop, which would desync the
        # bitstream for every subsequent read.
        i = 0
        while not read_bit():
            i += 1
        if i > 15:
            return -1
        value = 1
        while i > 0:
            value = (value << 1) | (1 if read_bit() else 0)
            i -= 1
        return value

    def read_offset() -> int:
        value = read_byte()
        if value < 128:
            return value
        i = 1 if read_bit() else 0
        i = (i << 1) | (1 if read_bit() else 0)
        i = (i << 1) | (1 if read_bit() else 0)
        i = (i << 1) | (1 if read_bit() else 0)
        return ((value & 127) | (i << 7)) + 128

    out.append(read_byte())
    while True:
        if not read_bit():
            out.append(read_byte())
        else:
            length = read_elias_gamma()
            if length <= 0:
                break
            length += 1
            offset = read_offset() + 1
            if offset > len(out):
                raise Zx7Error(f"back-reference offset {offset} > output length {len(out)}")
            for _ in range(length):
                out.append(out[len(out) - offset])
        if len(out) > max_size:
            raise Zx7Error("output exceeded max_size, likely wrong start offset")
    return bytes(out)
