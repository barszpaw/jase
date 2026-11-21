#!/usr/bin/env python3
"""PC client for the FDD3000 FSRV.COM file server (tools/fsrv/fsrv_fdd3000.asm).

Protocol logic ported 1:1 from bosman-8's own PC client
(tools/fsrv/fsrv, see its own docstring and the project's
tools/fsrv/README.md) -- the only change is the transport: that script
talks to a real serial port via pyserial, this one talks to the
station's WD2123 CHA test TCP port instead (jase's own
--fdd3000-serial1-port, or fdd3000_station's --serial1-port -- a raw,
unframed byte passthrough, see src/FddStation.h's own class comment),
since there's no real serial line here to begin with.
"""

from __future__ import annotations

import argparse
import math
import socket
import sys
from pathlib import Path

RECORD_SIZE = 128
DEFAULT_HOST = "127.0.0.1"
DEFAULT_PORT = 9700
DEFAULT_TIMEOUT = 10.0
DEFAULT_MASK = "*.*"


class TcpSerial:
    """Just enough of pyserial's Serial interface for this client's needs."""

    def __init__(self, host: str, port: int, timeout: float) -> None:
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)

    def read(self, size: int = 1) -> bytes:
        try:
            return self.sock.recv(size)
        except socket.timeout:
            return b""

    def write(self, data: bytes) -> int:
        self.sock.sendall(data)
        return len(data)

    def close(self) -> None:
        self.sock.close()


