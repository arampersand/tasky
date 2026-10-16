#!/usr/bin/env python3
"""Tell a Tasky (Agents edition) what your AI coding agents are doing, and
answer their permission prompts with Tasky's knob.

Tasky shows each session on its Agents screen and breathes its light bar while
an agent is waiting for you.

Claude Code hooks (the Claude Code plugin sets them up; or run install.sh):
    tasky_agent.py hook start     # SessionStart: a "Ready" card
    tasky_agent.py hook working   # UserPromptSubmit
    tasky_agent.py hook waiting   # Notification (permission / input needed)
    tasky_agent.py hook done      # Stop
    tasky_agent.py hook end       # SessionEnd
    tasky_agent.py ask            # PermissionRequest: allow / deny on Tasky
  The hook JSON arrives on stdin (session_id, cwd, message, tool_name, ...).

Any other tool or script:
    tasky_agent.py send --id build-42 --label "CI" --state working --msg "Running tests"

Something not working?
    tasky_agent.py doctor         # checks everything and says what to fix

How it talks to Tasky: one small background process (`tasky_agent.py daemon`,
started on demand) owns the connection; hooks talk to it over a Unix socket in
~/.tasky/. That lets a permission question wait for the knob while other
sessions keep reporting. The daemon quits after 2 hours without news.

USB or Wi-Fi: the first time Tasky (3.1+) is plugged in, the daemon saves its
pairing token in ~/.tasky/link.json. From then on, when Tasky isn't on USB,
it reaches it over Wi-Fi on the same network (TCP 7778, found again by a UDP
broadcast if its address changed), so Tasky can run on battery.

Never fails loudly: if Tasky isn't reachable, hooks exit 0 at once and Claude
Code carries on (permission prompts then show in the terminal as usual).

Needs only Python 3, on macOS, Linux or Windows. Port: auto-detected
(Espressif USB, vendor 0x303A, or the first USB serial port that answers
"TASKY?"), or set TASKY_PORT=/dev/cu.usbmodemXXXX (Windows: TASKY_PORT=COM5).
pyserial is used for detection if installed.
"""
import argparse
import glob
import platform
import json
import os
import select
import socket
import subprocess
import sys
import threading
import time
import unicodedata
import uuid

ESPRESSIF_VID = 0x303A
IS_WINDOWS = os.name == "nt"
# Hooks reach the daemon over a Unix socket; Windows (or TASKY_IPC=tcp, for
# tests) uses a localhost TCP port with a secret, both in ~/.tasky/agent.port
IPC_TCP = IS_WINDOWS or os.environ.get("TASKY_IPC") == "tcp"
STATES = ("working", "waiting", "done", "end")
HOOK_STATES = STATES + ("start",)  # start = a new Claude Code session
HOME = os.environ.get("TASKY_HOME") or os.path.join(os.path.expanduser("~"), ".tasky")
SOCK = os.environ.get("TASKY_SOCK", os.path.join(HOME, "agent.sock"))
PORT_FILE = SOCK + ".port" if os.environ.get("TASKY_SOCK") else os.path.join(HOME, "agent.port")
LINK = os.path.join(HOME, "link.json")  # Wi-Fi pairing, saved over USB
FIND_ADDR = os.environ.get("TASKY_FIND_ADDR", "255.255.255.255")  # where to look for Tasky
WIFI_RETRY_S = float(os.environ.get("TASKY_WIFI_RETRY", "10"))
IDLE_EXIT_S = 2 * 3600
ASK_TIMEOUT_S = int(os.environ.get("TASKY_ASK_TIMEOUT", "60"))
EDIT_TOOLS = ("Edit", "Write", "MultiEdit", "NotebookEdit")


def ascii_text(value, limit):
    """Tasky's screen fonts are ASCII-only: 'Canción ñ' -> 'Cancion n'."""
    text = unicodedata.normalize("NFKD", str(value)).encode("ascii", "ignore").decode()
    return " ".join(text.split())[:limit]


