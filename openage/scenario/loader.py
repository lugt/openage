"""
Scenario Loader (P2.4).

Turns a validated :class:`ScenarioDefinition` into an in-memory
:class:`LoadedScenario` whose contract matches spec §6.3:

    1. terrain and chunks
    2. players
    3. diplomacy
    4. placed entities
    5. ScenarioObjectId -> GameEntityId mapping
    6. areas and dependency watchers
    7. trigger instances
    8. initial objective and timer events

The loader is pure-Python and stateless w.r.t. the nyan database:
the GameEntityIds it returns are stable identifiers allocated from
the placement-list order, not whatever order the C++ EntityFactory
happens to consume nyan in. Spec §6.3 requires this: "The loader
must not rely on entity creation order as a substitute for scenario
object identity."

Validation gate
---------------
The loader runs :func:`openage.scenario.validation.validate` first.
Any ERROR-severity issue raises :class:`LoadError` before any side
effect, mirroring the writer's gate.
"""
from __future__ import annotations

import dataclasses
import enum
import typing as T

from openage.scenario.ir import (
    EntityPlacement,
    InvalidIR,
    PlayerDefinition,
    ScenarioDefinition,
    TriggerCondition,
    TriggerDefinition,
    TriggerEffect,
)
from openage.scenario.validation import validate


class LoadError(RuntimeError):
    """Raised when a ScenarioDefinition cannot be loaded."""


class LoadStep(enum.Enum):
    """Documented load steps per spec §6.3.

    The order is part of the public contract; downstream tests,
    docs, and CI may rely on it.
    """

    TERRAIN = "terrain"
    PLAYERS = "players"
    DIPLOMACY = "diplomacy"
    PLACEMENTS = "placements"
    SCENARIO_OBJECT_MAPPING = "scenario_object_mapping"
    AREAS = "areas"
    TRIGGERS = "triggers"
    INITIAL_EVENTS = "initial_events"


# ---------------------------------------------------------------------------
# Loaded-scenario types
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class LoadedTerrain:
    width: int
    height: int
    # Terrain ids are stored as a dense array matching (width*height)
    # positions in row-major order. We keep the dense form here so the
    # runtime can build chunks without re-validating size.
    terrain_ids: tuple[str, ...]
    elevation: tuple[int, ...]


@dataclasses.dataclass(frozen=True)
class LoadedPlayer:
    player_id: int
    name: str


@dataclasses.dataclass(frozen=True)
class LoadedPlacement:
    """A placement as the runtime sees it.

    ``game_entity_id`` is the integer ID the C++ side will assign
    when the entity is actually constructed. It is computed by the
    loader from the placement-list order (NOT from entity creation
    order) so triggers can look up by ScenarioObjectId deterministically.
    """

    scenario_object_id: str
    game_entity_id: int
    entity_definition_fqon: str
    player_id: int
    position: tuple[int, int]
    rotation: float


@dataclasses.dataclass(frozen=True)
class LoadedTriggerInstance:
    """An instance of a TriggerDefinition with stable runtime state."""

    trigger_id: str
    initially_enabled: bool
    looping: bool
    delay: float
    conditions: tuple[TriggerCondition, ...]
    effects: tuple[TriggerEffect, ...]


@dataclasses.dataclass(frozen=True)
class LoadedInitialEvent:
    """Initial event scheduled at simulation start (objective + timer).

    The runtime side converts these into actual EventLoop events.
    """

    kind: str
    target_scenario_object_id: str | None = None
    args: dict[str, T.Any] = dataclasses.field(default_factory=dict)


