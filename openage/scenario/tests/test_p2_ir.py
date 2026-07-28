# Copyright 2026 the openage authors. See copying.md for legal info.
"""
P2.1 tests for the normalized Scenario IR.

These tests pin the spec §6.1 shape and constructor invariants:

  - ``ScenarioDefinition`` owns: id, MapDefinition, players,
    placements, areas, triggers, VictoryDefinition; schema_version +
    produced_at_commit metadata.
  - ``MapDefinition`` owns: width, height, terrain_ids (dense),
    elevation (dense); width*height must equal the dense-array size.
  - ``PlayerDefinition`` owns: player_id (>=0, unique),
    name, starting_resources.
  - ``EntityPlacement`` owns: stable ScenarioObjectId, EntityDefinition
    FQON, PlayerId owner, position (x,y >= 0), rotation, overrides.
  - ``AreaDefinition`` owns: area_id, kind, geometry.
  - ``TriggerDefinition`` owns: trigger_id, initially_enabled,
    looping, delay, conditions, effects.
  - ``VictoryDefinition`` owns: kind + required_removals.
  - Schema version is required and pinned to ``"p2-ir-v1"``.

The tests cover:
  - construction succeeds with a valid minimal scenario
  - construction raises on each invariant violation
  - ``to_dict()`` round-trips losslessly
  - ``from_dump()`` consumes the P1 dump artifacts
"""
from __future__ import annotations

import pathlib

import pytest

# These imports intentionally reference the production module that
# P2.1 must create. RED: they fail to import until P2.1 ships.
from openage.scenario.ir import (
    AreaDefinition,
    EntityPlacement,
    InvalidIR,
    MapDefinition,
    PlayerDefinition,
    ScenarioDefinition,
    TriggerDefinition,
    VictoryDefinition,
)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _valid_map() -> MapDefinition:
    return MapDefinition(
        width=16,
        height=16,
        terrain_ids=["grass"] * 256,
        elevation=[0] * 256,
    )


def _valid_player(pid: int = 0) -> PlayerDefinition:
    return PlayerDefinition(
        player_id=pid,
        name=f"Player {pid}",
        starting_resources=(),
    )


def _valid_placement(name: str = "X", pid: int = 0) -> EntityPlacement:
    return EntityPlacement(
        scenario_object_id=name,
        entity_definition_fqon="fqon.A",
        player_id=pid,
        position=(0, 0),
        rotation=0.0,
        overrides={},
    )


def _valid_victory() -> VictoryDefinition:
    return VictoryDefinition(
        kind="objective-driven",
        required_removals=("EnemyVillager",),
    )


def _valid_scenario() -> ScenarioDefinition:
    return ScenarioDefinition(
        id="ww_mission1_reconstruction",
        source="hand-authored",
        source_file="assets/converted/ww_mvp/campaign/william_wallace/mission_01.nyan",
        produced_at_commit="fbb23fcfb6bb05a5ba0dccb8c860573e1e11b72e",
        map=_valid_map(),
        players=[_valid_player(0), _valid_player(1)],
        placements=[
            _valid_placement("PlayerVillager", 0),
            _valid_placement("PlayerTownCenter", 0),
            _valid_placement("EnemyVillager", 1),
        ],
        areas=[],
        triggers=[],
        victory=_valid_victory(),
    )


# ---------------------------------------------------------------------------
# MapDefinition invariants
# ---------------------------------------------------------------------------


def test_map_definition_construction_succeeds_with_matching_dimensions() -> None:
    m = _valid_map()
    assert m.width == 16
    assert m.height == 16
    assert len(m.terrain_ids) == 16 * 16
    assert len(m.elevation) == 16 * 16


def test_map_definition_rejects_terrain_count_mismatch() -> None:
    with pytest.raises(InvalidIR, match="terrain_ids"):
        MapDefinition(
            width=16, height=16,
            terrain_ids=["grass"] * 100,  # wrong count
            elevation=[0] * 256,
        )


