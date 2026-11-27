#!/usr/bin/env python3
"""
fdd3000_scp_receiver.py — interaktywny odbiornik plików z portu CH_A/CH_B
FDD3000 (src/Wd2123.h, --fdd3000-serial1-port/--fdd3000-serial2-port),
czekający na `MOVE *src TO ":CH_A"` / `SAVE *":CH_A"` wysłane z TOS.

Łączy dwie rzeczy znalezione podczas sesji debugowania 2026-11-27
(zob. tools/fdd3000_scp_xon_client.py):

  - CIĄGŁY handshake XON -- stacja czeka na dowolny bajt przychodzący
    (DcdDsr+RxReady na porcie 0x81) zanim zacznie/będzie kontynuować
    wysyłkę; bez tego transfer wisi w nieskończoność. Wartość bajtu nie
    ma znaczenia (potwierdzone empirycznie, zarówno z XON/XOFF=Y jak i
    =N skonfigurowanym przez FORMAT*) -- liczy się tylko to, że COŚ tam
    jest i COŚ wysyła, więc 0x11 (XON) jest używane tu tylko jako
    konwencjonalny, nieszkodliwy wybór.
  - rozpoznawanie 5/7-bajtowego nagłówka TOS SAVE* -- ta sama logika co
    tools/tos_ls.py (typ 0 BASIC / 1,2 DATA() / DATA$() / 3 CODE lub
    SCREEN$), użyta TUTAJ żeby policzyć oczekiwaną CAŁKOWITĄ długość
    pliku z samego nagłówka -- SCP to surowy strumień bajtów bez
    żadnego ramkowania/EOF na linii, więc inaczej nie wiedzielibyśmy,
    kiedy transfer się skończył. (Każda zmiana w tos_ls.py's nagłówkowej
    tabeli powinna trafić też tutaj.)

Dla nierozpoznanego typu (plik RAW, np. zapisany przez RDMEM/WRTMEM) nie
da się policzyć długości z nagłówka -- odbiornik kończy po
`--idle-timeout` sekundach ciszy na linii.

Użycie:
    python3 fdd3000_scp_receiver.py PORT [--out PLIK] [--host HOST]

Przykład (JASE uruchomiony z --fdd3000-serial1-port 10000, w TOS:
`MOVE *"R-TYPE.SCR" TO ":CH_A"`):
    python3 fdd3000_scp_receiver.py 10000
"""

import argparse
import socket
import struct
import sys
import termios
import time
import tty
from pathlib import Path

XON = b"\x11"


def decode_header(data: bytes):
    """Lustrzane odbicie tos_ls.py: zwraca (tname, info, expected_total).
    expected_total=None gdy nagłówek jeszcze niekompletny (więcej bajtów
    potrzeba) albo typ nierozpoznany (RAW, długości nie da się przewidzieć)."""
    if not data:
        return None, None, None

    ftype = data[0]

    # Typ 0: BASIC (7 bajtów: typ[1], line[2], total_len[2], prog_len[2])
    if ftype == 0:
        if len(data) < 7:
            return None, None, None
        line, total_len, _prog_len = struct.unpack("<HHH", data[1:7])
        info = f"LINE {line}" if line < 32768 else "brak LINE"
        return "BASIC", info, 7 + total_len

    # Typ 1/2: DATA tablica numeryczna/znakowa (5 bajtów: typ[1], len[2], var_id[2])
    if ftype in (1, 2):
        if len(data) < 5:
            return None, None, None
        dlen, var_id = struct.unpack("<HH", data[1:5])
        tname = "DATA ()" if ftype == 1 else "DATA $()"
        return tname, f"var={chr(var_id & 0x7F)}", 5 + dlen

    # Typ 3: CODE lub SCREEN$ (5 bajtów: typ[1], len[2], start[2])
    if ftype == 3:
        if len(data) < 5:
            return None, None, None
        clen, start = struct.unpack("<HH", data[1:5])
        tname = "SCREEN$" if (start == 16384 and clen == 6912) else "CODE"
        return tname, f"addr={start}", 5 + clen

    return "RAW/SUROWY", None, None


def wait_keypress(prompt):
    print(prompt, end="", flush=True)
    if not sys.stdin.isatty():
        # Piped/non-interactive stdin (scripting, automated testing) --
        # no real terminal to put in raw mode, just consume one byte.
        sys.stdin.read(1)
        print()
        return
    fd = sys.stdin.fileno()
    old = termios.tcgetattr(fd)
    try:
        tty.setraw(fd)
        sys.stdin.read(1)
    finally:
        termios.tcsetattr(fd, termios.TCSADRAIN, old)
    print()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", type=int, help="port CH_A/CH_B (np. --fdd3000-serial1-port)")
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--out", default=None, help="plik wyjściowy (domyślnie auto, wg rozpoznanego typu)")
    parser.add_argument("--xon-interval", type=float, default=0.01, help="co ile sekund odsyłać bajt gotowości")
    parser.add_argument("--idle-timeout", type=float, default=3.0,
                         help="dla plików RAW: ile sekund ciszy na linii = koniec transferu")
    args = parser.parse_args()

    wait_keypress("Press any Key to receive file...")

    s = socket.create_connection((args.host, args.port))
    s.settimeout(args.xon_interval)

    buf = bytearray()
    expected_total = None
    tname = None
    info = None
    last_xon = 0.0
    last_data = time.time()
    announced = False

    while True:
        try:
            chunk = s.recv(4096)
            if chunk:
                if not announced:
                    print("Incoming Data, please wait...")
                    announced = True
                buf.extend(chunk)
                last_data = time.time()
                if expected_total is None:
                    tname, info, expected_total = decode_header(bytes(buf))
                if expected_total is not None and len(buf) >= expected_total:
                    break
            elif chunk == b"":
                print("(polaczenie zamkniete przez TOS)")
                break
        except socket.timeout:
            if announced and expected_total is None and (time.time() - last_data) > args.idle_timeout:
                break  # RAW/nierozpoznany naglowek -- koniec po ciszy
        now = time.time()
        if now - last_xon > args.xon_interval:
            try:
                s.sendall(XON)
            except OSError:
                break
            last_xon = now

    if expected_total is not None and len(buf) > expected_total:
        buf = buf[:expected_total]  # odetnij ewentualny smiec po koncu pliku

    tname = tname or "RAW/SUROWY"
    size = len(buf)

    out_path = args.out
    if not out_path:
        ext = {"BASIC": "bas", "DATA ()": "dat", "DATA $()": "dat",
               "CODE": "cod", "SCREEN$": "scr"}.get(tname, "bin")
        out_path = f"received.{ext}"
    Path(out_path).write_bytes(bytes(buf))

    label = tname + (f" ({info})" if info else "")
    print(f"Received {size} bytes. Type of file: {label}")
    print(f"Saved to {out_path}")


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
