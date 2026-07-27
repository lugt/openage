#!/usr/bin/env bash
# =============================================================================
# Static guard: the formal William Wallace Mission 1 MVP modpack source
# (assets/nyan/ww_mvp/) must contain zero "Tutorial*" strings, and the
# libopenage production code must contain zero [DBG] instrumentation.
#
# "Tutorial*" was the name of the abandoned tutorial-level system pre-S0;
# renaming migrated to scenario / testscene / Mission / Testscene / etc.
# Anything that still says "Tutorial" in the formal path means a rename was
# missed. Anything that says "[DBG]" in libopenage means a temporary debug
# log from a hunt made it into the build.
#
# Exit 0 = all checks pass. Exit 1 = at least one violation.
# =============================================================================
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
cd "$REPO"

bad=0

echo "=== Static check: no 'Tutorial*' in formal ww_mvp modpack source ==="
WW_MVP_PATH="assets/nyan/ww_mvp"
if [ ! -d "$WW_MVP_PATH" ]; then
	echo "  MISSING: $WW_MVP_PATH"; bad=1
else
	hits="$(grep -rnE 'Tutorial[A-Za-z]?' "$WW_MVP_PATH" \
		--include='*.nyan' --include='*.sh' --include='*.toml' --include='*.md' \
		2>/dev/null || true)"
	if [ -n "$hits" ]; then
		echo "  FAIL: $WW_MVP_PATH contains 'Tutorial*' strings:"
		echo "$hits" | sed 's/^/    /'
		bad=1
	else
		echo "  OK ($WW_MVP_PATH has 0 'Tutorial*' strings)"
	fi
fi

echo
echo "=== Static check: no [DBG] instrumentation in libopenage ==="
dbg_hits="$(grep -rn '\[DBG\]' libopenage/ \
	--include='*.cpp' --include='*.h' 2>/dev/null || true)"
if [ -n "$dbg_hits" ]; then
	echo "  FAIL: libopenage contains [DBG] entries:"
	echo "$dbg_hits" | sed 's/^/    /'
	bad=1
else
	echo "  OK (libopenage has 0 [DBG] entries)"
fi

echo
echo "=== Static check: no canonical --tutorial CLI flag (only --scenario) ==="
tut_flag="$(grep -rn 'add_argument' openage/main/main.py 2>/dev/null \
	| grep -E '"--tutorial"|"--tutorial-' || true)"
if [ -n "$tut_flag" ]; then
	# --tutorial MAY exist as a deprecated alias (S1) but it must NOT be
	# the only / canonical way to launch scenario mode. Flag a warning, not
	# a failure, because the alias is part of the S1 migration window.
	echo "  INFO: --tutorial alias exists (deprecated S1 window):"
	echo "$tut_flag" | sed 's/^/    /'
else
	echo "  OK (--scenario is the only canonical scenario flag)"
fi

echo
echo "=== Static check: pymain bridge still reads scenario_mode ==="
pyx_bridge="$(grep -n 'scenario_mode' openage/main/main_cpp.pyx 2>/dev/null \
	| grep -v '^#' || true)"
if [ -n "$pyx_bridge" ]; then
	echo "  OK (main_cpp.pyx reads scenario_mode, matching C++ main.h args.scenario_mode):"
	echo "$pyx_bridge" | sed 's/^/    /'
else
	echo "  FAIL: main_cpp.pyx does not bridge scenario_mode"
	bad=1
fi

echo
if [ "$bad" -eq 0 ]; then
	echo "STATIC CHECK: OK"
	exit 0
else
	echo "STATIC CHECK: FAIL"
	exit 1
fi
