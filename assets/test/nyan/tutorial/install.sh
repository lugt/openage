#!/usr/bin/env bash
# =============================================================================
# Install the hand-written MVP tutorial fixtures + stopgap schemas into the
# engine modpack's runtime location (assets/converted/engine/util/).
#
# These 6 files are NOT converter products. They are hand-authored stopgaps
# that let the Phase 0 tutorial scenario load. See README.md in this folder.
#
# Usage:
#   bash assets/test/nyan/tutorial/install.sh
#
# Prerequisite: the base engine schemas must already exist under
#   assets/converted/engine/util/ (regenerate them with
#   `python -m openage convert-export-api assets/converted`). This script
#   then OVERLAYS the 3 stopgap schemas on top of that base and adds the
#   3 scenario fixtures.
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# tutorial/ lives at <repo>/assets/test/nyan/tutorial -> repo root is 4 dirs up.
REPO="$(cd "$HERE/../../../.." && pwd)"
DEST="$REPO/assets/converted/engine/util"

if [ ! -d "$DEST" ]; then
	echo "ERROR: engine modpack dir not found: $DEST" >&2
	echo "Run 'python -m openage convert-export-api $REPO/assets/converted' first" >&2
	exit 1
fi

mkdir -p "$DEST/setup" "$DEST/scenario"

# --- 3 scenario fixtures (hand-written) ---
cp "$HERE/fixtures/tutorial_data.nyan"        "$DEST/tutorial_data.nyan"
cp "$HERE/fixtures/setup/tutorial_setup.nyan"  "$DEST/setup/tutorial_setup.nyan"
cp "$HERE/fixtures/scenario/mission_1.nyan"   "$DEST/scenario/mission_1.nyan"

# --- 3 stopgap schemas (overlay the api_export base where applicable) ---
# game_entity.nyan / terrain.nyan: api_export emits these WITHOUT the stopgap
#   classes (PlacedEntity / Size / TerrainChunkLayout); this overlay adds them.
# scenario.nyan: api_export does NOT emit this at all; this creates it.
cp "$HERE/schemas/game_entity.nyan"  "$DEST/game_entity.nyan"
cp "$HERE/schemas/scenario.nyan"     "$DEST/scenario.nyan"
cp "$HERE/schemas/terrain.nyan"      "$DEST/terrain.nyan"

echo "Installed 3 tutorial fixtures + 3 stopgap schemas into $DEST"
echo "Validate with: bash $HERE/validate.sh"
