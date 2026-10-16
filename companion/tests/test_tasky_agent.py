"""Tests for tasky_agent.py against a fake Tasky on a pseudo-terminal (USB)
and a fake Tasky on localhost TCP (Wi-Fi).

Run: python3 -m unittest discover companion/tests
On Windows only the Wi-Fi and launcher tests run (no pseudo-terminals there).
"""
import json
import os
import select
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
import unittest

if os.name != "nt":
    import pty
    import tty

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.abspath(os.path.join(HERE, "..", "..", "claude-plugin", "tasky_agent.py"))


class FakeTasky:
    """Answers like the firmware: TASKY? status, TASKY AGENT ok, TASKY ASK -> answer."""

    def __init__(self, choice=0, answer_after=0.3, version="2.12.0", edition="agents", agents=None,
                 link=None):
        self.master, slave = pty.openpty()
        tty.setraw(slave)
        self.path = os.ttyname(slave)
        self.slave = slave  # keep it open so the pty stays alive
        self.choice = choice  # None = never answer
        self.answer_after = answer_after
        self.version, self.edition, self.agents = version, edition, agents
        self.link = link  # 3.1+: Wi-Fi pairing in the TASKY? reply
        self.lines = []
        self.stop = False
        threading.Thread(target=self.loop, daemon=True).start()

    def reply(self, obj):
        os.write(self.master, ("@tasky " + json.dumps(obj) + "\n").encode())

    def loop(self):
        buf = b""
        while not self.stop:
            try:
                r, _, _ = select.select([self.master], [], [], 0.1)
                if not r:
                    continue
                buf += os.read(self.master, 4096)
            except (OSError, ValueError):
                return  # closed by the test
            while b"\n" in buf:
                raw, buf = buf.split(b"\n", 1)
                line = raw.decode().strip()
                self.lines.append(line)
                os.write(self.master, b"[debug] some log line\n")  # noise the client must ignore
                if line == "TASKY?":
                    status = {"v": self.version, "edition": self.edition}
                    if self.agents is not None:
                        status["agents"] = self.agents
                    if self.link:
                        status["link"] = self.link
                        status["wifi"] = {"ip": "127.0.0.1"}
                    self.reply(status)
                elif line.startswith("TASKY AGENT ") and (self.edition == "agents" or self.agents):
                    self.reply({"ok": True})
                elif not line.startswith("TASKY ASK ") or self.version < "2.12":
                    self.reply({"ok": False, "error": "unknown command"})  # like old firmware
                elif line.startswith("TASKY ASK "):
                    ask = json.loads(line[len("TASKY ASK "):])
                    if self.choice is not None:
                        threading.Timer(self.answer_after, self.reply,
                                        [{"answer": {"id": ask["id"], "choice": self.choice}}]).start()

    def close(self):
        self.stop = True
        os.close(self.master)
        os.close(self.slave)


class FakeWifiTasky:
    """Tasky 3.1 on Wi-Fi: TCP with "TASKY AUTH <token>" first, UDP discovery."""

    TOKEN = "abcdefghjkmnpqrstuvw"

    def __init__(self, choice=0):
        self.server = socket.socket()
        self.server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server.bind(("127.0.0.1", 0))
        self.port = self.server.getsockname()[1]
        self.server.listen(2)
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind(("127.0.0.1", self.port))
        self.choice = choice
        self.lines = []
        self.conns = []
        self.stop = False
        threading.Thread(target=self.accept_loop, daemon=True).start()
        threading.Thread(target=self.udp_loop, daemon=True).start()

    def udp_loop(self):
        while not self.stop:
            try:
                data, addr = self.udp.recvfrom(512)
            except OSError:
                return
            if data.decode().strip() == "TASKY FIND 3f7757":
                self.udp.sendto(b'@tasky {"id":"3f7757","port":%d}\n' % self.port, addr)

    def accept_loop(self):
        while not self.stop:
            try:
                conn, _ = self.server.accept()
            except OSError:
                return
            threading.Thread(target=self.client, args=(conn,), daemon=True).start()

    def drop(self):
        """Tasky restarted: every connection closes."""
        for conn in list(self.conns):
            try:
                conn.shutdown(socket.SHUT_RDWR)
                conn.close()
            except OSError:
                pass

    def client(self, conn):
        self.conns.append(conn)
        f = conn.makefile("rwb")
        authed = False
        send = lambda obj: (f.write(("@tasky " + json.dumps(obj) + "\n").encode()), f.flush())
        try:
            self.serve(conn, f, send)
        except (OSError, ValueError):
            pass

    def serve(self, conn, f, send):
        authed = False
        for raw in f:
            line = raw.decode().strip()
            self.lines.append(line)
            if not authed:
                if line != "TASKY AUTH " + self.TOKEN:
                    send({"ok": False, "error": "pair over USB first"})
                    conn.close()
                    return
                authed = True
                send({"ok": True})
            elif line == "TASKY?":
                send({"v": "3.1.0", "edition": "tasky", "agents": True})
            elif line.startswith("TASKY AGENT "):
                send({"ok": True})
            elif line.startswith("TASKY ASK "):
                send({"ok": True})
                ask = json.loads(line[10:])
                threading.Timer(0.3, send, [{"answer": {"id": ask["id"], "choice": self.choice}}]).start()

    def close(self):
        self.stop = True
        self.server.close()
        self.udp.close()


