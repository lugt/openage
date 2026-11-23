#!/usr/bin/env bash
# Start the campaign menu, or launch a specified mode from a terminal.
set -euo pipefail
repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for python in "$repo/venv/bin/python3" "$repo/.venv/bin/python3"; do
    if [ -x "$python" ]; then
        exec "$python" "$repo/scripts/campaign_launcher.py" "$@"
    fi
done
echo "openage Python environment is missing. Build openage before launching the campaign." >&2
exit 1
