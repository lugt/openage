#!/usr/bin/env bash
# =============================================================================
# Install the hand-written ww_mvp modpack (William Wallace Mission 1 MVP)
# into the runtime modpack location (assets/converted/ww_mvp/).
#
# This modpack is NOT a converter product. It is hand-authored and ships the
# Mission 1 scenario + player setup. See assets/nyan/ww_mvp/ for the source.
#
# The runtime loader (enumerate_modpacks) only scans assets/converted/, so
# this script copies the tracked source into the gitignored product dir.
#
# Usage:
#   bash assets/nyan/ww_mvp/install.sh
#
# Prerequisite: the base engine modpack must already exist under
#   assets/converted/engine/ (regenerate with
#   `python -m openage convert-export-api assets/converted` and overlay the
#   engine fixtures with `bash assets/test/nyan/testscene/install.sh`).
#   ww_mvp depends on engine (declared in modpack.toml) and its Mission 1
#   scenario imports engine.test.testscene_data.TestsceneGrass.
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# ww_mvp/ lives at <repo>/assets/nyan/ww_mvp -> repo root is 3 dirs up.
REPO="$(cd "$HERE/../../.." && pwd)"
DEST="$REPO/assets/converted/ww_mvp"

if [ ! -d "$REPO/assets/converted/engine" ]; then
    echo "ERROR: engine modpack not found at $REPO/assets/converted/engine" >&2
    echo "Run 'python -m openage convert-export-api $REPO/assets/converted' first," >&2
    echo "then 'bash assets/test/nyan/testscene/install.sh' to overlay engine fixtures." >&2
    exit 1
fi

mkdir -p "$DEST/campaign/william_wallace"

cp "$HERE/modpack.toml"                                      "$DEST/modpack.toml"
cp "$HERE/campaign/william_wallace/mission_01.nyan"          "$DEST/campaign/william_wallace/mission_01.nyan"

echo "Installed ww_mvp modpack (Mission 1 scenario + player setup) into $DEST"
echo "Validate with: bash $REPO/assets/test/nyan/testscene/validate.sh"
