#!/usr/bin/env python3
"""
fdd3000_scp_sender.py — wysyła plik na port CH_A/CH_B FDD3000
(src/Wd2123.h, --fdd3000-serial1-port/--fdd3000-serial2-port), symulując
drugi komputer podłączony przez RS232C.

2026-11-27, user-confirmed na żywo: `LOAD *":CH_A" ...` nie działa
(błąd "Wrong data type" niezależnie od opcji CODE/SCREEN$ -- przyczyna
niepotwierdzona). Kierunek, który FAKTYCZNIE odbiera dane, to
`MOVE *":CH_A" TO ":DISKNAME:NAZWA_PLIKU"` (SCP jako źródło, zapis na
dysk). Uwaga: MOVE* z SCP nie zna długości pliku z góry (w przeciwieństwie
do kopiowania dysk-dysk, gdzie długość jest w katalogu) -- jedyny sygnał
"koniec transmisji" to prawdopodobnie zerwanie połączenia (spadek
DCD/DSR), dlatego ten skrypt jawnie zamyka gniazdo po wysłaniu całego
pliku. Jeśli mimo to TOS kopiuje dalej aż do zapełnienia dysku, to
sygnał że FddStation/Wd2123 nie propaguje poprawnie zerwania łącza do
stanu MOVE-odbioru -- warto to zdebugować przez LIVEDISASM.

Samo nawiązanie połączenia TCP wystarcza za sprzętowy handshake (DCD/DSR,
zob. Fdd3000::acceptSerialIfIdle()/setCarrierPresent()) -- w przeciwieństwie
do kierunku ODBIORU (zob. fdd3000_scp_receiver.py/fdd3000_scp_xon_client.py),
wysyłanie NIE wymaga odpowiadania żadnym bajtem gotowości z naszej strony,
więc ten skrypt po prostu pcha bajty pliku w małych porcjach, drenując przy
okazji cokolwiek przyjdzie z powrotem (żeby nie zapchać bufora, gdyby TOS
coś odesłało).

Plik powinien mieć już prawdziwy nagłówek TOS SAVE* (typ[1] + reszta wg
typu, zob. tools/tos_ls.py) -- ten skrypt wysyła bajty 1:1, bez żadnego
dodawania/zdejmowania nagłówka.

Użycie:
    python3 fdd3000_scp_sender.py PORT PLIK [--host HOST] [--chunk BAJTY] [--delay SEKUNDY]

Przykład (JASE uruchomiony z --fdd3000-serial1-port 10000, w TOS:
`MOVE *":CH_A" TO ":EMPTY640:R-TYPE.SCR"`):
    python3 fdd3000_scp_sender.py 10000 tests/dsk_browser/R-TYPE.SCR
"""

import argparse
import socket
import sys
import time
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", type=int, help="port CH_A/CH_B (np. --fdd3000-serial1-port)")
    parser.add_argument("file", help="plik do wyslania (z prawdziwym naglowkiem TOS SAVE*, jesli dotyczy)")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--chunk", type=int, default=256, help="rozmiar porcji wysylanej za jednym sendall()")
    parser.add_argument("--delay", type=float, default=0.01, help="pauza (s) miedzy porcjami")
    args = parser.parse_args()

    data = Path(args.file).read_bytes()
    print(f"loaded {len(data)} bytes from {args.file}", flush=True)

    s = socket.create_connection((args.host, args.port))
    print(f"connected to {args.host}:{args.port}", flush=True)
    s.settimeout(args.delay)

    sent = 0
    last_log = time.time()
    while sent < len(data):
        try:
            # drenuj cokolwiek wraca (XON/ack/cokolwiek) zeby nie zapychac bufora
            s.recv(4096)
        except socket.timeout:
            pass
        chunk = data[sent:sent + args.chunk]
        s.sendall(chunk)
        sent += len(chunk)
        if time.time() - last_log > 0.5:
            print(f"sent {sent}/{len(data)}", flush=True)
            last_log = time.time()
        time.sleep(args.delay)

    print(f"done, sent {sent} bytes total", flush=True)

    # Jawne "odloz sluchawke" -- MOVE* z zrodla SCP nie zna dlugosci pliku z
    # gory (w przeciwienstwie do kopiowania dysk-dysk, gdzie dlugosc jest w
    # katalogu), wiec jedyny sygnal "koniec transmisji" to prawdopodobnie
    # zerwanie polaczenia (spadek DCD/DSR -- zob. Fdd3000::dropSerialClient()/
    # Wd2123::setCarrierPresent()). Samo wyjscie z Pythona zamyka gniazdo
    # tez, ale niedeterministycznie/z opoznieniem -- to ma byc jawne i od razu.
    s.shutdown(socket.SHUT_RDWR)
    s.close()
    print("connection closed (hangup)", flush=True)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
