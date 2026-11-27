#!/usr/bin/env python3
"""
tos2tap.py — Konwerter plików Timex Operating System (TOS / FDD3000) do formatu .TAP.

Rozpoznaje 5- i 7-bajtowe nagłówki TOS (identycznie jak tos_ls.py), odcina je przed
utworzeniem bloków danych taśmy i przenosi oryginalne metadane (typ pliku, linię
autostartu BASIC, adres startowy CODE/SCREEN$, nazwę zmiennej DATA) do nagłówków .TAP.
Pliki bez nagłówka TOS (RAW/SUROWY) traktuje jako bloki CODE pod podanym adresem.

Dla plików typu BASIC (typ 0): domyślnie usuwa znak '*' TOS-owego rozszerzenia
dyskowego, gdy występuje bezpośrednio po tokenie komendy (LOAD/SAVE/CAT/MOVE/...),
POZA cudzysłowami i POZA liniami REM (REM bierze cały resztę linii jako surowy
tekst -- realne programy czasem chowają tam kod maszynowy, który może przypadkiem
zawierać te same bajty, patrz strip_line_asterisks()'s own comment). Dzięki temu
większość zwykłych programów BASIC (loadery gier itp.) da się uruchomić na zwykłym
Spectrum/taśmie -- NIE dotyczy programów, które do obsługi dysku używają kodu
maszynowego (PUTDAT/RDMEM/WRTMEM itp.), te nadal będą wymagać TOS-a. Wyłącz przez
--keep-stars.
"""

import os
import sys
import struct


def checksum(data: bytes) -> int:
    res = 0
    for b in data:
        res ^= b
    return res


def make_tap_block(flag: int, payload: bytes) -> bytes:
    body = bytes([flag]) + payload
    chk = bytes([checksum(body)])
    return struct.pack('<H', len(body) + 1) + body + chk


def make_header(filename: str, file_type: int, data_len: int, param1: int, param2: int) -> bytes:
    name_padded = filename[:10].ljust(10, ' ').encode('ascii', errors='replace')
    payload = struct.pack('<B10sHHH', file_type, name_padded, data_len, param1, param2)
    return make_tap_block(0x00, payload)


# Standard Sinclair BASIC token bytes for every TOS disk command that's
# immediately followed by a literal '*' in the tokenized line (per the
# timex-fdd-tos skill's command table). Values independently confirmed
# 2026-11-28 against a REAL extracted BASIC program (R-TYPE.DSK's own
# "START" autoloader): its real `LOAD *a$` / `LOAD *"..."` lines decode to
# token 0xEF (239) directly followed by 0x2A ('*'), matching the standard
# 48K/128K ROM token table exactly. OPEN#*/CLOSE#* are deliberately NOT
# included -- their '*' follows the literal '#' character, not the command
# token itself (see skill: "OPEN #*", "CLOSE #*" vs. "PRINT *#"/"INPUT *#"),
# a different (and rarer, channel-I/O-only) pattern not worth the extra
# lookback complexity for this feature's actual use case (game/program
# autoload stubs).
STAR_TOKENS = frozenset({
    171,  # ATTR
    207,  # CAT
    208,  # FORMAT
    209,  # MOVE
    210,  # ERASE
    213,  # MERGE
    214,  # VERIFY
    229,  # RESTORE
    233,  # DIM
    236,  # GO TO
    237,  # GO SUB
    238,  # INPUT
    239,  # LOAD
    240,  # LIST
    241,  # LET
    245,  # PRINT
    248,  # SAVE
    252,  # DRAW
})
REM_TOKEN = 234
QUOTE_BYTE = 0x22
STAR_BYTE = 0x2A


