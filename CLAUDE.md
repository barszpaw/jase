# CRITICAL SYSTEM INSTRUCTION: MEMORY RECOVERY
- After any context compaction, session reset, or when starting a complex task, ALWAYS read `MEMORY.md` before taking action.
- `MEMORY.md` contains the active state of the architecture (addresses, CBM IEC protocol state, SDI ROM hooks).
- Never guess state variables if they can be verified in `MEMORY.md`.


## Context & Memory Preservation Rules
1. **Memory Auto-Reload**: Whenever context compaction occurs, or if you feel you lack context about past architectural decisions, addresses, or bridge specifications, IMMEDIATELY read `MEMORY.md` before generating your response.
2. **Post-Compaction Check**: Always verify variables, Z80 memory addresses (e.g. $3500, $31B5), and SDI bridge specifications against `MEMORY.md` after any system summary.

## Custom Commands
- `restore-context`: Execute command to read `MEMORY.md`, read current `git diff`, and print current operational status of the SDI bridge work.

## SDI ROM Disassembly Workflow
- Po dokonaniu ostatecznej identyfikacji adresu procedur, skoków, danych lub
  tablic w ROM-ie SDI (np. przez live BREAK/STEP/REGS/MEM), uzupełnij
  tabelę `SYMBOLS` w `tools/sdi_disasm.py` — to jedyne miejsce, z którego
  nazwy symboliczne trafiają do `docs/VC1541/SDI103J_labeled_disasm.asm`
  (regenerowanego tym skryptem z logów INSTRTRACE, nie edytowanego ręcznie).
  Każdy wpis musi mieć komentarz wyjaśniający metodę potwierdzenia (live
  trace vs. statyczna dezasemblacja) i datę/sesję.

## Dandanator / Multiply Peripherals — status (sesja 2026-10-28)
- `src/Dandanator.h/.cpp` (bank-switch + protokół komend 1-49+52,
  `--dandanator`/`--dandanator-rom`, przycisk = F9 / `DANDANATOR BUTTON`
  w protokole debug) i `src/Multiply.h/.cpp` (fundament TX/RX: komenda
  52 Dandanatora + nibble-protokół na porcie 0x1F, `--multiply`) są
  ZAIMPLEMENTOWANE, zbudowane bez nowych warningów i przetestowane
  headless przez `--debug-port` (patrz testy w transkrypcie sesji
  d450f7d9 / memory poniżej — pełny round-trip Z80→Multiply→Z80
  potwierdzony).
- Pełne tło sprzętowe (schematy, protokół, źródła) i aktualny stan
  implementacji: auto-memory
  `project_jase_dandanator_multiply.md` (CZYTAJ PRZED kontynuacją tej
  pracy) + `docs/Dandanator/dandanator_multiply_notes.md` w repo
  (schematy, `reference_src/`).
- **Pułapka architektoniczna do zapamiętania**: peryferium, które musi
  WYGRAĆ wartość na współdzielonym porcie (nie tylko wywołać efekt
  uboczny, jak Multiface na 0x1F), musi być dopisane do `peripherals_`
  w `MinZX::init()` PRZED pluginem Kempston — inaczej Kempston (który
  rości sobie port 0x1F bezwarunkowo) cicho wygrywa każdy odczyt, bez
  błędu kompilacji ani crasha.
- Dwa nowe hooki w `Peripheral.h` powstały przy tej pracy i są dostępne
  dla przyszłych peryferiów: `onMemoryWrite()` (obserwacja zapisów do
  pamięci, której peryferium nie jest właścicielem — potrzebne dla
  Dandanatora) oraz `takeResetRequest()`/`takeNMIRequest()` (peryferium
  samo wyzwala reset/NMI, odpytywane raz na instrukcję).
- Brakuje: prawdziwy zestaw komend `CMD_ZX2SD_*` Multiply (przeglądanie
  katalogów SD, transfer Z80/SNA/TAP/romset — Multiply dziś odpowiada
  tylko na `REQ_ID`), Dan-SNAP (zamrażanie przez NMI), opóźnienie
  T-state'owe potwierdzeń komend, wbudowany Kempston Dandanatora, split
  na osobny plugin `.so`.
