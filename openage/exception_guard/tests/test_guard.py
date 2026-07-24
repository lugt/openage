# Copyright 2026 the openage authors. See copying.md for legal info.
"""
Smoke tests for the CLI/engine exception guard.

These verify the guard converts an uncaught Python exception into a
structured non-zero exit, with the exception name and traceback in
stderr, without ever producing a core dump or exiting via SIGABRT.
"""
import subprocess
import sys
import textwrap


def test_structured_exit_on_runtime_error():
    script = textwrap.dedent("""
        from openage.exception_guard.main import run_with_exception_guard
        def boom():
            raise RuntimeError("simulated nyan crash")
        raise SystemExit(run_with_exception_guard(boom))
    """)
    proc = subprocess.run([sys.executable, "-c", script], capture_output=True, text=True)
    assert proc.returncode != 0
    assert proc.returncode != -6  # not SIGABRT
    assert "simulated nyan crash" in proc.stderr
    assert "core" not in proc.stderr.lower()


def test_clean_entry_returns_int():
    script = textwrap.dedent("""
        from openage.exception_guard.main import run_with_exception_guard
        def ok():
            return 0
        raise SystemExit(run_with_exception_guard(ok))
    """)
    proc = subprocess.run([sys.executable, "-c", script], capture_output=True, text=True)
    assert proc.returncode == 0
