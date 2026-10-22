import re
import sys
import socket
import socketserver
import threading
import argparse
import textwrap
import logging
from logging.handlers import RotatingFileHandler
from abc import ABC, abstractmethod
from typing import Dict, List, Tuple, Type, Union, Optional

try:
    import serial  # pyserial - opcjonalne, wymagane tylko dla trybu "bridge"
except ImportError:
    serial = None


# =====================================================================
# 0. LOGOWANIE (konsola + plik z rotacją)
# =====================================================================

LOG_FORMAT = "%(asctime)s [%(levelname)-7s] [%(threadName)s] %(message)s"
DATE_FORMAT = "%Y-%m-%d %H:%M:%S"

logger = logging.getLogger("esp01_bridge")


def setup_logging(args: argparse.Namespace) -> None:
    logger.setLevel(logging.DEBUG)
    logger.handlers.clear()

    formatter = logging.Formatter(LOG_FORMAT, datefmt=DATE_FORMAT)

    if args.quiet:
        console_level = logging.WARNING
    elif args.verbose:
        console_level = logging.DEBUG
    else:
        console_level = logging.INFO

    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(console_level)
    console_handler.setFormatter(formatter)
    logger.addHandler(console_handler)

    if args.log_file:
        try:
            file_handler = RotatingFileHandler(
                args.log_file,
                maxBytes=args.log_max_bytes,
                backupCount=args.log_backup_count,
                encoding="utf-8",
            )
            file_handler.setLevel(logging.DEBUG)
            file_handler.setFormatter(formatter)
            logger.addHandler(file_handler)
            logger.info(
                f"Logowanie do pliku: '{args.log_file}' "
                f"(max {args.log_max_bytes} B/plik, {args.log_backup_count} kopii zapasowych)"
            )
        except OSError as exc:
            logger.warning(f"Nie można otworzyć pliku logu '{args.log_file}': {exc}. "
                            f"Kontynuuję bez logowania do pliku.")


def hexdump_preview(data: bytes, limit: int = 64) -> str:
    shown = data[:limit]
    hex_part = shown.hex(" ")
    suffix = f" ... (+{len(data) - limit} B)" if len(data) > limit else ""
    return f"{len(data)} B: {hex_part}{suffix}"


# =====================================================================
# 1. KONTEKST I WZORZEC COMMAND (Klasy Akcji)
# =====================================================================

class ATContext(ABC):
    """Interfejs kontekstu umożliwiający akcjom modyfikację stanu emulatora."""

    @abstractmethod
    def log(self, message: str) -> None:
        pass


class ATCommandAction(ABC):
    """Bazowa klasa dla wszystkich akcji (Wzorzec Command)."""

    @abstractmethod
    def execute(self, params: Dict[str, str], context: ATContext) -> bytes:
        pass


class GenericAction(ATCommandAction):
    """Uniwersalna akcja dla komend bez unikalnej logiki (stała odpowiedź)."""

    def __init__(self, label: str, response: bytes = b"OK\r\n"):
        self.label = label
        self.response = response

    def execute(self, params: Dict[str, str], context: ATContext) -> bytes:
        context.log(f"[ACTION:{self.label}] params={params}")
        return self.response


# --- Akcje podstawowe / systemowe / WiFi (bez zmian logiki) ---

class PingAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        return b"OK\r\n"


class EchoAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        context.log(f"[ACTION] Ustawienie Echo na: {params.get('mode')}")
        return b"OK\r\n"


class RestartAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        context.log("[ACTION] Restart modułu...")
        return b"OK\r\n\r\nready\r\n"


class RestoreAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        context.log("[ACTION] Przywracanie ustawień fabrycznych...")
        return b"OK\r\n\r\nready\r\n"


class VersionAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        return b"AT version:1.7.4.0\r\nSDK version:3.0.4\r\nOK\r\n"


class SleepAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        context.log(f"[ACTION] Usypianie na {params.get('time')} ms")
        return b"OK\r\n"


class WifiModeAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        context.log(f"[ACTION] Zmiana trybu WiFi na: {params.get('mode')}")
        return b"OK\r\n"


class WifiModeQueryAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        return b"+CWMODE:1\r\n\r\nOK\r\n"


class JoinApAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        context.log(f"[ACTION] Łączenie z siecią: {params.get('ssid')}")
        return b"WIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n"


class JoinApQueryAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        return b'+CWJAP:"MySSID","aa:bb:cc:dd:ee:ff",6,-45\r\n\r\nOK\r\n'


class QuitApAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        context.log("[ACTION] Rozłączanie z WiFi")
        return b"OK\r\n"


class ListApAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        return b'+CWLAP:(3,"MySSID",-45,"aa:bb:cc:dd:ee:ff",6,0,0)\r\n\r\nOK\r\n'


class SetApConfigAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        context.log(f"[ACTION] Konfiguracja AP: {params}")
        return b"OK\r\n"


class ListStationsAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        return b"192.168.4.2,5c:cf:7f:f4:f0:aa\r\n\r\nOK\r\n"


class GetIpAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        return (b'+CIFSR:STAIP,"192.168.1.100"\r\n'
                b'+CIFSR:STAMAC,"5c:cf:7f:f4:f0:aa"\r\n\r\nOK\r\n')


class CipStatusAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        return b"STATUS:2\r\n\r\nOK\r\n"


class PingHostAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        context.log(f"[ACTION] Ping do: {params.get('ip')}")
        return b"+42\r\n\r\nOK\r\n"


class CiUpdateAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        context.log("[ACTION] Rozpoczęto aktualizację OTA...")
        return (b"+CIPUPDATE:1\r\n+CIPUPDATE:2\r\n+CIPUPDATE:3\r\n"
                b"+CIPUPDATE:4\r\n\r\nOK\r\n")


class CipServerAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        context.log(f"[ACTION] Serwer TCP: mode={params.get('mode')}, port={params.get('port')} "
                     f"(UWAGA: tryb serwera nie jest w pełni zaimplementowany)")
        return b"OK\r\n"


class CipModeAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        context.log(f"[ACTION] CIPMODE ustawiony na: {params.get('mode')}")
        return b"OK\r\n"


class CipStoAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        context.log(f"[ACTION] Timeout serwera ustawiony na: {params.get('time')}s")
        return b"OK\r\n"


# --- NOWE / PRZEPISANE akcje: prawdziwa obsługa TCP (CIPSTART/CIPSEND/CIPCLOSE) ---

class ConnectAction(ATCommandAction):
    """AT+CIPSTART="{type}","{ip}",{port} - pojedyncze połączenie (CIPMUX=0, id=0)."""

    def execute(self, params, context) -> bytes:
        host = params["ip"]
        port = int(params["port"])
        conn_type = params["type"]
        context.log(f"[ACTION] Łączenie z {host}:{port} ({conn_type})")
        ok = context.open_connection(0, conn_type, host, port)
        if ok:
            return b"CONNECT\r\n\r\nOK\r\n"
        return b"CONNECT FAIL\r\n\r\nERROR\r\n"


class ConnectMuxAction(ATCommandAction):
    """AT+CIPSTART={id},"{type}","{ip}",{port} - połączenie wielokrotne (CIPMUX=1)."""

    def execute(self, params, context) -> bytes:
        link_id = int(params["id"])
        host = params["ip"]
        port = int(params["port"])
        conn_type = params["type"]
        context.log(f"[ACTION] (id={link_id}) Łączenie z {host}:{port} ({conn_type})")
        ok = context.open_connection(link_id, conn_type, host, port)
        if ok:
            return f"{link_id},CONNECT\r\n\r\nOK\r\n".encode()
        return f"{link_id},CONNECT FAIL\r\n\r\nERROR\r\n".encode()


class CipCloseAction(ATCommandAction):
    """AT+CIPCLOSE - zamyka pojedyncze połączenie (id=0)."""

    def execute(self, params, context) -> bytes:
        context.log("[ACTION] Zamykanie połączenia (id=0)")
        context.close_connection(0)
        return b"CLOSED\r\n\r\nOK\r\n"


class CipCloseMuxAction(ATCommandAction):
    """AT+CIPCLOSE={id} - zamyka konkretne połączenie w trybie multi."""

    def execute(self, params, context) -> bytes:
        link_id = int(params["id"])
        context.log(f"[ACTION] Zamykanie połączenia id={link_id}")
        context.close_connection(link_id)
        return f"{link_id},CLOSED\r\n\r\nOK\r\n".encode()


class CipMuxAction(ATCommandAction):
    """AT+CIPMUX={mode} - rzeczywiście przełącza tryb single/multi connection."""

    def execute(self, params, context) -> bytes:
        context.cipmux = int(params["mode"])
        context.log(f"[ACTION] CIPMUX ustawione na: {context.cipmux}")
        return b"OK\r\n"


class CipDinfoAction(ATCommandAction):
    """AT+CIPDINFO={enable} - wpływa na format ramki +IPD (z/bez adresu IP:port)."""

    def execute(self, params, context) -> bytes:
        context.cipdinfo = int(params["enable"])
        context.log(f"[ACTION] CIPDINFO ustawione na: {context.cipdinfo}")
        return b"OK\r\n"


class CipSendAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        link_id = 0
        if not context.begin_raw_send(link_id, None):
            return b"\r\nERROR\r\n"
        context.log(f"[ACTION] CIPSEND: tryb transparentny do '+++' (id={link_id})")
        return b"\r\nOK\r\n\r\n> "


class CipSendLenAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        link_id = 0
        length = int(params["length"])
        if not context.begin_raw_send(link_id, length):
            return b"\r\nERROR\r\n"
        context.log(f"[ACTION] CIPSEND: oczekiwanie {length} B surowych danych (id={link_id})")
        return b"\r\nOK\r\n\r\n> "

class CipSendMuxLenAction(ATCommandAction):
    def execute(self, params, context) -> bytes:
        link_id = int(params["id"])
        length = int(params["length"])
        if not context.begin_raw_send(link_id, length):
            return b"\r\nERROR\r\n"
        context.log(f"[ACTION] CIPSEND: oczekiwanie {length} B surowych danych (id={link_id})")
        return b"\r\nOK\r\n\r\n> "

# =====================================================================
# 2. DISPATCHER / SESJA (Tabela Mapowania MVC + Regex + prawdziwy TCP relay)
# =====================================================================

RouteAction = Union[Type[ATCommandAction], ATCommandAction]


class ATCommandDispatcher(ATContext):
    """
    Pełni podwójną rolę:
    1. Routera komend AT (jak dotychczas) - dopasowanie regex -> akcja.
    2. Sesji TCP: trzyma prawdziwe gniazda otwarte przez AT+CIPSTART,
       obsługuje tryb surowego wysyłania (AT+CIPSEND) i odbiór danych
       ze zdalnego hosta (ramki +IPD).
    """

    def __init__(self, routes_table: List[Tuple[str, RouteAction]], client_sock: socket.socket):
        self.client_sock = client_sock
        self.write_lock = threading.Lock()

        # Stan "sieciowy" sesji (odpowiednik wewnętrznego stosu TCP/IP ESP8266)
        self.cipmux = 0
        self.cipdinfo = 0
        self.connections: Dict[int, socket.socket] = {}
        # raw_mode: None | ("exact", link_id, remaining_bytes) | ("transparent", link_id)
        self.raw_mode: Optional[Tuple] = None

        self._compiled_routes: List[Tuple[re.Pattern, List[str], ATCommandAction, str]] = []
        for path_pattern, action in routes_table:
            self._register_route(path_pattern, action)

    # --- Rejestracja tras (bez zmian) ---

    def _register_route(self, path_pattern: str, action: RouteAction):
        action_instance = action() if isinstance(action, type) else action
        param_names = re.findall(r'\{([a-zA-Z0-9_]+)\}', path_pattern)
        escaped_pattern = re.escape(path_pattern)
        escaped_pattern = escaped_pattern.replace(r'\{', '{').replace(r'\}', '}')
        regex_pattern = re.sub(r'\{[a-zA-Z0-9_]+\}', r'([^,]+)', escaped_pattern)
        regex_pattern = f"^{regex_pattern}$"
        compiled_regex = re.compile(regex_pattern, re.IGNORECASE)
        self._compiled_routes.append((compiled_regex, param_names, action_instance, path_pattern))

    def describe_routes(self) -> List[Tuple[str, str]]:
        result = []
        for _, _, action, pattern in self._compiled_routes:
            label = getattr(action, "label", None) or action.__class__.__name__
            result.append((pattern, label))
        return result

    def log(self, message: str) -> None:
        logger.info(message)

    # --- Komunikacja z klientem TCP (wspólny, bezpieczny wątkowo punkt wyjścia) ---

    def send_to_client(self, data: bytes) -> None:
        if not data:
            return
        with self.write_lock:
            try:
                self.client_sock.sendall(data)
            except OSError as exc:
                logger.debug(f"Nie udało się wysłać danych do klienta: {exc}")

    # --- Zarządzanie prawdziwymi połączeniami TCP (AT+CIPSTART/CIPCLOSE) ---

    def open_connection(self, link_id: int, conn_type: str, host: str, port: int) -> bool:
        self.close_connection(link_id)  # zamknij ewentualne stare połączenie pod tym id
        try:
            remote_sock = socket.create_connection((host, port), timeout=5)
        except (OSError, socket.gaierror) as exc:
            logger.warning(f"Nie udało się połączyć z {host}:{port} ({conn_type}): {exc}")
            return False

        self.connections[link_id] = remote_sock
        reader = threading.Thread(
            target=self._remote_reader_loop,
            args=(link_id, remote_sock),
            daemon=True,
            name=f"Remote-{link_id}:{host}:{port}",
        )
        reader.start()
        logger.info(f"Połączono (id={link_id}) z {host}:{port} ({conn_type})")
        return True

    def close_connection(self, link_id: int) -> bool:
        remote_sock = self.connections.pop(link_id, None)
        if remote_sock is not None:
            try:
                remote_sock.close()
            except OSError:
                pass
            return True
        return False

    def close_all_connections(self) -> None:
        for link_id in list(self.connections.keys()):
            self.close_connection(link_id)

    def forward_to_remote(self, link_id: int, data: bytes) -> None:
        remote_sock = self.connections.get(link_id)
        if remote_sock is None:
            logger.warning(f"Próba wysłania danych do nieistniejącego połączenia id={link_id}")
            return
        try:
            remote_sock.sendall(data)
        except OSError as exc:
            logger.warning(f"Błąd wysyłania do zdalnego hosta (id={link_id}): {exc}")

    def _remote_reader_loop(self, link_id: int, remote_sock: socket.socket) -> None:
        """Wątek czytający odpowiedzi ze zdalnego hosta i przekazujący je do klienta jako +IPD."""
        try:
            while True:
                data = remote_sock.recv(4096)
                if not data:
                    break
                logger.debug(f"[REMOTE id={link_id}] <- {hexdump_preview(data)}")
                self._deliver_incoming(link_id, data)
        except OSError:
            pass
        finally:
            self.connections.pop(link_id, None)
            self.send_to_client(self._format_closed(link_id))
            logger.info(f"Zdalne połączenie id={link_id} zamknięte")

    def _deliver_incoming(self, link_id: int, data: bytes) -> None:
        if self.cipmux:
            prefix = f"+IPD,{link_id},{len(data)}:".encode()
        else:
            prefix = f"+IPD,{len(data)}:".encode()
        self.send_to_client(prefix + data)

    def _format_closed(self, link_id: int) -> bytes:
        if self.cipmux:
            return f"{link_id},CLOSED\r\n".encode()
        return b"CLOSED\r\n"

    # --- Tryb surowego wysyłania (AT+CIPSEND) ---

    def begin_raw_send(self, link_id, length):
        if link_id not in self.connections:
            self.log(f"[ODRZUCONO] CIPSEND dla nieistniejącego/zamkniętego połączenia id={link_id}")
            return False
        if length is None:
            self.raw_mode = ("transparent", link_id)
        else:
            self.raw_mode = ("exact", link_id, length)
        return True

    def feed_raw(self, data: bytes) -> bytes:
        """
        Konsumuje bajty zgodnie z aktualnym raw_mode (tryb po AT+CIPSEND).
        Zwraca niewykorzystaną resztę danych (do dalszego parsowania jako komendy AT).
        """
        while data and self.raw_mode is not None:
            mode = self.raw_mode

            if mode[0] == "exact":
                _, link_id, remaining = mode
                take = data[:remaining]
                data = data[len(take):]
                if take:
                    self.forward_to_remote(link_id, take)
                remaining -= len(take)
                if remaining <= 0:
                    self.raw_mode = None
                    self.send_to_client(b"\r\nSEND OK\r\n")
                else:
                    self.raw_mode = ("exact", link_id, remaining)

            elif mode[0] == "transparent":
                _, link_id = mode
                idx = data.find(b"+++")
                if idx == -1:
                    self.forward_to_remote(link_id, data)
                    data = b""
                else:
                    if idx > 0:
                        self.forward_to_remote(link_id, data[:idx])
                    self.raw_mode = None
                    self.send_to_client(b"OK\r\n")
                    data = data[idx + 3:]

        return data

    # --- Routing komend AT (uproszczone - bez starego is_streaming/"+++") ---

    def dispatch(self, raw_bytes: bytes) -> bytes:
        raw_str = raw_bytes.decode("utf-8", errors="ignore").strip()

        for regex, param_names, action, _pattern in self._compiled_routes:
            match = regex.match(raw_str)
            if match:
                params = dict(zip(param_names, match.groups()))
                return action.execute(params, self)

        return b"ERROR\r\n"


