# Tasky companion: AI agents on your desk

For the **Tasky for Agents** firmware. Your coding agents report what they're doing, and Tasky shows it:

- **Agents** screen: one card per session with a little robot that shows its state (working / **waiting for you** / done) and the last message. Turn to go through the cards.
- **Approve from the knob:** when Claude Code asks permission ("run `npm test`?", "edit `ui.cpp`?"), Tasky jumps to that card. **Turn** to pick *Allow*, *Deny* or *Allow edits*, **press** to answer. **Long press** leaves the question to the terminal.
- The **light bar breathes** while an agent waits for you, plus a short beep (muted during focus sessions).
- **Home** says "1 agent needs you" instead of the phrase.

Tasky talks over the same USB cable, so it has to be plugged into the computer running the agents.

## Set up (macOS / Linux)

1. Flash **Tasky for Agents** from https://tasky-flash.vercel.app.
2. Run:
   ```bash
   curl -fsSL https://tasky-flash.vercel.app/agents/install.sh | sh
   ```
   It puts `tasky_agent.py` in `~/.tasky/` and adds the hooks to `~/.claude/settings.json`. It backs the file up first (`settings.json.tasky-backup`) and leaves your other settings and hooks alone. Running it again updates it.
3. Restart Claude Code. If Tasky was plugged in, it shows a "Claude Code" card already.

To remove it: `curl -fsSL https://tasky-flash.vercel.app/agents/install.sh | sh -s -- --uninstall`

Only Python 3 is needed (macOS: `xcode-select --install` if `python3` is missing). Windows isn't supported yet.

| Claude Code event | What Tasky does |
|---|---|
| `UserPromptSubmit` | card: working |
| `PermissionRequest` | the question on the card: allow / deny with the knob |
| `Notification` (permission or input needed) | card: **waiting**, the light breathes |
| `Stop` | card: done |
| `SessionEnd` | card removed |

Sessions are named after the project folder. Tasky keeps up to 4 and forgets ones with no news for 2 hours.

### About approvals

- Claude Code runs the permission hook **before** it shows its own prompt. While a question is on Tasky, the terminal waits for it: up to **60 s** (`TASKY_ASK_TIMEOUT`), then the normal prompt appears. A long press on Tasky sends it to the terminal at once.
- If Tasky isn't plugged in, the hook returns immediately and you get the normal prompt. Nothing is ever approved by itself.
- *Allow edits* (for Edit/Write) also switches that session to accept edits, like choosing it in the terminal.
- Tasky can't answer Claude's multiple-choice questions or send it new messages: Claude Code hooks don't allow that.
- Anyone who can press the knob can approve. Keep that in mind on a shared desk.

## How it works

`tasky_agent.py daemon` (started on demand, quits after 2 hours without news) is the only program that opens Tasky's USB port. Hooks talk to it over a Unix socket in `~/.tasky/` that only you can open. That way a question can wait for the knob while other sessions keep reporting. It never toggles the port's DTR/RTS lines, so it never resets Tasky.

## Other agents and scripts

Anything that can run a command can report:

```bash
python3 ~/.tasky/tasky_agent.py send --id deploy --label "Prod deploy" --state working --msg "Rolling out"
python3 ~/.tasky/tasky_agent.py send --id deploy --state done
python3 ~/.tasky/tasky_agent.py status    # is Tasky connected?
```

Protocol, one line over USB serial at 115200 baud (Tasky answers with lines starting `@tasky `):

```
TASKY AGENT {"id":"deploy","label":"Prod deploy","state":"working","msg":"Rolling out"}
TASKY ASK {"id":"r1","session":"deploy","label":"Prod deploy","tool":"Bash","detail":"kubectl apply","options":["Allow","Deny"],"timeout":60}
   -> @tasky {"answer":{"id":"r1","choice":0}}     (-1 = answer in the terminal)
TASKY ASK-CANCEL {"id":"r1"}
```

`state` is `working`, `waiting`, `done` or `end`. Text is ASCII (Tasky's fonts have no accents; the script simplifies them).

<details>
<summary>Manual setup (without the installer)</summary>

```bash
mkdir -p ~/.tasky
curl -fsSL https://raw.githubusercontent.com/arampersand/tasky/main/companion/tasky_agent.py -o ~/.tasky/tasky_agent.py
python3 ~/.tasky/tasky_agent.py install       # or merge claude-hooks.json into ~/.claude/settings.json by hand
```
</details>

## Troubleshooting

- **Nothing shows up:** run `python3 ~/.tasky/tasky_agent.py status`. Close the web setup page or any serial monitor: only one program can hold the port. If several USB devices are connected, set `TASKY_PORT=/dev/cu.usbmodemXXXX`.
- **Logs:** `~/.tasky/daemon.log`.
- **macOS asks to "identify your keyboard"** after you pair Tasky over Bluetooth (Controls screen): close that window. Tasky's shortcuts work anyway.

## Tests

```bash
python3 -m unittest discover companion/tests
```
They run the companion against a fake Tasky on a pseudo-terminal: status, allow / deny / allow edits, Tasky missing, timeout, a killed hook, concurrent sessions, and the settings.json installer.
