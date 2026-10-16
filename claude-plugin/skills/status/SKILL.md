---
name: status
description: Check that Tasky (the desk gadget) is connected to Claude Code, and say exactly what to fix if it isn't. Use when the user asks whether Tasky works, why their Tasky shows nothing, or how to set it up.
---

Run this and show the user its output:

```bash
python3 "${CLAUDE_PLUGIN_ROOT}/tasky_agent.py" doctor
```

Each line is a check (✓ ok, ✗ problem). If something failed, tell the user the one fix the output suggests, in plain words, and offer to run it if it's a command. Common fixes:

- **No Tasky found:** plug Tasky into this computer with a data USB-C cable.
- **Port busy:** close the tab of tasky-flash.vercel.app (or the app named in the output) that holds Tasky's USB port.
- **Standard firmware / older than 2.12:** flash **Tasky for Agents** at https://tasky-flash.vercel.app (it keeps the settings).
- **Hooks installed twice** (this plugin and `install.sh`): run `curl -fsSL https://tasky-flash.vercel.app/agents/install.sh | sh -s -- --uninstall`.

Don't change any files yourself.