def test_map_definition_rejects_elevation_count_mismatch() -> None:
    with pytest.raises(InvalidIR, match="elevation"):
        MapDefinition(
            width=16, height=16,
            terrain_ids=["grass"] * 256,
            elevation=[0] * 100,
        )


def test_map_definition_rejects_nonpositive_dimensions() -> None:
    with pytest.raises(InvalidIR, match="width"):
        MapDefinition(width=0, height=16, terrain_ids=[], elevation=[])
    with pytest.raises(InvalidIR, match="height"):
        MapDefinition(width=16, height=0, terrain_ids=[], elevation=[])


# ---------------------------------------------------------------------------
# PlayerDefinition invariants
# ---------------------------------------------------------------------------


def test_player_definition_rejects_negative_id() -> None:
    with pytest.raises(InvalidIR, match="player_id"):
        PlayerDefinition(player_id=-1, name="X", starting_resources=())


def test_player_definition_rejects_empty_name() -> None:
    with pytest.raises(InvalidIR, match="name"):
        PlayerDefinition(player_id=0, name="", starting_resources=())


# ---------------------------------------------------------------------------
# EntityPlacement invariants
# ---------------------------------------------------------------------------


def test_entity_placement_rejects_empty_scenario_object_id() -> None:
    with pytest.raises(InvalidIR, match="scenario_object_id"):
        EntityPlacement(
            scenario_object_id="",
            entity_definition_fqon="fqon.A",
            player_id=0,
            position=(0, 0),
            rotation=0.0,
            overrides={},
        )


def test_entity_placement_rejects_empty_entity_definition() -> None:
    with pytest.raises(InvalidIR, match="entity_definition_fqon"):
        EntityPlacement(
            scenario_object_id="X",
            entity_definition_fqon="",
            player_id=0,
            position=(0, 0),
            rotation=0.0,
            overrides={},
        )


def test_entity_placement_rejects_negative_position() -> None:
    with pytest.raises(InvalidIR, match="position"):
        EntityPlacement(
            scenario_object_id="X",
            entity_definition_fqon="fqon.A",
            player_id=0,
            position=(-1, 0),
            rotation=0.0,
            overrides={},
        )


# ---------------------------------------------------------------------------
# ScenarioDefinition invariants
# ---------------------------------------------------------------------------


def test_scenario_definition_construction_succeeds_with_valid_inputs() -> None:
    s = _valid_scenario()
    assert s.id == "ww_mission1_reconstruction"
    assert s.schema_version == "p2-ir-v1"
    assert s.produced_at_commit  # non-empty
    assert len(s.placements) == 3
    assert len(s.players) == 2


def test_scenario_definition_rejects_duplicate_placement_ids() -> None:
    with pytest.raises(InvalidIR, match="duplicate scenario_object_id"):
        ScenarioDefinition(
            id="X",
            source="hand-authored",
            source_file="f.nyan",
            map=_valid_map(),
            players=[_valid_player(0)],
            placements=[
                _valid_placement("Dup", 0),
                _valid_placement("Dup", 0),
            ],
            areas=[],
            triggers=[],
            victory=_valid_victory(),
        )


def test_scenario_definition_rejects_duplicate_player_ids() -> None:
    with pytest.raises(InvalidIR, match="duplicate player_id"):
        ScenarioDefinition(
            id="X",
            source="hand-authored",
            source_file="f.nyan",
            map=_valid_map(),
            players=[_valid_player(0), _valid_player(0)],
            placements=[],
            areas=[],
            triggers=[],
            victory=_valid_victory(),
        )


def test_scenario_definition_rejects_owner_out_of_range() -> None:
    with pytest.raises(InvalidIR, match="owner player_id"):
        ScenarioDefinition(
            id="X",
            source="hand-authored",
            source_file="f.nyan",
            map=_valid_map(),
            players=[_valid_player(0)],
            placements=[_valid_placement("X", 5)],  # owner 5 missing
            areas=[],
            triggers=[],
            victory=_valid_victory(),
        )


