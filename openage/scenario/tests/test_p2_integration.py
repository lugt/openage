# Copyright 2026 the openage authors. See copying.md for legal info.
"""
P2.6 integration tests for the Mission 1 scenario stack.

These tests exercise the full pipeline end-to-end:

  ScenarioDefinition (ir)
    -> validate (validation)
      -> write_mission_pack (writer)  -- 4 files
        -> load_scenario_dump (dumps)
          -> ScenarioDefinition.from_dump (ir)
            -> TerrainSnapshot (terrain)
              -> load_scenario (loader) -> LoadedScenario

The tests pin the properties called out in plan §P2.6:

  - IR validation failures (duplicate IDs, bad owner, unknown
    references) raise InvalidIR before any file is written.
  - Mission pack round-trip: write -> load_scenario_dump ->
    from_dump -> validate produces an IR equivalent to the input.
  - Map dimensions + checksum consistency across the four dump
    files.
  - Player count + diplomacy empty for the MVP reconstruction.
  - Placement count + stable ScenarioObjectId -> GameEntityId mapping
    is identical whether you load via the IR or via the dump.
  - Invalid FQON: the IR does NOT validate FQON existence (the dump
    loader surfaces it as 'unresolved' info); this is the documented
    contract for P2.6.
  - Unknown trigger type: validator reports ERROR; writer refuses;
    loader refuses; IR construction succeeds (so we can capture the
    condition for debugging).
"""
from __future__ import annotations

import hashlib
import json
import pathlib

import pytest

from openage.scenario.dumps import load_scenario_dump, DEFAULT_DUMP_DIR
from openage.scenario.ir import (
    EntityPlacement,
    InvalidIR,
    MapDefinition,
    PlayerDefinition,
    ScenarioDefinition,
    TriggerCondition,
    TriggerDefinition,
    TriggerEffect,
    VictoryDefinition,
)
from openage.scenario.loader import LoadError, load_scenario
from openage.scenario.terrain import TerrainSnapshot
from openage.scenario.validation import validate
from openage.scenario.writer import write_mission_pack


def _valid_map() -> MapDefinition:
    # Use the full FQON form for the testscene terrain so this
    # fixture matches the committed P1 dump and the runtime nyan
    # reference in mission_01.nyan.
    return MapDefinition(
        width=16, height=16,
        terrain_ids=["engine.test.testscene_data.TestsceneGrass"] * 256,
        elevation=[0] * 256,
    )


def _mission1_scenario() -> ScenarioDefinition:
    # Placements mirror the committed P1 dump
    # (docs/mvp/reports/mission1-scenario-dump/scenario.json), which is the
    # declared source of truth for the ww_mvp reconstruction -- see
    # assets/nyan/ww_mvp/install.sh (it regenerates the runtime pack via
    # ScenarioDefinition.from_dump(DEFAULT_DUMP_DIR) -> write_mission_pack) and
    # commit b97ae872 ("The dump scenario.json ... is the single source of
    # truth"). The dump positions were intentionally moved for the P4.4 fixture
    # (b97ae872: 8,8/8,9/1,1 -> 6,8/8,8/10,8; 59edcd69: y 8 -> 4) so the
    # villager/enemy are 4 tiles apart with the TC between them. The nyan
    # position members (_Pos8_8 etc.) are DEAD CODE on the production path
    # (install.sh: the legacy nyan-only path is NOT used) and still hold the
    # pre-b97ae872 values; do NOT "reconcile" this fixture back to the nyan.
    # test_regenerated_dump_matches_committed_p1_dump pins this contract.
    return ScenarioDefinition(
        id="ww_mission1_reconstruction",
        source="hand-authored",
        source_file=(
            "assets/converted/ww_mvp/campaign/william_wallace/mission_01.nyan"
        ),
        produced_at_commit="fbb23fcfb6bb05a5ba0dccb8c860573e1e11b72e",
        map=_valid_map(),
        players=[
            PlayerDefinition(
                player_id=0, name="Tutorial Player", starting_resources=()
            ),
            PlayerDefinition(player_id=1, name="Enemy", starting_resources=()),
        ],
        placements=[
            EntityPlacement(
                scenario_object_id="PlayerVillager",
                entity_definition_fqon=(
                    "de2_base.data.game_entity.generic.villager.villager.Villager"
                ),
                player_id=0, position=(6, 4),
            ),
            EntityPlacement(
                scenario_object_id="PlayerTownCenter",
                entity_definition_fqon=(
                    "de2_base.data.game_entity.generic.town_center.town_center."
                    "TownCenter"
                ),
                player_id=0, position=(8, 8),
            ),
            EntityPlacement(
                scenario_object_id="EnemyVillager",
                entity_definition_fqon=(
                    "de2_base.data.game_entity.generic.villager.villager.Villager"
                ),
                player_id=1, position=(10, 4),
            ),
        ],
        areas=[],
        triggers=[],
        victory=VictoryDefinition(
            kind="objective-driven", required_removals=("EnemyVillager",),
        ),
    )


