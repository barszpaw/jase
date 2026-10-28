#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# Multiply bridge -- companion process for JASE's Multiply peripheral
# (src/Multiply.cpp), the Arduino+microSD addon for the ZX Dandanator! Mini
# (github.com/mad3001/Multiply). See docs/Dandanator/dandanator_multiply_notes.md
# for the full hardware/protocol research this is built from.
#
# Same split as tools/sdi_bridge/sdi_bridge.py: JASE's C++ side (Multiply.cpp)
# is a dumb byte-pipe -- it doesn't know what CMD_ZX2SD_* commands MEAN, only
# how to shuttle bytes to/from here over TCP. All the real logic lives in
# this package, across a few single-responsibility modules rather than one
# large script:
#   protocol.py       -- wire constants + read-framing primitives
#   virtual_sdcard.py -- directory navigation/listing/classification
#   snapshot.py        -- SNA/Z80 snapshot parsing + block serving
#   tap.py              -- TAP block-by-block streaming
#   session.py          -- per-connection state + the actual dispatch/I/O loop
# This file is just the entrypoint: argparse + the accept loop.
#
# Wire protocol: UNFRAMED raw bytes, both directions, on one TCP connection
# -- a direct reflection of the real hardware's own wire protocol: the
# Dandanator's command-52 channel delivers one byte at a time with no
# message delimiters, and the real Arduino firmware tracks command/payload
# boundaries itself. See protocol.py and session.py for exactly which
# commands are implemented and which are merely payload-framed (consumed
# correctly but not acted on) -- getting a payload length wrong desyncs
# every command after it for the rest of the connection, so even
# unimplemented commands can't just be skipped.
# -----------------------------------------------------------------------------

import argparse
import os
import socket
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from session import Session, handle_client  # noqa: E402

VERSION = "0.3"


def main() -> None:
    parser = argparse.ArgumentParser(description="Multiply bridge for JASE's Multiply peripheral")
    parser.add_argument('--tcp-port', type=int, default=9701)
    parser.add_argument('--basedir', required=True, help="directory to serve as the virtual SD card root")
    args = parser.parse_args()

    print(f"=== Multiply Bridge v{VERSION} ===")
    print(f"SD card root: {os.path.abspath(args.basedir)}")

    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.tcp_port))
    srv.listen(1)
    print(f"Listening on 127.0.0.1:{args.tcp_port}. Waiting for JASE's Multiply peripheral... (Ctrl+C to stop)")

    while True:
        conn, addr = srv.accept()
        print(f"\n*** Client connected from {addr}.")
        session = Session(args.basedir)
        try:
            handle_client(conn, session)
        except ConnectionError:
            pass
        finally:
            print("*** Client disconnected.")
            conn.close()


if __name__ == "__main__":
    main()
