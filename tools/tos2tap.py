#!/usr/bin/env python3
"""
tos2tap.py — Konwerter plików Timex Operating System (TOS / FDD3000) do formatu .TAP.

Rozpoznaje 5- i 7-bajtowe nagłówki TOS (identycznie jak tos_ls.py), odcina je przed
utworzeniem bloków danych taśmy i przenosi oryginalne metadane (typ pliku, linię
autostartu BASIC, adres startowy CODE/SCREEN$, nazwę zmiennej DATA) do nagłówków .TAP.
Pliki bez nagłówka TOS (RAW/SUROWY) traktuje jako bloki CODE pod podanym adresem.
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


def parse_tos_file(raw: bytes, default_code_addr: int = 32768):
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
            # W .TAP brak autostartu to wartość >= 32768 (np. 32768 lub 0x8000)
            p1 = line_num if line_num < 32768 else 32768
            label = f"BASIC (LINE {line_num})" if line_num < 32768 else "BASIC"
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


def convert_folder(input_dir: str, output_tap: str, load_addr: int = 32768):
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

        ftype, payload, p1, p2, label = parse_tos_file(raw, default_code_addr=load_addr)

        header_block = make_header(tap_name, ftype, len(payload), p1, p2)
        data_block = make_tap_block(0xFF, payload)

        tap_data.extend(header_block)
        tap_data.extend(data_block)

        print(f"Dodano: {tap_name:<10} | {label:<18} | dane={len(payload):<5} B | p1={p1:<5} | p2={p2}")

    with open(output_tap, 'wb') as out:
        out.write(tap_data)
    print(f"\nZapisano obraz taśmy: {output_tap} ({len(tap_data)} bajtów)")


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Użycie: python3 tos2tap.py <katalog_z_plikami> <wynik.tap> [adres_RAW_dec]")
        sys.exit(1)

    in_dir = sys.argv[1]
    out_file = sys.argv[2]
    addr = int(sys.argv[3]) if len(sys.argv) > 3 else 32768
    convert_folder(in_dir, out_file, load_addr=addr)