@dataclasses.dataclass(frozen=True)
class LoadedScenario:
    step_trace: tuple[LoadStep, ...]
    terrain: LoadedTerrain
    players: dict[int, LoadedPlayer]
    diplomacy: dict[tuple[int, int], str]
    placements: dict[str, LoadedPlacement]
    scenario_object_to_entity: dict[str, int]
    areas: dict[str, T.Any]
    triggers: dict[str, LoadedTriggerInstance]
    initial_events: tuple[LoadedInitialEvent, ...]


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def load_scenario(scenario: ScenarioDefinition) -> LoadedScenario:
    """Load the scenario in the documented spec §6.3 order.

    Raises :class:`LoadError` if validation reports any ERROR.
    Raises :class:`InvalidIR` if the IR itself violates a constructor
    invariant.
    """
    # Validation gate (no side effects happen before this).
    result = validate(scenario)
    if not result.ok:
        msgs = "; ".join(str(e) for e in result.errors)
        raise LoadError(f"scenario fails validation: {msgs}")

    trace: list[LoadStep] = []

    # 1. Terrain and chunks.
    trace.append(LoadStep.TERRAIN)
    terrain = _load_terrain(scenario.map)

    # 2. Players.
    trace.append(LoadStep.PLAYERS)
    players = _load_players(scenario.players)

    # 3. Diplomacy. Empty for the MVP reconstruction.
    trace.append(LoadStep.DIPLOMACY)
    diplomacy = _load_diplomacy(scenario)

    # 4. Placed entities.
    trace.append(LoadStep.PLACEMENTS)
    placements = _load_placements(scenario.placements)

    # 5. ScenarioObjectId -> GameEntityId mapping.
    trace.append(LoadStep.SCENARIO_OBJECT_MAPPING)
    scenario_object_to_entity = {
        sid: p.game_entity_id for sid, p in placements.items()
    }

    # 6. Areas + dependency watchers. Empty for the MVP reconstruction.
    trace.append(LoadStep.AREAS)
    areas = _load_areas(scenario.areas)

    # 7. Trigger instances.
    trace.append(LoadStep.TRIGGERS)
    triggers = _load_triggers(scenario.triggers)

    # 8. Initial objective + timer events.
    trace.append(LoadStep.INITIAL_EVENTS)
    initial_events = _load_initial_events(scenario)

    return LoadedScenario(
        step_trace=tuple(trace),
        terrain=terrain,
        players=players,
        diplomacy=diplomacy,
        placements=placements,
        scenario_object_to_entity=scenario_object_to_entity,
        areas=areas,
        triggers=triggers,
        initial_events=initial_events,
    )


# ---------------------------------------------------------------------------
# Per-step loaders
# ---------------------------------------------------------------------------


def _load_terrain(map_def) -> LoadedTerrain:
    return LoadedTerrain(
        width=map_def.width,
        height=map_def.height,
        terrain_ids=tuple(map_def.terrain_ids),
        elevation=tuple(map_def.elevation),
    )


def _load_players(players: list[PlayerDefinition]) -> dict[int, LoadedPlayer]:
    out: dict[int, LoadedPlayer] = {}
    for p in players:
        if p.player_id in out:
            # IR construction already rejects duplicates, but be
            # defensive in case the loader is reused on a manually
            # constructed IR.
            raise LoadError(
                f"duplicate player_id during load: {p.player_id}"
            )
        out[p.player_id] = LoadedPlayer(player_id=p.player_id, name=p.name)
    return out


def _load_diplomacy(scenario: ScenarioDefinition) -> dict[tuple[int, int], str]:
    return {(source, target): relation for source, target, relation in scenario.diplomacy}


def _load_placements(
    placements: list[EntityPlacement],
) -> dict[str, LoadedPlacement]:
    out: dict[str, LoadedPlacement] = {}
    for index, p in enumerate(placements):
        if p.scenario_object_id in out:
            # Defensive: the IR constructor rejects this; raising
            # here makes the loader usable even if a caller bypasses
            # the IR check.
            raise LoadError(
                f"duplicate scenario_object_id during load: "
                f"{p.scenario_object_id}"
            )
        out[p.scenario_object_id] = LoadedPlacement(
            scenario_object_id=p.scenario_object_id,
            game_entity_id=index,  # placement-list order, not creation order
            entity_definition_fqon=p.entity_definition_fqon,
            player_id=p.player_id,
            position=p.position,
            rotation=p.rotation,
        )
    return out


def _load_areas(areas) -> dict[str, T.Any]:
    return {a.area_id: a for a in areas}


def _load_triggers(
    triggers: list[TriggerDefinition],
) -> dict[str, LoadedTriggerInstance]:
    out: dict[str, LoadedTriggerInstance] = {}
    for t in triggers:
        out[t.trigger_id] = LoadedTriggerInstance(
            trigger_id=t.trigger_id,
            initially_enabled=t.initially_enabled,
            looping=t.looping,
            delay=t.delay,
            conditions=tuple(t.conditions),
            effects=tuple(t.effects),
        )
    return out


def _load_initial_events(
    scenario: ScenarioDefinition,
) -> tuple[LoadedInitialEvent, ...]:
    """Translate the scenario's VictoryDefinition + the spec's
    'always-present initial objective' into runtime events.

    The MVP reconstruction has one initial event: SetObjective for
    the first required removal target. Future missions will add
    DisplayInstruction etc.; the loader extension point is here.
    """
    events: list[LoadedInitialEvent] = []
    for sid in scenario.victory.required_removals:
        events.append(LoadedInitialEvent(
            kind="SetObjective",
            target_scenario_object_id=sid,
            args={"objective_kind": scenario.victory.kind},
        ))
    return tuple(events)