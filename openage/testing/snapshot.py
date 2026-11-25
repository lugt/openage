# Copyright 2026+ the openage authors. See copying.md for legal info.

"""Exact text snapshots with bounded first-mismatch diagnostics."""

from pathlib import Path

from .testing import TestError

UPDATE_GOLDEN = False


class SnapshotMismatchError(TestError):
    """A bounded snapshot failure, reported without a traceback by the CLI."""


def assert_snapshot(name: str, expected: str, actual: str, tick: int) -> None:
    """Compare exact text, including the final newline, and stop at the first difference."""
    if expected == actual:
        return
    before = expected.splitlines(keepends=True)
    after = actual.splitlines(keepends=True)
    line = next(
        idx
        for idx in range(max(len(before), len(after)))
        if (before[idx] if idx < len(before) else None) != (after[idx] if idx < len(after) else None)
    )
    expected_line = repr(before[line]) if line < len(before) else "<EOF>"
    actual_line = repr(after[line]) if line < len(after) else "<EOF>"
    context = []
    for idx in range(max(0, line - 3), min(max(len(before), len(after)), line + 4)):
        if idx == line:
            continue
        left = repr(before[idx]) if idx < len(before) else "<EOF>"
        right = repr(after[idx]) if idx < len(after) else "<EOF>"
        context.append(f"  {idx + 1}: {left}" if left == right else f"  {idx + 1}: {left} | {right}")
    raise SnapshotMismatchError(
        f"{name}: line {line + 1}, Tick {tick}\n"
        f"Expected: {expected_line}\nActual:   {actual_line}\n" + "\n".join(context)
    )


def check_golden(path: Path, actual: str, tick: int) -> None:
    """Read/write UTF-8 bytes so host newline translation never changes a baseline."""
    if UPDATE_GOLDEN:
        path.write_bytes(actual.encode("utf-8"))
        return
    if not path.is_file():
        raise SnapshotMismatchError(f"{path.stem}: missing golden file; run with --update-golden")
    assert_snapshot(path.stem, path.read_bytes().decode("utf-8"), actual, tick)


def headless_regression() -> None:
    """One scenario covers state, draw order, RNG and in-flight movement restoration."""
    from ..config import BUILD_SRC_DIR
    from .cpp_testing import snapshot_scenario

    fixtures = Path(BUILD_SRC_DIR) / "tests" / "golden"
    scenario = (fixtures / "headless.json").read_text(encoding="utf-8")
    state, resumed, render, resumed_render, saved, loaded = snapshot_scenario(scenario)
    assert_snapshot("save_restore_roundtrip", saved, loaded, 50)
    assert_snapshot("save_restore_state", state, resumed, 100)
    assert_snapshot("save_restore_render", render, resumed_render, 100)
    # Restore assertions always run, including when intentionally updating baselines.
    check_golden(fixtures / "headless_state.golden", state, 100)
    check_golden(fixtures / "headless_render.golden", render, 100)
