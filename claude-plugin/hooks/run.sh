#!/bin/sh
# Runs tasky_agent.py with whatever Python 3 this computer has: the py
# launcher on Windows (Claude Code runs hooks in Git Bash there), python3 on
# macOS and Linux. No Python: exit quietly, Claude Code carries on.
# Claude Code sets CLAUDE_PLUGIN_ROOT (a C:\... path on Windows, which
# dirname in Git Bash may not split)
script="${CLAUDE_PLUGIN_ROOT:-$(dirname "$0")/..}/tasky_agent.py"
if command -v py >/dev/null 2>&1; then
  exec py -3 "$script" "$@"
fi
for python in python3 python; do
  if command -v "$python" >/dev/null 2>&1 &&
     "$python" -c 'import sys; sys.exit(sys.version_info < (3, 8))' >/dev/null 2>&1; then
    exec "$python" "$script" "$@"
  fi
done
exit 0
