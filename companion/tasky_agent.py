#!/usr/bin/env python3
"""Tell a Tasky (Agents edition) what your AI coding agents are doing, and
answer their permission prompts with Tasky's knob.

Tasky shows each session on its Agents screen and breathes its light bar while
an agent is waiting for you.

Claude Code hooks (see companion/README.md, or run install.sh):
    tasky_agent.py hook working   # UserPromptSubmit
    tasky_agent.py hook waiting   # Notification (permission / input needed)
    tasky_agent.py hook done      # Stop
    tasky_agent.py hook end       # SessionEnd
    tasky_agent.py ask            # PermissionRequest: allow / deny on Tasky
  The hook JSON arrives on stdin (session_id, cwd, message, tool_name, ...).

Any other tool or script:
    tasky_agent.py send --id build-42 --label "CI" --state working --msg "Running tests"

How it talks to Tasky: one small background process (`tasky_agent.py daemon`,
started on demand) owns the USB serial port; hooks talk to it over a Unix
socket in ~/.tasky/. That lets a permission question wait for the knob while
other sessions keep reporting. The daemon quits after 2 hours without news.

Never fails loudly: if Tasky isn't plugged in, hooks exit 0 at once and Claude
Code carries on (permission prompts then show in the terminal as usual).

Needs only Python 3 on macOS / Linux. Port: auto-detected (Espressif USB,
vendor 0x303A, or the first USB modem that answers "TASKY?"), or set
TASKY_PORT=/dev/cu.usbmodemXXXX. pyserial is used for detection if installed.
"""
import argparse
import glob
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
STATES = ("working", "waiting", "done", "end")
HOME = os.path.join(os.path.expanduser("~"), ".tasky")
SOCK = os.environ.get("TASKY_SOCK", os.path.join(HOME, "agent.sock"))
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


def candidate_ports():
    if os.environ.get("TASKY_PORT"):
        return [os.environ["TASKY_PORT"]]
    try:  # precise: only Espressif devices
        from serial.tools import list_ports

        return [p.device for p in list_ports.comports() if p.vid == ESPRESSIF_VID]
    except ImportError:
        return sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))


def open_tasky():
    """Open the first port that answers like a Tasky: (port, status) or (None, None).
    status is the TASKY? reply (edition, version)."""
    for path in candidate_ports():
        try:
            port = SerialPort(path)
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
        self.last_activity = time.time()
        self.scanned = threading.Event()  # first port search finished

    def connected(self):
        # A hook that started the daemon must not lose its message while the
        # first search for Tasky is still running.
        self.scanned.wait(3)
        return self.port is not None

    def agents_edition(self):
        """Only Tasky for Agents shows agent cards."""
        return self.connected() and self.status.get("edition") == "agents"

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

    def serial_loop(self):
        while True:
            if not self.port:
                port, status = open_tasky()
                with self.lock:
                    self.port, self.status, self.broken = port, status or {}, False
                self.scanned.set()
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
            self.last_activity = time.time()
            if req.get("op") == "send":
                reply = {"ok": self.agents_edition() and self.write(req["line"])}
            elif req.get("op") == "ask":
                reply = {"choice": self.ask(conn, req) if self.can_ask() else None}
            elif req.get("op") == "ping":
                reply = {"ok": True, "connected": self.agents_edition(), "asks": self.can_ask(),
                         "version": self.status.get("v"), "edition": self.status.get("edition")}
            else:
                reply = {"ok": False}
            conn.sendall((json.dumps(reply) + "\n").encode())
        except (OSError, ValueError, KeyError):
            pass
        finally:
            conn.close()

    def ask(self, conn, req):
        rid = req["id"]
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

    def run(self):
        import fcntl

        os.makedirs(HOME, exist_ok=True)
        lock = open(os.path.join(HOME, "daemon.lock"), "w")
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError:
            return  # another daemon already runs
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
        os.unlink(SOCK)


# --------------------------------------------------------------------- client
def request(req, timeout=2.0, start=True):
    """Send one request to the daemon (starting it if needed). None on failure."""
    for attempt in range(12):
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.settimeout(timeout)
            s.connect(SOCK)
            break
        except (OSError, AttributeError):
            s.close()
            if not start:
                return None
            if attempt == 0:
                os.makedirs(HOME, exist_ok=True)
                log = open(os.path.join(HOME, "daemon.log"), "a")
                subprocess.Popen([sys.executable, os.path.abspath(__file__), "daemon"],
                                 stdin=subprocess.DEVNULL, stdout=log, stderr=log,
                                 start_new_session=True)
            time.sleep(0.15)
    else:
        return None
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
    reply = request({"op": "send", "line": agent_line(payload)})
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
    }.get(state, "")
    return {"id": session, "label": project, "state": state, "msg": msg}


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
    reply = request({"op": "ask", "id": rid, "timeout": timeout,
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
    "UserPromptSubmit": "hook working",
    "Notification": "hook waiting",
    "Stop": "hook done",
    "SessionEnd": "hook end",
    "PermissionRequest": "ask",
}


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
            hook = {"type": "command", "command": f'python3 "{script_path}" {args}'}
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


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    h = sub.add_parser("hook", help="called by a Claude Code hook (reads JSON on stdin)")
    h.add_argument("state", choices=STATES)
    sub.add_parser("ask", help="PermissionRequest hook: answer on Tasky (reads JSON on stdin)")
    s = sub.add_parser("send", help="send a status by hand")
    s.add_argument("--id", required=True)
    s.add_argument("--label", default="")
    s.add_argument("--state", required=True, choices=STATES)
    s.add_argument("--msg", default="")
    sub.add_parser("daemon", help="own the USB port (started automatically)")
    sub.add_parser("status", help="is Tasky connected?")
    i = sub.add_parser("install", help="add the hooks to Claude Code's settings.json")
    i.add_argument("--uninstall", action="store_true", help="remove them instead")
    i.add_argument("--settings", default=os.path.join(os.path.expanduser("~"), ".claude", "settings.json"))
    args = ap.parse_args()

    if args.cmd == "daemon":
        Daemon().run()
    elif args.cmd == "hook":
        send(from_hook(args.state, read_hook_event()))
    elif args.cmd == "ask":
        decision = ask_from_hook(read_hook_event())
        if decision:
            print(json.dumps(decision))
    elif args.cmd == "install":
        try:
            print(install_hooks(args.settings, os.path.abspath(__file__), args.uninstall))
        except ValueError as e:
            print(f"{args.settings} isn't valid JSON ({e}); fix it first, nothing was changed.")
            sys.exit(1)
    elif args.cmd == "status":
        reply = request({"op": "ping"}, timeout=6)
        if reply and reply.get("connected"):
            print(f"Tasky for Agents {reply.get('version')} connected"
                  + ("" if reply.get("asks") else " (update to 2.12 to answer permission prompts)"))
        elif reply and reply.get("edition"):
            print("This Tasky runs the standard firmware: flash Tasky for Agents at tasky-flash.vercel.app")
            sys.exit(1)
        else:
            print("No Tasky found (plug it in, close the web setup page, or set TASKY_PORT)")
            sys.exit(1)
    else:
        ok = send({"id": args.id, "label": args.label, "state": args.state, "msg": args.msg})
        print("sent" if ok else "No Tasky found (plug it in, close the web setup page, or set TASKY_PORT)")
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