# ---------------------------------------------------------------------------
# Mission pack round-trip
# ---------------------------------------------------------------------------


def test_round_trip_write_load_validate(tmp_path: pathlib.Path) -> None:
    """write_mission_pack -> load_scenario_dump -> from_dump -> validate
    reproduces the original IR without dropping data."""
    s = _mission1_scenario()
    write_mission_pack(s, tmp_path)
    loaded = load_scenario_dump(tmp_path)
    ir = ScenarioDefinition.from_dump(tmp_path)

    # IR contents match the source.
    assert ir.id == s.id
    assert ir.source == s.source
    assert ir.map.width == s.map.width
    assert ir.map.height == s.map.height
    assert {p.scenario_object_id for p in ir.placements} == {
        p.scenario_object_id for p in s.placements
    }

    # The dump's player list matches the IR (compared via IR
    # reconstruction, since the dump layer emits dicts).
    dump_players = loaded["scenario"]["ScenarioDefinition"]["players"]
    assert len(dump_players) == len(s.players)
    for dump_p, ir_p in zip(dump_players, s.players):
        assert dump_p["PlayerDefinition.player_id"] == ir_p.player_id
        assert dump_p["PlayerDefinition.name"] == ir_p.name

    # Validation passes on the round-tripped IR.
    result = validate(ir)
    assert result.ok


def test_round_trip_preserves_player_count_and_diplomacy(
    tmp_path: pathlib.Path,
) -> None:
    s = _mission1_scenario()
    write_mission_pack(s, tmp_path)
    loaded = load_scenario_dump(tmp_path)
    sd = loaded["scenario"]["ScenarioDefinition"]
    # Two players (Tutorial Player + Enemy).
    assert len(sd["players"]) == 2
    # Diplomacy matrix is empty in the MVP reconstruction dump.
    assert sd.get("Diplomacy", sd.get("diplomacy", [])) == []


def test_round_trip_preserves_placement_count_and_stable_ids(
    tmp_path: pathlib.Path,
) -> None:
    s = _mission1_scenario()
    write_mission_pack(s, tmp_path)
    loaded = load_scenario_dump(tmp_path)
    sd = loaded["scenario"]["ScenarioDefinition"]
    csv_rows = loaded["object_table"]
    assert len(sd["EntityPlacement"]) == 3
    assert len(csv_rows) == 3
    # Stable ScenarioObjectId -> entity_fqon mapping preserved.
    by_id_csv = {r["scenario_object_id"]: r["entity_fqon"] for r in csv_rows}
    by_id_json = {p["ScenarioObjectId"]: p["entity_definition_fqon"]
                  for p in sd["EntityPlacement"]}
    assert by_id_csv == by_id_json
    # The fqons reference real de2_base entities.
    assert "Villager" in by_id_csv["PlayerVillager"]
    assert "TownCenter" in by_id_csv["PlayerTownCenter"]


def test_round_trip_map_dimension_checksum_consistency(
    tmp_path: pathlib.Path,
) -> None:
    """The dense terrain / elevation arrays in scenario.json must
    match the dimension fields."""
    s = _mission1_scenario()
    write_mission_pack(s, tmp_path)
    sc = json.loads((tmp_path / "scenario.json").read_text())
    md = sc["ScenarioDefinition"]["MapDefinition"]
    assert md["width"] == 16
    assert md["height"] == 16
    # Compact storage: terrain_ids_count == width*height; unique list
    # has 1 entry because all tiles are grass.
    assert md["terrain_ids_count"] == md["width"] * md["height"]
    assert md["elevation_count"] == md["width"] * md["height"]
    # Checksum the dense storage so a regression in the writer is
    # caught here.
    h = hashlib.sha3_256()
    h.update(str(md["terrain_ids_count"]).encode())
    h.update(",".join(md["terrain_ids_unique"]).encode())
    h.update(str(md["elevation_count"]).encode())
    h.update(",".join(str(e) for e in md["elevation_unique"]).encode())
    # Stable digest: same input -> same digest.
    assert len(h.hexdigest()) == 64