# ---------------------------------------------------------------- serial port
class SerialPort:
    """Raw 115200 8N1 serial port with the standard library (termios).

    The OS raises DTR and RTS when the port opens, which is harmless for the
    ESP32-C3/C6 USB-Serial/JTAG (it resets on RTS high *with DTR low*). We never
    toggle them, so opening the port never resets Tasky.
    """

    def __init__(self, path):
        import termios

        self.path = path
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        attrs = termios.tcgetattr(self.fd)
        iflag, oflag, cflag, lflag, _, _, cc = attrs
        iflag = 0
        oflag = 0
        lflag = 0
        cflag = (cflag & ~(termios.CSIZE | termios.PARENB | termios.CSTOPB)) | termios.CS8
        cflag |= termios.CREAD | termios.CLOCAL
        cflag &= ~termios.HUPCL  # don't drop DTR/RTS when we close
        cc[termios.VMIN] = 0
        cc[termios.VTIME] = 0
        speed = termios.B115200
        termios.tcsetattr(self.fd, termios.TCSANOW, [iflag, oflag, cflag, lflag, speed, speed, cc])
        self.buf = b""

    def write_line(self, line):
        data = (line + "\n").encode("utf-8")
        end = time.time() + 2
        while data:
            if time.time() > end:
                raise OSError("write timeout")
            try:
                n = os.write(self.fd, data)
                data = data[n:]
            except BlockingIOError:
                select.select([], [self.fd], [], 0.1)

    def read_lines(self, timeout):
        """Lines received within `timeout` seconds (may be empty)."""
        ready, _, _ = select.select([self.fd], [], [], timeout)
        if ready:
            try:
                chunk = os.read(self.fd, 4096)
            except BlockingIOError:
                chunk = b""
            if not chunk:
                raise OSError("Tasky unplugged")
            self.buf += chunk
        lines = []
        while b"\n" in self.buf:
            raw, self.buf = self.buf.split(b"\n", 1)
            lines.append(raw.decode("utf-8", "replace").strip())
        return lines

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass


class WinSerialPort:
    """The same serial port on Windows, with the Win32 API through ctypes.

    DTR and RTS stay off, like the web setup page: on the ESP32-C3/C6 USB
    port, RTS with DTR low resets the chip.
    """

    def __init__(self, path):
        import ctypes
        from ctypes import wintypes

        class DCB(ctypes.Structure):
            _fields_ = [("DCBlength", wintypes.DWORD), ("BaudRate", wintypes.DWORD),
                        ("flags", wintypes.DWORD), ("wReserved", wintypes.WORD),
                        ("XonLim", wintypes.WORD), ("XoffLim", wintypes.WORD),
                        ("ByteSize", wintypes.BYTE), ("Parity", wintypes.BYTE),
                        ("StopBits", wintypes.BYTE), ("XonChar", ctypes.c_char),
                        ("XoffChar", ctypes.c_char), ("ErrorChar", ctypes.c_char),
                        ("EofChar", ctypes.c_char), ("EvtChar", ctypes.c_char),
                        ("wReserved1", wintypes.WORD)]

        class COMMTIMEOUTS(ctypes.Structure):
            _fields_ = [(name, wintypes.DWORD) for name in (
                "ReadIntervalTimeout", "ReadTotalTimeoutMultiplier", "ReadTotalTimeoutConstant",
                "WriteTotalTimeoutMultiplier", "WriteTotalTimeoutConstant")]

        self.ctypes, self.wintypes = ctypes, wintypes
        self.k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        self.k32.CreateFileW.restype = wintypes.HANDLE
        self.path = path
        name = path if path.startswith("\\\\.\\") else "\\\\.\\" + path  # COM10 and up need this
        self.handle = self.k32.CreateFileW(name, 0xC0000000, 0, None, 3, 0, None)  # read+write, OPEN_EXISTING
        if self.handle in (None, wintypes.HANDLE(-1).value):
            raise OSError(ctypes.get_last_error(), f"can't open {path}")
        dcb = DCB()
        dcb.DCBlength = ctypes.sizeof(DCB)
        self.k32.GetCommState(self.handle, ctypes.byref(dcb))
        dcb.BaudRate, dcb.ByteSize, dcb.Parity, dcb.StopBits = 115200, 8, 0, 0
        dcb.flags = 1  # fBinary; DTR and RTS control disabled, no flow control
        timeouts = COMMTIMEOUTS(0xFFFFFFFF, 0, 0, 0, 2000)  # reads return at once; writes wait up to 2 s
        if not (self.k32.SetCommState(self.handle, ctypes.byref(dcb))
                and self.k32.SetCommTimeouts(self.handle, ctypes.byref(timeouts))):
            err = ctypes.get_last_error()
            self.close()
            raise OSError(err, f"can't set up {path}")
        self.k32.EscapeCommFunction(self.handle, 6)  # CLRDTR
        self.k32.EscapeCommFunction(self.handle, 4)  # CLRRTS
        self.buf = b""

    def write_line(self, line):
        data = (line + "\n").encode("utf-8")
        done = self.wintypes.DWORD(0)
        if not self.k32.WriteFile(self.handle, data, len(data), self.ctypes.byref(done), None) \
                or done.value != len(data):
            raise OSError("write failed")

    def read_lines(self, timeout):
        end = time.time() + timeout
        chunk = self.ctypes.create_string_buffer(4096)
        got = self.wintypes.DWORD(0)
        while True:
            if not self.k32.ReadFile(self.handle, chunk, 4096, self.ctypes.byref(got), None):
                raise OSError("Tasky unplugged")
            if got.value:
                self.buf += chunk.raw[:got.value]
                break
            if time.time() >= end:
                break
            time.sleep(0.02)
        lines = []
        while b"\n" in self.buf:
            raw, self.buf = self.buf.split(b"\n", 1)
            lines.append(raw.decode("utf-8", "replace").strip())
        return lines

    def close(self):
        if getattr(self, "handle", None):
            self.k32.CloseHandle(self.handle)
            self.handle = None


