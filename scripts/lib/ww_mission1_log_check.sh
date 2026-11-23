#!/usr/bin/env bash
# =============================================================================
# Shared Mission 1 crash-marker check (sourced library, not executed).
#
# Usage (from run_ww_mission1.sh / verify_ww_mission1.sh):
#   source "$REPO/scripts/lib/ww_mission1_log_check.sh"
#   if ww_mission1_log_has_fatal "$LOG"; then ... ENGINE_FAIL ...; fi
#
# ww_mission1_log_has_fatal <log>
#   Returns 0 (success) when <log> contains a REAL engine crash marker as
#   printed by libopenage/error/handlers.cpp:
#     - "FATAL: terminate has been called"   (terminate_handler, line ~87)
#     - "uncaught exception"                 (terminate_handler, line ~90)
#     - "SIGSEGV"                            (sigsegv_handler, line ~142,
#       printed as "\n\x1b[31;1mSIGSEGV\x1b[m\n" -- the handlers emit the
#       ANSI terminal control sequences unconditionally, so the escapes are
#       part of the real-crash signature in a saved log)
#   Returns 1 when the log is clean, missing or unreadable (no evidence =
#   never a crash claim). Any matched evidence lines are printed to stdout
#   so callers can surface them.
#
# IMPORTANT -- help-text exclusion: the launch scripts echo their own
# exit-code help lines, e.g.
#   "(0=clean exit, 124=killed by timeout=still alive, 134=SIGABRT, 139=SIGSEGV)"
# and verify_ww_mission1.sh captures run_ww_mission1.sh stdout (help echoes
# included) into its stage-4 log. Those "139=SIGSEGV" tokens are NOT crashes.
# Any line whose SIGSEGV is part of the "139=SIGSEGV" help token is excluded;
# every other SIGSEGV occurrence counts (the real handler output has no
# "139=" prefix).
# =============================================================================
ww_mission1_log_has_fatal() {
	if [ "$#" -ne 1 ]; then
		echo "ww_mission1_log_has_fatal: usage: ww_mission1_log_has_fatal <log>" >&2
		return 2
	fi
	local log="$1"
	if [ ! -f "$log" ] || [ ! -r "$log" ]; then
		return 1
	fi

	local fatal=1
	local hits=""

	# terminate_handler / uncaught-exception markers are unambiguous literals
	# (the ANSI escapes surround them, they do not interrupt them).
	local term
	term="$(grep -aE 'FATAL: terminate has been called|uncaught exception' "$log" 2>/dev/null || true)"
	if [ -n "$term" ]; then
		hits="${hits}${term}
"
		fatal=0
	fi

	# SIGSEGV: real crashes come from the engine signal handler (ANSI
	# terminal-control wrapped); the scripts' own "139=SIGSEGV" exit-code
	# help lines must NOT count. Excluding lines that contain the help token
	# keeps a REAL SIGSEGV on any other line detectable, even when help text
	# appears in the same log.
	local segv
	segv="$(grep -a 'SIGSEGV' "$log" 2>/dev/null | grep -av '139=SIGSEGV' || true)"
	if [ -n "$segv" ]; then
		hits="${hits}${segv}
"
		fatal=0
	fi

	if [ "$fatal" -eq 0 ]; then
		printf '%s' "$hits"
	fi
	return "$fatal"
}
