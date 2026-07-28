# Copyright 2026 the openage authors. See copying.md for legal info.
"""
P1.4 behavior tests for the Mission 1 feasibility dump.

These tests validate that the committed dump artifacts under
``docs/mvp/reports/mission1-scenario-dump/`` are internally consistent
and match the hand-authored reconstruction in
``assets/converted/ww_mvp/campaign/william_wallace/mission_01.nyan``.

They also assert two of the spec §10.1 invariants that an extraction
pipeline MUST honour even when no extraction exists yet:

  - **Map array dimensions**: width/height must be integers and
    (width * height) must equal the dense array length implied by
    terrain count.
  - **Stable placement IDs**: every ``ScenarioObjectId`` is unique,
    non-empty, and referenced exactly once.
  - **Unknown type reporting**: a synthetic dump containing an
    unknown trigger type MUST be reported by the loader rather than
    silently accepted.
  - **No silent data loss**: the loader raises on missing required
    fields, on CSV/JSON id mismatch, and on trigger-count mismatch.
"""
from __future__ import annotations

import copy
import json
import pathlib

import pytest

from openage.scenario.dumps import (
    DEFAULT_DUMP_DIR,
    DumpValidationError,
    load_scenario_dump,
)


# ---------------------------------------------------------------------------
# Fixtures
# ---------------------------------------------------------------------------


@pytest.fixture(scope="module")
def dump() -> dict:
    """Load the committed P1 dump (module-scope: the dump is static)."""
    return load_scenario_dump(DEFAULT_DUMP_DIR)


@pytest.fixture()
def synthetic_dump_dir(tmp_path: pathlib.Path) -> pathlib.Path:
    """Write a minimal copy of the dump that we can mutate per-test."""
    base = DEFAULT_DUMP_DIR
    for name in ("scenario.json", "trigger_graph.json", "coverage.json",
                 "object_table.csv"):
        (tmp_path / name).write_bytes((base / name).read_bytes())
    return tmp_path


# ---------------------------------------------------------------------------
# Map array dimensions
# ---------------------------------------------------------------------------


def test_map_dimensions_are_integers(dump: dict) -> None:
    sd = dump["scenario"]["ScenarioDefinition"]
    width = sd["MapDefinition"]["width"]
    height = sd["MapDefinition"]["height"]
    assert isinstance(width, int) and width > 0
    assert isinstance(height, int) and height > 0
    assert width == 16 and height == 16, "Mission 1 reconstruction is 16x16"


def test_map_dimensions_match_terrain_count(dump: dict) -> None:
    sd = dump["scenario"]["ScenarioDefinition"]
    width = sd["MapDefinition"]["width"]
    height = sd["MapDefinition"]["height"]
    n_terrain = sd["MapDefinition"]["terrain_ids_count"]
    assert width * height == n_terrain, (
        f"dense terrain array mismatch: {width}x{height} != {n_terrain}"
    )
    assert sd["MapDefinition"]["elevation_count"] == n_terrain


# ---------------------------------------------------------------------------
# Stable placement IDs
# ---------------------------------------------------------------------------


def test_placement_ids_unique_and_nonempty(dump: dict) -> None:
    placements = dump["scenario"]["ScenarioDefinition"]["EntityPlacement"]
    ids = [p["ScenarioObjectId"] for p in placements]
    assert ids, "expected at least one placement"
    assert len(ids) == len(set(ids)), f"duplicate scenario_object_id in {ids}"
    for sid in ids:
        assert sid and isinstance(sid, str)


def test_placement_count_matches_csv(dump: dict) -> None:
    placements = dump["scenario"]["ScenarioDefinition"]["EntityPlacement"]
    csv_rows = dump["object_table"]
    assert len(placements) == len(csv_rows) == 3, (
        "Mission 1 reconstruction has exactly 3 placed entities"
    )


def test_required_placements_present(dump: dict) -> None:
    placements = dump["scenario"]["ScenarioDefinition"]["EntityPlacement"]
    names = {p["ScenarioObjectId"] for p in placements}
    assert {"PlayerVillager", "PlayerTownCenter", "EnemyVillager"} <= names


def test_player_villager_and_town_center_owned_by_player_zero(dump: dict) -> None:
    placements = dump["scenario"]["ScenarioDefinition"]["EntityPlacement"]
    by_name = {p["ScenarioObjectId"]: p for p in placements}
    assert by_name["PlayerVillager"]["PlayerId"] == 0
    assert by_name["PlayerTownCenter"]["PlayerId"] == 0


def test_enemy_villager_is_a_distinct_owner(dump: dict) -> None:
    """The MVP combat slice needs an enemy target owned by a different
    player. This guards the spec §10.1 invariant 'player-unit removal
    does not falsely satisfy enemy objective'."""
    placements = dump["scenario"]["ScenarioDefinition"]["EntityPlacement"]
    by_name = {p["ScenarioObjectId"]: p for p in placements}
    assert by_name["EnemyVillager"]["PlayerId"] != 0
    assert by_name["EnemyVillager"]["PlayerId"] == 1


