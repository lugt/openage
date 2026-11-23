"""
Mission-pack writer (P2.3 + P2.7).

Serializes a :class:`ScenarioDefinition` into the four-file dump
format consumed by :func:`openage.scenario.dumps.load_scenario_dump`
and by the P2.4 / P4 workstream, plus a binary-friendly
``scenario.json`` that the C++ production loader (P2.7) reads.

Files written:

  - ``scenario.json``       - ScenarioDefinition per spec §6.1,
                              with the dense terrain / elevation
                              arrays stored compactly (count + the
                              unique-value list, since all 256 tiles
                              are identical in the reconstruction),
                              PLUS the per-chunk tile FQON list
                              (P2.7: native loader needs chunk
                              boundaries that the dense array does
                              not encode).
  - ``object_table.csv``    - one row per EntityPlacement with
                              stable column order.
  - ``trigger_graph.json``  - trigger / condition / effect
                              histograms + node / edge counts.
  - ``coverage.json``       - per-plan P4.1 "only implementation
                              list" plus supported-vs-unknown kind
                              reporting.

Determinism
-----------
The writer is byte-deterministic for a fixed input: no timestamps,
no random IDs, no OS-dependent ordering. The
``produced_at_commit`` field is copied verbatim from the input
ScenarioDefinition (it is metadata the writer does not invent).
The chunk layout uses a fixed 8x8 chunk grid (matches Mission 1
and the libopenage terrain_chunk API); edge chunks at the right /
bottom borders are NOT padded.

Validation gate
---------------
The writer calls :func:`openage.scenario.validation.validate` first.
If validation returns any ERRORs, the writer refuses to emit any
file (``InvalidIR`` is raised); the caller must fix the IR or accept
the validation result before retrying.
"""
from __future__ import annotations

import collections
import csv
import json
import math
import pathlib
import typing as T

from openage.scenario.ir import (
    InvalidIR,
    ScenarioDefinition,
    TriggerDefinition,
)
from openage.scenario.validation import (
    SUPPORTED_CONDITION_KINDS,
    SUPPORTED_EFFECT_KINDS,
    validate,
)


# ---------------------------------------------------------------------------
# Compact dense storage helpers
# ---------------------------------------------------------------------------


def _compact_dense(values: list[T.Any]) -> dict[str, T.Any]:
    """Compress a dense list into ``{count, unique}`` if all elements
    are equal; otherwise emit the full list. The dump loader knows
    how to expand both shapes."""
    if not values:
        return {"count": 0, "unique": []}
    first = values[0]
    if all(v == first for v in values):
        return {"count": len(values), "unique": [first]}
    return {"count": len(values), "unique": list(values)}


def _expand_dense(compact: dict[str, T.Any]) -> list[T.Any]:
    """Inverse of :func:`_compact_dense`. Used only by the round-trip
    test that verifies the writer's output is lossless."""
    count = int(compact["count"])
    unique = list(compact["unique"])
    if len(unique) == 1:
        return unique * count
    assert len(unique) == count, (
        f"compact storage inconsistent: count={count} unique={len(unique)}"
    )
    return unique


# ---------------------------------------------------------------------------
# Trigger histograms
# ---------------------------------------------------------------------------


def _trigger_histograms(
    triggers: list[TriggerDefinition],
) -> dict[str, int]:
    cond_hist: collections.Counter[str] = collections.Counter()
    eff_hist: collections.Counter[str] = collections.Counter()
    for trigger in triggers:
        for cond in trigger.conditions:
            cond_hist[cond.kind] += 1
        for eff in trigger.effects:
            eff_hist[eff.kind] += 1
    return {
        "condition_type_histogram": dict(sorted(cond_hist.items())),
        "effect_type_histogram": dict(sorted(eff_hist.items())),
    }


# ---------------------------------------------------------------------------
# Per-file serializers
# ---------------------------------------------------------------------------


