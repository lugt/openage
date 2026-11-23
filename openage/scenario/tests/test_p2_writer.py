# Copyright 2026 the openage authors. See copying.md for legal info.
"""
P2.3 tests for the mission-pack writer.

The writer takes a ScenarioDefinition and produces the four-file dump
format consumed by the IR loader (P2.4) and the P1 dump-golden tests:

  - scenario.json        - ScenarioDefinition per spec §6.1
  - object_table.csv     - one row per EntityPlacement
  - trigger_graph.json   - trigger/condition/effect histograms
  - coverage.json        - "only implementation list" per plan §P4.1

Properties pinned by the tests:

  - All four files are written.
  - Terrain / elevation dense arrays are stored compactly: the JSON
    records the dense size + the unique-value compression; the
    ground-truth can be reconstructed by repeating the unique value
    ``terrain_ids_count`` times.
  - Round-trip: write -> load_scenario_dump -> ScenarioDefinition.from_dump
    reconstructs the original IR byte-for-byte (modulo metadata fields
    the writer is allowed to ignore, e.g. produced_at_commit).
  - Determinism: writing the same ScenarioDefinition twice yields
    identical bytes (no timestamps, no random IDs).
  - Output is writable into a directory under tmp_path (not the
    repo-tracked P1 dump dir).
"""
from __future__ import annotations

import csv
import hashlib
import json
import pathlib
from dataclasses import replace

import pytest

from openage.scenario.ir import (
    EntityPlacement,
    MapDefinition,
    PlayerDefinition,
    ScenarioDefinition,
    TriggerCondition,
    TriggerDefinition,
    TriggerEffect,
    VictoryDefinition,
)
from openage.scenario.writer import write_mission_pack


def _valid_map() -> MapDefinition:
    return MapDefinition(
        width=16, height=16,
        terrain_ids=["grass"] * 256,
        elevation=[0] * 256,
    )


def _scenario_with_triggers() -> ScenarioDefinition:
    return ScenarioDefinition(
        id="ww_mission1_reconstruction",
        source="hand-authored",
        source_file="f.nyan",
        produced_at_commit="fbb23fcfb6bb05a5ba0dccb8c860573e1e11b72e",
        map=_valid_map(),
        players=[
            PlayerDefinition(player_id=0, name="Tutorial Player", starting_resources=()),
            PlayerDefinition(player_id=1, name="Enemy", starting_resources=()),
        ],
        placements=[
            EntityPlacement(
                scenario_object_id="PlayerVillager",
                entity_definition_fqon="de2_base.data.game_entity.generic.villager.villager.Villager",
                player_id=0,
                position=(8, 8),
            ),
            EntityPlacement(
                scenario_object_id="PlayerTownCenter",
                entity_definition_fqon="de2_base.data.game_entity.generic.town_center.town_center.TownCenter",
                player_id=0,
                position=(8, 9),
            ),
            EntityPlacement(
                scenario_object_id="EnemyVillager",
                entity_definition_fqon="de2_base.data.game_entity.generic.villager.villager.Villager",
                player_id=1,
                position=(1, 1),
            ),
        ],
        areas=[],
        triggers=[
            TriggerDefinition(
                trigger_id="T0",
                initially_enabled=True,
                looping=False,
                delay=0.0,
                conditions=(
                    TriggerCondition(kind="EntityDestroyed",
                                     target_scenario_object_id="EnemyVillager"),
                ),
                effects=(
                    TriggerEffect(kind="DeclareVictory"),
                ),
            ),
        ],
        victory=VictoryDefinition(
            kind="objective-driven",
            required_removals=("EnemyVillager",),
        ),
    )


def _hash_dir(d: pathlib.Path) -> str:
    """Stable hash of every file in d (sorted by relative path)."""
    h = hashlib.sha3_256()
    for p in sorted(d.rglob("*")):
        if p.is_file():
            h.update(str(p.relative_to(d)).encode())
            h.update(p.read_bytes())
    return h.hexdigest()


