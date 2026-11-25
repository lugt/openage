# Copyright 2026 the openage authors. See copying.md for legal info.
"""
Behavioral regression tests for the Mission 1 verification harness (Gate A
automation Task 1: make verification fail truthfully on crashes).

Covered behavior:

- ``scripts/lib/ww_mission1_log_check.sh`` -- ``ww_mission1_log_has_fatal``
  must detect the REAL engine crash markers printed by
  ``libopenage/error/handlers.cpp`` (ANSI-wrapped ``FATAL: terminate has
  been called``, ``uncaught exception`` and ``SIGSEGV``) while NOT treating
  the launch scripts' own exit-code help lines (``... 139=SIGSEGV)``) as
  crashes. Those help lines end up in the very logs the check inspects
  because verify_ww_mission1.sh captures run_ww_mission1.sh stdout, help
  echoes included, into its stage-4 log.

- ``scripts/run_ww_mission1.sh`` -- a crash marker in the log must fail the
  launch EVEN WHEN the engine process exits 0 (the false-positive that this
  task fixes); a clean log with rc=0 must still pass. The
  ``OPENAGE_RUN_BIN`` environment variable is a test hook that substitutes a
  fake engine binary; the default user command (``$REPO/bin/run``) is
  unchanged.

- ``scripts/p4_4d_regression.sh --regression`` -- any demo in the 35-demo
  regression set that does not print a PASS marker must make the whole run
  exit non-zero; an all-PASS run must exit 0.

- Missing shared checker -- if ``ww_mission1_log_check.sh`` cannot be
  sourced, both launch scripts must abort with a diagnostic instead of
  continuing silently (an unguarded failed ``source`` under
  ``set -uo pipefail`` left ``ww_mission1_log_has_fatal`` undefined and
  falsy in every ``if`` -- reintroducing the crash false-positive).

Safety: the launch-script tests MUST NOT fall back to running the real
engine. ``require_run_bin_hook`` fails fast (without launching anything)
when a script does not reference ``${OPENAGE_RUN_BIN:-...}``, so a broken
test hook can never silently spawn ``$REPO/bin/run`` (a Qt window / the
multi-minute real regression) from this test suite.
"""

import os
import re
import signal
import subprocess
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
LIB_CHECK = REPO / "scripts" / "lib" / "ww_mission1_log_check.sh"
RUN_SCRIPT = REPO / "scripts" / "run_ww_mission1.sh"
VERIFY_SCRIPT = REPO / "scripts" / "verify_ww_mission1.sh"
REGRESSION_SCRIPT = REPO / "scripts" / "p4_4d_regression.sh"

# Exact crash strings printed by libopenage/error/handlers.cpp:
#   terminate_handler  -> "\n\x1b[31;1mFATAL: terminate has been called\x1b[m\n"
#   terminate_handler  -> "\n\x1b[33muncaught exception\x1b[m\n"
#   sigsegv_handler    -> "\n\x1b[31;1mSIGSEGV\x1b[m\n"
# The handlers emit the ANSI terminal control sequences unconditionally
# (std::cout / write, no isatty check), so the escapes are part of the
# real-crash signature in a saved log.
FATAL_TERMINATE = "\n\x1b[31;1mFATAL: terminate has been called\x1b[m\n"
UNCAUGHT_EXCEPTION = "\n\x1b[33muncaught exception\x1b[m\n"
REAL_SIGSEGV = "\n\x1b[31;1mSIGSEGV\x1b[m\n"

# The launch scripts' own exit-code help lines (run_ww_mission1.sh,
# p4_4d_regression.sh run_windowed, verify_ww_mission1.sh stage 4). They
# contain the literal token "139=SIGSEGV" and must NEVER count as crashes.
HELP_LINES = (
    "engine exit code: 0\n"
    "(0=clean exit, 124=killed by timeout=still alive, 134=SIGABRT, 139=SIGSEGV)\n"
    "(0=clean, 124=still alive (good), 134=SIGABRT, 139=SIGSEGV)\n"
    "  rc=0 (0=clean, 124/137=killed while alive, 134=SIGABRT, 139=SIGSEGV)\n"
)

