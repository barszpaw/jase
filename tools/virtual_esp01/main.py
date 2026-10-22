#!/usr/bin/env python3
import sys
import socket
import select
import serial

remote_fd = None  # Używane tylko w trybie emulacji (--emulate)
wifi_announced = False  # czy już wysłano niezapowiedziane "WIFI GOT IP"


def log_hex(direction: str, data: bytes):
    """Pomocnicza funkcja do ładnego formatowania logów TEXT + HEX."""
    text_repr = data.decode("utf-8", errors="replace").replace("\r", "\\r").replace("\n", "\\n")
    hex_repr = " ".join(f"{b:02X}" for b in data)
    print(f"virtual_esp01: {direction:<10} | TXT: \"{text_repr}\" | HEX: [{hex_repr}]", flush=True)


def parse_cip_start(args: str):
    p1 = args.find(",")
    if p1 == -1:
        return None, None
    p2 = args.find(",", p1 + 1)
    if p2 == -1:
        return None, None

    host = args[p1 + 1 : p2]
    if host.startswith('"'):
        host = host[1:]
    if host.endswith('"'):
        host = host[:-1]

    port_str = args[p2 + 1 :]
    try:
        port = int(port_str.strip())
        if port > 0:
            return host, port
    except ValueError:
        pass

    return None, None