def test_scenario_definition_rejects_empty_id() -> None:
    with pytest.raises(InvalidIR, match="id"):
        ScenarioDefinition(
            id="",
            source="hand-authored",
            source_file="f.nyan",
            map=_valid_map(),
            players=[_valid_player(0)],
            placements=[],
            areas=[],
            triggers=[],
            victory=_valid_victory(),
        )


# ---------------------------------------------------------------------------
# Schema version + round-trip
# ---------------------------------------------------------------------------


def test_schema_version_is_pinned() -> None:
    assert ScenarioDefinition.SCHEMA_VERSION == "p2-ir-v1"


def test_to_dict_roundtrip() -> None:
    s = _valid_scenario()
    as_dict = s.to_dict()
    restored = ScenarioDefinition.from_dict(as_dict)
    assert restored.id == s.id
    assert restored.source == s.source
    assert restored.source_file == s.source_file
    assert restored.map.width == s.map.width
    assert restored.map.height == s.map.height
    assert list(restored.map.terrain_ids) == list(s.map.terrain_ids)
    assert list(restored.map.elevation) == list(s.map.elevation)
    assert {p.player_id for p in restored.players} == {p.player_id for p in s.players}
    assert {p.scenario_object_id for p in restored.placements} == {
        p.scenario_object_id for p in s.placements
    }


def test_from_dump_consumes_p1_artifacts(tmp_path: pathlib.Path) -> None:
    """The IR must be loadable from the P1 dump artifacts so the
    dump-golden diffs in P1.4 still pass after P2 replaces the
    manual dump with an auto-generated one."""
    # Use the committed P1 dump dir directly.
    from openage.scenario.dumps import DEFAULT_DUMP_DIR
    s = ScenarioDefinition.from_dump(DEFAULT_DUMP_DIR)
    assert s.id == "ww_mission1_reconstruction"
    assert s.map.width == 16
    assert s.map.height == 16
    assert len(s.placements) == 3
    by_id = {p.scenario_object_id: p for p in s.placements}
    assert by_id["PlayerVillager"].player_id == 0
    assert by_id["EnemyVillager"].player_id == 1


# ---------------------------------------------------------------------------
# TriggerDefinition invariants
# ---------------------------------------------------------------------------


def test_trigger_definition_stores_required_fields() -> None:
    t = TriggerDefinition(
        trigger_id="T0",
        initially_enabled=True,
        looping=False,
        delay=0.0,
        conditions=(),
        effects=(),
    )
    assert t.trigger_id == "T0"
    assert t.initially_enabled is True
    assert t.looping is False
    assert t.delay == 0.0


def test_scenario_definition_rejects_duplicate_trigger_ids() -> None:
    t = lambda tid: TriggerDefinition(  # noqa: E731
        trigger_id=tid, initially_enabled=True, looping=False,
        delay=0.0, conditions=(), effects=(),
    )
    with pytest.raises(InvalidIR, match="duplicate trigger_id"):
        ScenarioDefinition(
            id="X",
            source="hand-authored",
            source_file="f.nyan",
            map=_valid_map(),
            players=[_valid_player(0)],
            placements=[],
            areas=[],
            triggers=[t("T0"), t("T0")],
            victory=_valid_victory(),
        )


# ---------------------------------------------------------------------------
# AreaDefinition invariants
# ---------------------------------------------------------------------------


def test_area_definition_roundtrip() -> None:
    a = AreaDefinition(
        area_id="A0", kind="rect", geometry={"x0": 0, "y0": 0, "x1": 8, "y1": 8},
    )
    as_dict = a.to_dict()
    restored = AreaDefinition.from_dict(as_dict)
    assert restored.area_id == a.area_id
    assert restored.kind == a.kind
    assert restored.geometry == a.geometry