# A healthy engine log: startup, load, the three Mission 1 spawn lines, plus
# the scripts' help text (which is present in verify's stage-4 capture).
CLEAN_LOG = (
    "[INFO ] openage main: starting\n"
    "[PERF] load_data TOTAL ...\n"
    "[INFO ] spawned placed_instance=ww_mvp.campaign.william_wallace.mission_01.PlayerVillager\n"
    "[INFO ] spawned placed_instance=ww_mvp.campaign.william_wallace.mission_01.PlayerTownCenter\n"
    "[INFO ] spawned placed_instance=ww_mvp.campaign.william_wallace.mission_01.EnemyVillager\n"
) + HELP_LINES

# Fake engine binary. Behavior is selected via environment variables set by
# the tests (the launch scripts pass their environment through):
#   FAKE_MODE      clean | fatal | uncaught | segv   (for --scenario runs)
#   FAKE_FAIL_DEMO <demo id>                        (for regression runs:
#                  this demo prints a FATAL marker and exits 0 instead of
#                  printing a PASS marker)
FAKE_RUN = r'''#!/usr/bin/env bash
# Fake engine binary for the Mission 1 harness tests. Only ever invoked when
# OPENAGE_RUN_BIN points at it; never the real $REPO/bin/run.
if [ "${1:-}" = "main" ]; then
	shift
fi
if [ "${1:-}" = "--scenario" ]; then
	case "${FAKE_MODE:-clean}" in
	clean)
		echo "fake engine: clean startup (rc=0)"
		exit 0
		;;
	fatal)
		printf '\n\033[31;1mFATAL: terminate has been called\033[m\n'
		exit 0
		;;
	uncaught)
		printf '\n\033[33muncaught exception\033[m\n'
		exit 0
		;;
	segv)
		printf '\n\033[31;1mSIGSEGV\033[m\n'
		exit 0
		;;
	*)
		echo "fake engine: unknown FAKE_MODE=${FAKE_MODE}" >&2
		exit 2
		;;
	esac
fi
if [ "${1:-}" = "test" ]; then
	# Invoked as: run test -d gamestate.tests.simulation_demo <ID>
	id="${4:-}"
	if [ -n "${FAKE_FAIL_DEMO:-}" ] && [ "$id" = "$FAKE_FAIL_DEMO" ]; then
		# Crash marker + rc=0: exactly the false-positive this task fixes.
		printf '\033[31;1mFATAL: terminate has been called\033[m\n'
		exit 0
	fi
	echo "DEMO $id: PASS"
	exit 0
fi
echo "fake engine: unsupported invocation: $*" >&2
exit 2
'''


def write_fake_run(tmp_path: Path) -> Path:
	fake = tmp_path / "run"
	fake.write_text(FAKE_RUN)
	fake.chmod(0o755)
	return fake


def run_log_check(log_text: str, tmp_path: Path):
	"""Source the shared lib in bash and run ww_mission1_log_has_fatal."""
	log = tmp_path / "engine.log"
	log.write_text(log_text)
	proc = subprocess.run(
		["bash", "-c", 'source "$1"; ww_mission1_log_has_fatal "$2"',
		 "bash", str(LIB_CHECK), str(log)],
		capture_output=True, text=True)
	return proc.returncode, proc.stdout, proc.stderr


def run_script(args, env_extra=None, timeout=90):
	"""Run a launch script, killing the whole process group on timeout."""
	env = os.environ.copy()
	if env_extra:
		env.update({k: str(v) for k, v in env_extra.items()})
	proc = subprocess.Popen(
		[str(a) for a in args], env=env,
		stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
		text=True, start_new_session=True)
	try:
		out, _ = proc.communicate(timeout=timeout)
	except subprocess.TimeoutExpired:
		try:
			os.killpg(os.getpgid(proc.pid), signal.SIGKILL)
		except (ProcessLookupError, PermissionError):
			pass
		out, _ = proc.communicate()
		raise AssertionError(
			f"{args} did not finish within {timeout}s (killed; the script "
			f"probably launched the REAL engine instead of the fake). "
			f"Output tail:\n{out[-2000:]}")
	return proc.returncode, out


