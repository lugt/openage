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
# After copying the nyan, this script also generates the deterministic
# Mission Pack (scenario.json + object_table.csv + trigger_graph.json +
# coverage.json) under the campaign/william_wallace/ subdir. The C++
# production loader (libopenage/scenario/mission_pack_loader.cpp) reads
# these files at scenario-init time; the legacy nyan-only path is NOT
# used for the Mission 1 reconstruction.
#
# Usage:
#   bash assets/nyan/ww_mvp/install.sh
#
# Prerequisite: the base engine modpack must already exist under
#   assets/converted/engine/ (regenerate with
#   `python -m openage convert-export-api $REPO/assets/converted` and overlay the
#   engine fixtures with `bash assets/test/nyan/testscene/install.sh`).
#   ww_mvp depends on engine (declared in modpack.toml) and its Mission 1
#   scenario imports engine.test.testscene_data.TestsceneGrass.
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# ww_mvp/ lives at <repo>/assets/nyan/ww_mvp -> repo root is 3 dirs up.
REPO="$(cd "$HERE/../../.." && pwd)"
DEST="$REPO/assets/converted/ww_mvp"
PY="$REPO/.venv/bin/python3"

if [ ! -d "$REPO/assets/converted/engine" ]; then
    echo "ERROR: engine modpack not found at $REPO/assets/converted/engine" >&2
    echo "Run 'python -m openage convert-export-api $REPO/assets/converted' first," >&2
    echo "then 'bash assets/test/nyan/testscene/install.sh' to overlay engine fixtures." >&2
    exit 1
fi

mkdir -p "$DEST/campaign/william_wallace"

cp "$HERE/modpack.toml"                                      "$DEST/modpack.toml"
cp "$HERE/campaign/william_wallace/mission_01.nyan"          "$DEST/campaign/william_wallace/mission_01.nyan"

# Generate the deterministic Mission Pack that the C++ production
# loader consumes (P2.7). The script invokes the same writer the
# pytest suite uses, so the on-disk pack matches the canonical IR
# round-trip.
"$PY" -c "
import sys, pathlib
sys.path.insert(0, '$REPO')
from openage.scenario.dumps import DEFAULT_DUMP_DIR
from openage.scenario.ir import ScenarioDefinition
from openage.scenario.writer import write_mission_pack
ir = ScenarioDefinition.from_dump(DEFAULT_DUMP_DIR)
write_mission_pack(ir, pathlib.Path('$DEST/campaign/william_wallace'))
print('Mission Pack written to $DEST/campaign/william_wallace')
"

echo "Installed ww_mvp modpack (Mission 1 scenario + player setup) into $DEST"
echo "Mission Pack (P2.7) emitted under $DEST/campaign/william_wallace/"
echo "Validate with: bash $REPO/assets/test/nyan/testscene/validate.sh"