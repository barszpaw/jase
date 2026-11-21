# Źródło: loadzx.com "TI – Technical Info"

- URL: https://loadzx.com/timexcomputerworld/ti-technical-info/
- Autor sekcji "The FDD Interface": Jarek Adamski (© 2019-2024 Museum LOAD ZX Spectrum)
- Pobrane: 2026-11-20, przez `curl` + ręczne usunięcie HTML (strona jest
  zwykłym WordPressem, brak JS wymagany do treści).
- Powiązane strony tej samej witryny, sprawdzone przy tej samej okazji:
  `timexcomputerworld/tos-secrets/` (protokół TOS po stronie hosta — brak
  nowych faktów dla WD2123/stacji, tylko potwierdza już znane
  #0000/#0008/#0604, RAM #2000-#23FF, nazwy kanałów `:CH_A`/`:CH_B` przy
  komendzie 21 "RS232C formatting"), `timexcomputerworld/rom-disassembly/`
  (dezasemblacja ROM-u INTERFEJSU, nie stacji — nie dotyka portów WD2123),
  `timexcomputerworld/timex-interface-rs232/` (INNY produkt — bit-banged
  RS232 bez kontrolera UART, niezwiązany z FDD3000/WD2123).

## Mapa portów interfejsu (strona ZX, port #EF)

Cytat bezpośredni:

> Inside FDD is it port #0F, and MIRRORED: #2F, #4F, #6F, #8F, #AF, #CF, #EF.

| Bit | READ | WRITE |
|---|---|---|
| 7 | DREQ FDC 1772 ? | — |
| 6 | TI BIT 6 | TI BIT 6 |
| 5 | DREQ FDC 1772 ? | — |
| 4 | TI BIT 4 OR 7 (zależy od wersji TI, standard=4) | jak READ |
| 3 | TI BIT 3 | TI BIT 3 |
| 2 | TI BIT 2 | TI BIT 2 |
| 1 | TI BIT 1 | TI BIT 1 |
| 0 | TI BIT 0 | TI BIT 0 |

(Dotyczy strony ZX/interfejsu — JASE już to ma zaimplementowane jako port
`0xEF`, nie dotyczy bezpośrednio stacji/WD2123.)

## Mapa portów STACJI (realny hardware, nie ZX!) — KLUCZOWE dla `src/Wd2123.h`

Cytat bezpośredni ("INSIDE FDD – PORTS, and SHORT this PORT INSIDE CHIP
STERRED WHAT IT DO?"):

```
#10 WD2123 configuration (write) CHA
#11 WD2123 configuration (write) CHB
#40 WD2123 data CHB
#41 WD2123 status/command CHB
#80 WD2123 data CHA
#81 WD2123 status/command CHA
#C0 FDC1770 steering port/status
#C1 FDC1770 track port
#C2 FDC1770 sector port
#C3 FDC1770 data port
#E0 HELPING helping port(below)
```

Autor wprost pisze o rejestrach configuration/status-command: *"How to use
these ports? I still wait for the info…"* — czyli MAPA PORTÓW jest
potwierdzona, ale ZAWARTOŚĆ (bit-layout) rejestrów configuration i
command/status WD2123 NIE jest znana z tego źródła. Nie zgadywać dalej niż
to, co jest tu napisane.

Zastosowanie w `src/Wd2123.h`/`src/FddStation.cpp` (2026-11-20):
PLD's `RS1`=`#80`-`#9F` → CHA (nasz `ch=0`), `RS2`=`#40`-`#5F` → CHB
(`ch=1`), `RS3`=`#00`-`#1F` → configuration, bit0 portu wybiera
CHA(`#10`)/CHB(`#11`) — **poprawka** względem wcześniejszego zgadywania w
tym pliku (RS3 modelowany jako jeden wspólny rejestr bez podziału na
kanał, co to źródło obala).

## Port #E0 (bit-layout) — cross-check z istniejącym `writeConfigRegister()`

Cytat:

```
#E0 HELPING helping port(below)
BIT WRITE 1 0
7 HEAD DOWN UP
6 #0000-#1FFF ROM RAM
5 DENSITY FM MFM
4 SIDE 1 0
3 DRIVE 3 ON OFF
2 DRIVE 2 ON OFF
1 DRIVE 1 ON OFF
0 DRIVE 0 ON OFF
```

Zgadza się z `FddStation::writeConfigRegister()` (bit6=/BOOTROM, bit5=
/DDEN, bit4=side, bity0-3=drive select aktywnym zerem) — niezależne
potwierdzenie, że ten rejestr jest zaimplementowany poprawnie. Jedna
drobna niejasność: to źródło opisuje bit7 jako HEAD DOWN(1)/UP(0), a nasz
kod go nie modeluje (`Wd1772Fsm`'s own simplification list) — bez zmian,
tylko odnotowane.

## Fakt NIEZWIĄZANY z WD2123, znaleziony przy tej samej okazji (nie zaimplementowany)

Dla ZX Spectrum 128K/+2: interfejs musi wykonać `OUT #7FFD,#10` PRZED
aktywacją (żeby kod 128K nie czytał z ROM0). Dla +3: dodatkowo
`OUT #1FFD,#04`. `src/Fdd3000.cpp` tego NIE robi dziś (sprawdzone grepem,
2026-11-20) — potencjalna, osobna poprawka zgodności z 128K/+3, poza
zakresem tej sesji.