def require_run_bin_hook(script: Path) -> None:
	"""Fail fast unless the script supports the OPENAGE_RUN_BIN test hook.

	Without this guard, a script that ignores OPENAGE_RUN_BIN would make the
	tests below silently launch the REAL engine ($REPO/bin/run: a Qt window /
	the multi-minute 35-demo regression). The hook must keep the
	${OPENAGE_RUN_BIN:-$REPO/bin/run} default so the normal user command is
	unchanged.
	"""
	text = script.read_text()
	assert "${OPENAGE_RUN_BIN:-" in text, (
		f"{script} does not use '${{OPENAGE_RUN_BIN:-$REPO/bin/run}}'; "
		"refusing to execute it from the test suite because it would launch "
		"the REAL engine")


# ---- ww_mission1_log_has_fatal (scripts/lib/ww_mission1_log_check.sh) -----

def test_clean_log_with_help_text_is_not_fatal(tmp_path):
	rc, out, err = run_log_check(CLEAN_LOG, tmp_path)
	assert rc == 1, (
		f"a healthy log (incl. the scripts' 139=SIGSEGV help lines) was "
		f"classified as a crash; stderr: {err}")


def test_missing_log_is_not_fatal(tmp_path):
	# A missing/unreadable log is "no evidence", never a crash claim.
	log = tmp_path / "does-not-exist.log"
	proc = subprocess.run(
		["bash", "-c", 'source "$1"; ww_mission1_log_has_fatal "$2"',
		 "bash", str(LIB_CHECK), str(log)],
		capture_output=True, text=True)
	assert proc.returncode == 1


def test_fatal_terminate_marker_is_fatal(tmp_path):
	rc, out, err = run_log_check(CLEAN_LOG + FATAL_TERMINATE, tmp_path)
	assert rc == 0
	assert "FATAL: terminate has been called" in out


def test_uncaught_exception_marker_is_fatal(tmp_path):
	rc, out, err = run_log_check(CLEAN_LOG + UNCAUGHT_EXCEPTION, tmp_path)
	assert rc == 0
	assert "uncaught exception" in out


def test_real_sigsegv_marker_is_fatal_even_alongside_help_text(tmp_path):
	# The help lines must not mask a REAL (ANSI-wrapped) SIGSEGV marker in
	# the same log.
	rc, out, err = run_log_check(CLEAN_LOG + REAL_SIGSEGV, tmp_path)
	assert rc == 0
	assert "SIGSEGV" in out


# ---- scripts/run_ww_mission1.sh -------------------------------------------

@pytest.mark.parametrize("mode,marker", [
	("fatal", "FATAL: terminate has been called"),
	("uncaught", "uncaught exception"),
	("segv", "SIGSEGV"),
])
def test_run_script_crash_marker_fails_even_with_rc0(tmp_path, mode, marker):
	# The engine's crash handlers can leave a marker in the log while the
	# process still exits 0; the launch must fail truthfully anyway.
	require_run_bin_hook(RUN_SCRIPT)
	fake = write_fake_run(tmp_path)
	rc, out = run_script(
		["bash", str(RUN_SCRIPT)],
		{"OPENAGE_RUN_BIN": str(fake), "FAKE_MODE": mode},
		timeout=60)
	assert rc != 0, f"rc=0 accepted a '{marker}' log as success;\n{out}"
	assert "ENGINE_FAIL" in out, (
		f"crash marker '{marker}' did not produce an ENGINE_FAIL "
		f"classification;\n{out}")


def test_run_script_clean_startup_still_passes(tmp_path):
	require_run_bin_hook(RUN_SCRIPT)
	fake = write_fake_run(tmp_path)
	rc, out = run_script(
		["bash", str(RUN_SCRIPT)],
		{"OPENAGE_RUN_BIN": str(fake), "FAKE_MODE": "clean"},
		timeout=60)
	assert rc == 0, out


# ---- scripts/p4_4d_regression.sh --regression ------------------------------

def _parse_regression_summary(out: str):
	match = re.search(r"==== regression: PASS=(\d+) FAIL=(\d+) ====", out)
	assert match, f"no regression summary line in output:\n{out[-2000:]}"
	return int(match.group(1)), int(match.group(2))


