# Copyright 2026 the openage authors. See copying.md for legal info.
"""
P2.4 tests for the Scenario Loader.

The loader takes a :class:`ScenarioDefinition` and produces a
:class:`LoadedScenario` whose contract follows spec §6.3:

    1. terrain and chunks
    2. players
    3. diplomacy (empty for the MVP reconstruction)
    4. placed entities
    5. ScenarioObjectId -> GameEntityId mapping
    6. areas and dependency watchers (empty in the reconstruction)
    7. trigger instances
    8. initial objective and timer events

Properties pinned by these tests:

  - The loader fills every step in the documented order; a hook
    (``LoadStepCallback``) records the order so the test can assert
    that order == spec.
  - The loader exposes ``scenario_object_to_entity`` mapping
    (``dict[str, int]``). The ints are the GameEntityIds the runtime
    will use; the test pins that they are stable + unique.
  - EntityPlacement -> GameEntityId is one-to-one with the placement
    order, NOT entity creation order (so triggers can look up by
    ScenarioObjectId without depending on order-of-creation races).
  - The loader REFUSES to load an invalid IR; the same InvalidIR
    bubbles up before any side effect (no partial state).
  - The loader runs the validator first and surfaces its issues as
    ``LoadError`` if any ERROR is present.
  - LoadedScenario exposes ``placements`` (id -> entity), ``players``
    (id -> player), ``triggers`` (id -> TriggerInstance), and the
    load-step trace.
"""
from __future__ import annotations

import pytest

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
from openage.scenario.loader import (
    LoadedScenario,
    LoadError,
    LoadStep,
    load_scenario,
)


def _valid_map() -> MapDefinition:
    return MapDefinition(
        width=4, height=4,
        terrain_ids=["grass"] * 16,
        elevation=[0] * 16,
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
                position=(1, 1),
            ),
            EntityPlacement(
                scenario_object_id="PlayerTownCenter",
                entity_definition_fqon="de2_base.data.game_entity.generic.town_center.town_center.TownCenter",
                player_id=0,
                position=(1, 2),
            ),
            EntityPlacement(
                scenario_object_id="EnemyVillager",
                entity_definition_fqon="de2_base.data.game_entity.generic.villager.villager.Villager",
                player_id=1,
                position=(3, 3),
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
                effects=(TriggerEffect(kind="DeclareVictory"),),
            ),
        ],
        victory=VictoryDefinition(
            kind="objective-driven",
            required_removals=("EnemyVillager",),
        ),
    )


# ---------------------------------------------------------------------------
# Order / hooks
# ---------------------------------------------------------------------------


def test_loader_runs_steps_in_spec_order() -> None:
    s = _scenario_with_triggers()
    loaded = load_scenario(s)
    # spec §6.3 order:
    expected = (
        LoadStep.TERRAIN,
        LoadStep.PLAYERS,
        LoadStep.DIPLOMACY,
        LoadStep.PLACEMENTS,
        LoadStep.SCENARIO_OBJECT_MAPPING,
        LoadStep.AREAS,
        LoadStep.TRIGGERS,
        LoadStep.INITIAL_EVENTS,
    )
    assert loaded.step_trace == expected, (
        f"loader step order drifted from spec §6.3: {loaded.step_trace}"
    )


def test_loader_runs_each_step_at_most_once() -> None:
    s = _scenario_with_triggers()
    loaded = load_scenario(s)
    assert len(loaded.step_trace) == len(set(loaded.step_trace))


# ---------------------------------------------------------------------------
# Mapping
# ---------------------------------------------------------------------------


def test_loader_exposes_scenario_object_to_entity_mapping() -> None:
    s = _scenario_with_triggers()
    loaded = load_scenario(s)
    mapping = loaded.scenario_object_to_entity
    assert set(mapping.keys()) == {
        "PlayerVillager", "PlayerTownCenter", "EnemyVillager",
    }
    assert all(isinstance(v, int) for v in mapping.values())


