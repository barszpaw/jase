# FDD35 / FDD3000 — specyfikacja dla agenta implementującego peryferium JASE

## Cel

Zaimplementować stację FDD35/FDD3000 jako peryferium JASE, zgodnie ze szkieletem `Peripheral`, podobnie jak DivIDE integruje się z `MinZX`. Stacja jest jednak samodzielnym komputerem: peryferium zawiera drugi procesor Z80 z własną pamięcią i lokalnymi urządzeniami I/O. Nie implementować protokołu TOS ani CP/M jako logiki wysokiego poziomu. Uruchamiać firmware na Z80 stacji i emulować urządzenia, z którymi ten firmware rozmawia.

## Zasada architektoniczna

```text
MinZX / ZX Spectrum
  └── Fdd3000 : Peripheral
       ├── ZX-side interface (ROM/RAM paging + port 0xEF)
       ├── two byte latches (host→station, station→host)
       └── station computer
            ├── independent Z80 CPU
            ├── private 64 KiB address space
            ├── station I/O decoder
            ├── WD1770-class FDC
            └── drives backed by disk images
```

CPU ZX pozostaje własnością `MinZX`. CPU stacji, jego rejestry i pamięć są prywatne dla peryferium. Procesory nie współdzielą RAM. Jedyną drogą komunikacji host–stacja jest sprzętowe łącze reprezentowane przez dwa zatrzaski bajtowe.

## Ważne rozróżnienie źródeł

Dołączony `FDD35.sch.pdf` i `PLD_equations.md` opisują **płytę stacji FDD35**, w szczególności dekoder pamięci/I/O jej własnego Z80. Nie opisują dekodera ROMCS ani portu ZX. Stronę ZX należy oprzeć na dokumentacji interfejsu FDD3000 i istniejącej analizie ROM-u interfejsu. Nie przenosić dekodowania station-side GAL na porty ZX.

## 1. Magistrala ZX — host Spectrum ↔ interfejs FDD3000

| Zasób | Dekodowanie / zachowanie | Uwagi implementacyjne |
|---|---|---|
| Port łącza hosta | `IN`/`OUT` na porcie `0xEF` (dekodowanie niskiego bajtu portu) | `OUT`: zapisz cały bajt do zatrzasku host→stacja. `IN`: odczytaj zatrzask stacja→host. Nie interpretuj tu pakietów TOS. |
| ROM interfejsu | ROMCS mapuje obszar `0x0000–0x1FFF` | Wariant ROM 8 KiB jest właściwym pełnym obrazem. ROMCS włącza się przed pobraniem instrukcji, gdy PC hosta wynosi `0x0000` lub `0x0008`; wyłącza się przy PC `0x0604`, również przed pobraniem instrukcji. Zachować tę kolejność czasową. |
| RAM interfejsu | Obszar interfejsu zaczyna się pod `0x2000`; dokumentowany układ ma 1 KiB RAM (`0x2000–0x23FF`) | Nie zakładać, że cała strona 8 KiB jest fizycznym RAM-em. Jeśli API `MemoryMap` mapuje całe 8 KiB, doprecyzować zachowanie aliasów/pozostałej przestrzeni na podstawie schematu interfejsu. |
| Pozostała pamięć hosta | Zwykła pamięć ZX po zwolnieniu ROMCS | Przy page-out przywrócenie ROM/RAM zależy od aktualnej konfiguracji maszyny (m.in. 128K); nie zakodować na stałe standardowego ROM-u 48K. |

### Kontrakt `Peripheral` po stronie ZX

- `name()` zwraca `"FDD3000"` (lub uzgodnioną nazwę produktu).
- `in(port, value)` i `out(port, value)` dekodują tylko stronę hosta, w szczególności port `0xEF`.
- `onInstructionFetch(pc, memory, tstates)` realizuje pułapkowanie PC i mapowanie interfejsu ROM/RAM.
- `onInstructionExecuted(memory, tstates)` wykonuje krokowanie stacji po wykonaniu instrukcji hosta.
- `debugCommand()` udostępnia diagnostykę CPU stacji, łącza, FDC i napędów.

## 2. Magistrala pamięci stacji — lokalny Z80 FDD35