def handle_emulated_command(client_sock: socket.socket, raw_line: bytes, line_buf: bytearray) -> bytearray:
    """Obsługa poleceń w trybie czystej emulacji (bez fizycznego ESP)."""
    global remote_fd, wifi_announced

    log_hex("<- RECV", raw_line)

    line_str = raw_line.decode("utf-8", errors="replace")
    cmd = line_str.rstrip("\r\n")

    def send_line(msg: str):
        data = msg.encode("utf-8")
        log_hex("-> SEND", data)
        client_sock.sendall(data)

        # Wyłapanie sekwencji +++ (nawet jeśli ktoś dokleił do niej AT\r\n)
    if cmd.startswith("+++"):
        # Jeśli po +++ wysłano AT, odpowiadamy OK
        send_line("\r\nOK\r\n")
        return line_buf
    if (
        cmd == "AT"
        or cmd.startswith("ATE")
        or cmd.startswith("AT+CIPMUX=")
        or cmd.startswith("AT+CWMODE")
        or cmd.startswith("AT+CWAUTOCONN=")
        or cmd == "AT+CWQAP"
    ):
        send_line("\r\nOK\r\n")

    elif cmd == "AT+GMR":
        send_line(
            "AT version:1.3.0.0(Jul 14 2016 18:54:01)\r\n"
            "SDK version:2.0.0(5a875ba)\r\n"
            "v1.0.0.3\r\n"
            "Mar 13 2018 09:37:06\r\n"
            "\r\nOK\r\n"
        )

    elif cmd == "AT+CIFSR":
        send_line(
            '+CIFSR:STAIP,"192.168.1.100"\r\n'
            '+CIFSR:STAMAC,"02:00:00:00:00:01"\r\n'
            "\r\nOK\r\n"
        )

    elif cmd == "AT+CWJAP?":
        send_line('+CWJAP:"JASE-Virtual","02:00:00:00:00:02",1,-40\r\n\r\nOK\r\n')

    elif cmd == "AT+CWLAP":
        # Fake scan result -- same fictitious AP AT+CWJAP?/AT+CIFSR already
        # report as "joined", ecn=3 (WPA2_PSK), so network managers that
        # list/pick an AP before joining have exactly one entry to pick.
        send_line('+CWLAP:(3,"JASE-Virtual",-40,"02:00:00:00:00:02",1)\r\n\r\nOK\r\n')

    elif cmd.startswith("AT+CWJAP="):
        send_line("\r\nOK\r\n")

    elif cmd == "AT+RST":
        # Real ESP-AT gives no OK here -- the module just resets and the
        # only deterministic response is "ready" once boot completes.
        # Returning ERROR (this mock's old default/fallback) makes a real
        # driver that waits on AT+RST before continuing retry forever,
        # which is exactly the infinite ATE0/AT+RST loop seen against a
        # real BRZX client.
        send_line("\r\nready\r\n")

    elif (
        cmd.startswith("AT+CIPCLOSE=")
        or cmd.startswith("AT+CIPMODE=")
        or cmd.startswith("AT+CIPSERVER=")
        or cmd.startswith("AT+CIPDINFO=")
    ):
        # Standard driver init/cleanup boilerplate (close-all-links,
        # passthrough-mode-off, server-off, per-link-IP-info-off) -- real
        # ESP-AT accepts all of these with OK even when there's nothing to
        # do; this mock used to only match bare "AT+CIPCLOSE" (no "="),
        # falling through to ERROR for every one of these and never
        # letting the driver's init sequence finish.
        if cmd.startswith("AT+CIPCLOSE=") and remote_fd is not None:
            remote_fd.close()
            remote_fd = None
        send_line("\r\nOK\r\n")

        if cmd.startswith("AT+CIPDINFO=") and not wifi_announced:
            # Clients like Moon Rabbit never poll AT+CWJAP? -- they assume
            # CWAUTOCONN already rejoined a saved AP during boot and just
            # wait for this UNSOLICITED line real ESP-AT prints once that
            # join completes. A module with no saved AP (fresh/never
            # joined) never sends it -- confirmed hanging identically on
            # real hardware, not just this mock -- so fake it here once,
            # right after the standard init boilerplate finishes, so
            # --emulate testing isn't stuck on the same real-hardware
            # WiFi-provisioning prerequisite.
            wifi_announced = True
            send_line("\r\nWIFI CONNECTED\r\n\r\nWIFI GOT IP\r\n")

    elif cmd.startswith("AT+CIPSTART="):
        host, port = parse_cip_start(cmd[12:])
        if host is None or port is None:
            send_line("\r\nERROR\r\n")
            return line_buf

        if remote_fd is not None:
            remote_fd.close()
            remote_fd = None

        try:
            s = socket.create_connection((host, port), timeout=5.0)
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            s.setblocking(False)
            remote_fd = s
            print(f"virtual_esp01: [SYS] Połączono z serwerem {host}:{port}", flush=True)
            send_line("\r\nCONNECT\r\n\r\nOK\r\n")
        except Exception as e:
            print(f"virtual_esp01: [SYS] Błąd połączenia z {host}:{port} -> {e}", flush=True)
            send_line("\r\nERROR\r\n")

    elif cmd.startswith("AT+CIPSEND="):
        try:
            length = int(cmd[11:])
        except ValueError:
            length = 0

        if length <= 0 or remote_fd is None:
            send_line("\r\nERROR\r\n")
            return line_buf

        send_line("\r\nOK\r\n> ")

        data = bytearray()

        if len(line_buf) > 0:
            take = min(len(line_buf), length)
            data.extend(line_buf[:take])
            line_buf = line_buf[take:]

        while len(data) < length:
            want = length - len(data)
            try:
                chunk = client_sock.recv(want)
                if not chunk:
                    send_line("\r\nERROR\r\n")
                    return line_buf
                data.extend(chunk)
            except socket.error:
                send_line("\r\nERROR\r\n")
                return line_buf

        log_hex("=> NET OUT", bytes(data))

        try:
            remote_fd.sendall(data)
            send_line(f"\r\nRecv {len(data)} bytes\r\n")
            send_line("\r\nSEND OK\r\n")
        except socket.error:
            send_line("\r\nERROR\r\n")

    elif cmd == "AT+CIPCLOSE":
        if remote_fd is not None:
            remote_fd.close()
            remote_fd = None
        send_line("\r\nCLOSED\r\n\r\nOK\r\n")

    else:
        send_line("\r\nERROR\r\n")

    return line_buf


def run_bridge_mode(tcp_port: int, dev_path: str, baudrate: int):
    """
    TRYB MOSTEK / PASSTHROUGH:
    Odbiera komendy po TCP od emulatora, przekazuje do fizycznego /dev/ttyUSB0
    i odsyła odpowiedzi z fizycznego ESP01 z powrotem po TCP. Loguje wszystko w HEX.
    """
    print(f"virtual_esp01: [SYS] Otwieranie fizycznego ESP01 na {dev_path} ({baudrate} baud)...", flush=True)
    ser = serial.Serial(dev_path, baudrate=baudrate, timeout=0.01)

    listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listen_sock.bind(("127.0.0.1", tcp_port))
    listen_sock.listen(1)

    print(f"virtual_esp01: Nasłuchiwanie TCP na 127.0.0.1:{tcp_port} (tryb MOSTEK do {dev_path})...", flush=True)

    while True:
        client_sock, _ = listen_sock.accept()
        print("\nvirtual_esp01: [SYS] Klient TCP podłączony!", flush=True)

        try:
            while True:
                # Sprawdzamy stan obu źródeł (socket TCP i port szeregowy UART)
                readable, _, _ = select.select([client_sock], [], [], 0.01)

                # 1. Dane od emulatora (TCP) -> prześlij do fizycznego ESP01 (/dev/ttyUSB0)
                if readable:
                    data = client_sock.recv(1024)
                    if not data:
                        break  # Klient rozłączony
                    log_hex("<- TCP RECV", data)
                    ser.write(data)

                # 2. Dane z fizycznego ESP01 (/dev/ttyUSB0) -> prześlij do emulatora (TCP)
                if ser.in_waiting > 0:
                    ser_data = ser.read(ser.in_waiting)
                    log_hex("-> UART SEND", ser_data)
                    client_sock.sendall(ser_data)

        except (socket.error, serial.SerialException) as e:
            print(f"virtual_esp01: [SYS] Błąd połączenia: {e}", flush=True)

        print("virtual_esp01: [SYS] Klient TCP rozłączony.\n", flush=True)
        client_sock.close()


