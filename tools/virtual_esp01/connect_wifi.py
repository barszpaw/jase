#!/usr/bin/env python3
"""One-shot AT+CWJAP provisioning for a real ESP-01.

Moon Rabbit (and any client relying on AT+CWAUTOCONN instead of polling
AT+CWJAP? itself) silently hangs forever on a module that has never joined
an AP -- it just waits for the unsolicited "WIFI CONNECTED"/"WIFI GOT IP"
lines real ESP-AT prints after a successful join, which never arrive on a
module with no saved credentials. This sends AT+CWJAP once so the module
saves the AP and auto-joins (and prints those lines) on every later boot.

Must be run while nothing else (e.g. virtual_esp01's own bridge mode) has
the serial port open -- it talks to the ESP-01 directly, no TCP bridge.

Usage: connect_wifi.py <ssid> <password> [dev] [baud]
"""
import sys
import time
import serial


def esp_escape(s: str) -> str:
    # ESP-AT requires backslash-escaping ',', '"' and '\' inside a quoted
    # string argument -- SSIDs/passwords containing any of those would
    # otherwise desync the command's own comma-separated-argument parsing.
    return s.replace("\\", "\\\\").replace(",", "\\,").replace('"', '\\"')


def send_and_wait(ser: serial.Serial, cmd: str, timeout: float) -> str:
    ser.reset_input_buffer()
    ser.write((cmd + "\r\n").encode())
    deadline = time.time() + timeout
    buf = b""
    while time.time() < deadline:
        chunk = ser.read(256)
        if chunk:
            buf += chunk
            if b"OK" in buf or b"ERROR" in buf or b"FAIL" in buf:
                break
    return buf.decode(errors="replace")


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <ssid> <password> [dev] [baud]")
        sys.exit(1)

    ssid, password = sys.argv[1], sys.argv[2]
    dev = sys.argv[3] if len(sys.argv) > 3 else "/dev/ttyUSB1"
    baud = int(sys.argv[4]) if len(sys.argv) > 4 else 115200

    print(f"Otwieranie {dev} ({baud} baud)...")
    ser = serial.Serial(dev, baudrate=baud, timeout=0.2)
    time.sleep(0.2)

    print("-> AT")
    resp = send_and_wait(ser, "AT", 3)
    print(resp)
    if "OK" not in resp:
        print("Brak odpowiedzi na AT -- sprawdz polaczenie/baudrate.")
        sys.exit(1)

    print("-> AT+CWMODE=1 (station mode)")
    print(send_and_wait(ser, "AT+CWMODE=1", 3))

    cmd = f'AT+CWJAP="{esp_escape(ssid)}","{esp_escape(password)}"'
    print(f"-> {cmd}")
    # Joining a real AP can take several seconds (scan + handshake), not
    # the ~OK-instantly timing every other AT command here gets.
    resp = send_and_wait(ser, cmd, 20)
    print(resp)

    if "WIFI GOT IP" in resp or "OK" in resp:
        print("Polaczono i zapisano -- powinno auto-joinowac od teraz.")
    else:
        print("Nie udalo sie polaczyc (sprawdz SSID/haslo).")
        sys.exit(1)

    ser.close()


if __name__ == "__main__":
    main()
