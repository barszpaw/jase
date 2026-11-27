#!/usr/bin/env python3
"""
tos_ls.py — Inspektor nagłówków plików Timex Operating System (TOS / FDD3000).

Skanuje pliki w bieżącym katalogu (zgrane np przez cpmtools lub dsk_browser), rozpoznaje 5- i 7-bajtowe nagłówki systemowe TOS dodawane
przez polecenie SAVE * i wyświetla tabelę z metadanymi ZX Spectrum:

  - Typ 0 (7 B nagłówka): BASIC   — linia autostartu (LINE), długość całkowita i kodu
  - Typ 1 (5 B nagłówka): DATA () — tablica numeryczna, nazwa zmiennej
  - Typ 2 (5 B nagłówka): DATA $()— tablica znakowa, nazwa zmiennej
  - Typ 3 (5 B nagłówka): CODE / SCREEN$ — adres ładowania w RAM (np. 16384) i rozmiar
  - Brak nagłówka:        RAW/SUROWY — pliki zapisane bezpośrednio (np. RDMEM/WRTMEM)

Użycie:
    python3 tos_ls.py

"""

import struct
from pathlib import Path

print(f"{'PLIK':<20} {'ROZMIAR':>7} {'TYP':<9} {'START / LINE':<16} {'DANE (B)':>8} {'NAGŁÓWEK (HEX)'}")
print("-" * 84)

for p in sorted(Path(".").iterdir()):
    if not p.is_file() or p.suffix == ".py":
        continue
    data = p.read_bytes()
    size = len(data)
    if size < 5:
        continue

    ftype = data[0]
    info = "-"
    tname = "RAW/SUROWY"
    payload = size
    hdr_hex = data[:7].hex(" ")

    # Typ 0: BASIC (7 bajtów: typ[1], line[2], total_len[2], prog_len[2])
    if ftype == 0 and size >= 7:
        line, total_len, prog_len = struct.unpack("<HHH", data[1:7])
        if 7 + total_len == size:
            tname = "BASIC"
            info = f"LINE {line}" if line < 32768 else "brak LINE"
            payload = total_len
            hdr_hex = data[:7].hex(" ")

    # Typ 1 / 2: DATA tablica numeryczna / znakowa (5 bajtów: typ[1], len[2], var_id[2])
    elif ftype in (1, 2):
        dlen, var_id = struct.unpack("<HH", data[1:5])
        if 5 + dlen == size:
            tname = "DATA ()" if ftype == 1 else "DATA $()"
            info = f"var={chr(var_id & 0x7F)}"
            payload = dlen
            hdr_hex = data[:5].hex(" ")

    # Typ 3: CODE lub SCREEN$ (5 bajtów: typ[1], len[2], start[2])
    elif ftype == 3:
        clen, start = struct.unpack("<HH", data[1:5])
        if 5 + clen == size:
            tname = "SCREEN$" if (start == 16384 and clen == 6912) else "CODE"
            info = f"addr={start}"
            payload = clen
            hdr_hex = data[:5].hex(" ")

    print(f"{p.name:<20} {size:>7} {tname:<9} {info:<16} {payload:>8} {hdr_hex}")
