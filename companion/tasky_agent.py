#!/usr/bin/env python3
"""Tell a Tasky (Agents edition) what your AI coding agents are doing.

Tasky shows each session on its Agents screen and breathes its light bar while
an agent is waiting for you.

Claude Code hooks (see companion/README.md):
    tasky_agent.py hook working   # UserPromptSubmit
    tasky_agent.py hook waiting   # Notification (permission / input needed)
    tasky_agent.py hook done      # Stop
    tasky_agent.py hook end       # SessionEnd
  The hook JSON arrives on stdin (session_id, cwd, message).

Any other tool or script:
    tasky_agent.py send --id build-42 --label "CI" --state working --msg "Running tests"

Never fails loudly: if Tasky isn't plugged in, it exits 0 so your agent keeps going.
Needs pyserial:  pip install pyserial
Port: auto-detected (Espressif USB, VID 0x303A) or set TASKY_PORT=/dev/cu.usbmodemXXXX
"""
import argparse
import json
import os
import sys
import unicodedata

ESPRESSIF_VID = 0x303A
STATES = ("working", "waiting", "done", "end")


def find_port():
    if os.environ.get("TASKY_PORT"):
        return os.environ["TASKY_PORT"]
    from serial.tools import list_ports

    for p in list_ports.comports():
        if p.vid == ESPRESSIF_VID:
            return p.device
    return None


def ascii_text(value, limit):
    """Tasky's screen fonts are ASCII-only: 'Canción ñ' -> 'Cancion n'."""
    text = unicodedata.normalize("NFKD", str(value)).encode("ascii", "ignore").decode()
    return text[:limit]


def send(payload, verbose=False):
    payload = dict(payload)
    for key, limit in (("id", 39), ("label", 23), ("msg", 63)):
        if key in payload:
            payload[key] = ascii_text(payload[key], limit)
    try:
        import serial
    except ImportError:
        if verbose:
            print("pyserial missing: pip install pyserial", file=sys.stderr)
        return False
    port = find_port()
    if not port:
        if verbose:
            print("No Tasky found (set TASKY_PORT)", file=sys.stderr)
        return False
    try:
        s = serial.Serial()
        s.port = port
        s.baudrate = 115200
        s.timeout = 0.5
        s.write_timeout = 1
        s.dtr = False  # keep DTR/RTS low: toggling them resets the ESP32
        s.rts = False
        s.open()
        s.write(("TASKY AGENT " + json.dumps(payload) + "\n").encode("utf-8"))
        s.flush()
        if verbose:  # wait for the "@tasky {...}" reply
            for _ in range(10):
                line = s.readline().decode("utf-8", "replace").strip()
                if line.startswith("@tasky "):
                    print(line[7:])
                    break
        s.close()
        return True
    except Exception as e:  # port busy (setup page open), unplugged, ...
        if verbose:
            print(f"Couldn't talk to Tasky on {port}: {e}", file=sys.stderr)
        return False


def from_hook(state):
    try:
        event = json.load(sys.stdin)
    except Exception:
        event = {}
    session = str(event.get("session_id") or "claude")
    project = os.path.basename(str(event.get("cwd") or "")) or "Claude"
    msg = {
        "waiting": event.get("message") or "Needs your input",
        "done": "Finished",
        "working": "Working",
    }.get(state, "")
    return {"id": session[:39], "label": project[:23], "state": state, "msg": str(msg)[:63]}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    h = sub.add_parser("hook", help="called by a Claude Code hook (reads JSON on stdin)")
    h.add_argument("state", choices=STATES)
    s = sub.add_parser("send", help="send a status by hand")
    s.add_argument("--id", required=True)
    s.add_argument("--label", default="")
    s.add_argument("--state", required=True, choices=STATES)
    s.add_argument("--msg", default="")
    args = ap.parse_args()

    if args.cmd == "hook":
        send(from_hook(args.state))
    else:
        ok = send({"id": args.id, "label": args.label, "state": args.state, "msg": args.msg}, verbose=True)
        sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
