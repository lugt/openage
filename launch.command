#!/usr/bin/env bash
# Copyright 2026+ the openage authors. See copying.md for legal info.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$repo"
for launcher_python in "$repo/venv/bin/python3" "$repo/.venv/bin/python3" python3; do
    if "$launcher_python" -c 'import tkinter' >/dev/null 2>&1; then
        exec "$launcher_python" "$repo/scripts/launcher.py" "$@"
    fi
done
echo "openage launcher needs Python with Tk support. See doc/launcher.md." >&2
exit 1
