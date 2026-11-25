#!/usr/bin/env bash
# Copyright 2026+ the openage authors. See copying.md for legal info.
# One-click runner for all tick-driven William Wallace Mission 1 & 2 golden regression tests.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

UPDATE_FLAG=()
DO_BUILD=1
BARBAROSSA=0
GOLDEN_FLAGS=()
while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --update-golden)
            UPDATE_FLAG=("--update-golden")
            shift
            ;;
        --barbarossa)
            BARBAROSSA=1
            shift
            ;;
        --no-build)
            DO_BUILD=0
            shift
            ;;
        --jobs)
            if [[ "$#" -lt 2 || ! "$2" =~ ^[1-9][0-9]*$ ]]; then
                echo "--jobs requires a positive integer" >&2
                exit 2
            fi
            GOLDEN_FLAGS+=("--golden-jobs" "$2")
            shift 2
            ;;
        --timings)
            GOLDEN_FLAGS+=("--golden-timings")
            shift
            ;;
        *)
            echo "Unknown argument: $1" >&2
            exit 2
            ;;
    esac
done

ORIGINAL_DIR="$HOME/Library/Application Support/Steam/steamapps/common/AoE2DE/AgeOfEmpires2Data"
if [[ ! -d "$ORIGINAL_DIR" ]]; then
    ORIGINAL_DIR=""
fi

install_mission() {
    local mission_num="$1"
    local mission_json="$ROOT/logs/wallace-original/mission-${mission_num}.json"
    if [[ ! -f "$mission_json" || -z "$ORIGINAL_DIR" ]]; then
        echo "Missing $mission_json or AoE2DE install directory ($ORIGINAL_DIR)" >&2
        exit 1
    fi
    bash "$ROOT/assets/nyan/ww_mvp/install.sh" --original-scenario "$mission_json" "$ORIGINAL_DIR" >/dev/null
}

restore_mission1() {
    if [[ -f "$ROOT/logs/wallace-original/mission-1.json" && -n "$ORIGINAL_DIR" ]]; then
        install_mission 1 || true
    fi
}
trap restore_mission1 EXIT

if [[ "$DO_BUILD" -eq 1 ]]; then
    echo "==> Building openage..."
    cmake --build "$(cd "$ROOT/bin" && pwd -P)" -j8
fi

echo "==> Running headless snapshot regression..."
"$ROOT/bin/run" test "${UPDATE_FLAG[@]}" testing.snapshot.headless_regression

echo "==> Installing Mission 1 & running 7 Mission 1 golden cases..."
install_mission 1
"$ROOT/bin/run" test "${UPDATE_FLAG[@]}" "${GOLDEN_FLAGS[@]}" testing.wallace_golden.mission1_golden
"$ROOT/bin/run" test "${UPDATE_FLAG[@]}" "${GOLDEN_FLAGS[@]}" testing.wallace_golden.ai_mission1_golden

echo "==> Installing Mission 2 & running 7 Mission 2 golden cases..."
install_mission 2
"$ROOT/bin/run" test "${UPDATE_FLAG[@]}" "${GOLDEN_FLAGS[@]}" testing.wallace_golden.mission2_golden
"$ROOT/bin/run" test "${UPDATE_FLAG[@]}" "${GOLDEN_FLAGS[@]}" testing.wallace_golden.ai_mission2_golden

if [[ "$BARBAROSSA" -eq 1 ]]; then
    echo "==> Running original Barbarossa map sandbox (story triggers explicitly disabled)..."
    "$ROOT/venv/bin/python3" "$ROOT/scripts/export_wallace_scenarios.py" \
        --original-dir "$ORIGINAL_DIR" \
        --campaign "$ORIGINAL_DIR/resources/_common/campaign/cam5.aoe2campaign" \
        --scenarios 5_Barbarossa_1.aoe2scenario --ai-map-sandbox \
        --output "$ROOT/logs/barbarossa-ai"
    bash "$ROOT/assets/nyan/ww_mvp/install.sh" --original-scenario \
        "$ROOT/logs/barbarossa-ai/mission-1.json" "$ORIGINAL_DIR" >/dev/null
    "$ROOT/bin/run" test "${UPDATE_FLAG[@]}" "${GOLDEN_FLAGS[@]}" testing.wallace_golden.ai_barbarossa_golden
fi

echo "==> ALL WALLACE GOLDEN REGRESSION TESTS PASSED (14 original cases + 4 AI/replay cases + headless snapshot)"
