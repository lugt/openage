"""
Normalized Scenario IR (P2.1).

This module defines the source-independent Intermediate Representation
that the Scenario Loader fills in (P2.4) and that the rest of the
Mission 1 workstream consumes (P2.2 validation, P2.3 writer, P4 trigger
runtime, etc.).

The shape is pinned by spec §6.1:

    struct ScenarioDefinition {
        ScenarioId id;
        MapDefinition map;
        std::vector<PlayerDefinition> players;
        std::vector<EntityPlacement> placements;
        std::vector<AreaDefinition> areas;
        std::vector<TriggerDefinition> triggers;
        VictoryDefinition victory;
    };

    struct MapDefinition {
        uint32_t width;
        uint32_t height;
        std::vector<TerrainId> terrain_ids;
        std::vector<int8_t> elevation;
    };

    struct EntityPlacement {
        ScenarioObjectId scenario_object_id;
        EntityDefinitionId entity_definition;
        PlayerId owner;
        Coordinate position;
        float rotation;
        AttributeOverrides overrides;
    };

    struct TriggerDefinition {
        TriggerId id;
        bool initially_enabled;
        bool looping;
        time::time_t delay;
        std::vector<ConditionDefinition> conditions;
        std::vector<EffectDefinition> effects;
    };

Names follow repository conventions (Python dataclasses), but the
semantics are equivalent to the C++ structs above. All invariants
required by the spec are enforced at construction time via
``InvalidIR`` (a subclass of ``ValueError``).
"""
from __future__ import annotations

import dataclasses
import math
import json
import pathlib
import typing as T


class InvalidIR(ValueError):
    """Raised when a Scenario IR value violates spec §6.1 invariants.

    Inherits from ``ValueError`` so existing ``except ValueError`` blocks
    still work, but the dedicated name lets callers distinguish IR-layer
    failures from generic value errors.
    """


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def _ensure_positive_int(name: str, value: int) -> None:
    if not isinstance(value, int) or isinstance(value, bool) or value <= 0:
        raise InvalidIR(f"{name} must be a positive int, got {value!r}")


def _ensure_nonempty_str(name: str, value: str) -> None:
    if not isinstance(value, str) or not value:
        raise InvalidIR(f"{name} must be a non-empty str, got {value!r}")


def _ensure_non_negative_pair(name: str, value: tuple[int, int]) -> None:
    if (not isinstance(value, tuple)
            or len(value) != 2
            or not all(isinstance(v, int) and not isinstance(v, bool) and v >= 0
                       for v in value)):
        raise InvalidIR(
            f"{name} must be a tuple of two non-negative ints, got {value!r}"
        )


def _ensure_dense(name: str, dense: list, width: int, height: int) -> None:
    expected = width * height
    if len(dense) != expected:
        raise InvalidIR(
            f"{name} length {len(dense)} does not match "
            f"width*height={expected} (got {width}x{height})"
        )