def windows_ports():
    """USB serial ports (the usbser driver: ESP32 boards, not Bluetooth)."""
    import winreg

    ports = []
    try:
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DEVICEMAP\SERIALCOMM") as key:
            for i in range(winreg.QueryInfoKey(key)[1]):
                device, port, _ = winreg.EnumValue(key, i)
                if "usbser" in device.lower():
                    ports.append(port)
    except OSError:
        pass
    return sorted(ports)


def candidate_ports():
    if os.environ.get("TASKY_PORT"):
        return [os.environ["TASKY_PORT"]]
    try:  # precise: only Espressif devices
        from serial.tools import list_ports

        return [p.device for p in list_ports.comports() if p.vid == ESPRESSIF_VID]
    except ImportError:
        if IS_WINDOWS:
            return windows_ports()
        return sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))


def open_tasky():
    """Open the first port that answers like a Tasky: (port, status) or (None, None).
    status is the TASKY? reply (edition, version)."""
    for path in candidate_ports():
        try:
            port = WinSerialPort(path) if IS_WINDOWS else SerialPort(path)
        except OSError:
            continue
        try:
            port.write_line("TASKY?")
            end = time.time() + 1.5
            while time.time() < end:
                for line in port.read_lines(0.2):
                    if line.startswith("@tasky "):
                        try:
                            status = json.loads(line[7:])
                        except ValueError:
                            continue
                        if isinstance(status, dict) and "v" in status:
                            return port, status
        except OSError:
            pass
        port.close()
    return None, None


# ---------------------------------------------------------------------- Wi-Fi
class NetPort:
    """Tasky over Wi-Fi: the same lines as the serial port, on TCP."""

    def __init__(self, sock):
        self.sock = sock
        self.sock.settimeout(2)  # reads only after select() says data is there
        self.buf = b""
        self.path = "wifi"
        self.last_rx = time.time()
        self.pinged = False
        self.on_quiet = lambda: self.write_line("TASKY?")  # the daemon routes it through its lock

    def write_line(self, line):
        try:
            self.sock.sendall((line + "\n").encode("utf-8"))
        except socket.timeout:
            raise OSError("write timeout")

    def read_lines(self, timeout):
        ready, _, _ = select.select([self.sock], [], [], timeout)
        if ready:
            chunk = self.sock.recv(4096)  # socket.timeout is an OSError too
            if not chunk:
                raise OSError("Tasky closed the connection")
            if chunk:
                self.buf += chunk
                self.last_rx, self.pinged = time.time(), False
        # A Tasky that lost power or Wi-Fi never closes the socket: ask it
        # something now and then, and give up when it stays quiet. Generous:
        # Tasky's loop pauses up to ~40 s while it downloads a calendar.
        quiet = time.time() - self.last_rx
        if quiet > 30 and not self.pinged:
            self.pinged = True
            self.on_quiet()
        elif quiet > 90:
            raise OSError("Tasky stopped answering")
        lines = []
        while b"\n" in self.buf:
            raw, self.buf = self.buf.split(b"\n", 1)
            lines.append(raw.decode("utf-8", "replace").strip())
        return lines

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass


def save_link(status):
    """Remember how to reach this Tasky over Wi-Fi (its TASKY? reply on USB)."""
    link = status.get("link")
    if not isinstance(link, dict) or not link.get("token"):
        return
    data = {"id": link.get("id"), "port": link.get("port", 7778), "token": link["token"],
            "ip": (status.get("wifi") or {}).get("ip")}
    if load_link() == data:
        return
    os.makedirs(HOME, exist_ok=True)
    tmp = LINK + ".tmp"
    fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)  # the token is a secret
    if hasattr(os, "fchmod"):  # also when a leftover .tmp had looser permissions (not on Windows,
        os.fchmod(fd, 0o600)   # where your profile folder is already yours alone)
    with os.fdopen(fd, "w") as f:
        json.dump(data, f)
    os.replace(tmp, LINK)


def load_link():
    try:
        with open(LINK) as f:
            link = json.load(f)
        return link if isinstance(link, dict) and link.get("token") else None
    except (OSError, ValueError):
        return None


def find_tasky(link, timeout=1.5):
    """Ask the network where this Tasky is now: its IP, or None."""
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        s.settimeout(timeout)
        s.sendto(f"TASKY FIND {link.get('id')}\n".encode(), (FIND_ADDR, int(link["port"])))
        end = time.time() + timeout
        while time.time() < end:
            data, addr = s.recvfrom(512)
            text = data.decode("utf-8", "replace").strip()
            if not text.startswith("@tasky "):
                continue
            msg = json.loads(text[7:])
            if isinstance(msg, dict) and msg.get("id") == link.get("id"):
                return addr[0]
    except (OSError, ValueError, TypeError, KeyError) as e:
        print(f"tasky: no answer to the Wi-Fi search: {e!r}", file=sys.stderr)
    finally:
        s.close()
    return None


