# SDI bridge wire protocol

TCP, one connection per JASE run (JASE is the client, this bridge is the
server). Line-based ASCII for commands/headers; raw bytes follow wherever
a length is given, with no further framing or escaping.

- Lines are terminated by `\n`. A trailing `\r` is tolerated/stripped on
  read, never sent on write.
- Fields within a command line split on `,` if the line contains a comma,
  otherwise on space (`fields = line.split(',') if ',' in line else
  line.split(' ')`).
- `<ch_hex>` is the IEC secondary-address channel number, hex, no `0x`
  prefix (e.g. `0`, `F`).

## Commands (JASE -> bridge)

| Command | Fields | Response (success) | Response (failure) |
|---|---|---|---|
| `OPEN <ch_hex> <name>` | `name` = filename, or `$`/`$0`/`$1` for the directory catalog | `OK <len>\n` immediately followed by exactly `<len>` raw bytes (the full file/catalog content, pre-fetched in one shot) | `ERR <reason>\n` |
| `CLOSE <ch_hex>` | — | `OK\n` | — |
| `WRITE <ch_hex> <len> <E\|C>` | `len` = byte count immediately following on the wire; `E`=EOI (last write of this stream), `C`=continuing | `OK\n` | `ERR <reason>\n` |

There is no separate `READ` command. `OPEN` returns the **entire**
channel's content in one round trip; JASE's `IecBridgeClient` buffers it
locally (`std::map<int, ChannelBuffer>`) and drains it byte-by-byte with
**no further network I/O** — the IEC bit-banging this feeds is
T-state-timed, so a TCP round trip per byte was never an option.

`WRITE` (SAVE) is currently an honest stub server-side: the bridge reads
and discards the bytes, always replies `OK`. Not implemented yet, not
faked.

## Byte stream: the `$` catalog

`OPEN <ch> $` returns a real CBM-style `"$"` directory listing — the same
link-pointer-chained fake-BASIC-"program" format a genuine 1541/SD2IEC
emits over IEC. SDI's ROM is built for real IEC/1541 interop and parses
this exactly like it would from real hardware; it does **no** PETSCII
translation or column-alignment logic of its own (confirmed: its print
helper is a bare Spectrum `RST $10`, zero transformation) — the data
itself has to already be in final, display-ready form.

```
offset  size  content
0       2     load address, always 01 08
...     2     link pointer to next line (absolute address, LE)
...     2     "line number" field, repurposed as: 0 for the header line,
              block count for a file line, blocks-free count for the footer
...     N     line text (see below), not null-padded
...     1     0x00 line terminator
```
repeated per line, ending with a final `00 00` (link pointer 0 = end of
program — this is the catalog's own EOF marker; omitting it is why an
earlier version of this bridge left SDI's ROM reading into uninitialized
RAM past the real data).

| Line | Text content |
|---|---|
| Header (line number 0) | `"<disk name, 16 bytes>" <disk id, 2 bytes> <dos type, 2 bytes>` |
| Per file (line number = block count) | `"<filename, 16 bytes>" PRG` |
| Footer (line number = free block count) | `BLOCKS FREE.` |

Name/disk-name/disk-id padding is governed by `--charcode`:
- `ASCII` (default): PETSCII shifted-space (`0xA0`) converted to plain
  ASCII space (`0x20`), filenames uppercased and padded with real spaces
  to 16 chars. Verified working end-to-end against real SDI hardware's
  own display.
- `PETASCII`: real on-disk bytes sent completely unmodified (`0xA0` and
  all). **Empirically confirmed NOT safe on this ROM** — `0xA0` lands in
  the Spectrum charset's UDG range (144-163) and renders as garbage
  (uninitialized UDG bitmap), not a blank, when printed via SDI's bare
  `RST $10`. Kept as a diagnostic option, not recommended.

Block counts and the disk name/ID/free-block-count are read straight
from the real `.d64`'s own directory/BAM structures (not estimated from
file size) — see `load_d64_image()` for the exact byte offsets
(directory entry block-count at offset 28-29; BAM disk name/ID/DOS-type
at `0x90`/`0xA2`/`0xA5` of track 18 sector 0; free blocks = sum of each
track's free-sector count at `0x04 + 4*track`).

## Byte stream: a plain file (`OPEN <ch> <name>`, name != `$`)

Just the file's raw bytes, verbatim, no framing/wrapping of any kind —
whatever's in the `.d64`'s file-chain contents or the loose `.prg` file.