# ---------------------------------------------------------------------------
# File outputs
# ---------------------------------------------------------------------------


def test_writer_produces_all_four_files(tmp_path: pathlib.Path) -> None:
    s = _scenario_with_triggers()
    write_mission_pack(s, tmp_path)
    for name in ("scenario.json", "object_table.csv",
                 "trigger_graph.json", "coverage.json"):
        assert (tmp_path / name).is_file(), f"missing {name}"


def test_writer_scenario_json_round_trips_through_dump_loader(
    tmp_path: pathlib.Path,
) -> None:
    s = replace(_scenario_with_triggers(), fog_of_war=True, share_allied_vision=True)
    write_mission_pack(s, tmp_path)

    # Use the shared loader (P1) to consume the fresh dump.
    from openage.scenario.dumps import load_scenario_dump
    loaded = load_scenario_dump(tmp_path)
    sd = loaded["scenario"]["ScenarioDefinition"]
    assert sd["id"] == "ww_mission1_reconstruction"
    assert sd["MapDefinition"]["width"] == 16
    assert sd["MapDefinition"]["height"] == 16
    assert len(sd["EntityPlacement"]) == 3

    # The P2 IR can consume the dump as well.
    ir = ScenarioDefinition.from_dump(tmp_path)
    assert ir.fog_of_war and ir.share_allied_vision
    assert ScenarioDefinition.from_dict(ir.to_dict()).share_allied_vision
    assert ir.map.width == 16
    assert ir.map.height == 16
    assert {p.scenario_object_id for p in ir.placements} == {
        "PlayerVillager", "PlayerTownCenter", "EnemyVillager",
    }


# ---------------------------------------------------------------------------
# Determinism
# ---------------------------------------------------------------------------


def test_writer_is_deterministic_across_invocations(tmp_path: pathlib.Path) -> None:
    s = _scenario_with_triggers()
    a = tmp_path / "a"
    b = tmp_path / "b"
    write_mission_pack(s, a)
    write_mission_pack(s, b)
    assert _hash_dir(a) == _hash_dir(b), (
        "writer must be deterministic (no timestamps, no random IDs)"
    )


def test_writer_does_not_emit_unstable_metadata(tmp_path: pathlib.Path) -> None:
    """produced_at_commit is metadata; the writer must either copy it
    or leave it unchanged across calls. Most importantly, the writer
    must NOT inject timestamps or random IDs that would change the
    hash of the output between runs.
    """
    s = _scenario_with_triggers()
    a = tmp_path / "a"
    b = tmp_path / "b"
    write_mission_pack(s, a)
    write_mission_pack(s, b)
    # Compare individual files: scenario.json, trigger_graph.json,
    # coverage.json must be byte-identical. CSV file order is fixed by
    # the scenario's placement list, so it is also stable.
    for name in ("scenario.json", "object_table.csv",
                 "trigger_graph.json", "coverage.json"):
        assert (a / name).read_bytes() == (b / name).read_bytes()


# ---------------------------------------------------------------------------
# Compact dense storage
# ---------------------------------------------------------------------------


def test_writer_records_compact_dense_storage(tmp_path: pathlib.Path) -> None:
    """The P1 dump records `terrain_ids_unique` + `terrain_ids_count`
    so the dense 256-element array is stored as a single repeated
    value. The writer must keep that contract."""
    s = _scenario_with_triggers()
    write_mission_pack(s, tmp_path)
    sc = json.loads((tmp_path / "scenario.json").read_text())
    md = sc["ScenarioDefinition"]["MapDefinition"]
    assert md["terrain_ids_count"] == 256
    assert md["elevation_count"] == 256
    assert len(md["terrain_ids_unique"]) == 1
    assert md["terrain_ids_unique"][0] == "grass"
    assert md["elevation_unique"] == [0]


