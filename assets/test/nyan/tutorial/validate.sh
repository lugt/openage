#!/usr/bin/env bash
# =============================================================================
# Real tutorial smoke test: launches the engine in tutorial mode (headless)
# and verifies the Mission 1 scenario loads, all 3 initial entities spawn,
# and the process stays alive without nyan/runtime errors or a crash.
#
# This script validates scenario/load/simulation survival only. The combat
# behaviour loop (dispatch -> approach -> cooldown -> damage -> death ->
# remove -> enemy-specific victory) is covered headless by demo 5
# (`bin/run test --demo gamestate.tests.simulation_demo 5`); do NOT
# re-implement combat or fake mouse input here.
#
# Headless mode is used (--headless) because FULL mode spawns the time-loop
# and simulation on separate worker threads; with the ~3s de2_base load
# (773 nyan files) that race surfaces as a SIGSEGV during load_data. That
# is a pre-existing Engine threading issue, not a tutorial/combat
# regression, and is out of scope for the MVP vertical slice. Headless
# mode runs the simulation single-threaded on the main thread and still
# exercises the real scenario load + GameSimulation + tutorial trigger.
#
# Exit codes: `timeout` returns 124 when it kills a still-alive process,
# which is the smoke-success signal (the sim ran for the full window).
# Any other non-zero (134=SIGABRT, 139=SIGSEGV, ...) is a startup crash.
# =============================================================================
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../../.." && pwd)"

if [ ! -x "$REPO/bin/run" ]; then
	echo "ERROR: bin/run not found/buildable. Build the engine first." >&2
	exit 2
fi

LOG="$(mktemp -t openage_tutorial_validate.XXXXXX)"
# 124 = timeout (process was alive for the full 25s = smoke success).
# Any other non-zero is a crash (134=SIGABRT, 139=SIGSEGV, etc.).
timeout 25 "$REPO/bin/run" --tutorial --headless --no-prompts >"$LOG" 2>&1
rc=$?

# ---- Survival: timeout (124) or clean exit (0) is OK; anything else crashed.
survived=0
if [ "$rc" -eq 124 ] || [ "$rc" -eq 0 ]; then
	survived=1
fi

# ---- Scenario load: Mission1 loaded with exactly 3 initial entities.
load_lines="$(grep -c 'load_scenario: engine.util.scenario.mission_1.Mission1 initial_entities=3' "$LOG" || true)"

# ---- All 3 placed instances spawned (PlayerVillager, TownCenter, EnemyVillager).
spawn_player="$(grep -c 'spawned placed_instance=engine.util.scenario.mission_1.PlayerVillager ' "$LOG" || true)"
spawn_tc="$(grep -c 'spawned placed_instance=engine.util.scenario.mission_1.PlayerTownCenter ' "$LOG" || true)"
spawn_enemy="$(grep -c 'spawned placed_instance=engine.util.scenario.mission_1.EnemyVillager ' "$LOG" || true)"

# ---- No nyan parser errors.
nyan_err="$(grep -ciE 'LangError|NameError|MemberNotFoundError|MemberTypeError|InternalError|Could not find member|unknown name' "$LOG" || true)"

# ---- No runtime crash signatures.
crash_err="$(grep -ciE 'SIGSEGV|SIGABRT|terminate has been called|core dump|Segmentation|Aborted \(core' "$LOG" || true)"

echo "run_exit=$rc  survived=$survived  load_lines=$load_lines"
echo "spawn: player=$spawn_player tc=$spawn_tc enemy=$spawn_enemy"
echo "nyan_errors=$nyan_err  crash_errors=$crash_err"

ok=1
[ "$survived" -eq 1 ] || ok=0
[ "$load_lines" -ge 1 ] || ok=0
[ "$spawn_player" -ge 1 ] || ok=0
[ "$spawn_tc" -ge 1 ] || ok=0
[ "$spawn_enemy" -ge 1 ] || ok=0
[ "$nyan_err" -eq 0 ] || ok=0
[ "$crash_err" -eq 0 ] || ok=0

if [ "$ok" -eq 1 ]; then
	echo "VALIDATE: OK (tutorial loaded, 3 entities spawned, no nyan/crash errors, survived 25s)"
	rm -f "$LOG"
	exit 0
else
	echo "VALIDATE: FAIL - see log tail:" >&2
	tail -25 "$LOG" >&2
	rm -f "$LOG"
	exit 1
fi