def _scenario_dict(scenario: ScenarioDefinition) -> dict[str, T.Any]:
    # Expand the dense terrain / elevation arrays into the per-chunk
    # tile FQON lists. Chunks are 8x8 by default (matches Mission 1
    # and the libopenage terrain_chunk API). Edge chunks at the right /
    # bottom edges may be smaller than the configured chunk size; they
    # are NOT padded with bogus FQONs.
    chunk_size = 8
    chunks_x = math.ceil(scenario.map.width / chunk_size)
    chunks_y = math.ceil(scenario.map.height / chunk_size)
    elevations = sorted(set(scenario.map.elevation))
    chunk_records: list[dict[str, T.Any]] = []
    for cy in range(chunks_y):
        for cx in range(chunks_x):
            ox = cx * chunk_size
            oy = cy * chunk_size
            max_x = min(ox + chunk_size, scenario.map.width)
            max_y = min(oy + chunk_size, scenario.map.height)
            indices = [y * scenario.map.width + x
                       for y in range(oy, max_y) for x in range(ox, max_x)]
            chunk = {
                "offset": {"x": ox, "y": oy},
                "size": {"x": max_x - ox, "y": max_y - oy},
                "tile_fqons": [scenario.map.terrain_ids[i] for i in indices],
            }
            if len(elevations) > 1:
                chunk["tile_elevations"] = [scenario.map.elevation[i] for i in indices]
            chunk_records.append(chunk)

    return {
        "schema_version": scenario.schema_version,
        "produced_at_commit": scenario.produced_at_commit,
        "scenario_kind": "Original William Wallace scenario" if scenario.original_scenario else "William Wallace Mission 1 MVP reconstruction",
        "scenario_fqon": (
            "ww_mvp.campaign.william_wallace.mission_01.Mission"
        ),
        "player_setup_fqon": (
            "ww_mvp.campaign.william_wallace.mission_01.PlayerSetup"
        ),
        "source_status": "IMPORTED" if scenario.original_scenario else "BLOCKED",
        "source_blocker": (
            "see docs/mvp/reports/mission1-scenario-feasibility.md"
        ),
        "is_imported_original": bool(scenario.original_scenario),
        "chunk_size": chunk_size,
        "ScenarioDefinition": {
            "id": scenario.id,
            "original_scenario": scenario.original_scenario,
            "Diplomacy": [list(relation) for relation in scenario.diplomacy],
            "fog_of_war": scenario.fog_of_war,
            "share_allied_vision": scenario.share_allied_vision,
            "source": scenario.source,
            "source_file": scenario.source_file,
            "MapDefinition": {
                "width": scenario.map.width,
                "height": scenario.map.height,
                "terrain_ids_count": len(scenario.map.terrain_ids),
                "terrain_ids_unique": _compact_dense(
                    list(scenario.map.terrain_ids)
                )["unique"],
                "elevation_count": len(scenario.map.elevation),
                "elevation_unique": elevations,
                "chunks": chunk_records,
            },
            "players": [
                {
                    "PlayerDefinition.player_id": p.player_id,
                    "PlayerDefinition.name": p.name,
                    "PlayerDefinition.starting_resources": [
                        list(r) for r in p.starting_resources
                    ],
                }
                for p in scenario.players
            ],
            "EntityPlacement": [
                {
                    "ScenarioObjectId": p.scenario_object_id,
                    "entity_definition_fqon": p.entity_definition_fqon,
                    "PlayerId": p.player_id,
                    "position": {"x": p.position[0], "y": p.position[1]},
                    "rotation": p.rotation,
                    "AttributeOverrides": dict(p.overrides),
                }
                for p in scenario.placements
            ],
            "AreaDefinition": [
                {
                    "area_id": a.area_id,
                    "kind": a.kind,
                    "geometry": dict(a.geometry),
                }
                for a in scenario.areas
            ],
            "TriggerDefinition": [
                {
                    "trigger_id": t.trigger_id,
                    "initially_enabled": t.initially_enabled,
                    "looping": t.looping,
                    "delay": t.delay,
                    "conditions": [
                        {
                            "kind": c.kind,
                            "target_scenario_object_id": c.target_scenario_object_id,
                            "target_area_id": c.target_area_id,
                            "target_trigger_id": c.target_trigger_id,
                            "target_player_id": c.target_player_id,
                            "args": dict(c.args),
                        }
                        for c in t.conditions
                    ],
                    "effects": [
                        {
                            "kind": e.kind,
                            "target_scenario_object_id": e.target_scenario_object_id,
                            "target_area_id": e.target_area_id,
                            "target_trigger_id": e.target_trigger_id,
                            "target_player_id": e.target_player_id,
                            "args": dict(e.args),
                        }
                        for e in t.effects
                    ],
                }
                for t in scenario.triggers
            ],
            "VictoryDefinition": {
                "kind": scenario.victory.kind,
                "required_removals": list(scenario.victory.required_removals),
            },
        },
    }