def test_loader_entity_ids_are_unique_and_stable_across_calls() -> None:
    s = _scenario_with_triggers()
    loaded_a = load_scenario(s)
    loaded_b = load_scenario(s)
    # Same ScenarioObjectId -> same GameEntityId across two loads of
    # the same input. The ints must be unique within each load.
    assert len(set(loaded_a.scenario_object_to_entity.values())) == 3
    assert loaded_a.scenario_object_to_entity == loaded_b.scenario_object_to_entity


def test_loader_mapping_does_not_match_entity_creation_order() -> None:
    """spec §6.3: 'The loader must not rely on entity creation order
    as a substitute for scenario object identity.' The mapping uses
    the placement list order, NOT the entity id order."""
    s = _scenario_with_triggers()
    loaded = load_scenario(s)
    # Our scenario's placement list order: PlayerVillager, PlayerTownCenter,
    # EnemyVillager. The mapping should assign the lowest id to
    # PlayerVillager, then PlayerTownCenter, then EnemyVillager.
    mapping = loaded.scenario_object_to_entity
    ids = [mapping["PlayerVillager"], mapping["PlayerTownCenter"],
           mapping["EnemyVillager"]]
    assert ids == sorted(ids), (
        f"mapping should be in placement-list order, got {ids}"
    )


# ---------------------------------------------------------------------------
# Placements, players, triggers, areas
# ---------------------------------------------------------------------------


def test_loader_records_placements_and_players() -> None:
    s = _scenario_with_triggers()
    loaded = load_scenario(s)
    assert {p.player_id for p in loaded.players.values()} == {0, 1}
    assert {p.scenario_object_id for p in loaded.placements.values()} == {
        "PlayerVillager", "PlayerTownCenter", "EnemyVillager",
    }


def test_loader_creates_one_trigger_instance_per_definition() -> None:
    s = _scenario_with_triggers()
    loaded = load_scenario(s)
    assert "T0" in loaded.triggers
    inst = loaded.triggers["T0"]
    assert inst.initially_enabled is True
    assert inst.looping is False
    assert len(inst.conditions) == 1
    assert len(inst.effects) == 1


def test_loader_records_empty_areas_set() -> None:
    s = _scenario_with_triggers()
    loaded = load_scenario(s)
    assert loaded.areas == {}


# ---------------------------------------------------------------------------
# Diplomacy
# ---------------------------------------------------------------------------


def test_loader_records_diplomacy_as_empty_for_mvp_reconstruction() -> None:
    """The Mission 1 reconstruction has no enemy players in the
    diplomacy sense; the loader must record an empty matrix and not
    silently invent allies / enemies."""
    s = _scenario_with_triggers()
    loaded = load_scenario(s)
    assert loaded.diplomacy == {}


# ---------------------------------------------------------------------------
# Initial events
# ---------------------------------------------------------------------------


def test_loader_records_initial_objective_event() -> None:
    s = _scenario_with_triggers()
    loaded = load_scenario(s)
    # At least one initial event: the objective "remove EnemyVillager".
    assert any(ev.kind == "SetObjective" for ev in loaded.initial_events)


# ---------------------------------------------------------------------------
# Error handling
# ---------------------------------------------------------------------------


def test_loader_rejects_invalid_ir_before_any_side_effect() -> None:
    # Build an IR that passes construction but fails validation by
    # referencing an unknown trigger_id.
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
                    TriggerCondition(kind="TriggerFired",
                                     target_trigger_id="NoSuchTrigger"),
                ),
                effects=(),
            ),
        ],
        victory=VictoryDefinition(kind="objective-driven"),
    )
    with pytest.raises(LoadError, match="validation"):
        load_scenario(s)


def test_loader_surfaces_invalid_ir_construction_errors() -> None:
    """InvalidIR raised by IR construction must propagate."""
    with pytest.raises(InvalidIR):
        ScenarioDefinition(
            id="",
            source="hand-authored",
            source_file="f.nyan",
            map=_valid_map(),
            players=[PlayerDefinition(player_id=0, name="P0", starting_resources=())],
            placements=[],
            areas=[],
            triggers=[],
            victory=VictoryDefinition(kind="objective-driven"),
        )