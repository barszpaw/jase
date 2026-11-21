# Headless CP/M przez FDD3000 — jak uruchomić

Trzy niezależne sposoby dostępu do stacji FDD3000 przez TCP, bez ekranu
SDL. Wszystkie działają na tym samym `FddStation` (patrz `src/FddStation.h`),
więc stan stacji (dyski, CP/M) jest wspólny niezależnie od tego, którą
drogą się do niej dobierasz.

## 0. Build

```bash
make            # jase (in-process, z SDL)
make station    # fdd3000_station (standalone proces stacji, bez SDL)
```

## 1. Headless terminal CP/M (`MonitorLink`, `src/MonitorLink.h`)

Natywna, C++'owa reimplementacja protokołu linku hosta, jakiego używa
prawdziwy "Monitor Emulator for ZX SPECTRUM" (TMX Portugal 1986) do
rozmowy z CBIOS-em CP/M (`TXBY`/`RXBY`, port `#EF` na hoście). Zero kodu
Z80 na Spectrum, zero ROM-u interfejsu — podłącza się prosto do zatrzasku
linku `FddStation`. Protokół zweryfikowany live przez bezpośrednią
dezasemblację działającej pamięci stacji (`FDD3000 MEM`), nie zgadywany —
zobacz komentarz w `src/MonitorLink.h`.

```bash
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./jase \
  --fdd3000 \
  --fdd3000-station-rom fdd3boot.rom \
  --fdd3000-disk-a CPM-Boot_Disk_v2_2.dsk \
  --fdd3000-monitor-port 19905 \
  --debug-port 19902
```

Potem w drugim terminalu:

```bash
telnet 127.0.0.1 19905
```

Powinieneś od razu zobaczyć realny banner CP/M i prompt `A>`, bez
czekania na żaden boot TOS — stacja startuje prosto w CP/M. Przykładowa,
realnie przetestowana sesja:

```
A>DIR
A: MOVCPM   COM : PIP      COM : SUBMIT   COM : XSUB     COM
A: ED       COM : ASM      COM : DDT      COM : LOAD     COM
A: STAT     COM : SYSGEN   COM : DUMP     COM : BACKUP   COM
A: BIOS     ASM : FORMAT   COM : CBIOS    MAC : CONFIGUR COM
A>
```

`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy` tylko po to, żeby jase nie
próbował otworzyć prawdziwego okna/audio na maszynie bez X-a — nie jest
to wymagane, jeśli masz normalny desktop.

**Ważne ograniczenie**: nie uruchamiaj jednocześnie prawdziwego programu
Monitor na emulowanym Spectrum (np. przez `--load-tape monitor.zxs.tap`)
— oba mechanizmy pchają ten sam bajt linku, będą się bić.

Dostępne tylko w trybie in-process (nie pod `--fdd3000-bridge`) — zobacz
`Fdd3000::pumpMonitorSocket()`'s own comment.

### Ustawienie na stałe w `settings.json`

```json
{
  "peripherals": {
    "fdd3000": {
      "enabled": true,
      "stationRom": "fdd3boot.rom",
      "diskA": "CPM-Boot_Disk_v2_2.dsk",
      "monitorPort": 19905
    }
  }
}
```

## 2. WD2123 CH1/CH2 jako surowe porty TCP (`src/Wd2123.h`)

To dwa osobne, bajtowe kanały UART-opodobne stacji (porty `#80`/`#81`
CHA, `#40`/`#41` CHB — potwierdzone realnym źródłem, zob.
`docs/FDD3000/sources/loadzx-ti-technical-info.md`). Nie obsługują CP/M
console — to osobny protokół niż `MonitorLink` powyżej (prawdopodobnie
drukarka/AUX, zob. `CONFIGUR.COM`'s "RS232 channels A and B"). Wartość
dodana: pozwala testować/obserwować ruch na tych portach wprost przez
TCP, niezależnie od tego, co jeszcze robi stacja.

```bash
./jase --fdd3000 --fdd3000-station-rom fdd3boot.rom \
  --fdd3000-disk-a CPM-Boot_Disk_v2_2.dsk \
  --fdd3000-serial1-port 19903 \
  --fdd3000-serial2-port 19904
```

`settings.json`: klucze `serial1Port`/`serial2Port` w tej samej sekcji
`fdd3000` co wyżej.

## 3. Standalone proces stacji (`fdd3000_station`)

Ta sama `FddStation`, ale jako osobny proces bez SDL — przydatne, gdy
JASE (host Spectrum) ma żyć gdzie indziej (np. `--fdd3000-bridge`) albo
gdy w ogóle nie potrzebujesz hosta. Ma dziś CH1/CH2
(`--serial1-port`/`--serial2-port`) **i** `MonitorLink`
(`--monitor-port`) — ten sam headless terminal CP/M co w trybie
in-process, zweryfikowany tym samym testem (banner + `DIR`).

```bash
./fdd3000_station/fdd3000_station \
  --boot-rom fdd3boot.rom \
  --disk-a CPM-Boot_Disk_v2_2.dsk \
  --link-port 9600 --debug-port 9601 \
  --monitor-port 19905
```

potem `telnet 127.0.0.1 19905` — ten sam efekt co w trybie in-process
(sekcja 1 powyżej).

**Uwaga**: `--monitor-port` i `--link-port` (surowy bridge dla zdalnego
hosta JASE w `--fdd3000-bridge`) dzielą TEN SAM zatrzask linku —
włączenie `--monitor-port` wyłącza w tym procesie obsługę
`--link-port`/`pumpLinkSocket()` (gated w `main()`), żeby oba nie biły
się o ten sam bajt. Jeśli potrzebujesz prawdziwego mostu dla zdalnego
hosta, nie dawaj `--monitor-port`.

`--serial1-port`/`--serial2-port` (WD2123 CH1/CH2) działają niezależnie
od powyższego, można je łączyć z `--monitor-port` bez konfliktu:

```bash
./fdd3000_station/fdd3000_station \
  --boot-rom fdd3boot.rom \
  --disk-a CPM-Boot_Disk_v2_2.dsk \
  --link-port 9600 --debug-port 9601 \
  --monitor-port 19905 \
  --serial1-port 19903 --serial2-port 19904
```

Komenda debugowa (linia tekstowa, `nc 127.0.0.1 9601` albo dowolny
klient TCP): `STATUS`, `REGS`, `RESET`, `CHANGEDISK <drive> <path>`,
`MEM <hexaddr> <len>`, `POKE <hexaddr> <b1,b2,...>`, `STEP [n]`,
`LINKTRACE ON|OFF`, `WDTRACE ON|OFF` — pełna lista w
`fdd3000_station --help`.

## Diagnostyka

- `FDD3000` (bez argumentów) na `--debug-port` jasa: status stacji
  (PC, WD1772, link byte).
- `FDD3000 LINKTRACE ON`: loguje na stderr KAŻDY bajt na linku, obie
  strony (`side=HOST`/`side=STN`) — podstawowe narzędzie do diagnozowania
  protokołu, użyte do zweryfikowania `MonitorLink` powyżej.
- `FDD3000 MEM <hexaddr> <len>`: zrzut pamięci stacji — tak faktycznie
  zweryfikowano `src/MonitorLink.h`'ego protokół (bezpośrednia
  dezasemblacja żywego CBIOS-u, nie zgadywanie z dezasemblacji statycznej).