def _trigger_graph_dict(scenario: ScenarioDefinition) -> dict[str, T.Any]:
    hist = _trigger_histograms(scenario.triggers)
    return {
        "schema_version": scenario.schema_version,
        "produced_at_commit": scenario.produced_at_commit,
        "scenario_kind": "Original William Wallace scenario" if scenario.original_scenario else "William Wallace Mission 1 MVP reconstruction",
        "trigger_count": len(scenario.triggers),
        "node_count": len(scenario.triggers),
        "edge_count": sum(len(t.effects) for t in scenario.triggers),
        "trigger_ids": [t.trigger_id for t in scenario.triggers],
        **hist,
        "unsupported_or_unknown_types": [],
    }


def _coverage_dict(scenario: ScenarioDefinition) -> dict[str, T.Any]:
    used_condition_kinds = {c.kind for t in scenario.triggers for c in t.conditions}
    used_effect_kinds = {e.kind for t in scenario.triggers for e in t.effects}
    return {
        "scenario_kind": "Original William Wallace scenario" if scenario.original_scenario else "William Wallace Mission 1 MVP reconstruction",
        "scenario_fqon": (
            "ww_mvp.campaign.william_wallace.mission_01.Mission"
        ),
        "player_setup_fqon": (
            "ww_mvp.campaign.william_wallace.mission_01.PlayerSetup"
        ),
        "source_status": "IMPORTED" if scenario.original_scenario else "BLOCKED",
        "source_blocker": (
            "see docs/mvp/reports/mission1-scenario-feasibility.md"
        ),
        "is_imported_original": bool(scenario.original_scenario),
        "produced_at_commit": scenario.produced_at_commit,
        "schema_version": scenario.schema_version,
        "trigger_count": len(scenario.triggers),
        "trigger_types_present": sorted({
            t.trigger_id for t in scenario.triggers
        }),
        "condition_types_present": sorted(used_condition_kinds),
        "effect_types_present": sorted(used_effect_kinds),
        "unsupported_condition_kinds": sorted(
            used_condition_kinds - SUPPORTED_CONDITION_KINDS
        ),
        "unsupported_effect_kinds": sorted(
            used_effect_kinds - SUPPORTED_EFFECT_KINDS
        ),
        "trigger_types_candidate_from_spec": sorted(SUPPORTED_CONDITION_KINDS),
        "unsupported_or_missing": [],
    }


def _write_object_table_csv(
    scenario: ScenarioDefinition, dest: pathlib.Path,
) -> None:
    fieldnames = [
        "scenario_object_id", "scenario_fqon", "player_id",
        "entity_fqon", "pos_x", "pos_y", "rotation", "source",
    ]
    with dest.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=fieldnames)
        writer.writeheader()
        for p in scenario.placements:
            writer.writerow({
                "scenario_object_id": p.scenario_object_id,
                "scenario_fqon": f"<scenario>.{p.scenario_object_id}",
                "player_id": p.player_id,
                "entity_fqon": p.entity_definition_fqon,
                "pos_x": p.position[0],
                "pos_y": p.position[1],
                "rotation": p.rotation,
                "source": scenario.source,
            })


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------


def write_mission_pack(
    scenario: ScenarioDefinition, dest_dir: pathlib.Path,
) -> None:
    """Write the four-file mission pack under ``dest_dir``.

    Refuses to write any file if the scenario fails validation (any
    ERROR-severity issue from :func:`validate`). In that case an
    :class:`InvalidIR` is raised with the issue list appended to the
    message.
    """
    result = validate(scenario)
    if not result.ok:
        msgs = "; ".join(str(e) for e in result.errors)
        raise InvalidIR(f"scenario fails validation: {msgs}")

    dest_dir.mkdir(parents=True, exist_ok=True)
    # Stable key order in the JSON output (sort_keys=True) keeps the
    # hash invariant even if the dict is later populated in a
    # different order.
    (dest_dir / "scenario.json").write_text(
        json.dumps(_scenario_dict(scenario), indent=2, sort_keys=True)
        + "\n",
        encoding="utf-8",
    )
    (dest_dir / "trigger_graph.json").write_text(
        json.dumps(_trigger_graph_dict(scenario), indent=2, sort_keys=True)
        + "\n",
        encoding="utf-8",
    )
    (dest_dir / "coverage.json").write_text(
        json.dumps(_coverage_dict(scenario), indent=2, sort_keys=True)
        + "\n",
        encoding="utf-8",
    )
    _write_object_table_csv(scenario, dest_dir / "object_table.csv")