# =====================================================================
# 3. TABELA TRAS (Espressif AT Command Set - ESP8266/ESP-01)
# =====================================================================

COMMAND_ROUTES: List[Tuple[str, RouteAction]] = [

    # --- BASIC COMMANDS ---
    ("AT", PingAction),
    ("ATE{mode}", EchoAction),
    ("AT+RST", RestartAction),
    ("AT+GMR", VersionAction),
    ("AT+GSLP={time}", SleepAction),
    ("AT+RESTORE", RestoreAction),
    ("AT+UART_CUR={baud},{databits},{stopbits},{parity},{flow}", GenericAction("UART_CUR")),
    ("AT+UART_DEF={baud},{databits},{stopbits},{parity},{flow}", GenericAction("UART_DEF")),
    ("AT+SLEEP={mode}", GenericAction("SLEEP")),
    ("AT+RFPOWER={power}", GenericAction("RFPOWER")),
    ("AT+RFVDD={voltage}", GenericAction("RFVDD")),
    ("AT+SYSRAM?", GenericAction("SYSRAM_QUERY", b"+SYSRAM:28418\r\n\r\nOK\r\n")),
    ("AT+SYSMSG_CUR={mode}", GenericAction("SYSMSG_CUR")),
    ("AT+SYSLOG={enable}", GenericAction("SYSLOG")),
    ("AT+SYSFLASH={write},{partition},{length}", GenericAction("SYSFLASH")),

    # --- WI-FI: STATION / ACCESS POINT ---
    ("AT+CWMODE?", WifiModeQueryAction),
    ("AT+CWMODE={mode}", WifiModeAction),
    ("AT+CWMODE_CUR={mode}", GenericAction("CWMODE_CUR")),
    ("AT+CWMODE_DEF={mode}", GenericAction("CWMODE_DEF")),
    ("AT+CWJAP?", JoinApQueryAction),
    ('AT+CWJAP="{ssid}","{password}"', JoinApAction),
    ('AT+CWJAP_CUR="{ssid}","{password}"',
        GenericAction("CWJAP_CUR", b"WIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n")),
    ('AT+CWJAP_DEF="{ssid}","{password}"',
        GenericAction("CWJAP_DEF", b"WIFI CONNECTED\r\nWIFI GOT IP\r\n\r\nOK\r\n")),
    ("AT+CWLAPOPT={reserved},{mask}", GenericAction("CWLAPOPT")),
    ("AT+CWLAP", ListApAction),
    ("AT+CWQAP", QuitApAction),
    ('AT+CWSAP="{ssid}","{pwd}",{channel},{enc}', SetApConfigAction),
    ('AT+CWSAP_CUR="{ssid}","{pwd}",{channel},{enc}', GenericAction("CWSAP_CUR")),
    ('AT+CWSAP_DEF="{ssid}","{pwd}",{channel},{enc}', GenericAction("CWSAP_DEF")),
    ("AT+CWLIF", ListStationsAction),
    ("AT+CWDHCP={operate},{mode}", GenericAction("CWDHCP")),
    ("AT+CWDHCP_CUR={operate},{mode}", GenericAction("CWDHCP_CUR")),
    ("AT+CWDHCP_DEF={operate},{mode}", GenericAction("CWDHCP_DEF")),
    ("AT+CWDHCPS={enable},{lease},{start_ip},{end_ip}", GenericAction("CWDHCPS")),
    ("AT+CWAUTOCONN={enable}", GenericAction("CWAUTOCONN")),
    ('AT+CIPSTAMAC="{mac}"', GenericAction("CIPSTAMAC")),
    ('AT+CIPSTAMAC_CUR="{mac}"', GenericAction("CIPSTAMAC_CUR")),
    ('AT+CIPSTAMAC_DEF="{mac}"', GenericAction("CIPSTAMAC_DEF")),
    ('AT+CIPAPMAC="{mac}"', GenericAction("CIPAPMAC")),
    ('AT+CIPAPMAC_CUR="{mac}"', GenericAction("CIPAPMAC_CUR")),
    ('AT+CIPAPMAC_DEF="{mac}"', GenericAction("CIPAPMAC_DEF")),
    ('AT+CIPSTA="{ip}","{gateway}","{netmask}"', GenericAction("CIPSTA")),
    ('AT+CIPSTA_CUR="{ip}","{gateway}","{netmask}"', GenericAction("CIPSTA_CUR")),
    ('AT+CIPSTA_DEF="{ip}","{gateway}","{netmask}"', GenericAction("CIPSTA_DEF")),
    ('AT+CIPAP="{ip}","{gateway}","{netmask}"', GenericAction("CIPAP")),
    ('AT+CIPAP_CUR="{ip}","{gateway}","{netmask}"', GenericAction("CIPAP_CUR")),
    ('AT+CIPAP_DEF="{ip}","{gateway}","{netmask}"', GenericAction("CIPAP_DEF")),
    ("AT+CIFSR", GetIpAction),
    ("AT+CWSTARTSMART={type}", GenericAction("CWSTARTSMART")),
    ("AT+CWSTOPSMART", GenericAction("CWSTOPSMART")),
    ("AT+WPS={enable}", GenericAction("WPS")),
    ('AT+MDNS={enable},"{hostname}","{service}",{port}', GenericAction("MDNS")),

    # --- TCP / IP (realna obsługa połączeń) ---
    ("AT+CIPSTATUS", CipStatusAction),
    ('AT+CIPDOMAIN="{domain}"', GenericAction("CIPDOMAIN", b'+CIPDOMAIN:93.184.216.34\r\n\r\nOK\r\n')),

    ('AT+CIPSTART="{type}","{ip}",{port}', ConnectAction),
    ('AT+CIPSTART={id},"{type}","{ip}",{port}', ConnectMuxAction),

    ("AT+CIPSSLSIZE={size}", GenericAction("CIPSSLSIZE")),

    ("AT+CIPSEND", CipSendAction),
    ("AT+CIPSEND={length}", CipSendLenAction),
    ("AT+CIPSEND={id},{length}", CipSendMuxLenAction),
    ("AT+CIPSENDEX={length}", GenericAction("CIPSENDEX")),  # rzadko używane - stub

    ("AT+CIPBUFRESET={id}", GenericAction("CIPBUFRESET")),
    ("AT+CIPBUFSTATUS={id}", GenericAction("CIPBUFSTATUS", b"+CIPBUFSTATUS:0,0,5744,0,0\r\n\r\nOK\r\n")),
    ("AT+CIPCHECKSEQ={id},{seq}", GenericAction("CIPCHECKSEQ")),

    ("AT+CIPCLOSE", CipCloseAction),
    ("AT+CIPCLOSE={id}", CipCloseMuxAction),

    ("AT+CIPMUX={mode}", CipMuxAction),
    ("AT+CIPSERVER={mode},{port}", CipServerAction),  # nasłuch TCP po stronie ESP - niezaimplementowany
    ("AT+CIPSERVERMAXCONN={num}", GenericAction("CIPSERVERMAXCONN")),
    ("AT+CIPMODE={mode}", CipModeAction),
    ('AT+SAVETRANSLINK={enable},"{ip}",{port},{type}', GenericAction("SAVETRANSLINK")),
    ("AT+CIPSTO={time}", CipStoAction),
    ("AT+CIPDINFO={enable}", CipDinfoAction),
    ('AT+PING="{ip}"', PingHostAction),
    ("AT+CIUPDATE", CiUpdateAction),
]


