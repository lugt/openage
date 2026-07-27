#!/usr/bin/env bash
# =============================================================================
# Install the hand-written MVP testscene fixtures + stopgap schemas into the
# engine modpack's runtime location (assets/converted/engine/test/ and util/).
#
# These files are NOT converter products. They are hand-authored stopgaps
# that let the Phase 0 testscene scenario load. See README.md in this folder.
#
# Usage:
#   bash assets/test/nyan/testscene/install.sh
#
# Prerequisite: the base engine schemas must already exist under
#   assets/converted/engine/util/ (regenerate them with
#   `python -m openage convert-export-api assets/converted`). This script
#   then OVERLAYS the 3 stopgap schemas on top of that base and adds the
#   testscene_data fixture.
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# testscene/ lives at <repo>/assets/test/nyan/testscene -> repo root is 4 dirs up.
REPO="$(cd "$HERE/../../../.." && pwd)"
DEST_UTIL="$REPO/assets/converted/engine/util"
DEST_TEST="$REPO/assets/converted/engine/test"

if [ ! -d "$DEST_UTIL" ]; then
	echo "ERROR: engine modpack dir not found: $DEST_UTIL" >&2
	echo "Run 'python -m openage convert-export-api $REPO/assets/converted' first" >&2
	exit 1
fi

mkdir -p "$DEST_UTIL/setup" "$DEST_UTIL/scenario" "$DEST_TEST"

# --- 1 testscene fixture (hand-written) ---
# Mission 1 scenario + player setup moved to the ww_mvp modpack
# (assets/nyan/ww_mvp/campaign/william_wallace/mission_01.nyan, installed by
# assets/nyan/ww_mvp/install.sh); only the self-contained testscene_data
# (TestsceneGrass terrain) still lives here and is overlaid into the engine
# modpack under engine/test/ so ww_mvp's mission_01.nyan can import
# engine.test.testscene_data.
cp "$HERE/fixtures/testscene_data.nyan"       "$DEST_TEST/testscene_data.nyan"

# --- 3 stopgap schemas (overlay the api_export base where applicable) ---
# game_entity.nyan / terrain.nyan: api_export emits these WITHOUT the stopgap
#   classes (PlacedEntity / Size / TerrainChunkLayout); this overlay adds them.
# scenario.nyan: api_export does NOT emit this at all; this creates it.
# These keep their engine.util.* FQONs (imported as engine.util.game_entity /
# engine.util.scenario / engine.util.terrain by mission_01.nyan), so they stay
# under util/.
cp "$HERE/schemas/game_entity.nyan"  "$DEST_UTIL/game_entity.nyan"
cp "$HERE/schemas/scenario.nyan"     "$DEST_UTIL/scenario.nyan"
cp "$HERE/schemas/terrain.nyan"      "$DEST_UTIL/terrain.nyan"

echo "Installed 1 testscene fixture -> $DEST_TEST + 3 stopgap schemas -> $DEST_UTIL"
echo "Validate with: bash $HERE/validate.sh"
