#!/usr/bin/env bash
# =============================================================================
# William Wallace Mission 1 -- real game-window startup script.
#
# This is the SINGLE source of truth for launching the scenario. Both the
# agent's smoke checks and the user's manual launch MUST call this script;
# do not invent alternative commands that test a weaker entry point.
#
# What this script does:
#   - Prints the exact commit / python / executable / asset dir / modpacks /
#     scenario+player-setup FQON / log path it is using.
#   - Checks that every required file/dir exists (executable, venv, modpack
#     definitions, scenario nyan, built libopenage).
#   - Forbids auto-conversion (--no-convert). It runs against whatever is
#     currently in assets/converted and prints those mtimes so staleness is
#     visible, never silently regenerating or swapping in test fixtures.
#   - Calls the REAL game-window entry: `bin/run --scenario --no-convert
#     --no-prompts` (FULL mode: creates the Qt window + renderer + presenter).
#     It does NOT use --headless or QT_QPA_PLATFORM=offscreen, because those
#     bypass the window/renderer path that is the whole point of the launch.
#   - Saves full stdout+stderr to a timestamped log.
#   - Returns the engine's real exit code (non-zero on failure).
#
# Exit codes are the engine's own: 0 = clean exit, 124 = killed by an outer
# timeout (process was still alive -- smoke success), 134 = SIGABRT,
# 139 = SIGSEGV, etc. EXCEPTION: if the log contains a crash marker
# (FATAL: terminate has been called / uncaught exception / SIGSEGV) the
# script exits 134 EVEN IF the engine itself exited 0 -- rc=0 alone is not
# proof of success (see the crash-marker audit near the end of this script).
# =============================================================================
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
cd "$REPO"

# Shared crash-marker check (FATAL: terminate has been called / uncaught
# exception / SIGSEGV, excluding this script's own exit-code help lines).
# Fail fast if the checker cannot be sourced: under `set -uo pipefail`
# (no -e) a failed source would otherwise continue silently,
# ww_mission1_log_has_fatal would be command-not-found (falsy in the if
# below), and a crash-marker log would pass -- the exact false-positive
# this checker exists to prevent.
source "$REPO/scripts/lib/ww_mission1_log_check.sh" || {
	echo "CHECK FAIL: cannot source $REPO/scripts/lib/ww_mission1_log_check.sh (missing or unreadable); aborting -- refusing to launch without crash-marker detection" >&2
	exit 2
}

TS="$(date +%Y%m%d_%H%M%S)"
LOGDIR="$REPO/logs"
mkdir -p "$LOGDIR"
LOG="$LOGDIR/ww_mission1_${TS}.log"

SCENARIO_FQON="ww_mvp.campaign.william_wallace.mission_01.Mission"
PLAYER_SETUP_FQON="ww_mvp.campaign.william_wallace.mission_01.PlayerSetup"

print_line() { printf '=%.0s' {1..78}; printf '\n'; }

echo "================ openage WW Mission 1 launch ================"
echo "timestamp        : $TS"
echo "repo             : $REPO"
echo "commit           : $(git -C "$REPO" rev-parse HEAD 2>/dev/null || echo '(not a git repo)')"
echo "branch           : $(git -C "$REPO" branch --show-current 2>/dev/null || echo '-')"
echo "python           : $("$REPO/venv/bin/python3" --version 2>&1 || "$REPO/.venv/bin/python3" --version 2>&1 || echo 'MISSING venv python')"
echo "executable       : $REPO/bin/run"
echo "asset dir        : $REPO/assets/converted"
echo "scenario FQON    : $SCENARIO_FQON"
echo "player setup FQON: $PLAYER_SETUP_FQON"
echo "modpacks (scenario activates 'ww_mvp'; engine auto-added by C++, de2_base present for nyan import resolution):"
for m in ww_mvp engine de2_base; do
	f="$REPO/assets/converted/$m/modpack.toml"
	if [ -f "$f" ]; then
		ver="$(grep -E '^version' "$f" | head -1 | sed 's/.*= *"\(.*\)".*/\1/')"
		# Cross-platform mtime: `date -r FILE` works on both BSD (macOS) and GNU date.
		mt="$(date -r "$f" +'%F %T' 2>/dev/null || echo 'unknown')"
		echo "  - $m v$ver  (modpack.toml mtime: $mt)"
	else
		echo "  - $m  MISSING modpack.toml"
	fi
