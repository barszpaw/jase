#!/usr/bin/env python3
"""
fdd3000_scp_xon_client.py — testowy klient TCP dla portu CH_A/CH_B FDD3000
(src/Wd2123.h, --fdd3000-serial1-port/--fdd3000-serial2-port).

TOS sprawdza DWA uzgodnienia przed wysłaniem pliku przez MOVE *src TO ":CH_A"
("TOS uses software and hardware protocols to ensure that the transmitter
only sends data when the receiver is ready" -- fdd3000_user_manual.pdf,
rozdz. 6.3):
  - sprzętowe (DCD/DSR) -- spełnione samym nawiązaniem połączenia TCP
    (Fdd3000::acceptSerialIfIdle() ustawia je przez setCarrierPresent()),
  - programowe (XON/XOFF) -- stacja czeka, aż odbiorca przyśle bajt XON
    (0x11) zanim wyśle kolejną porcję danych; to jest prawdziwy
    stop-and-wait, nie jednorazowy handshake, więc trzeba odpowiadać
    CIĄGLE, nie tylko raz na starcie.

Zwykły pasywny listener (tylko recv(), nigdy send()) zawiesza transfer na
zawsze -- stacja kręci się w pętli czekając na RxReady, który nigdy nie
nadejdzie. Ten skrypt wysyła XON w pętli (domyślnie co 10ms) i zapisuje
wszystko, co odbierze, do pliku -- użyty do zweryfikowania naprawy skew=7
w TosFs (zob. src/TosFs.cpp) przez odebranie R-TYPE.SCR przez MOVE* i
porównanie sha256 z referencją.

Użycie:
    python3 fdd3000_scp_xon_client.py PORT [--out PLIK] [--host HOST] [--interval SEKUNDY]

Przykład (odbiór R-TYPE.SCR wysłanego przez `MOVE *"R-TYPE.SCR" TO ":CH_A"`,
z JASE uruchomionym jako `--fdd3000-serial1-port 10000`):
    python3 fdd3000_scp_xon_client.py 10000 --out rtype_received.scr
"""

import argparse
import socket
import sys
import time

XON = b"\x11"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", type=int, help="port CH_A/CH_B (np. --fdd3000-serial1-port)")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--out", default=None, help="plik na odebrane bajty (domyślnie wd2123_recv_<port>.log)")
    parser.add_argument("--interval", type=float, default=0.01, help="co ile sekund wysyłać XON (domyślnie 10ms)")
    args = parser.parse_args()

    out_path = args.out or f"wd2123_recv_{args.port}.log"

    s = socket.create_connection((args.host, args.port))
    print(f"connected to {args.host}:{args.port}, writing to {out_path}", flush=True)
    s.settimeout(args.interval)

    last_xon = 0.0
    total = 0
    with open(out_path, "wb") as f:
        while True:
            try:
                data = s.recv(4096)
                if data:
                    f.write(data)
                    f.flush()
                    total += len(data)
                    print(f"recv {len(data)} bytes, total={total}", flush=True)
                elif data == b"":
                    print("connection closed", flush=True)
                    break
            except socket.timeout:
                pass
            now = time.time()
            if now - last_xon > args.interval:
                s.sendall(XON)
                last_xon = now


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