# ---------------------------------------------------------------------------
# Map
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class MapDefinition:
    """Dense-array map. See spec §6.1."""

    width: int
    height: int
    terrain_ids: list[str]
    elevation: list[int]

    def __post_init__(self) -> None:
        _ensure_positive_int("width", self.width)
        _ensure_positive_int("height", self.height)
        _ensure_dense("terrain_ids", self.terrain_ids, self.width, self.height)
        _ensure_dense("elevation", self.elevation, self.width, self.height)
        if any(type(height) is not int or not 0 <= height <= 255
               for height in self.elevation):
            raise InvalidIR("elevation values must be integers from 0 to 255")

    def to_dict(self) -> dict[str, T.Any]:
        return {
            "width": self.width,
            "height": self.height,
            "terrain_ids": list(self.terrain_ids),
            "elevation": list(self.elevation),
        }

    @classmethod
    def from_dict(cls, d: dict[str, T.Any]) -> "MapDefinition":
        return cls(
            width=int(d["width"]),
            height=int(d["height"]),
            terrain_ids=list(d["terrain_ids"]),
            elevation=list(d["elevation"]),
        )


    @classmethod
    def from_dump(cls, data: dict[str, T.Any]) -> "MapDefinition":
        """Reassemble dense row-major tile data from a mission-pack map."""
        width, height = data["width"], data["height"]
        _ensure_positive_int("width", width)
        _ensure_positive_int("height", height)
        tiles = [None] * (width * height)
        elevations = [None] * (width * height)
        terrain_ids, heights = data["terrain_ids_unique"], data["elevation_unique"]
        if not data.get("chunks"):
            if len(terrain_ids) != 1 or len(heights) != 1:
                raise InvalidIR("a legacy map without chunks must have uniform terrain and elevation")
            return cls(width, height, terrain_ids * data["terrain_ids_count"],
                       heights * data["elevation_count"])

        for chunk in data["chunks"]:
            x, y = chunk["offset"]["x"], chunk["offset"]["y"]
            w, h = chunk["size"]["x"], chunk["size"]["y"]
            if not (w > 0 and h > 0 and 0 <= x <= width - w and 0 <= y <= height - h):
                raise InvalidIR(f"terrain chunk at ({x}, {y}) is out of bounds")
            chunk_terrain = chunk["tile_fqons"]
            chunk_heights = chunk.get("tile_elevations")
            if chunk_heights is None:
                if len(heights) != 1:
                    raise InvalidIR("a mixed-elevation map needs tile_elevations in every chunk")
                chunk_heights = heights * (w * h)
            _ensure_dense("tile_fqons", chunk_terrain, w, h)
            _ensure_dense("tile_elevations", chunk_heights, w, h)
            for row in range(h):
                start = (y + row) * width + x
                if any(tile is not None for tile in tiles[start:start + w]):
                    raise InvalidIR(f"overlapping terrain chunk at ({x}, {y})")
                tiles[start:start + w] = chunk_terrain[row * w:(row + 1) * w]
                elevations[start:start + w] = chunk_heights[row * w:(row + 1) * w]
        if None in tiles:
            raise InvalidIR("terrain chunks do not cover the map")
        return cls(width, height, tiles, elevations)


# ---------------------------------------------------------------------------
# Player
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class PlayerDefinition:
    player_id: int
    name: str
    starting_resources: tuple[T.Any, ...] = ()

    def __post_init__(self) -> None:
        if not isinstance(self.player_id, int) or isinstance(self.player_id, bool) \
                or self.player_id < 0:
            raise InvalidIR(
                f"player_id must be a non-negative int, got {self.player_id!r}"
            )
        _ensure_nonempty_str("name", self.name)
        self.starting_resources  # attribute access for type-checkers

    def to_dict(self) -> dict[str, T.Any]:
        return {
            "player_id": self.player_id,
            "name": self.name,
            "starting_resources": list(self.starting_resources),
        }

    @classmethod
    def from_dict(cls, d: dict[str, T.Any]) -> "PlayerDefinition":
        # Accept both the round-trip format produced by :meth:`to_dict`
        # (``{"player_id": 0}``) and the P1 hand-written dump format
        # (``{"PlayerDefinition.player_id": 0}``).
        pid = d.get("player_id", d.get("PlayerDefinition.player_id"))
        name = d.get("name", d.get("PlayerDefinition.name"))
        return cls(
            player_id=int(pid),
            name=str(name),
            starting_resources=tuple(d.get("starting_resources", ())),
        )