done
echo "log              : $LOG"
print_line

# ---- Pre-flight checks. Any missing file -> abort with a clear message.
fail=0
check() {
	if [ ! -e "$1" ]; then
		echo "CHECK FAIL: missing $2: $1" >&2
		fail=1
	fi
}
check "$REPO/bin/run"                 "executable"
# venv python may live at venv/bin/python3 (macOS layout) or .venv/bin/python3 (Linux layout).
if [ ! -x "$REPO/venv/bin/python3" ] && [ ! -x "$REPO/.venv/bin/python3" ]; then
	echo "CHECK FAIL: missing venv python: $REPO/venv/bin/python3 (or .venv)" >&2
	fail=1
fi
check "$REPO/assets/converted/engine/modpack.toml"   "engine modpack def"
check "$REPO/assets/converted/de2_base/modpack.toml" "de2_base modpack def"
check "$REPO/assets/converted/ww_mvp/campaign/william_wallace/mission_01.nyan" "mission_01 scenario nyan"
# Built library -- the extension differs by platform: .so on Linux, .dylib on macOS.
so="$(find "$REPO/.bin" \( -name 'libopenage.so*' -o -name 'libopenage.*.dylib' \) 2>/dev/null | head -1)"
if [ -z "$so" ]; then
	echo "CHECK FAIL: libopenage.{so,dylib} not found under $REPO/.bin (build the engine first)" >&2
	fail=1
else
	echo "libopenage       : $so"
fi
if [ "$fail" -ne 0 ]; then
	echo "PRE-FLIGHT FAILED; not launching." >&2
	exit 2
fi
print_line

# ---- Real game-window launch. No --headless, no offscreen override.
# --no-convert is the hard kill switch (default is already no-convert; this
# makes it explicit so the launch never silently regenerates assets).
#
# NOTE: we do NOT set -e here; we want the engine's real exit code.
# OPENAGE_RUN_BIN is a TEST hook only (scripts/tests/ use it to substitute a
# fake engine); the default and the normal user command remain $REPO/bin/run.
RUN_BIN="${OPENAGE_RUN_BIN:-$REPO/bin/run}"
echo "Launching: $RUN_BIN --scenario --no-convert --no-prompts"
echo "(FULL mode: Qt window + renderer + presenter. stdout/stderr -> $LOG)"
print_line

"$RUN_BIN" --scenario --no-convert --no-prompts 2>&1 | tee "$LOG"
rc=${PIPESTATUS[0]}

# Crash-marker audit BEFORE passing: the engine's terminate/SIGSEGV handlers
# (libopenage/error/handlers.cpp) can leave "FATAL: terminate has been
# called" / "uncaught exception" / an ANSI "SIGSEGV" in the log while the
# process still exits 0. rc=0 alone is therefore NOT proof of success -- a
# crash marker must fail the launch even when rc=0. (This script's own
# "139=SIGSEGV" exit-code help line is excluded by the shared checker.)
if ww_mission1_log_has_fatal "$LOG"; then
	print_line
	echo "engine exit code: $rc"
	echo "(0=clean exit, 124=killed by timeout=still alive, 134=SIGABRT, 139=SIGSEGV)"
	echo "log saved: $LOG"
	echo "ENGINE_FAIL: crash marker in $LOG (FATAL: terminate has been called / uncaught exception / SIGSEGV); failing the launch regardless of engine exit code $rc"
	exit 134
fi

print_line
echo "engine exit code: $rc"
echo "(0=clean exit, 124=killed by timeout=still alive, 134=SIGABRT, 139=SIGSEGV)"
echo "log saved: $LOG"

exit "$rc"
