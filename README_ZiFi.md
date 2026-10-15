# ZiFi networking in JASE

JASE's `ZiFi` peripheral is a plain TCP **client**. It never speaks to a
real serial port itself — whatever's on the other end of that TCP
connection (a real ESP-01, or a piece of software pretending to be one)
is a completely separate process. JASE's own code never branches on
which one it is.

```
 Z80 guest code  <--ports-->  JASE (ZiFi peripheral, TCP client)
                                        |
                                        | TCP, 127.0.0.1:<port>
                                        v
                      +-----------------------------+
                      |  tools/virtual_esp01          |  <- software-only ESP01
                      |  (speaks ESP-AT over the      |
                      |   same TCP socket, opens      |
                      |   REAL sockets to the internet)|
                      +-----------------------------+
                               -- or --
                      +-----------------------------+
                      |  socat bridging a REAL        |  <- real hardware
                      |  ESP-01/Wemos D1 serial port   |
                      |  onto the same TCP convention  |
                      +-----------------------------+
```

## Running the software-only virtual ESP01

```sh
# build (one-time)
g++ -std=c++17 -O2 tools/virtual_esp01/main.cpp -o tools/virtual_esp01/virtual_esp01

# run it (default port 9100; pass a different port as argv[1])
./tools/virtual_esp01/virtual_esp01 9100
```

Then point JASE at it:

```sh
./jase --zifi 127.0.0.1:9100 [--load-tape yourgame.tap] [--debug-port 9000]
```

That's it — no other configuration. The daemon implements a minimal
real ESP-AT subset (`AT`, `AT+CWMODE`/`AT+CWJAP` stubbed OK, `AT+CIPSTART=
"TCP",host,port` — opens a real TCP connection via `getaddrinfo`,
`AT+CIPSEND=<len>`, `AT+CIPCLOSE`, and async `+IPD,<len>:<data>` when the
real remote connection has data), written from scratch against
Espressif's own public ESP-AT command reference — no GPL code involved,
so this stays independent of JASE's own MIT license regardless of how
it's written.

## Bridging a REAL ESP-01/Wemos D1 instead

No custom code needed — use the standard `socat` tool to put the real
serial port behind the same TCP convention:

```sh
socat TCP-LISTEN:9100,reuseaddr,fork FILE:/dev/ttyUSB0,raw,echo=0,b115200
./jase --zifi 127.0.0.1:9100
```

## Port layout the Z80 guest side sees

Two independent port windows, sharing the same RX/TX byte rings and the
one TCP connection — use whichever one the guest software expects:

**0xEF FIFO** (pico-spec's own "ZiFi-API" convention — port low byte
`0xEF`, high byte selects the register):

| Port (hi:lo) | Access | Meaning |
|---|---|---|
| `00:EF`..`BF:EF` | IN | pull next RX byte (`0xFF` if empty) |
| `00:EF`..`BF:EF` | OUT | push a TX byte |
| `C0:EF` | IN | RX ring fill count (0-255) |
| `C1:EF` | IN | TX ring fill count (0-255) |

**ZX-UNO window** (`#FC3B`/`#FD3B`, Karabas-Pro's real historical ESP8266
bridge convention — the one real existing Z80 software like Moon Rabbit
actually targets, not something JASE invented):

| Port | Access | Meaning |
|---|---|---|
| `#FC3B` | OUT | select internal register (`0xC6` or `0xC7`) |
| `#FD3B` | IN/OUT, reg=`0xC6` | UART data: IN pulls next RX byte, OUT pushes a TX byte |
| `#FD3B` | IN, reg=`0xC7` | status: bit0 = RX_RECV, bit1 = TX_BUSY (TX ring full) |

Not implemented: IMR/CR on the FIFO window, the 16550 UART window, UART2
on the ZX-UNO window. Add them if a concrete piece of software needs
them — don't pre-build.

## Debugging

`ZIFI` debug-protocol command (see the rest of JASE's debug protocol in
project memory) reports `connected`/`rx`/`tx` ring state.

## Known gap: esxDOS / DivMMC-DivSD are NOT emulated

**Moon Rabbit (the real pure-Z80 Gopher client over these exact ZiFi
ports) needs esxDOS to load at all.** JASE has no DivMMC/DivSD emulation
yet, so Moon Rabbit — or anything else that loads via esxDOS — can't run
in JASE regardless of how correct the ZiFi/ZX-UNO port emulation is.
This is a real, known blocker, not a detail to paper over. Whoever picks
this up next: DivMMC/DivSD emulation is a prerequisite for testing Moon
Rabbit specifically, independent of anything in this file.