Poniższa mapa jest odczytem równań GAL z `PLD_equations.md`. `MREQ`, `IORQ`, `RD`, `WR` i `RFSH` w równaniach są sygnałami aktywnymi po uwzględnieniu inwersji zapisanej w deklaracjach pinów. Wyjścia `/ROMCS`, `/RAMCS0`, `/RAMCS1`, `/FDC` itd. są aktywne stanem niskim. Dla implementacji wygodnie przeliczyć je na logiczne `selected=true`.

### Pamięć stacji

| Zakres adresów Z80 stacji | Układ | Warunek / opis |
|---|---|---|
| `0x0000–0x1FFF` | ROM, wybór `/ROMCS` | Tylko cykl odczytu pamięci (`MREQ`, bez refresh), przy aktywnym `BOOT`. |
| `0x0000–0x7FFF` | RAM0, 32 KiB | `A15=0`. Zapis zawsze wybiera RAM0, również pod overlay ROM. Odczyt z `0x0000–0x1FFF` wybiera RAM0, gdy ROM overlay nie jest aktywny; przy aktywnym ROM overlay wybiera ROM. |
| `0x8000–0xFFFF` | RAM1, 32 KiB | `A15=1`; odczyt/zapis pamięci, bez cyklu refresh. |

Równania GAL, do zachowania przy implementacji:

```text
FMREQ  = MREQ & !RFSH
ROMCS  = FMREQ & BOOT & RD & !A15 & !A14 & !A13
RAMCS0 = FMREQ & (((!BOOT # A14 # A13) & RD) # WR) & !A15
RAMCS1 = FMREQ & (RD # WR) & A15
```

**Rozbieżność do rozstrzygnięcia przed finalizacją ROM-u:** równania stacji dekodują okno ROM `8 KiB`, a znany boot ROM FDD3000 i obecna implementacja referencyjna używają ROM-u startowego o rozmiarze `2 KiB` (`0x0000–0x07FF`). Nie utożsamiać pojemności/okna wybranego przez GAL z pojemnością właściwego ROM-u. Zweryfikować, jak linie adresowe i `/BOOT` są podłączone w konkretnym wariancie FDD35 oraz czy ROM jest lustrzany w wybranym oknie.

## 3. Porty I/O lokalnego Z80 stacji

Dekoder opiera się na `A5–A7`, a więc niższe bity adresu są częściowo ignorowane. Zakresy poniżej podano dla dolnego bajtu portu; odpowiadają równaniom PLD.

| Zakres portów stacji | Sygnał GAL | Kierunek | Funkcja / wymagane zachowanie |
|---|---|---|---|
| `0x00–0x1F` | `RS3` | OUT | WD2123 **konfiguracja** (write-only), per-kanał: `#10`=CHA, `#11`=CHB (bit0 portu wybiera kanał) -- POTWIERDZONE, zob. `docs/FDD3000/sources/loadzx-ti-technical-info.md`. Zawartość rejestru (format/baud) nieznana. |
| `0x20–0x3F` | `COUT` / `CIN` | OUT / IN | Łącze host–stacja. OUT stacji zapisuje zatrzask stacja→host; IN stacji czyta zatrzask host→stacja. Przy IN bit 7 odzwierciedla lokalny sygnał DRQ FDC, a bity 0–6 pochodzą z zatrzasku host→stacja. |
| `0x40–0x5F` | `RS2` | IN/OUT | WD2123 CHB: `#40` data, `#41` status(IN)/command(OUT) -- POTWIERDZONE (to samo źródło jak wyżej). Znaczenie bitów command/status nieznane. |
| `0x60–0x7F` | brak równania wyboru | IN/OUT | Brak osobnego wyboru w przedstawionym PLD; zachowanie elektryczne/odczyt nieokreślone tym dokumentem. |
| `0x80–0x9F` | `RS1` | IN/OUT | WD2123 CHA: `#80` data, `#81` status(IN)/command(OUT) -- POTWIERDZONE, w równaniu A5 jest nieużywane. |
| `0xA0–0xBF` | brak równania wyboru | IN/OUT | Brak osobnego wyboru w przedstawionym PLD. |
| `0xC0–0xDF` | `FDC` | IN/OUT | WD1770. `port & 3`: `0` status przy IN / command przy OUT, `1` track, `2` sector, `3` data. Pozostałe niskie bity są aliasami. |
| `0xE0–0xFF` | `PE0` | OUT | Zapis do zatrzasku U4, używany jako rejestr konfiguracji stacji/napędu. Odczyt nie jest wybierany przez to równanie. |

Równania I/O:

