#!/usr/bin/env python3
# -----------------------------------------------------------------------------
# Multiply bridge -- companion process for JASE's Multiply peripheral
# (src/Multiply.cpp), the Arduino+microSD addon for the ZX Dandanator! Mini
# (github.com/mad3001/Multiply). See docs/Dandanator/dandanator_multiply_notes.md
# for the full hardware/protocol research this is built from.
#
# Same split as tools/sdi_bridge/sdi_bridge.py: JASE's C++ side (Multiply.cpp)
# is a dumb byte-pipe -- it doesn't know what CMD_ZX2SD_* commands MEAN, only
# how to shuttle bytes to/from here over TCP. All the real logic (SD
# directory browsing, Z80/SNA/TAP parsing and generation) belongs in THIS
# process, written in Python rather than C++ for the same reason
# sdi_bridge.py exists: file-format-heavy logic doesn't belong in the hot
# Z80-emulation path, and Python makes it far quicker to get (and keep)
# right.
#
# Wire protocol: UNFRAMED raw bytes, both directions, on one TCP connection
# -- a direct reflection of the real hardware's own wire protocol (see the
# docs above): the Dandanator's command-52 channel delivers one byte at a
# time with no message delimiters, and the real Arduino firmware tracks
# command/payload boundaries itself (via getBuffer_0's null-terminated reads
# and getBuffer_N's fixed-length reads -- see
# docs/Dandanator/reference_src/libSerialCmds.ino). This bridge has to do
# the same once it grows past single-byte commands.
#
# v1 here is deliberately minimal: it only recognizes CMD_ZX2SD_REQ_ID
# (0x01, no payload) and replies with a single fixed identification byte --
# enough to prove the whole TX/RX pipe works end-to-end (Z80 -> Dandanator
# command 52 -> JASE's Multiply -> this bridge -> back through the port-0x1F
# nibble protocol -> Z80). The real CMD_ZX2SD_* command set (SD directory
# listing, Z80/SNA/TAP/romset streaming) is NOT implemented here yet -- see
# docs/Dandanator/reference_src/multiply_commands_v1.asm for the full
# command table this should eventually grow into.
# -----------------------------------------------------------------------------

import argparse
import socket

VERSION = "0.1"

CMD_ZX2SD_REQ_ID = 0x01

# Placeholder reply payload -- the real firmware's actual identification
# response FORMAT (length, fields) wasn't captured in the sources read for
# this (see docs/Dandanator/dandanator_multiply_notes.md); this single byte
# only proves the round trip works, it is not a faithful reproduction of a
# real reply.
MULTIPLY_ID_BYTE = 0x01


def handle_client(conn: socket.socket) -> None:
    while True:
        data = conn.recv(1)
        if not data:
            break
        cmd = data[0]
        print(f"[RECV] cmd=0x{cmd:02x}")
        if cmd == CMD_ZX2SD_REQ_ID:
            conn.sendall(bytes([MULTIPLY_ID_BYTE]))
            print(f"[SEND] 0x{MULTIPLY_ID_BYTE:02x}")
        else:
            print(f"[?] unrecognized command 0x{cmd:02x} -- not implemented yet, ignored")


def main() -> None:
    parser = argparse.ArgumentParser(description="Multiply bridge for JASE's Multiply peripheral")
    parser.add_argument('--tcp-port', type=int, default=9701)
    args = parser.parse_args()

    print(f"=== Multiply Bridge v{VERSION} ===")
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", args.tcp_port))
    srv.listen(1)
    print(f"Listening on 127.0.0.1:{args.tcp_port}. Waiting for JASE's Multiply peripheral... (Ctrl+C to stop)")

    while True:
        conn, addr = srv.accept()
        print(f"\n*** Client connected from {addr}.")
        try:
            handle_client(conn)
        except ConnectionError:
            pass
        finally:
            print("*** Client disconnected.")
            conn.close()


if __name__ == "__main__":
    main()
