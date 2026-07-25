# Copyright 2026 the openage authors. See copying.md for legal info.
"""
Behavior test for the openage.error module.

Verifies the public API that callers in
``openage/main/main.py:172`` and ``openage/game/main.py:121``
depend on, both from the source checkout and from a build-tree
``sys.path`` injection (so the test would also catch a missing
import in the built ``bin/`` tree).
"""
from __future__ import annotations

import importlib
import sys


def test_can_import_from_source():
    """`from openage.error import Error` works on the source checkout."""
    if "openage.error" in sys.modules:
        del sys.modules["openage.error"]
    mod = importlib.import_module("openage.error")
    assert hasattr(mod, "Error"), "openage.error must expose Error class"


def test_error_is_exception():
    """`Error` must be catchable as a regular Python exception."""
    from openage.error import Error

    assert issubclass(Error, Exception), "Error must inherit from Exception"
    try:
        raise Error("nyan API modpack missing")
    except Error as exc:
        assert str(exc) == "nyan API modpack missing"
        assert exc.type_name() == "Error"
    except Exception as exc:
        raise AssertionError(f"Error was not caught as Error: {exc!r}")


def test_error_with_empty_message():
    """`Error('')` must still be a valid exception with str('') == ''."""
    from openage.error import Error

    e = Error("")
    assert str(e) == ""
    assert e.msg == ""
    assert repr(e) == "Error('')"


def test_error_callers_can_import():
    """The actual call sites in main.py and game/main.py must import cleanly.

    This is the smoke test the user asked for: `python -m openage
    game --help` and `python -m openage main --help` must work. The
    command-line help path triggers the module imports without
    requiring an asset path. If the import regresses (e.g. someone
    deletes openage/error/ again) the test fails immediately.
    """
    import subprocess
    result = subprocess.run(
        [sys.executable, "-m", "openage", "game", "--help"],
        capture_output=True, text=True, timeout=30,
    )
    assert result.returncode == 0, (
        f"openage game --help failed (rc={result.returncode}): "
        f"stderr={result.stderr[:200]!r}"
    )
    assert "ModpackError" not in result.stderr
    assert "ModuleNotFoundError" not in result.stderr