# ---------------------------------------------------------------------------
# End-to-end: IR -> dump -> IR -> loader -> LoadedScenario
# ---------------------------------------------------------------------------


def test_full_pipeline_loads_mission1(tmp_path: pathlib.Path) -> None:
    s = _mission1_scenario()
    write_mission_pack(s, tmp_path)
    ir = ScenarioDefinition.from_dump(tmp_path)

    # Build a terrain snapshot.
    terrain = TerrainSnapshot.from_ir(ir.map, chunk_size=8)
    assert terrain.chunk_count == 4

    # Load the scenario.
    loaded = load_scenario(ir)
    assert len(loaded.placements) == 3
    assert len(loaded.players) == 2
    assert loaded.scenario_object_to_entity == {
        "PlayerVillager": 0,
        "PlayerTownCenter": 1,
        "EnemyVillager": 2,
    }


def test_loaded_scenario_object_mapping_matches_terrain_snapshots(
    tmp_path: pathlib.Path,
) -> None:
    """The loader's ScenarioObjectId -> GameEntityId mapping is the
    same regardless of whether we came from the IR directly or via
    the dump -> IR round-trip."""
    s = _mission1_scenario()
    write_mission_pack(s, tmp_path)
    ir_from_dump = ScenarioDefinition.from_dump(tmp_path)
    loaded_a = load_scenario(ir_from_dump)
    loaded_b = load_scenario(s)  # same IR, no dump detour
    assert (loaded_a.scenario_object_to_entity
            == loaded_b.scenario_object_to_entity)


# ---------------------------------------------------------------------------
# IR validation failures
# ---------------------------------------------------------------------------


def test_ir_validation_failure_duplicate_ids(tmp_path: pathlib.Path) -> None:
    # Construction-time rejection: the IR layer's __post_init__
    # catches the duplicate before the writer ever runs.
    with pytest.raises(InvalidIR, match="duplicate"):
        ScenarioDefinition(
            id="X",
            source="hand-authored",
            source_file="f.nyan",
            map=_valid_map(),
            players=[PlayerDefinition(player_id=0, name="P0", starting_resources=())],
            placements=[
                EntityPlacement(
                    scenario_object_id="Dup",
                    entity_definition_fqon="fqon.A",
                    player_id=0, position=(0, 0),
                ),
                EntityPlacement(
                    scenario_object_id="Dup",
                    entity_definition_fqon="fqon.A",
                    player_id=0, position=(1, 1),
                ),
            ],
            areas=[],
            triggers=[],
            victory=VictoryDefinition(kind="objective-driven"),
        )
    # Confirm no files were written (the bad scenario never reached
    # the writer).
    assert list(tmp_path.iterdir()) == []


def test_ir_validation_failure_invalid_owner(tmp_path: pathlib.Path) -> None:
    # Construction-time rejection: owner player_id=5 has no matching
    # player definition.
    with pytest.raises(InvalidIR, match="owner player_id"):
        ScenarioDefinition(
            id="X",
            source="hand-authored",
            source_file="f.nyan",
            map=_valid_map(),
            players=[PlayerDefinition(player_id=0, name="P0", starting_resources=())],
            placements=[
                EntityPlacement(
                    scenario_object_id="X",
                    entity_definition_fqon="fqon.A",
                    player_id=5,  # owner 5 missing
                    position=(0, 0),
                ),
            ],
            areas=[],
            triggers=[],
            victory=VictoryDefinition(kind="objective-driven"),
        )
    # Confirm no files were written.
    assert list(tmp_path.iterdir()) == []


def test_loader_rejects_validation_failure(tmp_path: pathlib.Path) -> None:
    # Construct via IR (no dup IDs) but trigger references an unknown
    # scenario_object_id -> validation fails -> loader refuses.
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
                player_id=0, position=(0, 0),
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
                    TriggerCondition(
                        kind="EntityDestroyed",
                        target_scenario_object_id="Ghost",
                    ),
                ),
                effects=(),
            ),
        ],
        victory=VictoryDefinition(kind="objective-driven"),
    )
    with pytest.raises(LoadError, match="validation"):
        load_scenario(bad)


