#!/usr/bin/env bash
# =============================================================================
# Minimal validation that the tutorial scenario loads: runs the engine with
# --tutorial --no-prompts and checks the log for a successful scenario load
# with zero nyan parser errors. Catches the "fixture silently never loaded"
# failure mode.
#
# Requires: the engine built (bin/run) and the fixtures installed (install.sh).
# =============================================================================
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../../.." && pwd)"

if [ ! -x "$REPO/bin/run" ]; then
	echo "ERROR: bin/run not found/buildable. Build the engine first." >&2
	exit 2
fi

LOG="$(mktemp -t openage_tutorial_validate.XXXXXX)"
# 124 = timeout (process was alive = good). Other non-zero may be a crash.
timeout 25 "$REPO/bin/run" --tutorial --no-prompts >"$LOG" 2>&1
rc=$?

ok_load="$(grep -c 'load_scenario: engine.util.scenario.mission_1.Mission1 initial_entities=2' "$LOG" || true)"
nyan_err="$(grep -ciE 'LangError|NameError|MemberNotFoundError|MemberTypeError|InternalError|Could not find member|unknown name' "$LOG" || true)"

echo "load_success_lines=$ok_load  nyan_errors=$nyan_err  run_exit=$rc"

if [ "$ok_load" -ge 1 ] && [ "$nyan_err" -eq 0 ]; then
	echo "VALIDATE: OK (tutorial scenario loaded, no nyan errors)"
	rm -f "$LOG"
	exit 0
else
	echo "VALIDATE: FAIL — see log tail:" >&2
	tail -25 "$LOG" >&2
	rm -f "$LOG"
	exit 1
fi
