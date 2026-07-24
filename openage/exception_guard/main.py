# Copyright 2026 the openage authors. See copying.md for legal info.
"""
CLI/engine exception guard.

Wraps a CLI entry point so uncaught Python exceptions produce a structured
non-zero exit with a traceback, instead of allowing the interpreter to
silently abort, dump core, or invoke std::terminate from a native side.
"""
import signal
import sys
import traceback


_INSTALLED = False


def install_terminate_handler() -> None:
    """
    Install signal handlers so SIGTERM / SIGINT raise a clean SystemExit
    rather than dumping core. Idempotent.
    """
    global _INSTALLED
    if _INSTALLED:
        return

    def _handler(signum, _frame):
        sys.exit(128 + signum)

    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, _handler)
    _INSTALLED = True


def run_with_exception_guard(entry) -> int:
    """
    Invoke ``entry`` and return its int return value, translating any
    uncaught exception into a structured exit code of 1 with the
    exception name and traceback written to stderr.
    """
    try:
        return int(entry())
    except KeyboardInterrupt:
        return 130
    except SystemExit as exc:
        return int(exc.code) if exc.code is not None else 0
    except BaseException as exc:  # noqa: BLE001
        sys.stderr.write(
            f"[openage] uncaught {type(exc).__name__}: {exc}\n"
        )
        sys.stderr.write(
            "".join(traceback.format_exception(type(exc), exc, exc.__traceback__))
        )
        return 1