def test_regression_demo_without_pass_marker_fails_whole_run(tmp_path):
	# Demo 13 prints a FATAL crash marker and exits 0 (no PASS marker):
	# the regression run must exit non-zero, not just print "FAIL".
	require_run_bin_hook(REGRESSION_SCRIPT)
	fake = write_fake_run(tmp_path)
	rc, out = run_script(
		["bash", str(REGRESSION_SCRIPT), "--regression"],
		{"OPENAGE_RUN_BIN": str(fake), "FAKE_FAIL_DEMO": "13"},
		timeout=120)
	assert rc != 0, f"regression exited 0 although demo 13 printed no PASS:\n{out}"
	passed, failed = _parse_regression_summary(out)
	assert failed == 1
	assert passed >= 34
	assert "demo 13: FAIL" in out


def test_regression_all_demos_pass_exits_zero(tmp_path):
	require_run_bin_hook(REGRESSION_SCRIPT)
	fake = write_fake_run(tmp_path)
	rc, out = run_script(
		["bash", str(REGRESSION_SCRIPT), "--regression"],
		{"OPENAGE_RUN_BIN": str(fake)},
		timeout=120)
	assert rc == 0, out
	passed, failed = _parse_regression_summary(out)
	assert failed == 0
	# The mission-relevant regression set currently has 35 demos; Task 5 may
	# register more, so assert the floor, not the exact count.
	assert passed >= 35


# ---- missing shared checker must fail loudly, never silently pass ---------

SOURCE_LINE = 'source "$REPO/scripts/lib/ww_mission1_log_check.sh"'


def copy_script_with_missing_lib(script: Path, tmp_path: Path) -> Path:
	"""Copy a launch script into a sandbox, pointing its `source` of the
	shared crash checker at a nonexistent file.

	This models a missing/unreadable helper WITHOUT touching the real repo
	file (renaming/chmod-ing the real one would corrupt the checkout for
	every parallel consumer). The substitution must match the script's real
	source line, otherwise the test fails -- it never silently tests
	something else.
	"""
	text = script.read_text()
	assert SOURCE_LINE in text, (
		f"{script} no longer sources the shared checker via {SOURCE_LINE!r}")
	missing = tmp_path / "missing_ww_mission1_log_check.sh"
	copy = tmp_path / script.name
	copy.write_text(text.replace(SOURCE_LINE, f'source "{missing}"'))
	copy.chmod(0o755)
	return copy


@pytest.mark.parametrize("script,expected_rc,needs_run_bin_hook", [
	(RUN_SCRIPT, 2, True),    # run_ww_mission1.sh: pre-flight failure code
	(VERIFY_SCRIPT, 1, False),  # verify_ww_mission1.sh: HARD_FAIL semantics
])
def test_missing_checker_fails_loudly_instead_of_passing(
		tmp_path, script, expected_rc, needs_run_bin_hook):
	# Regression for the review finding: under `set -uo pipefail` (no -e) an
	# unguarded failed `source` let both scripts continue silently;
	# ww_mission1_log_has_fatal then resolved to command-not-found (127),
	# which is FALSY in the crash-marker `if` conditions -- so a crash-marker
	# log passed verification with no error message: the exact false-positive
	# this task fixes, reintroduced via a missing helper. Both scripts must
	# abort with a diagnostic instead.
	#
	# Engine-launch safety: run_ww_mission1.sh gets the fail-fast hook guard.
	# verify_ww_mission1.sh has no OPENAGE_RUN_BIN hook of its own (it
	# delegates stage 4 to run_ww_mission1.sh); its sandbox copy is safe by
	# construction because REPO resolves to the pytest tmp parent, which has
	# no bin/run -- no stage can launch the real engine even if the source
	# guard were removed.
	if needs_run_bin_hook:
		require_run_bin_hook(script)
	copy = copy_script_with_missing_lib(script, tmp_path)
	fake = write_fake_run(tmp_path)
	rc, out = run_script(
		["bash", str(copy)],
		{"OPENAGE_RUN_BIN": str(fake), "FAKE_MODE": "fatal"},
		timeout=60)
	assert "cannot source" in out, (
		f"missing checker produced no diagnostic; script continued "
		f"(rc={rc}):\n{out}")
	assert rc == expected_rc, out
	# It must abort BEFORE launching the engine (no fake-engine output).
	assert "fake engine" not in out
