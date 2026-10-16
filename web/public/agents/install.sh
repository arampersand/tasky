#!/bin/sh
# Tasky for Agents: connect Claude Code to your Tasky (macOS / Linux).
#
#   curl -fsSL https://tasky-flash.vercel.app/agents/install.sh | sh
#   curl -fsSL https://tasky-flash.vercel.app/agents/install.sh | sh -s -- --uninstall
#
# Puts tasky_agent.py in ~/.tasky/ and adds its hooks to ~/.claude/settings.json
# (backup: settings.json.tasky-backup). Your other settings and hooks stay.
# Prefer the Claude Code plugin (/plugin marketplace add arampersand/tasky, then
# /plugin install tasky@tasky); use one or the other, not both.
# Source: https://github.com/arampersand/tasky/tree/main/claude-plugin
set -eu

DIR="$HOME/.tasky"
SCRIPT="$DIR/tasky_agent.py"
URL="${TASKY_AGENT_URL:-https://tasky-flash.vercel.app/agents/tasky_agent.py}"

if ! command -v python3 >/dev/null 2>&1; then
  echo "Tasky needs Python 3. Install it (macOS: xcode-select --install) and run this again." >&2
  exit 1
fi

if [ "${1:-}" = "--uninstall" ]; then
  if [ -f "$SCRIPT" ]; then
    python3 "$SCRIPT" install --uninstall
    pkill -f "$SCRIPT daemon" 2>/dev/null || true
    rm -f "$SCRIPT" "$DIR/agent.sock" "$DIR/daemon.lock" "$DIR/daemon.log"
    rmdir "$DIR" 2>/dev/null || true
  fi
  echo "Tasky is disconnected from Claude Code."
  exit 0
fi

mkdir -p "$DIR"
if ! curl -fsSL "$URL" -o "$SCRIPT.tmp"; then
  rm -f "$SCRIPT.tmp"
  echo "Couldn't download the Tasky companion from $URL. Try again in a minute." >&2
  exit 1
fi
mv "$SCRIPT.tmp" "$SCRIPT"
pkill -f "$SCRIPT daemon" 2>/dev/null || true  # an update restarts it
python3 "$SCRIPT" install

if python3 "$SCRIPT" status >/dev/null 2>&1; then
  python3 "$SCRIPT" send --id tasky-setup --label "Claude Code" --state done --msg "Connected. Restart Claude Code." >/dev/null
  echo "Done. Tasky shows a \"Claude Code\" card. Restart Claude Code to start."
else
  echo "Done. Plug in Tasky (and close the web setup page, it holds the USB port), then restart Claude Code."
fi
