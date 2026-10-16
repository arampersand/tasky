"""Tests for tasky_agent.py against a fake Tasky on a pseudo-terminal.

Run: python3 -m unittest discover companion/tests
"""
import json
import os
import pty
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
import tty
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
SCRIPT = os.path.abspath(os.path.join(HERE, "..", "..", "claude-plugin", "tasky_agent.py"))


class FakeTasky:
    """Answers like the firmware: TASKY? status, TASKY AGENT ok, TASKY ASK -> answer."""

    def __init__(self, choice=0, answer_after=0.3, version="2.12.0", edition="agents"):
        self.master, slave = pty.openpty()
        tty.setraw(slave)
        self.path = os.ttyname(slave)
        self.slave = slave  # keep it open so the pty stays alive
        self.choice = choice  # None = never answer
        self.answer_after = answer_after
        self.version, self.edition = version, edition
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
                    self.reply({"v": self.version, "edition": self.edition})
                elif line.startswith("TASKY AGENT ") and self.edition == "agents":
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


class AgentTest(unittest.TestCase):
    def setUp(self):
        self.home = tempfile.mkdtemp(prefix="tasky-test-")
        self.env = dict(os.environ, HOME=self.home, TASKY_SOCK=os.path.join(self.home, "s.sock"))
        self.tasky = None

    def tearDown(self):
        # stop the daemon this test started
        # Stop this test's daemon and wait until it's really gone: Linux reuses
        # /dev/pts numbers, and a dying daemon must not reopen the next test's pty.
        subprocess.run(["pkill", "-f", f"{SCRIPT} daemon"], env=self.env, capture_output=True)
        for _ in range(50):
            if subprocess.run(["pgrep", "-f", f"{SCRIPT} daemon"], capture_output=True).returncode:
                break
            time.sleep(0.1)
        if self.tasky:
            self.tasky.close()
        shutil.rmtree(self.home, ignore_errors=True)

    def plug(self, **kw):
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
        self.assertIn("Flash Tasky for Agents", r.stdout)
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

    def test_standard_edition_gets_nothing(self):
        self.plug(edition="tasky")
        r = self.run_agent("ask", stdin=self.ask_event())
        self.assertEqual(r.stdout, "")
        self.run_agent("hook", "working", stdin={"session_id": "a"})
        self.assertFalse(any(l.startswith(("TASKY AGENT ", "TASKY ASK ")) for l in self.tasky.lines),
                         self.tasky.lines)
        self.assertIn("standard firmware", self.run_agent("status").stdout)

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


if __name__ == "__main__":
    unittest.main()