def test_mixed_map_round_trips_with_rectangular_chunks(tmp_path: pathlib.Path) -> None:
    from dataclasses import replace

    width, height = 13, 11
    tiles = ["grass"] * (width * height)
    elevations = [0] * (width * height)
    tiles[2 * width + 12] = "water"
    tiles[10 * width + 7] = "forest"
    elevations[10 * width + 12] = 4
    elevations[7 * width + 8] = 2
    original = MapDefinition(width, height, tiles, elevations)
    write_mission_pack(replace(_scenario_with_triggers(), map=original), tmp_path)

    assert ScenarioDefinition.from_dump(tmp_path).map == original


# ---------------------------------------------------------------------------
# Object table
# ---------------------------------------------------------------------------


def test_writer_object_table_matches_placements(tmp_path: pathlib.Path) -> None:
    s = _scenario_with_triggers()
    write_mission_pack(s, tmp_path)
    with (tmp_path / "object_table.csv").open() as fh:
        rows = list(csv.DictReader(fh))
    assert {r["scenario_object_id"] for r in rows} == {
        "PlayerVillager", "PlayerTownCenter", "EnemyVillager",
    }
    by_id = {r["scenario_object_id"]: r for r in rows}
    assert by_id["PlayerVillager"]["pos_x"] == "8"
    assert by_id["PlayerVillager"]["pos_y"] == "8"
    assert by_id["EnemyVillager"]["player_id"] == "1"


# ---------------------------------------------------------------------------
# Trigger graph + coverage
# ---------------------------------------------------------------------------


def test_writer_records_trigger_count_and_condition_kind(
    tmp_path: pathlib.Path,
) -> None:
    s = _scenario_with_triggers()
    write_mission_pack(s, tmp_path)
    tg = json.loads((tmp_path / "trigger_graph.json").read_text())
    assert tg["trigger_count"] == 1
    # EntityDestroyed condition in our test scenario
    assert tg["condition_type_histogram"].get("EntityDestroyed", 0) >= 1


def test_writer_records_coverage_supported_vs_unknown(
    tmp_path: pathlib.Path,
) -> None:
    s = _scenario_with_triggers()
    write_mission_pack(s, tmp_path)
    cov = json.loads((tmp_path / "coverage.json").read_text())
    assert cov["trigger_count"] == 1
    # All kinds in this scenario are supported, so unknown_* lists stay empty
    assert cov.get("unsupported_condition_kinds", []) == []
    assert cov.get("unsupported_effect_kinds", []) == []


# ---------------------------------------------------------------------------
# Validation gate
# ---------------------------------------------------------------------------


def test_writer_refuses_to_emit_invalid_scenario(tmp_path: pathlib.Path) -> None:
    """The writer must validate first; an invalid IR never reaches
    disk."""
    s = _scenario_with_triggers()
    # Bypass IR construction by mutating a frozen dataclass is not
    # possible; instead, construct a fresh scenario with a trigger
    # that references an unknown scenario_object_id and confirm the
    # writer raises before writing any file.
    from openage.scenario.ir import InvalidIR
    bad = ScenarioDefinition(
        id="X",
        source="hand-authored",
        source_file="f.nyan",
        map=_valid_map(),
        players=[PlayerDefinition(player_id=0, name="P0", starting_resources=())],
        placements=[
            EntityPlacement(
                scenario_object_id="A",
                entity_definition_fqon="fqon.A",
                player_id=0,
                position=(0, 0),
            ),
        ],
        areas=[],
        triggers=[
            TriggerDefinition(
                trigger_id="T0",
                initially_enabled=True,
                looping=False,
                delay=0.0,
                conditions=(
                    TriggerCondition(kind="EntityDestroyed",
                                     target_scenario_object_id="Ghost"),
                ),
                effects=(),
            ),
        ],
        victory=VictoryDefinition(kind="objective-driven"),
    )
    with pytest.raises(InvalidIR):
        write_mission_pack(bad, tmp_path)