```text
RS3 = IORQ & WR & !A7 & !A6 & !A5
COUT = IORQ & WR & !A7 & !A6 & A5
CIN  = IORQ & RD & !A7 & !A6 & A5
RS2 = IORQ & (RD # WR) & !A7 & A6
RS1 = IORQ & (RD # WR) & A7 & !A6
FDC = IORQ & (RD # WR) & A7 & A6 & !A5
PE0 = IORQ & WR & A7 & A6 & A5
```

Bit-layout rejestru `PE0` z dokumentacji sprzętowej używanej przez istniejący model:

| Bit | Znaczenie |
|---|---|
| 0–3 | wybór napędu aktywnym zerem; pierwszy wyzerowany bit wybiera napęd A–D |
| 4 | wybór strony: `0` = strona B, `1` = strona A (`/SIDE_B`) |
| 5 | gęstość: `0` = double density (`/DDEN`) |
| 6 | `/BOOTROM`: `0` włącza ROM startowy stacji, `1` wyłącza overlay |
| 7 | `/HEAD`: aktywne zero oznacza załadowaną głowicę; wymagania czasowe nieustalone |

Przed implementacją rejestru `PE0` potwierdzić polaryzację z rzeczywistym firmware'em/schematem. Dekoder PLD i znaczenia bitów pochodzą z różnych warstw dokumentacji.

## 4. Złącze i link host–stacja

Schemat FDD35 pokazuje łącze przez złącze D-Sub15. Dostępne linie danych mają pozycje bitowe `{0,1,2,3,4,6}`; pozycje 5 i 7 nie są liniami danych tego kabla. Pin 15 jest sygnałem sterującym odczytem I/O, nie bitem danych. Nie implementować tego jako dodatkowego protokołu bajtowego w C++: firmware obu CPU realizuje handshake programowo na prostych zatrzaskach.

Model logiczny zatrzasków:

```text
host OUT 0xEF  -> latchHostToStation
station IN block 1 -> latchHostToStation | (FDC.DRQ ? 0x80 : 0)
station OUT block 1 -> latchStationToHost
host IN 0xEF -> latchStationToHost
```

Zatrzaski są niezależne, zachowują ostatnio zapisany bajt i same nie realizują ACK, timeoutów ani dekodowania komend.

## 5. Cykl pracy dwóch CPU

1. `MinZX` pobiera/wykonuje instrukcję hosta.
2. `Fdd3000::onInstructionExecuted()` aktualizuje czas stacji i wykonuje należne instrukcje drugiego Z80.
3. Dostępy I/O stacji trafiają do lokalnego dekodera; WD1770 jest taktowany według zegara stacji.
4. Stan zatrzasków i linii DRQ/INTRQ jest widoczny drugiemu CPU przy jego następnym odczycie.

Nie używać zegara ściennego ani osobnego wątku dla CPU stacji. Nie zakładać stosunku taktów bez pomiaru/źródła: schemat nie definiuje częstotliwości ani harmonogramu emulacji. Używać hooka `onInstructionExecuted`, nie `onInstructionFetch`, aby krok stacji następował po wykonaniu instrukcji hosta.

## 6. Proponowany szkielet klas (projekt, nie gotowy kod)

```cpp
class Fdd3000 : public Peripheral {
public:
    const char* name() const override;
    bool in(uint16_t port, uint8_t& value) override;
    bool out(uint16_t port, uint8_t value) override;
    void onInstructionFetch(uint16_t pc, MemoryMap& memory,
                            uint32_t tstates) override;
    void onInstructionExecuted(MemoryMap& memory,
                               uint32_t tstates) override;
    std::string debugCommand(
        const std::vector<std::string>& toks) override;

private:
    // ZX-side interface: ROM/RAM banks, ROMCS state and PC paging.
    InterfaceState interface_;

    // Two independent byte latches; no protocol parsing here.
    uint8_t hostToStation_ = 0;
    uint8_t stationToHost_ = 0;

    // Station-side machine with its own CPU, 64 KiB address space,
    // boot-ROM overlay and station I/O decoder.
    FddStation station_;

    // WD1770, drives and disk-image backing store may be owned by
    // FddStation or composed as explicit members.
};
```

Nazwy `InterfaceState` i `FddStation` są propozycją podziału odpowiedzialności, nie wymaganiem dodawania osobnych plików. Minimalna wersja może trzymać te pola bezpośrednio w `Fdd3000`, pod warunkiem utrzymania wyraźnej granicy między stroną ZX a komputerem stacji.