class AgentTest(unittest.TestCase):
    EXTRA_ENV = {}

    def setUp(self):
        self.home = tempfile.mkdtemp(prefix="tasky-test-")
        self.env = dict(os.environ, HOME=self.home, TASKY_HOME=os.path.join(self.home, ".tasky"),
                        TASKY_SOCK=os.path.join(self.home, "s.sock"))
        self.env.update(self.EXTRA_ENV)
        self.tasky = None

    def tearDown(self):
        # Stop this test's daemon and wait until it's really gone: Linux reuses
        # /dev/pts numbers, and a dying daemon must not reopen the next test's pty.
        if os.name == "nt":
            try:
                with open(os.path.join(self.home, ".tasky", "daemon.pid")) as f:
                    os.kill(int(f.read()), signal.SIGTERM)
                time.sleep(0.5)
            except (OSError, ValueError):
                pass
        else:
            subprocess.run(["pkill", "-f", f"{SCRIPT} daemon"], env=self.env, capture_output=True)
            for _ in range(50):
                if subprocess.run(["pgrep", "-f", f"{SCRIPT} daemon"], capture_output=True).returncode:
                    break
                time.sleep(0.1)
        if self.tasky:
            self.tasky.close()
        shutil.rmtree(self.home, ignore_errors=True)

    def pair(self, ip="127.0.0.1", token=FakeWifiTasky.TOKEN):
        """link.json as the daemon saves it over USB; Tasky itself not on USB."""
        self.wifi = FakeWifiTasky()
        self.addCleanup(self.wifi.close)
        os.makedirs(os.path.join(self.home, ".tasky"), exist_ok=True)
        with open(os.path.join(self.home, ".tasky", "link.json"), "w") as f:
            json.dump({"id": "3f7757", "port": self.wifi.port, "token": token, "ip": ip}, f)
        self.env["TASKY_PORT"] = "/dev/does-not-exist"
        self.env["TASKY_FIND_ADDR"] = "127.0.0.1"

    def test_usb_pairs_for_wifi(self):
        link = {"id": "3f7757", "port": 7778, "token": "abcdefghjkmnpqrstuvw"}
        self.plug(version="3.1.0", edition="tasky", agents=True, link=link)
        self.assertEqual(self.run_agent("hook", "working", stdin={"session_id": "a"}).returncode, 0)
        path = os.path.join(self.home, ".tasky", "link.json")
        with open(path) as f:
            self.assertEqual(json.load(f), dict(link, ip="127.0.0.1"))
        self.assertEqual(os.stat(path).st_mode & 0o777, 0o600)  # the token is a secret

    def test_wifi_cards_and_questions(self):
        self.pair()
        r = self.run_agent("hook", "working", stdin={"session_id": "w1", "cwd": "/x/app"})
        self.assertEqual(r.returncode, 0)
        self.assertEqual(self.wifi.lines[0], "TASKY AUTH " + FakeWifiTasky.TOKEN)
        self.assertTrue(any(l.startswith("TASKY AGENT ") and '"w1"' in l for l in self.wifi.lines))
        out = json.loads(self.run_agent("ask", stdin=self.ask_event()).stdout)
        self.assertEqual(out["hookSpecificOutput"]["decision"]["behavior"], "allow")
        self.assertIn("over Wi-Fi", self.run_agent("status").stdout)

    def test_wifi_finds_a_new_address(self):
        self.pair(ip="192.0.2.1")  # an old DHCP lease: nobody answers there
        r = self.run_agent("hook", "working", stdin={"session_id": "w2"})
        self.assertEqual(r.returncode, 0)
        self.assertTrue(any('"w2"' in l for l in self.wifi.lines))
        with open(os.path.join(self.home, ".tasky", "link.json")) as f:
            self.assertEqual(json.load(f)["ip"], "127.0.0.1")

    def test_cards_come_back_after_a_reconnect(self):
        # Tasky restarted: its cards are gone, the daemon sends them again
        self.pair()
        self.env["TASKY_WIFI_RETRY"] = "0.5"
        self.run_agent("hook", "working", stdin={"session_id": "r1", "cwd": "/x/app"})
        self.run_agent("hook", "end", stdin={"session_id": "gone"})
        self.wifi.drop()
        for _ in range(80):
            if sum('"r1"' in l for l in self.wifi.lines) >= 2:
                break
            time.sleep(0.1)
        self.assertEqual(sum('"r1"' in l for l in self.wifi.lines), 2, self.wifi.lines)
        self.assertEqual(sum(l.startswith("TASKY AUTH ") for l in self.wifi.lines), 2)

    def test_wifi_wrong_token_gets_nothing(self):
        self.pair(token="not-the-token-at-all")
        r = self.run_agent("ask", stdin=self.ask_event())
        self.assertEqual((r.returncode, r.stdout), (0, ""))
        self.assertFalse(any(l.startswith("TASKY AGENT") for l in self.wifi.lines))

    def test_launcher_runs_a_hook(self):
        # hooks/run.sh: how the plugin's hooks start Python on every OS
        if not shutil.which("sh"):
            self.skipTest("no sh")
        self.pair()
        root = os.path.dirname(SCRIPT)
        r = subprocess.run(["sh", os.path.join(root, "hooks", "run.sh"), "hook", "working"],
                           input=json.dumps({"session_id": "sh1"}), text=True, capture_output=True,
                           env=dict(self.env, CLAUDE_PLUGIN_ROOT=root), timeout=30)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertTrue(any('"sh1"' in l for l in self.wifi.lines), self.wifi.lines)

    def plug(self, **kw):
        if os.name == "nt":
            self.skipTest("fake USB Tasky needs a pseudo-terminal")
        self.tasky = FakeTasky(**kw)
        self.env["TASKY_PORT"] = self.tasky.path

    def run_agent(self, *args, stdin=None, timeout=20):
        return subprocess.run([sys.executable, SCRIPT, *args], input=json.dumps(stdin or {}),
                              env=self.env, capture_output=True, text=True, timeout=timeout)

    def ask_event(self, tool="Bash", tool_input=None, mode="default"):
        return {"session_id": "s1", "cwd": "/home/me/my-app", "hook_event_name": "PermissionRequest",
                "tool_name": tool, "tool_input": tool_input or {"command": "npm test"},
                "permission_mode": mode}

    def test_hook_reports_status(self):
        self.plug()
        r = self.run_agent("hook", "working", stdin={"session_id": "abc", "cwd": "/x/Cancion ñ"})
        self.assertEqual(r.returncode, 0)
        sent = [l for l in self.tasky.lines if l.startswith("TASKY AGENT ")]
        self.assertEqual(len(sent), 1)
        payload = json.loads(sent[0][len("TASKY AGENT "):])
        self.assertEqual(payload, {"id": "abc", "label": "Cancion n", "state": "working", "msg": "Working"})

    def test_session_start_shows_a_ready_card(self):
        self.plug()
        self.run_agent("hook", "start", stdin={"session_id": "s9", "cwd": "/x/web"})
        sent = [json.loads(l[12:]) for l in self.tasky.lines if l.startswith("TASKY AGENT ")]
        self.assertEqual(sent, [{"id": "s9", "label": "web", "state": "done", "msg": "Ready"}])

    def test_compact_is_not_a_new_session(self):
        self.plug()
        self.run_agent("hook", "start", stdin={"session_id": "s9", "cwd": "/x/web", "source": "compact"})
        self.run_agent("hook", "working", stdin={"session_id": "s9", "cwd": "/x/web"})
        states = [json.loads(l[12:])["state"] for l in self.tasky.lines if l.startswith("TASKY AGENT ")]
        self.assertEqual(states, ["working"])

    def test_same_question_twice_is_asked_once(self):
        # plugin hooks + install.sh hooks: both ask; Tasky shows it once
        self.plug(choice=0, answer_after=1.5)
        procs = [subprocess.Popen([sys.executable, SCRIPT, "ask"], stdin=subprocess.PIPE, env=self.env,
                                  stdout=subprocess.PIPE, text=True) for _ in range(2)]
        for p in procs:
            p.stdin.write(json.dumps(self.ask_event()))
            p.stdin.close()
            time.sleep(0.4)
        outs = sorted(p.stdout.read() for p in procs)
        for p in procs:
            p.wait()
        self.assertEqual(outs[0], "")  # the duplicate stays out of it
        self.assertEqual(json.loads(outs[1])["hookSpecificOutput"]["decision"]["behavior"], "allow")
        self.assertEqual(sum(l.startswith("TASKY ASK ") for l in self.tasky.lines), 1)

    def test_doctor(self):
        settings = os.path.join(self.home, "settings.json")
        with open(settings, "w") as f:
            json.dump({"enabledPlugins": {"tasky@tasky": True}}, f)
        self.plug()
        r = self.run_agent("doctor", "--settings", settings)
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn("Tasky found: agents 2.12.0", r.stdout)
        self.assertIn("Tasky plugin", r.stdout)
        # the same hooks also installed by install.sh -> duplicate warning
        with open(settings, "w") as f:
            json.dump({"enabledPlugins": {"tasky@tasky": True}, "hooks": {"Stop": [{"hooks": [
                {"type": "command", "command": "python3 ~/.tasky/tasky_agent.py hook done"}]}]}}, f)
        r = self.run_agent("doctor", "--settings", settings)
        self.assertEqual(r.returncode, 1)
        self.assertIn("installed twice", r.stdout)

    def test_doctor_standard_firmware(self):
        self.plug(edition="tasky")
        r = self.run_agent("doctor", "--settings", os.path.join(self.home, "none.json"))
        self.assertEqual(r.returncode, 1)
        self.assertIn("Update Tasky at https://tasky-flash.vercel.app", r.stdout)
        self.assertIn("/plugin install tasky@tasky", r.stdout)

    def test_ask_allow(self):
        self.plug(choice=0)
        r = self.run_agent("ask", stdin=self.ask_event())
        out = json.loads(r.stdout)
        self.assertEqual(out, {"hookSpecificOutput": {"hookEventName": "PermissionRequest",
                                                      "decision": {"behavior": "allow"}}})
        ask = json.loads(next(l for l in self.tasky.lines if l.startswith("TASKY ASK "))[10:])
        self.assertEqual((ask["tool"], ask["detail"], ask["label"]), ("Bash", "npm test", "my-app"))
        self.assertEqual(ask["options"], ["Allow", "Deny"])

    def test_ask_deny(self):
        self.plug(choice=1)
        out = json.loads(self.run_agent("ask", stdin=self.ask_event()).stdout)
        self.assertEqual(out["hookSpecificOutput"]["decision"]["behavior"], "deny")

    def test_ask_allow_edits_sets_mode(self):
        self.plug(choice=2)
        out = json.loads(self.run_agent("ask", stdin=self.ask_event(
            "Edit", {"file_path": "/home/me/my-app/src/main.py"})).stdout)
        self.assertEqual(out["hookSpecificOutput"]["decision"], {
            "behavior": "allow",
            "updatedPermissions": [{"type": "setMode", "mode": "acceptEdits", "destination": "session"}]})
        ask = json.loads(next(l for l in self.tasky.lines if l.startswith("TASKY ASK "))[10:])
        self.assertEqual(ask["detail"], "main.py")

    def test_no_tasky_is_fast_and_silent(self):
        self.env["TASKY_PORT"] = "/dev/does-not-exist"
        start = time.time()
        r = self.run_agent("ask", stdin=self.ask_event())
        self.assertEqual((r.returncode, r.stdout), (0, ""))  # terminal prompt as usual
        self.assertLess(time.time() - start, 6)
        self.assertEqual(self.run_agent("hook", "done", stdin={}).returncode, 0)

    def test_old_firmware_never_holds_the_terminal(self):
        # Agents 2.11 shows cards but can't answer: questions go straight to the terminal
        self.plug(version="2.11.0")
        start = time.time()
        r = self.run_agent("ask", stdin=self.ask_event())
        self.assertEqual((r.returncode, r.stdout), (0, ""))
        self.assertLess(time.time() - start, 6)
        self.assertFalse(any(l.startswith("TASKY ASK ") for l in self.tasky.lines))
        self.run_agent("hook", "working", stdin={"session_id": "a"})
        self.assertTrue(any(l.startswith("TASKY AGENT ") for l in self.tasky.lines))

    def test_one_firmware_3_0(self):
        # 3.0: edition "tasky" with "agents": true does cards and questions
        self.plug(version="3.0.0", edition="tasky", agents=True, choice=0)
        out = json.loads(self.run_agent("ask", stdin=self.ask_event()).stdout)
        self.assertEqual(out["hookSpecificOutput"]["decision"]["behavior"], "allow")
        self.run_agent("hook", "working", stdin={"session_id": "a"})
        self.assertTrue(any(l.startswith("TASKY AGENT ") for l in self.tasky.lines))

    def test_standard_edition_gets_nothing(self):
        self.plug(edition="tasky")
        r = self.run_agent("ask", stdin=self.ask_event())
        self.assertEqual(r.stdout, "")
        self.run_agent("hook", "working", stdin={"session_id": "a"})
        self.assertFalse(any(l.startswith(("TASKY AGENT ", "TASKY ASK ")) for l in self.tasky.lines),
                         self.tasky.lines)
        self.assertIn("no agent cards", self.run_agent("status").stdout)

    def test_timeout_falls_back_to_terminal(self):
        self.plug(choice=None)
        self.env["TASKY_ASK_TIMEOUT"] = "1"
        r = self.run_agent("ask", stdin=self.ask_event())
        self.assertEqual((r.returncode, r.stdout), (0, ""))
        time.sleep(0.3)
        self.assertTrue(any(l.startswith("TASKY ASK-CANCEL ") for l in self.tasky.lines))

    def test_killed_hook_cancels_question(self):
        self.plug(choice=None)
        p = subprocess.Popen([sys.executable, SCRIPT, "ask"], stdin=subprocess.PIPE, env=self.env,
                             stdout=subprocess.PIPE, text=True)
        p.stdin.write(json.dumps(self.ask_event()))
        p.stdin.close()
        for _ in range(50):
            if any(l.startswith("TASKY ASK ") for l in self.tasky.lines):
                break
            time.sleep(0.1)
        p.send_signal(signal.SIGKILL)  # Claude Code gave up / Ctrl+C
        p.wait()
        for _ in range(20):
            if any(l.startswith("TASKY ASK-CANCEL ") for l in self.tasky.lines):
                break
            time.sleep(0.1)
        self.assertTrue(any(l.startswith("TASKY ASK-CANCEL ") for l in self.tasky.lines))

    def test_status_while_a_question_waits(self):
        # One session waits on the knob; another keeps reporting meanwhile.
        self.plug(choice=0, answer_after=2.0)
        asker = subprocess.Popen([sys.executable, SCRIPT, "ask"], stdin=subprocess.PIPE, env=self.env,
                                 stdout=subprocess.PIPE, text=True)
        asker.stdin.write(json.dumps(self.ask_event()))
        asker.stdin.close()
        time.sleep(0.8)
        start = time.time()
        r = self.run_agent("hook", "working", stdin={"session_id": "other", "cwd": "/x/other"})
        self.assertEqual(r.returncode, 0)
        self.assertLess(time.time() - start, 2)
        self.assertEqual(json.loads(asker.stdout.read())["hookSpecificOutput"]["decision"]["behavior"], "allow")
        asker.wait()
        self.assertTrue(any('"id": "other"' in l for l in self.tasky.lines))



@unittest.skipIf(os.name == "nt", "Windows always uses TCP: AgentTest covers it")
class AgentTestTcp(AgentTest):
    """The same tests with hooks reaching the daemon over localhost TCP, as on Windows."""
    EXTRA_ENV = {"TASKY_IPC": "tcp"}


if __name__ == "__main__":
    unittest.main()