def strip_line_asterisks(tokens: bytes) -> bytes:
    """Removes a '*' byte immediately following a disk-command token,
    outside quoted strings, within ONE already-isolated BASIC line's own
    token bytes. A REM line's whole argument is raw, untokenized text (not
    re-parsed by the ROM at all) -- left completely untouched, since
    embedded binary data in a REM (a common real-world trick) can contain
    the exact same byte patterns by sheer coincidence (confirmed hitting
    this exact false-positive against R-TYPE's own real "START" file,
    which embeds machine code inside a REM line)."""
    if tokens and tokens[0] == REM_TOKEN:
        return tokens
    out = bytearray()
    in_quotes = False
    i = 0
    n = len(tokens)
    while i < n:
        b = tokens[i]
        if b == QUOTE_BYTE:
            in_quotes = not in_quotes
            out.append(b)
            i += 1
            continue
        if not in_quotes and b in STAR_TOKENS and i + 1 < n and tokens[i + 1] == STAR_BYTE:
            out.append(b)
            i += 2  # drop the '*'
            continue
        out.append(b)
        i += 1
    return bytes(out)


def strip_tos_asterisks(payload: bytes, prog_len: int):
    """Walks the real BASIC line structure ([line_num:2 BE][len:2 LE]
    [tokens...]) up to prog_len, stripping disk-command '*' markers line by
    line via strip_line_asterisks(), then appends the variables area
    (prog_len..end) untouched. Returns (new_payload, new_prog_len,
    stars_removed). Bails out (returns the input unmodified) on any
    structural inconsistency rather than risk corrupting a file it can't
    actually parse."""
    out = bytearray()
    pos = 0
    removed = 0
    n = len(payload)
    while pos < prog_len:
        if pos + 4 > n:
            return payload, prog_len, 0  # malformed -- don't risk it
        linelen = payload[pos + 2] | (payload[pos + 3] << 8)
        tok_start = pos + 4
        tok_end = tok_start + linelen
        if tok_end > n:
            return payload, prog_len, 0  # malformed -- don't risk it
        tokens = payload[tok_start:tok_end]
        new_tokens = strip_line_asterisks(tokens)
        removed += len(tokens) - len(new_tokens)
        out += payload[pos:pos + 2]  # line number, unchanged
        new_len = len(new_tokens)
        out += bytes([new_len & 0xFF, (new_len >> 8) & 0xFF])
        out += new_tokens
        pos = tok_end
    out += payload[prog_len:]  # variables area, always untouched
    new_prog_len = len(out) - (n - prog_len)
    return bytes(out), new_prog_len, removed