# ---------------------------------------------------------------------------
# Placement
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class EntityPlacement:
    scenario_object_id: str
    entity_definition_fqon: str
    player_id: int
    position: tuple[int | float, int | float]
    rotation: float = 0.0
    overrides: dict[str, T.Any] = dataclasses.field(default_factory=dict)

    def __post_init__(self) -> None:
        _ensure_nonempty_str("scenario_object_id", self.scenario_object_id)
        _ensure_nonempty_str("entity_definition_fqon", self.entity_definition_fqon)
        if not isinstance(self.player_id, int) or isinstance(self.player_id, bool) \
                or self.player_id < 0:
            raise InvalidIR(
                f"player_id must be a non-negative int, got {self.player_id!r}"
            )
        if len(self.position) != 2 or any(
                isinstance(value, bool) or not isinstance(value, (int, float))
                or not math.isfinite(value) or value < 0 for value in self.position):
            raise InvalidIR(f"position must contain two finite non-negative coordinates, got {self.position!r}")
        if not isinstance(self.rotation, (int, float)) or isinstance(self.rotation, bool):
            raise InvalidIR(f"rotation must be a number, got {self.rotation!r}")

    def to_dict(self) -> dict[str, T.Any]:
        return {
            "ScenarioObjectId": self.scenario_object_id,
            "entity_definition_fqon": self.entity_definition_fqon,
            "PlayerId": self.player_id,
            "position": list(self.position),
            "rotation": float(self.rotation),
            "AttributeOverrides": dict(self.overrides),
        }

    @classmethod
    def from_dict(cls, d: dict[str, T.Any]) -> "EntityPlacement":
        pos = d["position"]
        # Accept either tuple/list (``[x, y]``) or mapping (``{"x": x, "y": y}``)
        # so the same parser consumes both the round-trip format produced by
        # :meth:`to_dict` and the P1 hand-written dump format.
        if isinstance(pos, dict):
            x, y = pos["x"], pos["y"]
        else:
            x, y = pos[0], pos[1]
        return cls(
            scenario_object_id=str(d["ScenarioObjectId"]),
            entity_definition_fqon=str(d["entity_definition_fqon"]),
            player_id=int(d["PlayerId"]),
            position=(x, y),
            rotation=float(d.get("rotation", 0.0)),
            overrides=dict(d.get("AttributeOverrides", {})),
        )


# ---------------------------------------------------------------------------
# Area
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class AreaDefinition:
    area_id: str
    kind: str
    geometry: dict[str, T.Any] = dataclasses.field(default_factory=dict)

    def __post_init__(self) -> None:
        _ensure_nonempty_str("area_id", self.area_id)
        _ensure_nonempty_str("kind", self.kind)

    def to_dict(self) -> dict[str, T.Any]:
        return {
            "area_id": self.area_id,
            "kind": self.kind,
            "geometry": dict(self.geometry),
        }

    @classmethod
    def from_dict(cls, d: dict[str, T.Any]) -> "AreaDefinition":
        return cls(
            area_id=str(d["area_id"]),
            kind=str(d["kind"]),
            geometry=dict(d.get("geometry", {})),
        )


# ---------------------------------------------------------------------------
# Trigger
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class TriggerCondition:
    """A single condition inside a trigger.

    Spec §6.1 / plan §7 list candidate condition kinds. The validator
    (P2.2) decides which kinds are supported in a given Mission 1 build;
    a condition with an unsupported kind is reported, not silently
    accepted.
    """

    kind: str
    target_scenario_object_id: str | None = None
    target_area_id: str | None = None
    target_trigger_id: str | None = None
    target_player_id: int | None = None
    args: dict[str, T.Any] = dataclasses.field(default_factory=dict)

    def __post_init__(self) -> None:
        _ensure_nonempty_str("kind", self.kind)


@dataclasses.dataclass(frozen=True)
class TriggerEffect:
    """A single effect inside a trigger.

    Spec §6.1 / plan §7 list candidate effect kinds.
    """

    kind: str
    target_scenario_object_id: str | None = None
    target_area_id: str | None = None
    target_trigger_id: str | None = None
    target_player_id: int | None = None
    args: dict[str, T.Any] = dataclasses.field(default_factory=dict)

    def __post_init__(self) -> None:
        _ensure_nonempty_str("kind", self.kind)


@dataclasses.dataclass(frozen=True)
class TriggerDefinition:
    trigger_id: str
    initially_enabled: bool
    looping: bool
    delay: float
    conditions: tuple[T.Any, ...] = ()
    effects: tuple[T.Any, ...] = ()

    def __post_init__(self) -> None:
        _ensure_nonempty_str("trigger_id", self.trigger_id)
        if not isinstance(self.initially_enabled, bool):
            raise InvalidIR(
                f"initially_enabled must be bool, got {self.initially_enabled!r}"
            )
        if not isinstance(self.looping, bool):
            raise InvalidIR(f"looping must be bool, got {self.looping!r}")
        if not isinstance(self.delay, (int, float)) or isinstance(self.delay, bool) \
                or self.delay < 0:
            raise InvalidIR(f"delay must be a non-negative number, got {self.delay!r}")

    def to_dict(self) -> dict[str, T.Any]:
        return {
            "trigger_id": self.trigger_id,
            "initially_enabled": bool(self.initially_enabled),
            "looping": bool(self.looping),
            "delay": float(self.delay),
            "conditions": list(self.conditions),
            "effects": list(self.effects),
        }

    @classmethod
    def from_dict(cls, d: dict[str, T.Any]) -> "TriggerDefinition":
        return cls(
            trigger_id=str(d["trigger_id"]),
            initially_enabled=bool(d["initially_enabled"]),
            looping=bool(d["looping"]),
            delay=float(d["delay"]),
            conditions=tuple(d.get("conditions", ())),
            effects=tuple(d.get("effects", ())),
        )