def open_wifi():
    """Tasky over Wi-Fi with the saved pairing: (port, status) or (None, None)."""
    link = load_link()
    if not link:
        return None, None
    try:
        port_no = int(link["port"])
    except (TypeError, ValueError, KeyError):
        return None, None
    hosts = [link["ip"]] if link.get("ip") else []
    for attempt in range(2):
        for host in hosts:
            try:
                sock = socket.create_connection((host, port_no), timeout=1.5)
            except OSError:
                continue
            port = NetPort(sock)
            try:
                port.write_line("TASKY AUTH " + link["token"])
                port.write_line("TASKY?")
                end = time.time() + 2
                authed = False
                while time.time() < end:
                    for line in port.read_lines(0.2):
                        if not line.startswith("@tasky "):
                            continue
                        msg = json.loads(line[7:])
                        if not isinstance(msg, dict):
                            continue
                        if msg.get("ok") is True:
                            authed = True
                        elif msg.get("ok") is False:
                            raise OSError(msg.get("error", "refused"))
                        elif authed and "v" in msg:
                            if host != link.get("ip"):
                                link["ip"] = host
                                save_link({"link": link, "wifi": {"ip": host}})
                            return port, msg
            except (OSError, ValueError):
                pass
            port.close()
        if attempt == 0:  # the saved address didn't answer: look for it
            found = find_tasky(link)
            if not found or found in hosts:
                break
            hosts = [found]
    return None, None


def version_tuple(v):
    try:
        return tuple(int(x) for x in str(v).split(".")[:3])
    except ValueError:
        return (0,)


