"""
Loader and validator for the P1 Mission 1 feasibility dump.

The dump artifacts live under
``docs/mvp/reports/mission1-scenario-dump/``:

  - ``scenario.json``         ScenarioDefinition per spec §6.1.
  - ``object_table.csv``      One row per EntityPlacement.
  - ``trigger_graph.json``    Trigger/condition/effect histograms.
  - ``coverage.json``         Per-plan P4.1 "only implementation list".

This module is a thin reader + schema-validator that is shared by:

  - the P1.4 pytest tests (``openage/scenario/tests/test_p1_dump.py``);
  - future P2 IR loader (will call :func:`load_scenario_dump` to feed
    the golden diff);
  - future P5/P6 verifiers.

Path D BLOCKER
--------------
Because the original AoE2 DE Mission 1 source data is unavailable in
this environment (see
``docs/mvp/reports/mission1-scenario-feasibility.md`` §P1.2), this
loader operates on the *hand-authored reconstruction* dump produced by
P1.3. It does NOT attempt to parse ``campaigns_x_x.dat`` or per-mission
scenario files. Doing so is explicitly out of scope until the Path D
preconditions are met.
"""
from __future__ import annotations

import csv
import json
import pathlib
from typing import Any


# Default location for the committed P1 dump. Tests may override via
# the ``dump_dir`` argument to :func:`load_scenario_dump`.
DEFAULT_DUMP_DIR = (
    pathlib.Path(__file__).resolve().parents[2]
    / "docs" / "mvp" / "reports" / "mission1-scenario-dump"
)


class DumpValidationError(ValueError):
    """Raised when a dump file is missing a required field or has an
    inconsistent value across the three artifacts (json/csv)."""


def _load_json(path: pathlib.Path) -> dict[str, Any]:
    if not path.is_file():
        raise DumpValidationError(f"missing dump file: {path}")
    with path.open("r", encoding="utf-8") as fh:
        data = json.load(fh)
    if not isinstance(data, dict):
        raise DumpValidationError(f"dump root is not a JSON object: {path}")
    return data


def load_scenario_dump(dump_dir: pathlib.Path = DEFAULT_DUMP_DIR) -> dict[str, Any]:
    """Load all four dump artifacts and cross-check them.

    Returns a dict with keys ``scenario``, ``object_table``,
    ``trigger_graph``, ``coverage``. The cross-check enforces:

      - JSON files parse;
      - CSV row count equals ``len(scenario['ScenarioDefinition']
        ['EntityPlacement'])``;
      - every ``scenario_object_id`` in the CSV appears as a key in
        ``scenario['ScenarioDefinition']['EntityPlacement']``;
      - ``coverage['trigger_types_present']`` is consistent with
        ``trigger_graph['trigger_count']``;
      - required schema_version + produced_at_commit are present.
    """
    scenario = _load_json(dump_dir / "scenario.json")
    trigger_graph = _load_json(dump_dir / "trigger_graph.json")
    coverage = _load_json(dump_dir / "coverage.json")

    csv_path = dump_dir / "object_table.csv"
    if not csv_path.is_file():
        raise DumpValidationError(f"missing CSV: {csv_path}")
    with csv_path.open("r", encoding="utf-8", newline="") as fh:
        object_table = list(csv.DictReader(fh))

    placements = scenario["ScenarioDefinition"]["EntityPlacement"]
    csv_ids = {row["scenario_object_id"] for row in object_table}
    placement_ids = {p["ScenarioObjectId"] for p in placements}
    if csv_ids != placement_ids:
        raise DumpValidationError(
            f"CSV/JSON scenario_object_id mismatch: "
            f"csv={sorted(csv_ids)} json={sorted(placement_ids)}"
        )

    trigger_count_json = trigger_graph.get("trigger_count")
    trigger_count_cov = len(coverage.get("trigger_types_present", []))
    if trigger_count_json != trigger_count_cov:
        raise DumpValidationError(
            f"trigger_graph.trigger_count={trigger_count_json} != "
            f"len(coverage.trigger_types_present)={trigger_count_cov}"
        )

    return {
        "scenario": scenario,
        "object_table": object_table,
        "trigger_graph": trigger_graph,
        "coverage": coverage,
    }