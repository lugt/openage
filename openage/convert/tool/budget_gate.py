# Copyright 2026 the openage authors. See copying.md for legal info.

"""
Memory budget gate for the convert pipeline.

Activated by the ``--memory-budget-test`` CLI flag (see
``openage.convert.main.init_subparser``). When active it:

* forces the lazy converter backend (``OPENAGE_CONVERTER_BACKEND=lazy``),
* continuously samples process RSS in a background thread so the true peak
  is caught even if it falls between the stage marks,
* records RSS + lazy-LRU stats at known stage boundaries, and
* writes a structured report to ``docs/superpowers/reports/phase1.txt``.

Acceptance budgets (see design doc
``docs/superpowers/plans/2026-07-21-lazy-convert.md``):

  P2 (release gate): peak RSS < 500 MiB
  P3 (stretch):       peak RSS < 300 MiB

The gate is deliberately decoupled from the pipeline: stage marks are
opt-in ``budget.mark("...")`` calls in ``tool.driver``, and the sampler
thread is daemonised so it never blocks process exit.
"""
from __future__ import annotations

import gc
import os
import threading
from datetime import datetime
from typing import Optional

from ...log import info

# P2 is the release gate (must pass). P3 is the stretch goal.
DEFAULT_BUDGET_MIB = 500
STRETCH_BUDGET_MIB = 300

_SAMPLE_INTERVAL_S = 0.25

# Resolve the report path against the repo root (4 dirnames up from this
# file: tool -> convert -> openage -> repo) so the report lands in
# ``docs/superpowers/reports/`` regardless of the caller's CWD.
_REPO_ROOT = os.path.dirname(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
)
DEFAULT_REPORT_PATH = os.path.join(
    _REPO_ROOT, "docs", "superpowers", "reports", "phase1.txt"
)


def _read_rss_mib() -> float:
    """Return current RSS in MiB from /proc/self/status, or -1 on error."""
    try:
        with open("/proc/self/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) / 1024.0
    except OSError:
        pass
    return -1.0


def _lru_line() -> str:
    """One-line summary of the lazy-backend LRU cache, or n/a if unavailable."""
    try:
        from ..value_object.read.dynamic_loader import lru_stats, _lru_cap
        built, cap = lru_stats()
        return f"LRU built/live={built} cap={cap}"
    except Exception:  # pylint: disable=broad-except
        return "LRU: n/a (legacy backend or import failed)"


class BudgetGate:
    """Tracks peak RSS across a convert run and writes a budget report."""

    def __init__(self, budget_mib: int = DEFAULT_BUDGET_MIB,
                 stretch_mib: int = STRETCH_BUDGET_MIB,
                 report_path: str = DEFAULT_REPORT_PATH) -> None:
        self.budget_mib = budget_mib
        self.stretch_mib = stretch_mib
        self.report_path = report_path
        self._peak = 0.0
        self._peak_at = "<init>"
        self._marks: list[tuple[str, float]] = []
        self._stop: Optional[threading.Event] = None
        self._thread: Optional[threading.Thread] = None

    @property
    def peak_mib(self) -> float:
        return self._peak

    def start(self) -> None:
        """Start the background RSS sampler (idempotent)."""
        if self._thread is not None:
            return
        self._stop = threading.Event()
        self._thread = threading.Thread(
            target=self._sample_loop, args=(self._stop,),
            name="openage-budget-gate", daemon=True,
        )
        self._thread.start()
        info("BUDGET_GATE started (budget=%d MiB, stretch=%d MiB; "
             "sampling every %.2fs)", self.budget_mib, self.stretch_mib,
             _SAMPLE_INTERVAL_S)

    def _sample_loop(self, stop: threading.Event) -> None:
        # Skip the first interval so the first sample reflects a real allocation
        # rather than pre-pipeline baseline.
        while not stop.wait(_SAMPLE_INTERVAL_S):
            rss = _read_rss_mib()
            if rss > self._peak:
                self._peak = rss
                self._peak_at = "<sampling>"

    def mark(self, stage: str) -> None:
        """Record RSS (and bump peak) at a named stage boundary."""
        gc.collect()
        rss = _read_rss_mib()
        if rss > self._peak:
            self._peak = rss
            self._peak_at = stage
        self._marks.append((stage, rss))
        info("BUDGET_GATE %-42s rss=%.1f MiB  peak=%.1f MiB",
             stage, rss, self._peak)

    def finish(self, notes: Optional[list[str]] = None) -> dict:
        """Stop sampling and write the report. Returns a result dict."""
        if self._stop is not None:
            self._stop.set()
        # one last check after GC so the reported peak is final
        gc.collect()
        rss = _read_rss_mib()
        if rss > self._peak:
            self._peak = rss
            self._peak_at = "<final>"

        p2_pass = self._peak < self.budget_mib
        p3_pass = self._peak < self.stretch_mib
        self._write_report(p2_pass, p3_pass, notes or [])

        info("BUDGET_GATE finished: peak=%.1f MiB  "
             "P2(<%d)=%s  P3(<%d)=%s  report=%s",
             self._peak, self.budget_mib, "PASS" if p2_pass else "FAIL",
             self.stretch_mib, "PASS" if p3_pass else "FAIL", self.report_path)
        return {
            "peak_mib": self._peak,
            "p2_pass": p2_pass,
            "p3_pass": p3_pass,
            "verdict": "PASS" if p2_pass else "FAIL",
            "report_path": self.report_path,
        }

    def _write_report(self, p2_pass: bool, p3_pass: bool,
                      notes: list[str]) -> None:
        backend = os.environ.get("OPENAGE_CONVERTER_BACKEND", "legacy")
        lru = _lru_line()
        lines: list[str] = []
        lines.append("openage convert - Phase 1 memory budget test")
        lines.append("=" * 60)
        lines.append("Generated: %s" % datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
        lines.append("Backend: %s" % backend)
        lines.append(lru)
        lines.append("")
        lines.append("Peak RSS:           %8.1f MiB  (at %s)"
                     % (self._peak, self._peak_at))
        lines.append("P2 budget (<%d MiB):  %s  %s"
                     % (self.budget_mib, "PASS" if p2_pass else "FAIL",
                        "(release gate)" if p2_pass else "(RELEASE BLOCKER)"))
        lines.append("P3 budget (<%d MiB):  %s  (stretch)"
                     % (self.stretch_mib, "PASS" if p3_pass else "FAIL"))
        lines.append("")
        lines.append("Stage RSS trajectory:")
        if self._marks:
            for stage, rss in self._marks:
                marker = "  <- peak" if stage == self._peak_at else ""
                lines.append("  %-42s %8.1f MiB%s" % (stage, rss, marker))
        else:
            lines.append("  (no stage marks recorded)")
        lines.append("")
        if notes:
            lines.append("Notes:")
            for note in notes:
                lines.append("  - %s" % note)
            lines.append("")
        lines.append("Reproduce:")
        lines.append("  OPENAGE_CONVERTER_BACKEND=lazy openage convert \\")
        lines.append("      --memory-budget-test --source-dir <DAT> "
                     "--output-dir <OUT> --no-media")
        lines.append("")

        os.makedirs(os.path.dirname(self.report_path), exist_ok=True)
        with open(self.report_path, "w") as f:
            f.write("\n".join(lines))