# =====================================================================
# 4. TCP SERVER / BRIDGE
# =====================================================================

DEFAULT_SERIAL_PORT = "COM1" if sys.platform.startswith("win") else "/dev/ttyUSB0"


class BridgeMode:
    EMULATE = "emulate"
    SERIAL = "bridge"


class ATTCPServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True

    def __init__(self, server_address, handler_cls, *, mode: str, routes,
                 serial_port: str, baudrate: int, serial_timeout: float):
        super().__init__(server_address, handler_cls)
        self.mode = mode
        self.routes = routes
        self.serial_port = serial_port
        self.baudrate = baudrate
        self.serial_timeout = serial_timeout
        self.serial_lock = threading.Lock()


class ATRequestHandler(socketserver.BaseRequestHandler):
    """Obsługa pojedynczego połączenia TCP - emulacja (z realnym TCP relay) lub bridge do UART."""

    def setup(self):
        addr = self.client_address
        threading.current_thread().name = f"Client-{addr[0]}:{addr[1]}"

    def handle(self):
        addr = self.client_address
        logger.info(f"Klient połączony: {addr[0]}:{addr[1]}")
        try:
            if self.server.mode == BridgeMode.EMULATE:
                self._handle_emulate()
            else:
                self._handle_serial_bridge()
        except (ConnectionResetError, BrokenPipeError, OSError) as exc:
            logger.warning(f"Błąd połączenia {addr[0]}:{addr[1]}: {exc}")
        except Exception:
            logger.exception(f"Niespodziewany błąd obsługi klienta {addr[0]}:{addr[1]}")
        finally:
            logger.info(f"Klient rozłączony: {addr[0]}:{addr[1]}")

    def _handle_emulate(self):
        sock = self.request
        dispatcher = ATCommandDispatcher(self.server.routes, sock)
        buffer = b""

        try:
            while True:
                data = sock.recv(4096)
                if not data:
                    break

                # 1. Jeśli jesteśmy w trybie surowym (po AT+CIPSEND) - konsumuj surowe bajty
                if dispatcher.raw_mode is not None:
                    data = dispatcher.feed_raw(data)
                    if not data:
                        continue  # wszystko zużyte jako dane surowe, czekaj na kolejny recv()

                # 2. Standardowy parser linii komend AT
                buffer += data
                while b"\n" in buffer and dispatcher.raw_mode is None:
                    line, buffer = buffer.split(b"\n", 1)
                    clean = line.rstrip(b"\r")
                    if clean:
                        logger.debug(f"RX: {clean!r}")
                        response = dispatcher.dispatch(clean + b"\r\n")
                        if response:
                            logger.debug(f"TX: {response!r}")
                            dispatcher.send_to_client(response)

                    # Jeśli komenda przełączyła nas w tryb surowy (np. AT+CIPSEND),
                    # reszta bufora (jeśli coś już przyszło w tym samym pakiecie)
                    # musi być natychmiast potraktowana jako dane surowe.
                    if dispatcher.raw_mode is not None and buffer:
                        buffer = dispatcher.feed_raw(buffer)
        finally:
            dispatcher.close_all_connections()

    def _handle_serial_bridge(self):
        sock = self.request

        if serial is None:
            logger.error("Próba użycia trybu 'bridge' bez zainstalowanego pyserial")
            sock.sendall(b"ERROR: pyserial nie jest zainstalowany (pip install pyserial)\r\n")
            return

        if not self.server.serial_lock.acquire(blocking=False):
            logger.warning(f"Port {self.server.serial_port} zajęty przez innego klienta")
            sock.sendall(b"ERROR: Port szeregowy jest obecnie zajety przez innego klienta\r\n")
            return

        ser = None
        try:
            ser = serial.Serial(
                self.server.serial_port,
                baudrate=self.server.baudrate,
                timeout=self.server.serial_timeout,
            )
            logger.info(f"Otwarto port szeregowy {self.server.serial_port} @ {self.server.baudrate} bd")

            stop_event = threading.Event()

            def pump_tcp_to_serial():
                while not stop_event.is_set():
                    try:
                        data = sock.recv(4096)
                    except OSError:
                        break
                    if not data:
                        break
                    logger.debug(f"TCP->UART {hexdump_preview(data)}")
                    try:
                        ser.write(data)
                    except serial.SerialException as exc:
                        logger.error(f"Błąd zapisu do portu szeregowego: {exc}")
                        break
                stop_event.set()

            def pump_serial_to_tcp():
                while not stop_event.is_set():
                    try:
                        waiting = ser.in_waiting or 1
                        data = ser.read(waiting)
                    except serial.SerialException as exc:
                        logger.error(f"Błąd odczytu z portu szeregowego: {exc}")
                        break
                    if data:
                        logger.debug(f"UART->TCP {hexdump_preview(data)}")
                        try:
                            sock.sendall(data)
                        except OSError:
                            break
                stop_event.set()

            t_in = threading.Thread(target=pump_tcp_to_serial, daemon=True, name="TCP->UART")
            t_out = threading.Thread(target=pump_serial_to_tcp, daemon=True, name="UART->TCP")
            t_in.start()
            t_out.start()
            t_in.join()
            t_out.join()

        except serial.SerialException as exc:
            err = f"ERROR: Nie mozna otworzyc portu {self.server.serial_port}: {exc}\r\n"
            try:
                sock.sendall(err.encode())
            except OSError:
                pass
            logger.error(f"Nie można otworzyć portu {self.server.serial_port}: {exc}")
        finally:
            if ser is not None and ser.is_open:
                ser.close()
                logger.info(f"Zamknięto port szeregowy {self.server.serial_port}")
            self.server.serial_lock.release()


