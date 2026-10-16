"""install_hooks() keeps your settings and is safe to run again."""
import importlib.util
import json
import os
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("tasky_agent", os.path.join(HERE, "..", "..", "claude-plugin", "tasky_agent.py"))
ta = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ta)

MINE = {
    "model": "opus",
    "permissions": {"allow": ["Bash(git status)"]},
    "hooks": {
        "Stop": [{"hooks": [{"type": "command", "command": "say done"}]}],
        "PreToolUse": [{"matcher": "Bash", "hooks": [{"type": "command", "command": "lint.sh"}]}],
        # an old manual Tasky setup, to be replaced
        "Notification": [{"hooks": [{"type": "command",
                                     "command": "python3 ~/tasky/companion/tasky_agent.py hook waiting"}]}],
    },
}


class InstallTest(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.path = os.path.join(self.dir, "settings.json")
        with open(self.path, "w") as f:
            json.dump(MINE, f)

    def load(self):
        with open(self.path) as f:
            return json.load(f)

    def tasky_commands(self, s):
        return sorted(h["command"] for groups in s.get("hooks", {}).values() for g in groups
                      for h in g["hooks"] if "tasky_agent.py" in h["command"])

    def test_install_keeps_everything_else(self):
        ta.install_hooks(self.path, "/home/me/.tasky/tasky_agent.py")
        s = self.load()
        self.assertEqual(s["model"], "opus")
        self.assertEqual(s["permissions"], MINE["permissions"])
        self.assertIn({"type": "command", "command": "say done"}, s["hooks"]["Stop"][0]["hooks"])
        self.assertEqual(s["hooks"]["PreToolUse"], MINE["hooks"]["PreToolUse"])
        cmds = self.tasky_commands(s)
        self.assertEqual(len(cmds), 6)  # the old manual hook was replaced
        self.assertTrue(all('"/home/me/.tasky/tasky_agent.py"' in c for c in cmds))
        ask = s["hooks"]["PermissionRequest"][0]["hooks"][0]
        self.assertTrue(ask["command"].endswith(" ask"))
        self.assertGreater(ask["timeout"], ta.ASK_TIMEOUT_S)
        with open(self.path + ".tasky-backup") as f:
            self.assertEqual(json.load(f), MINE)

    def test_install_twice_is_the_same(self):
        ta.install_hooks(self.path, "/x/tasky_agent.py")
        once = self.load()
        ta.install_hooks(self.path, "/x/tasky_agent.py")
        self.assertEqual(self.load(), once)

    def test_uninstall_restores(self):
        ta.install_hooks(self.path, "/x/tasky_agent.py")
        ta.install_hooks(self.path, "/x/tasky_agent.py", uninstall=True)
        expected = json.loads(json.dumps(MINE))
        del expected["hooks"]["Notification"]  # ours, now gone
        self.assertEqual(self.load(), expected)

    def test_no_settings_file_yet(self):
        os.remove(self.path)
        path = os.path.join(self.dir, "new", "settings.json")
        ta.install_hooks(path, "/x/tasky_agent.py")
        with open(path) as f:
            self.assertEqual(len(self.tasky_commands(json.load(f))), 6)

    def test_unusual_values_are_kept(self):
        odd = {"hooks": {"Stop": {"weird": True}, "Custom": "keep me"}}
        with open(self.path, "w") as f:
            json.dump(odd, f)
        msg = ta.install_hooks(self.path, "/x/tasky_agent.py")
        s = self.load()
        self.assertEqual((s["hooks"]["Stop"], s["hooks"]["Custom"]), ({"weird": True}, "keep me"))
        self.assertIn("skipped Stop", msg)
        ta.install_hooks(self.path, "/x/tasky_agent.py", uninstall=True)
        self.assertEqual(self.load(), odd)

    def test_not_an_object_is_refused(self):
        for text in ("[]", '{"hooks": []}'):
            with open(self.path, "w") as f:
                f.write(text)
            with self.assertRaises(ValueError):
                ta.install_hooks(self.path, "/x/tasky_agent.py")
            with open(self.path) as f:
                self.assertEqual(f.read(), text)

    def test_invalid_json_is_left_alone(self):
        with open(self.path, "w") as f:
            f.write("{ not json")
        with self.assertRaises(ValueError):
            ta.install_hooks(self.path, "/x/tasky_agent.py")
        with open(self.path) as f:
            self.assertEqual(f.read(), "{ not json")


if __name__ == "__main__":
    unittest.main()