# --------------------------------------------------------------------- daemon
class Daemon:
    def __init__(self):
        self.port = None
        self.status = {}
        self.broken = False  # a write failed: the reader thread closes the port
        self.lock = threading.Lock()
        self.answers = {}  # ask id -> choice (int), filled by the reader
        self.waiting = set()
        self.asking = {}  # question key -> request id on Tasky now
        self.last_activity = time.time()
        self.scanned = threading.Event()  # first port search finished
        self.wifi_tried = 0.0
        self.cards = {}  # session id -> (time, last TASKY AGENT line): replayed after a reconnect
        self.secret = None  # TCP mode: what a hook must send

    def connected(self):
        # A hook that started the daemon must not lose its message while the
        # first search for Tasky (USB, then Wi-Fi) is still running.
        self.scanned.wait(5)
        return self.port is not None

    def agents_edition(self):
        """Tasky 3.0+ (one firmware, "agents": true) or a 2.x Tasky for Agents."""
        return self.connected() and (self.status.get("agents") is True
                                     or self.status.get("edition") == "agents")

    def can_ask(self):
        """Permission questions need Tasky for Agents 2.12 or newer; older
        firmware would leave the terminal waiting for nothing."""
        return self.agents_edition() and version_tuple(self.status.get("v")) >= (2, 12, 0)

    def write(self, line):
        self.connected()  # wait for the first search
        with self.lock:
            if not self.port or self.broken:
                return False
            try:
                self.port.write_line(line)
                return True
            except OSError:
                self.broken = True  # only the reader closes it (its fd could be reused)
                return False

    def remember(self, line):
        """Keep each session's last card, so a Tasky that restarted (or a
        new connection) gets them back without waiting for the next event."""
        try:
            card = json.loads(line[len("TASKY AGENT "):])
        except ValueError:
            return
        if card.get("state") == "end":
            self.cards.pop(card.get("id"), None)
        else:
            self.cards[card.get("id")] = (time.time(), line)

    def replay(self):
        fresh = time.time() - IDLE_EXIT_S  # Tasky forgets cards after 2 hours too
        for when, line in sorted(self.cards.values()):
            if when > fresh:
                self.write(line)

    def serial_loop(self):
        while True:
            if not self.port:
                port, status = open_tasky()
                try:
                    if port:
                        save_link(status)  # pair for Wi-Fi while it's on USB
                    elif time.time() - self.wifi_tried > WIFI_RETRY_S:
                        self.wifi_tried = time.time()
                        port, status = open_wifi()
                        if port:
                            port.on_quiet = lambda: self.write("TASKY?")
                except Exception as e:  # never let the reader thread die
                    print(f"tasky: connection search failed: {e!r}", file=sys.stderr)
                with self.lock:
                    self.port, self.status, self.broken = port, status or {}, False
                self.scanned.set()
                if port:
                    self.replay()
                if not port:
                    time.sleep(2)
                    continue
            port = self.port  # only this thread replaces or closes it
            try:
                if self.broken:
                    raise OSError("write failed")
                for line in port.read_lines(0.5):
                    self.on_line(line)
            except OSError:
                with self.lock:
                    port.close()
                    self.port = None

    def on_line(self, line):
        if not line.startswith("@tasky "):
            return  # debug log
        try:
            msg = json.loads(line[7:])
        except ValueError:
            return
        answer = msg.get("answer") if isinstance(msg, dict) else None
        if isinstance(answer, dict) and answer.get("id") in self.waiting:
            self.answers[answer["id"]] = answer.get("choice", -1)

    def handle_client(self, conn):
        try:
            conn.settimeout(5)
            data = b""
            while b"\n" not in data:
                chunk = conn.recv(4096)
                if not chunk:
                    return
                data += chunk
            req = json.loads(data.split(b"\n", 1)[0])
            if self.secret and req.get("key") != self.secret:
                return  # TCP on localhost: only hooks that can read ~/.tasky/agent.port
            self.last_activity = time.time()
            if req.get("op") == "send":
                if req["line"].startswith("TASKY AGENT "):
                    self.remember(req["line"])
                reply = {"ok": self.agents_edition() and self.write(req["line"])}
            elif req.get("op") == "ask":
                reply = {"choice": self.ask(conn, req) if self.can_ask() else None}
            elif req.get("op") == "ping":
                reply = {"ok": True, "connected": self.agents_edition(), "asks": self.can_ask(),
                         "version": self.status.get("v"), "edition": self.status.get("edition"),
                         "via": "wifi" if isinstance(self.port, NetPort) else "usb" if self.port else None}
            else:
                reply = {"ok": False}
            conn.sendall((json.dumps(reply) + "\n").encode())
        except (OSError, ValueError, KeyError):
            pass
        finally:
            conn.close()

    def ask(self, conn, req):
        rid = req["id"]
        key = req.get("key")
        with self.lock:  # two hook processes can ask at the same instant
            if key and key in self.asking:
                return None  # the same question from a second set of hooks: the first one answers
            if key:
                self.asking[key] = rid
        self.waiting.add(rid)
        try:
            if not self.write(req["line"]):
                return None
            end = time.time() + float(req.get("timeout", ASK_TIMEOUT_S))
            conn.settimeout(None)
            while time.time() < end:
                if rid in self.answers:
                    return self.answers.pop(rid)
                # the hook was killed (Claude Code timeout, Ctrl+C): stop asking
                ready, _, _ = select.select([conn], [], [], 0.2)
                if ready and not conn.recv(1, socket.MSG_PEEK):
                    break
            self.write("TASKY ASK-CANCEL " + json.dumps({"id": rid}))
            return None
        finally:
            self.waiting.discard(rid)
            self.answers.pop(rid, None)
            if key and self.asking.get(key) == rid:
                del self.asking[key]

    def run(self):
        os.makedirs(HOME, exist_ok=True)
        lock = open(os.path.join(HOME, "daemon.lock"), "w")
        try:
            if IS_WINDOWS:
                import msvcrt

                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl

                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            return  # another daemon already runs
        with open(os.path.join(HOME, "daemon.pid"), "w") as f:
            f.write(str(os.getpid()))
        if IPC_TCP:
            # localhost only, on a free port; the port and a secret go in a file
            # in your home folder, so only you can talk to your Tasky
            server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            server.bind(("127.0.0.1", 0))
            self.secret = uuid.uuid4().hex
            tmp = PORT_FILE + ".tmp"
            fd = os.open(tmp, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            with os.fdopen(fd, "w") as f:
                f.write(f"{server.getsockname()[1]} {self.secret}")
            os.replace(tmp, PORT_FILE)
        else:
            if os.path.exists(SOCK):
                os.unlink(SOCK)
            server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            server.bind(SOCK)
            os.chmod(SOCK, 0o600)  # only you can talk to your Tasky
        server.listen(16)
        server.settimeout(5)
        threading.Thread(target=self.serial_loop, daemon=True).start()
        while time.time() - self.last_activity < IDLE_EXIT_S or self.waiting:
            try:
                conn, _ = server.accept()
            except socket.timeout:
                continue
            threading.Thread(target=self.handle_client, args=(conn,), daemon=True).start()
        server.close()
        try:
            os.unlink(PORT_FILE if IPC_TCP else SOCK)
        except OSError:
            pass


# --------------------------------------------------------------------- client
def connect_daemon(timeout):
    """A socket to the running daemon; OSError if there is none."""
    if not IPC_TCP:
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.settimeout(timeout)
        try:
            s.connect(SOCK)
        except OSError:
            s.close()
            raise
        return s, None
    try:
        with open(PORT_FILE) as f:
            port, secret = f.read().split()
    except (OSError, ValueError):
        raise OSError("no daemon")
    s = socket.create_connection(("127.0.0.1", int(port)), timeout=timeout)
    s.settimeout(timeout)
    return s, secret


def start_daemon():
    os.makedirs(HOME, exist_ok=True)
    log = open(os.path.join(HOME, "daemon.log"), "a")
    if IS_WINDOWS:  # in the background, with no console window
        flags = 0x00000200 | 0x08000000  # CREATE_NEW_PROCESS_GROUP | CREATE_NO_WINDOW
        subprocess.Popen([sys.executable, os.path.abspath(__file__), "daemon"],
                         stdin=subprocess.DEVNULL, stdout=log, stderr=log, creationflags=flags)
    else:
        subprocess.Popen([sys.executable, os.path.abspath(__file__), "daemon"],
                         stdin=subprocess.DEVNULL, stdout=log, stderr=log, start_new_session=True)


def request(req, timeout=2.0, start=True):
    """Send one request to the daemon (starting it if needed). None on failure."""
    for attempt in range(12):
        try:
            s, secret = connect_daemon(timeout)
            break
        except (OSError, AttributeError):
            if not start:
                return None
            if attempt == 0:
                start_daemon()
            time.sleep(0.15 if not IS_WINDOWS else 0.3)
    else:
        return None
    if secret:
        req = dict(req, key=secret)
    try:
        s.sendall((json.dumps(req) + "\n").encode())
        data = b""
        while b"\n" not in data:
            chunk = s.recv(4096)
            if not chunk:
                return None
            data += chunk
        return json.loads(data.split(b"\n", 1)[0])
    except (OSError, ValueError):
        return None
    finally:
        s.close()


def agent_line(payload):
    payload = dict(payload)
    for key, limit in (("id", 39), ("label", 23), ("msg", 63)):
        if key in payload:
            payload[key] = ascii_text(payload[key], limit)
    return "TASKY AGENT " + json.dumps(payload)


def send(payload):
    reply = request({"op": "send", "line": agent_line(payload)}, timeout=7)
    return bool(reply and reply.get("ok"))


def read_hook_event():
    try:
        return json.load(sys.stdin)
    except Exception:
        return {}


def from_hook(state, event):
    session = str(event.get("session_id") or "claude")
    project = os.path.basename(str(event.get("cwd") or "")) or "Claude"
    msg = {
        "waiting": event.get("message") or "Needs your input",
        "done": "Finished",
        "working": "Working",
        "start": "Ready",
    }.get(state, "")
    return {"id": session, "label": project, "state": "done" if state == "start" else state, "msg": msg}


def describe_tool(event):
    """What Claude wants to do, in a line that fits Tasky's screen."""
    tool = str(event.get("tool_name") or "Tool")
    args = event.get("tool_input") or {}
    if not isinstance(args, dict):
        args = {}
    if tool == "Bash":
        detail = args.get("command", "")
    elif tool in EDIT_TOOLS:
        detail = os.path.basename(str(args.get("file_path") or args.get("notebook_path") or ""))
    elif tool in ("WebFetch", "WebSearch"):
        detail = args.get("url") or args.get("query") or ""
    else:
        detail = next((v for v in args.values() if isinstance(v, str)), "")
    return tool, ascii_text(detail, 60)


def ask_from_hook(event, timeout=ASK_TIMEOUT_S):
    """PermissionRequest hook: show the question on Tasky, print the decision."""
    tool, detail = describe_tool(event)
    options = ["Allow", "Deny"]
    edits = tool in EDIT_TOOLS and event.get("permission_mode") == "default"
    if edits:
        options.append("Allow edits")
    rid = uuid.uuid4().hex[:12]
    payload = {
        "id": rid,
        "session": ascii_text(event.get("session_id") or "claude", 39),
        "label": ascii_text(os.path.basename(str(event.get("cwd") or "")) or "Claude", 23),
        "tool": ascii_text(tool, 23),
        "detail": detail,
        "options": options,
        "timeout": timeout,
    }
    # Quick check first, so an unplugged Tasky never delays the terminal prompt
    ping = request({"op": "ping"}, timeout=6)  # a fresh daemon may still be scanning
    if not ping or not ping.get("asks"):
        return None
    key = "|".join((payload["session"], payload["tool"], payload["detail"]))
    reply = request({"op": "ask", "id": rid, "key": key, "timeout": timeout,
                     "line": "TASKY ASK " + json.dumps(payload)}, timeout=timeout + 5)
    choice = reply.get("choice") if reply else None
    if choice is None or not isinstance(choice, int) or not 0 <= choice < len(options):
        return None  # timed out / long-pressed on Tasky: ask in the terminal
    out = {"hookEventName": "PermissionRequest"}
    if options[choice] == "Deny":
        out["decision"] = {"behavior": "deny", "message": "Denied on Tasky"}
    else:
        out["decision"] = {"behavior": "allow"}
        if options[choice] == "Allow edits":  # like "accept edits" in the terminal, this session only
            out["decision"]["updatedPermissions"] = [
                {"type": "setMode", "mode": "acceptEdits", "destination": "session"}]
    return {"hookSpecificOutput": out}


# ------------------------------------------------------------------ install
# Claude Code event -> what this script does. PermissionRequest waits on the
# knob, so its hook timeout leaves room for TASKY_ASK_TIMEOUT.
HOOKS = {
    "SessionStart": "hook start",
    "UserPromptSubmit": "hook working",
    "Notification": "hook waiting",
    "Stop": "hook done",
    "SessionEnd": "hook end",
    "PermissionRequest": "ask",
}


def python_command():
    """How a hook starts Python: python3 on macOS / Linux; on Windows (hooks
    run in Git Bash) the py launcher, or this very python.exe."""
    if not IS_WINDOWS:
        return "python3"
    import shutil

    return "py -3" if shutil.which("py") else '"' + sys.executable.replace("\\", "/") + '"'


def is_ours(hook):
    return isinstance(hook, dict) and "tasky_agent.py" in str(hook.get("command", ""))


def install_hooks(settings_path, script_path, uninstall=False):
    """Add (or remove) Tasky's hooks in a Claude Code settings.json.

    Keeps every other setting and hook, replaces older Tasky hooks (any command
    mentioning tasky_agent.py) and backs the file up once before changing it.
    Returns a short message.
    """
    settings = {}
    if os.path.exists(settings_path):
        with open(settings_path) as f:
            text = f.read()
        if text.strip():
            settings = json.loads(text)  # invalid JSON: stop, never clobber it
        if not isinstance(settings, dict) or not isinstance(settings.get("hooks", {}), dict):
            raise ValueError("expected an object with a \"hooks\" object")
        backup = settings_path + ".tasky-backup"
        if not os.path.exists(backup):
            with open(backup, "w") as f:
                f.write(text)
    hooks = settings.setdefault("hooks", {})
    for event in list(hooks):
        if not isinstance(hooks[event], list):
            continue  # not a shape we know: leave it exactly as it is
        groups = []
        for group in hooks[event]:
            if isinstance(group, dict) and isinstance(group.get("hooks"), list):
                group = dict(group, hooks=[h for h in group["hooks"] if not is_ours(h)])
                if not group["hooks"]:
                    continue
            groups.append(group)
        if groups:
            hooks[event] = groups
        else:
            del hooks[event]
    skipped = []
    if not uninstall:
        for event, args in HOOKS.items():
            if not isinstance(hooks.get(event, []), list):
                skipped.append(event)  # an unusual value we won't touch
                continue
            hook = {"type": "command", "command": f'{python_command()} "{script_path}" {args}'}
            if event == "PermissionRequest":
                hook["timeout"] = ASK_TIMEOUT_S + 30
            hooks.setdefault(event, []).append({"hooks": [hook]})
    if not hooks:
        del settings["hooks"]
    os.makedirs(os.path.dirname(settings_path) or ".", exist_ok=True)
    tmp = settings_path + ".tmp"
    with open(tmp, "w") as f:
        json.dump(settings, f, indent=2)
        f.write("\n")
    os.replace(tmp, settings_path)
    if uninstall:
        return "Removed Tasky's hooks"
    note = f" (skipped {', '.join(skipped)}: not a list in your settings)" if skipped else ""
    return f"Added Tasky's hooks to {settings_path}{note}"


# ------------------------------------------------------------------- doctor
def port_holders(path):
    """Names of the programs that have the port open (macOS / Linux), or []."""
    if IS_WINDOWS:
        return []  # no lsof; a busy COM port just fails to open
    try:
        out = subprocess.run(["lsof", "-Fc", path], capture_output=True, text=True, timeout=5).stdout
    except (OSError, subprocess.SubprocessError):
        return []
    return sorted({line[1:] for line in out.splitlines() if line.startswith("c")} - {"python3", "Python"})


def plugin_enabled(settings):
    return bool((settings.get("enabledPlugins") or {}).get("tasky@tasky"))


def settings_hooks(settings):
    hooks = settings.get("hooks") if isinstance(settings.get("hooks"), dict) else {}
    return sum(1 for groups in hooks.values() if isinstance(groups, list) for g in groups
               if isinstance(g, dict) for h in g.get("hooks", []) if is_ours(h))


def doctor(settings_path):
    """Check every link between Claude Code and Tasky. Returns True if all good."""
    problems = []

    def check(good, text, fix=""):
        print(("  ✓ " if good else "  ✗ ") + text)
        if not good:
            problems.append(fix)
        return good

    print("Tasky doctor")
    check(sys.version_info >= (3, 8), f"Python {platform.python_version()}", "Install Python 3.8 or newer")

    ping = request({"op": "ping"}, timeout=8) or {}
    if ping.get("edition"):
        via = " over Wi-Fi" if ping.get("via") == "wifi" else ""
        check(bool(ping.get("connected")), f"Tasky found{via}: {ping.get('edition')} {ping.get('version')}",
              "Update Tasky at https://tasky-flash.vercel.app (keeps your settings)")
        if ping.get("connected"):
            check(bool(ping.get("asks")), "Firmware answers permission prompts (2.12+)",
                  "Update Tasky at https://tasky-flash.vercel.app")
            if load_link():
                print("  ✓ Paired for Wi-Fi: works unplugged on the same network")
            else:
                print("  · Wi-Fi: plug Tasky (3.1+) in once to pair it, then it works unplugged")
    else:
        ports = candidate_ports()
        busy = {p: port_holders(p) for p in ports}
        held = [f"{p} (in use by {', '.join(h)})" for p, h in busy.items() if h]
        link = load_link()
        if not ports and link:
            check(False, "Tasky not found on USB or Wi-Fi",
                  "Check that Tasky is on and on the same Wi-Fi network as this computer, or plug it in")
        elif not ports:
            check(False, "No Tasky on USB", "Plug Tasky in with a data USB-C cable (charge-only cables don't work)")
        elif held:
            check(False, "Tasky's USB port is busy: " + "; ".join(held),
                  "Close that app or tab (usually tasky-flash.vercel.app), then try again")
        else:
            check(False, f"No answer from {', '.join(ports)}",
                  "Unplug and replug Tasky. If it still fails, reflash it at https://tasky-flash.vercel.app")

    settings = {}
    try:
        with open(settings_path) as f:
            settings = json.load(f)
    except (OSError, ValueError):
        pass
    plugin, manual = plugin_enabled(settings), settings_hooks(settings)
    if plugin and manual:
        check(False, "Hooks installed twice (Claude Code plugin and install.sh)",
              "Keep the plugin: curl -fsSL https://tasky-flash.vercel.app/agents/install.sh | sh -s -- --uninstall")
    else:
        check(plugin or manual > 0,
              "Claude Code hooks: " + ("Tasky plugin" if plugin else "install.sh" if manual else "not installed"),
              "In Claude Code run: /plugin marketplace add arampersand/tasky  then  /plugin install tasky@tasky")

    if problems:
        print("\nTo fix:" + "".join(f"\n  - {p}" for p in problems if p))
        return False
    print("\nAll good. Restart Claude Code if you just installed the hooks.")
    return True


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    h = sub.add_parser("hook", help="called by a Claude Code hook (reads JSON on stdin)")
    h.add_argument("state", choices=HOOK_STATES)
    sub.add_parser("ask", help="PermissionRequest hook: answer on Tasky (reads JSON on stdin)")
    s = sub.add_parser("send", help="send a status by hand")
    s.add_argument("--id", required=True)
    s.add_argument("--label", default="")
    s.add_argument("--state", required=True, choices=STATES)
    s.add_argument("--msg", default="")
    sub.add_parser("daemon", help="own the USB port (started automatically)")
    sub.add_parser("status", help="is Tasky connected?")
    dr = sub.add_parser("doctor", help="check everything and say what to fix")
    dr.add_argument("--settings", default=os.path.join(os.path.expanduser("~"), ".claude", "settings.json"))
    i = sub.add_parser("install", help="add the hooks to Claude Code's settings.json")
    i.add_argument("--uninstall", action="store_true", help="remove them instead")
    i.add_argument("--settings", default=os.path.join(os.path.expanduser("~"), ".claude", "settings.json"))
    args = ap.parse_args()

    if args.cmd == "daemon":
        Daemon().run()
    elif args.cmd == "hook":
        event = read_hook_event()
        # SessionStart also fires when Claude compacts its context mid-turn:
        # that's not a new session, keep the card as it is
        if not (args.state == "start" and event.get("source") == "compact"):
            send(from_hook(args.state, event))
    elif args.cmd == "ask":
        decision = ask_from_hook(read_hook_event())
        if decision:
            print(json.dumps(decision))
    elif args.cmd == "doctor":
        sys.exit(0 if doctor(args.settings) else 1)
    elif args.cmd == "install":
        try:
            print(install_hooks(args.settings, os.path.abspath(__file__), args.uninstall))
        except ValueError as e:
            print(f"{args.settings} isn't valid JSON ({e}); fix it first, nothing was changed.")
            sys.exit(1)
    elif args.cmd == "status":
        reply = request({"op": "ping"}, timeout=6)
        if reply and reply.get("connected"):
            print(f"Tasky {reply.get('version')} connected" + (" over Wi-Fi" if reply.get("via") == "wifi" else "")
                  + ("" if reply.get("asks") else " (update to 2.12 to answer permission prompts)"))
        elif reply and reply.get("edition"):
            print("This Tasky's firmware has no agent cards: update it at tasky-flash.vercel.app")
            sys.exit(1)
        else:
            print("No Tasky found (plug it in, close the web setup page, or set TASKY_PORT)")
            sys.exit(1)
    else:
        ok = send({"id": args.id, "label": args.label, "state": args.state, "msg": args.msg})
        print("sent" if ok else "No Tasky found (plug it in or check its Wi-Fi, close the web setup page, or set TASKY_PORT)")
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