def parse_tos_file(raw: bytes, default_code_addr: int = 32768, strip_stars: bool = True):
    """
    Analizuje nagłówek Timex TOS (7 B dla BASIC, 5 B dla DATA/CODE/SCREEN$)
    i zwraca krotkę zgodną z nagłówkiem taśmy ZX Spectrum:
        (file_type, payload_bytes, param1, param2, label)
    """
    n = len(raw)

    # 1. Typ 0: BASIC — 7-bajtowy nagłówek:
    #    [0x00] [autostart_line:2 LE] [total_len:2 LE] [prog_len:2 LE]
    #    Kolejność zgodna z tos_ls.py (zweryfikowanym wczesniej w tej
    #    sesji): LINE jest PIERWSZE, nie total_len -- wczesniejsza wersja
    #    tego pliku miala te dwa pola zamienione, przez co kazdy prawdziwy
    #    program BASIC (np. prawdziwe R-TYPE/START) nie przechodzil
    #    walidacji dlugosci i trafial do fallbacku RAW->CODE.
    if n >= 7 and raw[0] == 0x00:
        line_num, total_len, prog_len = struct.unpack('<HHH', raw[1:7])
        if 7 + total_len == n and prog_len <= total_len:
            payload = raw[7:]
            stars_removed = 0
            if strip_stars:
                payload, prog_len, stars_removed = strip_tos_asterisks(payload, prog_len)
            # W .TAP brak autostartu to wartość >= 32768 (np. 32768 lub 0x8000)
            p1 = line_num if line_num < 32768 else 32768
            label = f"BASIC (LINE {line_num})" if line_num < 32768 else "BASIC"
            if stars_removed:
                label += f", -{stars_removed}*"
            return 0, payload, p1, prog_len, label

    # 2. Typ 1 i 2: DATA () / DATA $() — 5-bajtowy nagłówek:
    #    [0x01 lub 0x02] [data_len:2 LE] [var_byte:1] [0x80]
    if n >= 5 and raw[0] in (0x01, 0x02):
        data_len, var_byte, marker = struct.unpack('<HBB', raw[1:5])
        if 5 + data_len == n and marker == 0x80:
            ftype = raw[0]
            payload = raw[5:]
            # W nagłówku .TAP param1 ma nazwę zmiennej w starszym bajcie (var_byte << 8)
            p1 = var_byte << 8
            p2 = 32768
            var_char = chr((var_byte & 0x1F) + 64)
            suffix = "$" if ftype == 2 else ""
            return ftype, payload, p1, p2, f"DATA {var_char}{suffix}()"

    # 3. Typ 3: CODE / SCREEN$ — 5-bajtowy nagłówek:
    #    [0x03] [data_len:2 LE] [start_addr:2 LE]
    if n >= 5 and raw[0] == 0x03:
        data_len, start_addr = struct.unpack('<HH', raw[1:5])
        if 5 + data_len == n:
            payload = raw[5:]
            is_scr = (start_addr == 16384 and data_len == 6912)
            label = "SCREEN$ @16384" if is_scr else f"CODE @{start_addr}"
            return 3, payload, start_addr, 32768, label

    # 4. Brak rozpoznanego nagłówka TOS -> RAW / SUROWY (jako CODE pod adresem domyślnym)
    return 3, raw, default_code_addr, 32768, f"RAW->CODE @{default_code_addr}"


def convert_folder(input_dir: str, output_tap: str, load_addr: int = 32768, strip_stars: bool = True):
    tap_data = bytearray()
    files = sorted(f for f in os.listdir(input_dir) if os.path.isfile(os.path.join(input_dir, f)))

    if not files:
        print("Brak plików w katalogu:", input_dir)
        return

    for fname in files:
        if fname.startswith('.') or fname.lower().endswith(('.tap', '.py', '.png', '.md', '.txt')):
            continue

        filepath = os.path.join(input_dir, fname)
        with open(filepath, 'rb') as f:
            raw = f.read()

        # Zachowujemy pełną nazwę TOS (np. R-TYPE.SCR -> R-TYPE.SCR, obcięte do 10 znaków)
        clean_name = fname.lstrip('*').strip()
        tap_name = clean_name[:10]

        ftype, payload, p1, p2, label = parse_tos_file(raw, default_code_addr=load_addr, strip_stars=strip_stars)

        header_block = make_header(tap_name, ftype, len(payload), p1, p2)
        data_block = make_tap_block(0xFF, payload)

        tap_data.extend(header_block)
        tap_data.extend(data_block)

        print(f"Dodano: {tap_name:<10} | {label:<18} | dane={len(payload):<5} B | p1={p1:<5} | p2={p2}")

    with open(output_tap, 'wb') as out:
        out.write(tap_data)
    print(f"\nZapisano obraz taśmy: {output_tap} ({len(tap_data)} bajtów)")


if __name__ == '__main__':
    args = [a for a in sys.argv[1:] if a != '--keep-stars']
    keep_stars = len(args) != len(sys.argv) - 1
    if len(args) < 2:
        print("Użycie: python3 tos2tap.py [--keep-stars] <katalog_z_plikami> <wynik.tap> [adres_RAW_dec]")
        sys.exit(1)

    in_dir = args[0]
    out_file = args[1]
    addr = int(args[2]) if len(args) > 2 else 32768
    convert_folder(in_dir, out_file, load_addr=addr, strip_stars=not keep_stars)