def crc_xor(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
    return crc & 0xFF


def strip_cpm_eof(data: bytes) -> bytes:
    return data.rstrip(b"\x1A")


def pad_cpm_records(data: bytes) -> tuple[int, bytes]:
    records = math.ceil(len(data) / RECORD_SIZE) if data else 0
    return records, data.ljust(records * RECORD_SIZE, b"\x1A")


def normalize_cpm_name(name: str, *, allow_wildcards: bool = False) -> str:
    value = name.strip().upper()
    if not value:
        raise ValueError("empty CP/M name")

    drive = ""
    body = value
    if len(value) >= 2 and value[1] == ":":
        if value[0] < "A" or value[0] > "P":
            raise ValueError(f"invalid CP/M drive: {value[:2]}")
        drive = value[:2]
        body = value[2:]
        if not body:
            raise ValueError("missing CP/M file name after drive")

    if "/" in body or "\\" in body or ":" in body:
        raise ValueError(f"invalid CP/M name: {name!r}")
    if not allow_wildcards and ("*" in body or "?" in body):
        raise ValueError(f"wildcards are not valid for this command: {name!r}")

    parts = body.split(".")
    if len(parts) > 2:
        raise ValueError(f"invalid CP/M 8.3 name: {name!r}")
    stem = parts[0]
    ext = parts[1] if len(parts) == 2 else ""

    if not stem:
        raise ValueError(f"missing CP/M file name: {name!r}")
    if len(stem) > 8 or len(ext) > 3:
        raise ValueError(f"CP/M name is longer than 8.3: {name!r}")

    return drive + stem + (f".{ext}" if ext else "")


def recv_exact(port: TcpSerial, size: int) -> bytes:
    buf = bytearray()
    while len(buf) < size:
        chunk = port.read(size - len(buf))
        if not chunk:
            raise TimeoutError(f"timeout while waiting for {size} bytes; got {len(buf)}")
        buf.extend(chunk)
    return bytes(buf)


def recv_byte(port: TcpSerial) -> int:
    return recv_exact(port, 1)[0]


def send_name(port: TcpSerial, name: str, *, allow_wildcards: bool = False) -> str:
    normalized = normalize_cpm_name(name, allow_wildcards=allow_wildcards)
    port.write(normalized.encode("ascii") + b"\x00")
    return normalized


def expect_ok(port: TcpSerial) -> None:
    response = recv_byte(port)
    if response == ord("E"):
        raise RuntimeError("server returned error")
    if response != ord("K"):
        raise RuntimeError(f"unexpected server response: 0x{response:02X}")


def get_file(port: TcpSerial, cpm_name: str) -> bytes:
    port.write(b"G")
    send_name(port, cpm_name)

    response = recv_byte(port)
    if response == ord("E"):
        raise RuntimeError("server returned error for GET")
    if response != ord("K"):
        raise RuntimeError(f"unexpected GET response: 0x{response:02X}")

    records = recv_byte(port) | (recv_byte(port) << 8)
    print(f"GET {cpm_name}: {records} records ({records * RECORD_SIZE} bytes)", file=sys.stderr)

    data = bytearray()
    for rec in range(records):
        rec_data = recv_exact(port, RECORD_SIZE)
        server_crc = recv_byte(port)
        local_crc = crc_xor(rec_data)
        if server_crc != local_crc:
            port.write(b"E")
            raise RuntimeError(f"CRC mismatch on record {rec}: server={server_crc:02X} local={local_crc:02X}")
        data.extend(rec_data)
        port.write(b"K")  # per-record ACK

    recv_byte(port)  # final K from server (file closed)
    return strip_cpm_eof(bytes(data))


def put_file(port: TcpSerial, local_data: bytes, cpm_name: str) -> int:
    records, padded = pad_cpm_records(local_data)
    if records > 0xFFFF:
        raise ValueError("file is too large for the FSRV protocol")

    port.write(b"P")
    send_name(port, cpm_name)
    port.write(bytes([records & 0xFF, records >> 8]))
    print(f"PUT {cpm_name}: {records} records ({records * RECORD_SIZE} bytes)", file=sys.stderr)

    expect_ok(port)  # initial ACK: server created file, ready for records

    for rec in range(records):
        offset = rec * RECORD_SIZE
        rec_data = padded[offset : offset + RECORD_SIZE]
        rec_crc = crc_xor(rec_data)
        port.write(rec_data)
        port.write(bytes([rec_crc]))
        expect_ok(port)  # per-record ACK

    expect_ok(port)  # final ACK: file closed OK
    print(f"PUT {cpm_name}: done, {records} records", file=sys.stderr)
    return records


def decode_dir_entry(raw: bytes) -> str:
    if len(raw) != 11:
        raise ValueError("directory entry must be exactly 11 bytes")
    clean = bytes(byte & 0x7F for byte in raw)
    stem = clean[:8].decode("ascii", errors="replace").rstrip()
    ext = clean[8:].decode("ascii", errors="replace").rstrip()
    return f"{stem}.{ext}" if ext else stem


def dir_files(port: TcpSerial, mask: str = DEFAULT_MASK) -> list[str]:
    normalized = normalize_cpm_name(mask, allow_wildcards=True)
    port.write(b"D")
    port.write(normalized.encode("ascii") + b"\x00")

    entries: list[str] = []
    while True:
        raw = recv_exact(port, 11)
        if raw == b"\x00" * 11:
            return sorted(entries)
        entries.append(decode_dir_entry(raw))


def quit_server(port: TcpSerial) -> None:
    port.write(b"Q")
    expect_ok(port)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Client for FDD3000 FSRV.COM over WD2123 CHA (TCP)")
    parser.add_argument("-H", "--host", default=DEFAULT_HOST, help="WD2123 CHA TCP host")
    parser.add_argument("-P", "--tcp-port", default=DEFAULT_PORT, type=int, help="WD2123 CHA TCP port")
    parser.add_argument("-t", "--timeout", default=DEFAULT_TIMEOUT, type=float, help="timeout in seconds")

    sub = parser.add_subparsers(dest="command", required=True)

    dir_parser = sub.add_parser("dir", help="list CP/M files")
    dir_parser.add_argument("mask", nargs="?", default=DEFAULT_MASK)

    get_parser = sub.add_parser("get", help="copy a file from CP/M")
    get_parser.add_argument("cpm_name")
    get_parser.add_argument("local_path", nargs="?")

    put_parser = sub.add_parser("put", help="copy a file to CP/M")
    put_parser.add_argument("local_path")
    put_parser.add_argument("cpm_name")

    sub.add_parser("quit", help="stop the CP/M server")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)

    try:
        port = TcpSerial(args.host, args.tcp_port, args.timeout)
    except OSError as exc:
        print(f"fsrv_fdd3000: failed to connect: {exc}", file=sys.stderr)
        return 2

    try:
        if args.command == "dir":
            for entry in dir_files(port, args.mask):
                print(entry)
        elif args.command == "get":
            local_path = Path(args.local_path) if args.local_path else Path(normalize_cpm_name(args.cpm_name).split(":", 1)[-1])
            data = get_file(port, args.cpm_name)
            local_path.write_bytes(data)
            print(f"{local_path}: {len(data)} bytes")
        elif args.command == "put":
            local_path = Path(args.local_path)
            data = local_path.read_bytes()
            records = put_file(port, data, args.cpm_name)
            print(f"{normalize_cpm_name(args.cpm_name)}: {records} records")
        elif args.command == "quit":
            quit_server(port)
            print("server stopped")
    except (OSError, RuntimeError, TimeoutError, ValueError) as exc:
        print(f"fsrv_fdd3000: {exc}", file=sys.stderr)
        return 1
    finally:
        port.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