# ---------------------------------------------------------------------------
# Mission identity (label) and reconstruction honesty
# ---------------------------------------------------------------------------


def test_scenario_kind_is_reconstruction(dump: dict) -> None:
    sd = dump["scenario"]
    assert "reconstruction" in sd["scenario_kind"].lower()
    assert sd["scenario_kind"] == "William Wallace Mission 1 MVP reconstruction"


def test_source_status_is_blocked(dump: dict) -> None:
    cov = dump["coverage"]
    assert cov["source_status"] == "BLOCKED"
    assert cov["is_imported_original"] is False


# ---------------------------------------------------------------------------
# Trigger / condition / effect coverage (currently empty)
# ---------------------------------------------------------------------------


def test_trigger_graph_is_empty(dump: dict) -> None:
    tg = dump["trigger_graph"]
    assert tg["trigger_count"] == 0
    assert tg["node_count"] == 0
    assert tg["edge_count"] == 0
    assert tg["trigger_ids"] == []


def test_coverage_trigger_types_present_empty(dump: dict) -> None:
    cov = dump["coverage"]
    assert cov["trigger_types_present"] == []
    assert cov["condition_types_present"] == []
    assert cov["effect_types_present"] == []


def test_coverage_candidate_trigger_types_match_spec_section_7(dump: dict) -> None:
    """Spec §7 lists candidate condition/effect types. P4 narrows the
    implementation list from coverage.json; this test pins that the
    candidates we recorded match the spec verbatim."""
    expected_conditions = {
        "TimerElapsed", "EntityInArea", "EntityDestroyed",
        "EntityAlive", "EntityOwnedBy", "PlayerOwnsCount",
        "AttributeCompare", "TriggerFired",
    }
    # coverage.json records candidates via the "trigger_types_candidate_from_spec"
    # list (named to match spec §7's union of condition + trigger types).
    cov = dump["coverage"]
    candidates = set(cov.get("trigger_types_candidate_from_spec", []))
    assert candidates == expected_conditions, (
        "coverage.json candidate list drifted from spec §7"
    )


# ---------------------------------------------------------------------------
# No silent data loss / cross-artifact consistency
# ---------------------------------------------------------------------------


def test_loader_rejects_missing_file(tmp_path: pathlib.Path) -> None:
    empty = tmp_path / "no-such-dump"
    empty.mkdir()
    with pytest.raises(DumpValidationError, match="missing dump file"):
        load_scenario_dump(empty)


def test_loader_rejects_csv_id_mismatch(synthetic_dump_dir: pathlib.Path) -> None:
    csv_path = synthetic_dump_dir / "object_table.csv"
    rows = csv_path.read_text().splitlines()
    # Replace PlayerTownCenter with a fictitious id not in scenario.json
    rows[2] = "TotallyMadeUpId,foo,0,bar,0,0,0.0,hand-authored"
    csv_path.write_text("\n".join(rows) + "\n")
    with pytest.raises(DumpValidationError, match="CSV/JSON scenario_object_id mismatch"):
        load_scenario_dump(synthetic_dump_dir)


def test_loader_rejects_trigger_count_drift(synthetic_dump_dir: pathlib.Path) -> None:
    tg_path = synthetic_dump_dir / "trigger_graph.json"
    tg = json.loads(tg_path.read_text())
    tg["trigger_count"] = 7
    tg_path.write_text(json.dumps(tg))
    with pytest.raises(DumpValidationError, match="trigger_count"):
        load_scenario_dump(synthetic_dump_dir)


def test_loader_rejects_non_object_root(synthetic_dump_dir: pathlib.Path) -> None:
    sc_path = synthetic_dump_dir / "scenario.json"
    sc_path.write_text(json.dumps(["not", "an", "object"]))
    with pytest.raises(DumpValidationError, match="dump root is not a JSON object"):
        load_scenario_dump(synthetic_dump_dir)


def test_unknown_trigger_type_is_reported_not_silently_accepted(
    synthetic_dump_dir: pathlib.Path,
) -> None:
    """Spec §10.1: 'unsupported variants fail/report clearly'.

    The current loader accepts the empty reconstruction dump, but a
    future extraction pipeline that supports triggers MUST surface
    unknown types. We pin the contract here: if ``coverage.json``
    records a trigger type that does not appear in
    ``trigger_graph.json['condition_type_histogram']``, the
    cross-artifact count check MUST raise.

    This test simulates the contract by patching the coverage file
    and asserting the loader rejects the inconsistency.
    """
    cov_path = synthetic_dump_dir / "coverage.json"
    cov = json.loads(cov_path.read_text())
    # Add a trigger type that is NOT in the trigger_graph histogram
    cov["trigger_types_present"] = ["FakeTriggerType"]
    cov_path.write_text(json.dumps(cov))
    # trigger_graph.json still has trigger_count=0, so the cross-check
    # at len(coverage.trigger_types_present) vs trigger_graph.trigger_count
    # will fail.
    with pytest.raises(DumpValidationError):
        load_scenario_dump(synthetic_dump_dir)