def run_emulated_mode(tcp_port: int):
    """TRYB EMULACJI: Brak fizycznego sprzętu, czysta emulacja programowa po TCP."""
    global remote_fd, wifi_announced

    listen_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listen_sock.bind(("127.0.0.1", tcp_port))
    listen_sock.listen(1)

    print(f"virtual_esp01: Nasłuchiwanie TCP na 127.0.0.1:{tcp_port} (tryb EMULATORA)...", flush=True)

    while True:
        client_sock, _ = listen_sock.accept()
        print("\nvirtual_esp01: [SYS] Klient TCP podłączony!", flush=True)
        wifi_announced = False  # nowe "boot" -> jeszcze nie ogłoszono WIFI GOT IP

        line_buf = bytearray()

        while True:
            read_sockets = [client_sock]
            if remote_fd is not None:
                read_sockets.append(remote_fd)

            readable, _, _ = select.select(read_sockets, [], [], 0.1)

            if not readable:
                continue

            if client_sock in readable:
                try:
                    data = client_sock.recv(512)
                    if not data:
                        break
                    line_buf.extend(data)

                    while b"\n" in line_buf:
                        pos = line_buf.find(b"\n")
                        raw_line = bytes(line_buf[: pos + 1])
                        line_buf = line_buf[pos + 1 :]

                        if len(raw_line.strip(b"\r\n")) > 0:
                            line_buf = handle_emulated_command(client_sock, raw_line, line_buf)
                except socket.error:
                    break

            if remote_fd is not None and remote_fd in readable:
                try:
                    data = remote_fd.recv(512)
                    if not data:
                        print("virtual_esp01: [SYS] Zdalny serwer zamknął połączenie", flush=True)
                        remote_fd.close()
                        remote_fd = None
                        # Real ESP-AT sends this UNSOLICITED when the remote
                        # end closes -- gopher (and plenty of other
                        # protocols) signal "response complete" purely by
                        # closing, no length header at all, so a client has
                        # no other way to learn the transfer is done.
                        # Confirmed missing end-to-end against a real gopher
                        # server (vtrd.in:70): every byte of the response
                        # arrived via +IPD, but with no CLOSED notice Moon
                        # Rabbit waited forever for a completion signal that
                        # never came, even though it already had everything
                        # it needed.
                        closed_msg = b"\r\nCLOSED\r\n"
                        log_hex("-> SEND", closed_msg)
                        client_sock.sendall(closed_msg)
                    else:
                        log_hex("<= NET IN", data)
                        header = f"\r\n+IPD,{len(data)}:".encode("utf-8")
                        log_hex("-> SEND", header)
                        client_sock.sendall(header + data)
                except socket.error:
                    remote_fd.close()
                    remote_fd = None

        print("virtual_esp01: [SYS] Klient TCP rozłączony.\n", flush=True)
        client_sock.close()
        if remote_fd is not None:
            remote_fd.close()
            remote_fd = None


def main():
    sys.stdout.reconfigure(line_buffering=True)

    tcp_port = 9100

    # Sparsowanie argumentów
    if len(sys.argv) > 1 and sys.argv[1] != "--emulate":
        dev_path = sys.argv[1]
        baudrate = int(sys.argv[2]) if len(sys.argv) > 2 else 115200
        run_bridge_mode(tcp_port, dev_path, baudrate)
    else:
        run_emulated_mode(tcp_port)


if __name__ == "__main__":
    main()