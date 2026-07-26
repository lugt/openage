#!/usr/bin/env bash
# =============================================================================
# William Wallace Mission 1 -- staged verification.
#
# Runs four independent stages and reports PASS/FAIL for each. The final
# stage calls scripts/run_ww_mission1.sh (the SAME script the user runs) so
# the agent's smoke and the user's manual launch never diverge.
#
# Stages:
#   1. build/import smoke  -- binaries + key nyan files present.
#   2. nyan database load  -- headless load confirms nyan parses (this stage
#      is headless ON PURPOSE: it validates the data layer only; it is NOT
#      the windowed-startup proof, which is stage 4).
#   3. media reference audit -- for Villager/TownCenter/terrain, find the
#      .sprite/.animation/.terrain files referenced by the nyan and check
#      they exist on disk.
#   4. real game startup    -- scripts/run_ww_mission1.sh under a timeout.
#      timeout-kill (124/137) = process was still alive = PASS; a crash
#      (134=SIGABRT, 139=SIGSEGV, ...) = FAIL. 0 = clean exit = PASS.
#
# Overall exit is non-zero if any stage fails. Stage 4 is the only one that
# can pass via timeout; stages 1-3 must genuinely pass.
# =============================================================================
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
cd "$REPO"

CONV="$REPO/assets/converted"
STAGE_OK=0

stage() { echo; echo "########## STAGE $1: $2 ##########"; }
pass() { echo "STAGE $1: PASS ($2)"; }
fail() { echo "STAGE $1: FAIL ($2)" >&2; STAGE_OK=1; }

# ---- Stage 1: build/import smoke ------------------------------------------
stage 1 "build/import smoke"
s1=0
[ -x "$REPO/bin/run" ] || { echo "  missing bin/run"; s1=1; }
[ -x "$REPO/.venv/bin/python3" ] || { echo "  missing .venv/bin/python3"; s1=1; }
so="$(find "$REPO/.bin" -name 'libopenage.so*' 2>/dev/null | head -1)"
[ -n "$so" ] || { echo "  missing libopenage.so"; s1=1; }
for f in \
	"$CONV/engine/modpack.toml" \
	"$CONV/de2_base/modpack.toml" \
	"$CONV/engine/util/scenario/mission_1.nyan" \
	"$CONV/de2_base/data/game_entity/generic/villager/villager.nyan" \
	"$CONV/de2_base/data/game_entity/generic/town_center/town_center.nyan" ; do
	[ -f "$f" ] || { echo "  missing $f"; s1=1; }
done
if [ "$s1" -eq 0 ]; then pass 1 "binaries + key nyan present"; else fail 1 "missing files above"; fi

# ---- Stage 2: nyan database load (headless, data-layer only) ---------------
stage 2 "nyan database load (headless)"
LOG2="$(mktemp -t ww_verify_nyan.XXXXXX)"
# Headless = single-threaded, no window. This stage only proves nyan parses +
# the scenario loads; it does NOT prove window/renderer/media.
timeout 25 "$REPO/bin/run" --tutorial --headless --no-convert --no-prompts >"$LOG2" 2>&1
rc2=$?
load_total="$(grep -c '\[PERF\] load_data TOTAL' "$LOG2" || true)"
init_ent="$(grep -c 'load_scenario:.*initial_entities=3' "$LOG2" || true)"
nyan_err="$(grep -ciE 'LangError|NameError|MemberNotFoundError|MemberTypeError|InternalError' "$LOG2" || true)"
echo "  rc=$rc2 load_total_lines=$load_total initial_entities=3_lines=$init_ent nyan_errors=$nyan_err"
if [ "$load_total" -ge 1 ] && [ "$init_ent" -ge 1 ] && [ "$nyan_err" -eq 0 ]; then
	pass 2 "nyan parses, scenario loads 3 entities, 0 nyan errors"
else
	fail 2 "nyan load incomplete (see $LOG2)"
fi

# ---- Stage 3: media reference audit ---------------------------------------
stage 3 "media reference audit"
# Find media file references inside the Villager / TownCenter nyan and the
# mission_1 terrain, then check each referenced file exists under assets/converted.
s3=0
media_refs="$(mktemp -t ww_media_refs.XXXXXX)"
grep -rhoE '"[^"]+\.(sprite|animation|terrain)"' \
	"$CONV/de2_base/data/game_entity/generic/villager/" \
	"$CONV/de2_base/data/game_entity/generic/town_center/" \
	"$CONV/engine/util/scenario/mission_1.nyan" \
	"$CONV/engine/util/tutorial_data/" 2>/dev/null \
	| tr -d '"' | sort -u > "$media_refs"
ref_count="$(wc -l < "$media_refs")"
echo "  distinct media references found in villager/towncenter/terrain nyan: $ref_count"
missing=0; checked=0
while IFS= read -r ref; do
	[ -z "$ref" ] && continue
	checked=$((checked+1))
	# Media refs are relative to a modpack root; search under converted/.
	hits="$(find "$CONV" -path "*/$ref" 2>/dev/null | head -1)"
	if [ -z "$hits" ]; then
		echo "  MISSING media: $ref"
		missing=$((missing+1))
	fi
done < "$media_refs"
echo "  checked=$checked missing=$missing"
if [ "$checked" -gt 0 ] && [ "$missing" -eq 0 ]; then
	pass 3 "all $checked referenced media files exist"
elif [ "$checked" -eq 0 ]; then
	fail 3 "no media references found in nyan (villager/towncenter have no Graphic media refs?)"
else
	fail 3 "$missing/$checked referenced media files MISSING"
fi
rm -f "$media_refs"

# ---- Stage 4: real game startup (windowed) --------------------------------
stage 4 "real game startup (windowed, via run_ww_mission1.sh)"
LOG4="$REPO/logs/ww_verify_startup_$(date +%Y%m%d_%H%M%S).log"
# Run the SAME script the user runs. Under timeout: 124/137 = still alive
# (window open) = PASS; 0 = clean exit = PASS; 134/139/other = crash = FAIL.
timeout -s KILL 30 bash "$REPO/scripts/run_ww_mission1.sh" >"$LOG4" 2>&1
rc4=$?
echo "  rc=$rc4 (124/137=killed while alive=PASS, 0=clean=PASS, 134=SIGABRT, 139=SIGSEGV)"
echo "  startup log: $LOG4"
case "$rc4" in
	0|124|137) pass 4 "windowed launch did not crash (rc=$rc4)" ;;
	*)         fail 4 "windowed launch CRASHED (rc=$rc4, see $LOG4)" ;;
esac

# ---- Summary ---------------------------------------------------------------
echo
if [ "$STAGE_OK" -eq 0 ]; then
	echo "VERIFY: ALL STAGES PASS"
	exit 0
else
	echo "VERIFY: ONE OR MORE STAGES FAILED (see above)"
	exit 1
fi