## 7. Integracja z JASE

- Instancja jest tworzona i dodawana do listy `peripherals_` w `MinZX::init()`.
- `MinZX` wywołuje `in()`/`out()` dla cykli portów hosta oraz hooki instrukcji.
- Stronicowanie ma używać `MemoryMap` i mechanizmu ROMCS zgodnego z innymi peryferiami; po zwolnieniu ROMCS JASE musi odtworzyć aktualne mapowanie maszyny.
- Stacja zachowuje własny reset (`stationReset`) niezależny od resetu ZX, jeśli takie sterowanie ma pozostać dostępne.
- Debug command ma raportować co najmniej: aktywność peryferium/ROMCS, PC i stan CPU stacji, link latches, wybór napędu/strony, status/DRQ/INTRQ WD1770 oraz obrazy w napędach.
- Konflikty portów ZX rozwiązuje istniejąca kolejność w `peripherals_`; nie zmieniać globalnej semantyki `Peripheral` dla potrzeb FDD.

## 8. Fakty potwierdzone i kwestie otwarte

### Potwierdzone w dostarczonych materiałach

- FDD35 to klon stacji FDD3/FDD3000.
- Stacja ma własny Z80, RAM, ROM, GAL i WD1770.
- GAL wybiera ROM/RAM wg `A13–A15`, sygnałów cyklu pamięci i `/BOOT`.
- Port stacji jest dekodowany blokami według `A5–A7`; FDC zajmuje blok `0xC0–0xDF`, a `PE0` blok `0xE0–0xFF` dla OUT.
- Port ZX `0xEF`, mapowanie interfejsu i trap-PC pochodzą z dokumentacji interfejsu FDD3000, nie z równań stacji FDD35.
- **2026-11-20**: `RS1`/`RS2`/`RS3` to WD2123 (DUART), POTWIERDZONE przez niezależne, opublikowane źródło (Jarek Adamski, "The FDD Interface", loadzx.com/timexcomputerworld/ti-technical-info/ — zobacz `docs/FDD3000/sources/loadzx-ti-technical-info.md`): `#80`/`#81`=CHA data/status-command (`RS1`), `#40`/`#41`=CHB data/status-command (`RS2`), `#10`/`#11`=CHA/CHB configuration write-only (`RS3`, dotąd mylnie modelowane jako jeden wspólny rejestr bez podziału na kanał). Te samo źródło nazywa wprost `#10`/`#11` "(write)" i mówi "I still wait for the info" o ich zawartości — więc dokładny bit-layout configuration/status/command wciąż nieznany.

### Nie zgadywać — zweryfikować przed uznaniem emulacji za wierną

- ROM 2 KiB kontra okno dekodera 8 KiB; linie adresowe ROM i lustrzane obszary.
- Dokładny aliasing RAM interfejsu ZX powyżej `0x23FF`.
- Bit-layout rejestru configuration (`#10`/`#11`) i bitów command (`#81`/`#41` przy OUT) WD2123 — port map potwierdzony (patrz wyżej), zawartość rejestrów nie.
- Wpływ bitów 5 i 7 rejestru `PE0` oraz moment przełączania ROM przez bit 6.
- Różnice WD1770 względem WD1772, szczególnie status, timing, DRQ/INTRQ i komendy używane przez ROM stacji.
- Częstotliwość Z80 stacji i proporcja jej krokowania do CPU hosta.
- Elektryczne/timingowe zachowanie kabla; schemat wystarcza do logicznego modelu zatrzasków, nie do analogowej symulacji magistrali.

## Materiały wejściowe

- `PLD_equations.md` — równania GAL22V10 FDD35.
- `FDD35.sch.pdf` — schemat płyty stacji FDD35; BOM wskazuje Z80A, RAM 62256, ROM 27C256, GAL22V10 i WD1770.
- `README.md` — zakres projektu FDD35 i licencja dokumentacji.
- `src/Peripheral.h`, `src/DivIde.h/.cpp`, `src/Fdd3000.h/.cpp` — wzorce JASE oraz istniejąca implementacja; przed zmianą kodu agent ma sprawdzić ich aktualny stan.

Dokumentacja FDD35 jest oznaczona CC BY-NC-SA 4.0, o ile nie zaznaczono inaczej. Projekt integracyjny powinien zachować atrybucję źródeł; licencja kodu JASE i istniejące ustalenia dla FDD3000 pozostają osobną kwestią.