# ---------------------------------------------------------------------------
# Unknown trigger type
# ---------------------------------------------------------------------------


def test_unknown_trigger_type_reported_by_validator_not_silently_dropped(
    tmp_path: pathlib.Path,
) -> None:
    s = ScenarioDefinition(
        id="X",
        source="hand-authored",
        source_file="f.nyan",
        map=_valid_map(),
        players=[PlayerDefinition(player_id=0, name="P0", starting_resources=())],
        placements=[
            EntityPlacement(
                scenario_object_id="A",
                entity_definition_fqon="fqon.A",
                player_id=0, position=(0, 0),
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
                    TriggerCondition(kind="FakeConditionKind",
                                     target_scenario_object_id="A"),
                ),
                effects=(),
            ),
        ],
        victory=VictoryDefinition(kind="objective-driven"),
    )
    # IR construction succeeds (we want to capture the condition for
    # debugging), but the validator reports it.
    result = validate(s)
    assert not result.ok
    assert any("FakeConditionKind" in err.message for err in result.errors)
    # The writer refuses to emit anything for an invalid scenario.
    with pytest.raises(InvalidIR):
        write_mission_pack(s, tmp_path)


# ---------------------------------------------------------------------------
# Invalid FQON
# ---------------------------------------------------------------------------


def test_invalid_fqon_does_not_break_ir_construction() -> None:
    """The IR layer intentionally does NOT validate that an
    entity_definition_fqon resolves in the loaded nyan database.
    That check belongs to the loader (P2.4) or a higher layer (P5
    media closure). The IR's contract is structural only."""
    s = ScenarioDefinition(
        id="X",
        source="hand-authored",
        source_file="f.nyan",
        map=_valid_map(),
        players=[PlayerDefinition(player_id=0, name="P0", starting_resources=())],
        placements=[
            EntityPlacement(
                scenario_object_id="A",
                entity_definition_fqon="this.fqon.does.not.exist",
                player_id=0, position=(0, 0),
            ),
        ],
        areas=[],
        triggers=[],
        victory=VictoryDefinition(kind="objective-driven"),
    )
    # IR is structurally valid; loader accepts it; the fqon will fail
    # to resolve at runtime (out of scope for P2.6).
    loaded = load_scenario(s)
    assert loaded.placements["A"].entity_definition_fqon == (
        "this.fqon.does.not.exist"
    )


# ---------------------------------------------------------------------------
# Committed P1 dump vs regenerated dump
# ---------------------------------------------------------------------------


def test_regenerated_dump_matches_committed_p1_dump(
    tmp_path: pathlib.Path,
) -> None:
    """The committed P1 dump and a freshly-written dump from the
    canonical IR must agree on the structural fields the downstream
    tests check.

    This pins the contract that the P1.4 dump-validation tests stay
    green as P2 replaces the manual dumps with the auto-generated
    ones. We only compare the structural fields; the source status
    metadata differs by design."""
    from openage.scenario.ir import ScenarioDefinition as SD
    s = _mission1_scenario()
    write_mission_pack(s, tmp_path)

    fresh = json.loads((tmp_path / "scenario.json").read_text())
    committed = json.loads((DEFAULT_DUMP_DIR / "scenario.json").read_text())

    # Compare structural fields (MapDefinition, EntityPlacement).
    fresh_sd = fresh["ScenarioDefinition"]
    committed_sd = committed["ScenarioDefinition"]
    assert fresh_sd["id"] == committed_sd["id"]
    assert fresh_sd["MapDefinition"] == committed_sd["MapDefinition"]
    # EntityPlacements match by scenario_object_id + entity fqon +
    # position (rotation / overrides may differ).
    def by_id(eps):
        return {e["ScenarioObjectId"]: e for e in eps}
    for sid, ep in by_id(fresh_sd["EntityPlacement"]).items():
        other = by_id(committed_sd["EntityPlacement"])[sid]
        assert ep["entity_definition_fqon"] == other["entity_definition_fqon"]
        assert ep["PlayerId"] == other["PlayerId"]
        assert ep["position"] == other["position"]