def run_server(args: argparse.Namespace) -> None:
    server = ATTCPServer(
        (args.host, args.port),
        ATRequestHandler,
        mode=args.mode,
        routes=COMMAND_ROUTES,
        serial_port=args.serial_port,
        baudrate=args.baudrate,
        serial_timeout=args.serial_timeout,
    )

    if args.mode == BridgeMode.EMULATE:
        mode_desc = "EMULACJA (z realnym TCP relay dla CIPSTART/CIPSEND/CIPCLOSE)"
    else:
        mode_desc = f"BRIDGE -> {args.serial_port} @ {args.baudrate} bd"
        if serial is None:
            mode_desc += "  [UWAGA: brak pyserial - zainstaluj: pip install pyserial]"

    logger.info("=== ESP-01 UART Bridge / Emulator ===")
    logger.info(f"Nasłuchiwanie na: {args.host}:{args.port}")
    logger.info(f"Tryb pracy: {mode_desc}")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        logger.info("Zatrzymywanie serwera (Ctrl+C)...")
    finally:
        server.server_close()
        logger.info("Serwer zatrzymany.")


# =====================================================================
# 5. OBSŁUGA CLI (argparse)
# =====================================================================

def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="esp01_bridge.py",
        description="Emulator / bridge komunikacji UART dla modułu ESP8266 (ESP-01) przez TCP.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=textwrap.dedent(f"""\
            Przykłady użycia:

              # Serwer TCP w trybie EMULACJI (realny TCP relay, nie potrzeba sprzętu):
              %(prog)s --serve

              # Serwer TCP jako BRIDGE do prawdziwego ESP-01 na USB (Windows):
              %(prog)s --serve --mode bridge --serial-port COM3 --baudrate 115200

              # To samo na Linuksie, z logowaniem do pliku (z rotacją):
              %(prog)s --serve --mode bridge --serial-port /dev/ttyUSB0 \\
                  --log-file bridge.log --log-max-bytes 1048576 --log-backup-count 5

              # Szczegółowy log (z zrzutem hex ruchu) na konsoli:
              %(prog)s --serve -v

              # Tryby lokalne (bez serwera TCP):
              %(prog)s -i
        """),
    )

    mode_group = parser.add_mutually_exclusive_group()
    mode_group.add_argument("--serve", action="store_true",
        help="Uruchom serwer TCP (bridge/emulator) nasłuchujący na porcie (domyślnie 9100).")
    mode_group.add_argument("-i", "--interactive", action="store_true",
        help="Uruchom interaktywną konsolę (REPL) lokalnie, bez serwera TCP (bez relay TCP).")
    mode_group.add_argument("--list-routes", action="store_true",
        help="Wypisz listę wszystkich zarejestrowanych komend AT i zakończ.")

    server_group = parser.add_argument_group("Opcje serwera TCP (--serve)")
    server_group.add_argument("--host", default="0.0.0.0",
        help="Adres nasłuchiwania serwera TCP (domyślnie 0.0.0.0).")
    server_group.add_argument("--port", type=int, default=9100,
        help="Port TCP do nasłuchiwania (domyślnie 9100).")
    server_group.add_argument("--mode", choices=[BridgeMode.EMULATE, BridgeMode.SERIAL],
        default=BridgeMode.EMULATE,
        help="'emulate' - realny TCP relay, 'bridge' - przekazuje do portu szeregowego.")
    server_group.add_argument("--serial-port", default=DEFAULT_SERIAL_PORT,
        help=f"Port szeregowy dla trybu 'bridge' (domyślnie: {DEFAULT_SERIAL_PORT}).")
    server_group.add_argument("--baudrate", type=int, default=115200,
        help="Prędkość transmisji UART w trybie 'bridge' (domyślnie 115200).")
    server_group.add_argument("--serial-timeout", type=float, default=1.0,
        help="Timeout odczytu z portu szeregowego w sekundach (domyślnie 1.0).")

    log_group = parser.add_argument_group("Logowanie")
    log_group.add_argument("--log-file", metavar="PLIK",
        help="Zapisuj pełny log (DEBUG) do wskazanego pliku, niezależnie od poziomu na konsoli.")
    log_group.add_argument("--log-max-bytes", type=int, default=5 * 1024 * 1024,
        help="Maksymalny rozmiar pliku logu przed rotacją, w bajtach (domyślnie 5 MB).")
    log_group.add_argument("--log-backup-count", type=int, default=3,
        help="Liczba zachowywanych zrotowanych plików logu (domyślnie 3).")
    log_group.add_argument("-v", "--verbose", action="store_true",
        help="Pokaż szczegółowe logi DEBUG na konsoli (m.in. zrzut hex przesyłanych danych).")
    log_group.add_argument("-q", "--quiet", action="store_true",
        help="Wycisz konsolę do poziomu WARNING. Plik logu nadal zbiera wszystko.")

    return parser


def print_routes(dispatcher: ATCommandDispatcher) -> None:
    routes = dispatcher.describe_routes()
    print(f"Zarejestrowane komendy AT ({len(routes)}):\n")
    for pattern, label in routes:
        print(f"  {pattern:60} -> {label}")


def run_interactive() -> None:
    """
    Tryb lokalny bez prawdziwego gniazda TCP - dispatcher wymaga client_sock,
    więc podstawiamy lekki obiekt-atrapę tylko do testowania komend AT
    niezwiązanych z realnym TCP relay (CIPSTART/CIPSEND nie zadziałają w pełni bez sieci).
    """
    class _DummySock:
        def sendall(self, data: bytes) -> None:
            sys.stdout.write(data.decode("utf-8", errors="replace"))

    dispatcher = ATCommandDispatcher(COMMAND_ROUTES, _DummySock())
    print("Tryb interaktywny (lokalny, bez TCP). Użyj --serve do pełnej funkcjonalności sieciowej.")
    print("Wpisz komendę AT i naciśnij Enter. 'exit'/'quit' aby zakończyć.\n")
    try:
        while True:
            try:
                line = input("AT> ")
            except EOFError:
                print()
                break
            stripped = line.strip()
            if stripped.lower() in ("exit", "quit"):
                break
            if not stripped:
                continue
            response = dispatcher.dispatch(stripped.encode() + b"\r\n")
            sys.stdout.write(response.decode("utf-8", errors="replace"))
            if not response.endswith(b"\n"):
                sys.stdout.write("\n")
    except KeyboardInterrupt:
        print("\nPrzerwano (Ctrl+C).")


def main(argv: List[str] = None) -> None:
    parser = build_arg_parser()
    args = parser.parse_args(argv)

    setup_logging(args)

    if args.serve:
        run_server(args)
        return

    if args.list_routes:
        dummy = ATCommandDispatcher(COMMAND_ROUTES, socket.socket())
        print_routes(dummy)
        return

    if args.interactive:
        run_interactive()
        return

    parser.print_help()


if __name__ == "__main__":
    main()