# ---------------------------------------------------------------------------
# Victory
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class VictoryDefinition:
    kind: str
    required_removals: tuple[str, ...] = ()

    def __post_init__(self) -> None:
        _ensure_nonempty_str("kind", self.kind)

    def to_dict(self) -> dict[str, T.Any]:
        return {
            "kind": self.kind,
            "required_removals": list(self.required_removals),
        }

    @classmethod
    def from_dict(cls, d: dict[str, T.Any]) -> "VictoryDefinition":
        return cls(
            kind=str(d["kind"]),
            required_removals=tuple(d.get("required_removals", ())),
        )


# ---------------------------------------------------------------------------
# Scenario
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class ScenarioDefinition:
    """Normalized scenario. See spec §6.1.

    ``produced_at_commit`` is a 40-char git SHA (plus optional branch
    suffix). It is used for provenance and golden tests; the writer
    ignores it when comparing two scenarios for equality.
    """

    SCHEMA_VERSION: T.ClassVar[str] = "p2-ir-v1"

    id: str
    source: str
    source_file: str
    map: MapDefinition
    players: list[PlayerDefinition]
    placements: list[EntityPlacement]
    areas: list[AreaDefinition]
    triggers: list[TriggerDefinition]
    victory: VictoryDefinition
    schema_version: str = dataclasses.field(default=SCHEMA_VERSION)
    produced_at_commit: str = ""
    diplomacy: tuple[tuple[int, int, str], ...] = ()
    fog_of_war: bool = False
    share_allied_vision: bool = False
    original_scenario: dict[str, T.Any] = dataclasses.field(default_factory=dict)

    def __post_init__(self) -> None:
        _ensure_nonempty_str("id", self.id)
        _ensure_nonempty_str("source", self.source)
        if not isinstance(self.source_file, str):
            raise InvalidIR(
                f"source_file must be a str, got {self.source_file!r}"
            )
        if self.schema_version != self.SCHEMA_VERSION:
            raise InvalidIR(
                f"schema_version {self.schema_version!r} does not match "
                f"required {self.SCHEMA_VERSION!r}"
            )
        if not isinstance(self.produced_at_commit, str):
            raise InvalidIR(
                f"produced_at_commit must be a str, got {self.produced_at_commit!r}"
            )

        # uniqueness invariants
        player_ids = [p.player_id for p in self.players]
        if len(set(player_ids)) != len(player_ids):
            dupes = sorted({pid for pid in player_ids if player_ids.count(pid) > 1})
            raise InvalidIR(f"duplicate player_id(s): {dupes}")

        placement_ids = [p.scenario_object_id for p in self.placements]
        if len(set(placement_ids)) != len(placement_ids):
            dupes = sorted(
                {sid for sid in placement_ids if placement_ids.count(sid) > 1}
            )
            raise InvalidIR(f"duplicate scenario_object_id(s): {dupes}")

        valid_player_ids = set(player_ids)
        pairs = set()
        for source, target, relation in self.diplomacy:
            if (source == target or source not in valid_player_ids or target not in valid_player_ids
                    or (source, target) in pairs or relation not in {"ally", "neutral", "enemy"}):
                raise InvalidIR(f"Invalid diplomacy relation: {(source, target, relation)!r}")
            pairs.add((source, target))
        for p in self.placements:
            if p.player_id not in valid_player_ids:
                raise InvalidIR(
                    f"placement {p.scenario_object_id!r} has owner player_id="
                    f"{p.player_id}, but no such player is defined "
                    f"(valid: {sorted(valid_player_ids)})"
                )

        trigger_ids = [t.trigger_id for t in self.triggers]
        if len(set(trigger_ids)) != len(trigger_ids):
            dupes = sorted({tid for tid in trigger_ids if trigger_ids.count(tid) > 1})
            raise InvalidIR(f"duplicate trigger_id(s): {dupes}")

    def to_dict(self) -> dict[str, T.Any]:
        return {
            "schema_version": self.schema_version,
            "produced_at_commit": self.produced_at_commit,
            "id": self.id,
            "source": self.source,
            "source_file": self.source_file,
            "MapDefinition": self.map.to_dict(),
            "players": [p.to_dict() for p in self.players],
            "EntityPlacement": [p.to_dict() for p in self.placements],
            "areas": [a.to_dict() for a in self.areas],
            "triggers": [t.to_dict() for t in self.triggers],
            "victory": self.victory.to_dict(),
            "diplomacy": [list(relation) for relation in self.diplomacy],
            "fog_of_war": self.fog_of_war,
            "share_allied_vision": self.share_allied_vision,
            "original_scenario": self.original_scenario,
        }

    @classmethod
    def from_dict(cls, d: dict[str, T.Any]) -> "ScenarioDefinition":
        return cls(
            id=str(d["id"]),
            source=str(d["source"]),
            source_file=str(d["source_file"]),
            map=MapDefinition.from_dict(d["MapDefinition"]),
            players=[PlayerDefinition.from_dict(p) for p in d.get("players", [])],
            placements=[
                EntityPlacement.from_dict(p) for p in d.get("EntityPlacement", [])
            ],
            areas=[AreaDefinition.from_dict(a) for a in d.get("areas", [])],
            triggers=[TriggerDefinition.from_dict(t) for t in d.get("triggers", [])],
            victory=VictoryDefinition.from_dict(d["victory"]),
            schema_version=str(d.get("schema_version", cls.SCHEMA_VERSION)),
            produced_at_commit=str(d.get("produced_at_commit", "")),
            diplomacy=tuple(tuple(relation) for relation in d.get("diplomacy", [])),
            fog_of_war=d.get("fog_of_war", False),
            share_allied_vision=d.get("share_allied_vision", False),
            original_scenario=dict(d.get("original_scenario", {})),
        )

    @classmethod
    def from_dump(cls, dump_dir: pathlib.Path) -> "ScenarioDefinition":
        """Consume the P1 feasibility dump artifacts (scenario.json +
        object_table.csv). The P2 IR is a strict superset of the P1
        dump format, so this is the bridge between the hand-written
        golden and the typed runtime API.
        """
        from openage.scenario.dumps import load_scenario_dump

        dump = load_scenario_dump(dump_dir)
        sd = dump["scenario"]["ScenarioDefinition"]

        placements = [EntityPlacement.from_dict(p) for p in sd["EntityPlacement"]]
        players = [
            PlayerDefinition.from_dict(p) for p in sd.get("players", [])
        ]
        areas = [
            AreaDefinition.from_dict(a) for a in sd.get("AreaDefinition", [])
        ]
        triggers = [
            TriggerDefinition.from_dict(t) for t in sd.get("TriggerDefinition", [])
        ]
        victory = VictoryDefinition.from_dict(sd["VictoryDefinition"])

        map_def = MapDefinition.from_dump(sd["MapDefinition"])

        return cls(
            id=str(sd.get("id", "ww_mission1_reconstruction")),
            source=str(sd.get("source", "hand-authored")),
            source_file=str(sd.get("source_file", "")),
            map=map_def,
            players=players,
            placements=placements,
            areas=areas,
            triggers=triggers,
            victory=victory,
            diplomacy=tuple(tuple(relation) for relation in sd.get("Diplomacy", [])),
            fog_of_war=sd.get("fog_of_war", False),
            share_allied_vision=sd.get("share_allied_vision", False),
            original_scenario=dict(sd.get("original_scenario", {})),
            produced_at_commit=str(
                dump["scenario"].get("produced_at_commit", "")
            ),
        )
