# Tasky companion: AI agents on your desk

For the **Tasky for Agents** firmware. Your coding agents report what they're doing, and Tasky shows it:

- **Agents** screen: every session with its project and state (working / **waiting** / done).
- The **light bar breathes** while an agent is waiting for your approval, plus a short beep (muted during focus sessions).
- **Home** says "1 agent needs you" instead of the phrase.
- On the Agents screen: **turn** to pick a session, **press** to acknowledge it (light stops), **long press** to dismiss it.

Tasky talks over the same USB cable, so it has to be plugged into the computer running the agents.

## Setup (Claude Code)

1. Flash **Tasky for Agents** from https://tasky-flash.vercel.app.
2. Get the script and its one dependency:
   ```bash
   git clone https://github.com/arampersand/tasky ~/tasky
   pip3 install pyserial
   ```
3. Test it (Tasky should list "Test" on its Agents screen):
   ```bash
   python3 ~/tasky/companion/tasky_agent.py send --id test --label Test --state waiting --msg "Hello from the terminal"
   ```
4. Add the hooks from [`claude-hooks.json`](claude-hooks.json) to `~/.claude/settings.json` (merge the `"hooks"` object with any you already have). Restart Claude Code.

| Claude Code event | Tasky state |
|---|---|
| `UserPromptSubmit` | working |
| `Notification` (permission or input needed) | **waiting** — light breathes |
| `Stop` | done |
| `SessionEnd` | removed |

Sessions are named after the project folder. Tasky keeps up to 4 and forgets ones with no news for 2 hours.

## Other agents and scripts

Anything that can run a command can report:

```bash
tasky_agent.py send --id deploy --label "Prod deploy" --state working --msg "Rolling out"
tasky_agent.py send --id deploy --state done
```

The protocol is one line over the USB serial port at 115200 baud:

```
TASKY AGENT {"id":"deploy","label":"Prod deploy","state":"working","msg":"Rolling out"}
```

`state` is `working`, `waiting`, `done` or `end`. Tasky answers `@tasky {"ok":true}`.

## Troubleshooting

- **Nothing shows up:** check the port with `python3 -m serial.tools.list_ports`; set `TASKY_PORT=/dev/cu.usbmodemXXXX` if auto-detection picks the wrong one.
- **Port busy:** close the web setup page or any serial monitor; only one program can hold the port.
- The script never blocks or fails your agent: if Tasky isn't there, the hook just exits.
