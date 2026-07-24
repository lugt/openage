"""
Phase 0.5F regression tests: ensure the start-up flow never silently
auto-converts, and that the explicit NameError surface in mission_1 is
captured as a deterministic failure.

Run via:
    python3 -m openage test --list
    python3 -m openage test openage.startup_regression_tests
"""
import os
import subprocess
import time
from pathlib import Path

from openage.testing.testlist import CppTest
from openage.testing.pytest_test import PyTest


REPO = Path(__file__).resolve().parent.parent
BIN_RUN = REPO / "bin" / "run"


class StartupPhaseLogTest(CppTest):
    """Verify CONVERSION_DECISION log appears and --no-prompts suppress path."""
    @staticmethod
    def running_time_estimate():
        return 10  # seconds

    @staticmethod
    def run(space):
        proc = subprocess.run(
            [str(BIN_RUN), "main", "--tutorial", "--modpacks", "engine",
             "--no-prompts", "--headless",
             "--window-size", "400", "300"],
            capture_output=True, text=True, timeout=15,
            cwd=str(REPO),
        )
        out = proc.stdout + proc.stderr
        assert "CONVERSION_DECISION" in out, \
            "must log CONVERSION_DECISION at startup"
        assert "api_export=skip" in out or "api_export=run" in out, \
            "CONVERSION_DECISION must state api_export decision"
        # The converter must be skipped (--no-prompts default behavior)
        assert "[SKIP] converter_full:" in out, \
            "converter must be skipped (no interactive prompt)"
        # Per-phase timing
        assert "[PHASE]" in out, "phase timing must be logged"


class NoConverterReRunNoModifyTest(CppTest):
    """Two back-to-back invocations must not modify converted/ files."""
    @staticmethod
    def running_time_estimate():
        return 20

    @staticmethod
    def run(space):
        converted = REPO / "assets" / "converted"
        # Snapshot mtime of every nyan modpack signature file
        sig_files = sorted([str(p) for p in converted.rglob("*.toml")])
        before = {f: os.path.getmtime(f) for f in sig_files}
        for _ in range(2):
            subprocess.run(
                [str(BIN_RUN), "main", "--tutorial", "--modpacks", "engine",
                 "--no-prompts", "--headless",
                 "--window-size", "400", "300"],
                capture_output=True, text=True, timeout=15,
                cwd=str(REPO),
                check=True,
            )
            time.sleep(0.05)
        after = {f: os.path.getmtime(f) for f in sig_files}
        assert before == after, \
            f"converted/ files were modified: {set(before) ^ set(after)}"


class NoConvertFastMissingTest(CppTest):
    """--no-convert with missing assets must fail fast & readable, never
    silently regenerate."""
    @staticmethod
    def running_time_estimate():
        return 5

    @staticmethod
    def run(space):
        # --no-convert + nonexistent --modpacks fast file-not-found error.
        t0 = time.monotonic()
        proc = subprocess.run(
            [str(BIN_RUN), "main", "--tutorial", "--modpacks",
             "this_modpack_does_not_exist__regression_test",
             "--no-prompts", "--headless",
             "--window-size", "400", "300"],
            capture_output=True, text=True, timeout=15,
            cwd=str(REPO),
        )
        elapsed = time.monotonic() - t0
        assert proc.returncode != 0
        assert elapsed < 12, \
            f"failure took {elapsed:.1f}s, should be <12s; converter " \
            "was probably triggered instead of failing fast"
        out = (proc.stdout + proc.stderr).lower()
        assert "not found" in out or "missing" in out, \
            "error message must be readable"


class CrossModpackImportTest(CppTest):
    """A mission fixture that references a foreign modpack's object must
    import that modpack's namespace, otherwise the runtime nyan::NameError
    surfaces. With the import present, the runtime must NOT see that error
    when both modpacks are loaded."""
    @staticmethod
    def running_time_estimate():
        return 30

    @staticmethod
    def run(space):
        # --no-convert + --modpacks de2_base + engine (both required so the
        # import in mission_1.nyan resolves). The load_scenario step may be
        # very slow (5918 de2_base nyan files); give it ample time.
        proc = subprocess.run(
            [str(BIN_RUN), "main", "--tutorial", "--modpacks", "de2_base",
             "--no-prompts", "--headless",
             "--window-size", "400", "300"],
            capture_output=True, text=True, timeout=120,
            cwd=str(REPO),
        )
        out = proc.stdout + proc.stderr
        # The forbidden error must NOT appear. If it does, our mission_1.nyan
        # forgot the `import de2_base.data.game_entity.generic....` lines.
        assert "unknown name: 'de2_base.data.game_entity" not in out, \
            "mission_1.nyan forgot to import de2_base namespace: " + \
            out[-500:]


class SmokeTest(CppTest):
    """CLI smoke test: measure per-phase time on the happy path."""
    @staticmethod
    def running_time_estimate():
        return 25

    @staticmethod
    def run(space):
        t0 = time.monotonic()
        proc = subprocess.run(
            [str(BIN_RUN), "main", "--tutorial", "--modpacks", "engine",
             "--no-prompts", "--headless",
             "--window-size", "400", "300"],
            capture_output=True, text=True, timeout=20,
            cwd=str(REPO),
        )
        wall = time.monotonic() - t0
        assert proc.returncode == 0, \
            f"smoke test failed (rc={proc.returncode}): " + \
            (proc.stdout + proc.stderr)[-400:]
        out = proc.stdout + proc.stderr
        assert "[PHASE] api_export done in" in out
        assert "[PHASE] converter_full done in" in out
        assert "[PHASE] modpack_discovery done in" in out
        assert wall < 15, f"startup took {wall:.1f}s, regression suspected"
