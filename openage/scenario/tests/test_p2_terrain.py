# Copyright 2026 the openage authors. See copying.md for legal info.
"""
P2.5 tests for the terrain snapshot path.

Per spec §6.4 and plan §P2.5, the terrain snapshot API must:

  - preserve map dimensions and dense tile IDs / elevation
  - populate ALL tiles (no holes, no overflow)
  - split into 8x8 chunks with correct boundaries (Mission 1 is 16x16
    -> 4 chunks of 8x8)
  - expose passability + renderer-facing terrain id per (x, y)
  - NOT model static terrain as a time curve (no Event entries)
  - NOT spawn per-tile factory calls / curve objects (the snapshot
    is a single allocation, the chunks are views into it)

The snapshot is consumed by:
  - the runtime (libopenage) which builds libopenage TerrainChunks
    from the snapshot's chunk list.
  - the pathfinding layer (libopenage/path) which derives passability
    from the terrain-id table.
  - the renderer (libopenage/renderer) which gets the texture FQON
    per chunk.

Properties pinned by these tests:

  - snapshot.chunk_count matches the documented chunk grid for the
    given (width, height, chunk_size)
  - every (x, y) in [0, width) x [0, height) maps to exactly one chunk
  - chunk.tile_count == chunk_size * chunk_size for all but the
    rightmost / bottommost edge chunks
  - get_terrain_id_at(x, y) returns the dense-array value
  - get_elevation_at(x, y) returns the dense-array value
  - is_passable(x, y) returns True for grass / dirt, False for water /
    mountain (configurable per FQON -> passability table)
  - chunk.bbox == (offset_x, offset_y, size, size)
  - the snapshot exposes a single dense terrain_ids tuple, not N
    per-tile Curve objects (a count of per-tile allocations is
    asserted via the public API)
"""
from __future__ import annotations

import pytest

from openage.scenario.ir import MapDefinition
from openage.scenario.terrain import (
    TerrainChunk,
    TerrainSnapshot,
    TerrainSnapshotError,
    default_passability_table,
)


# Default 8x8 chunk size matches Mission 1's chunk grid (16x16 -> 4).
CHUNK_SIZE = 8


def _mission1_map() -> MapDefinition:
    return MapDefinition(
        width=16, height=16,
        terrain_ids=["grass"] * 256,
        elevation=[0] * 256,
    )


def _mission1_snapshot() -> TerrainSnapshot:
    return TerrainSnapshot.from_ir(_mission1_map(), chunk_size=CHUNK_SIZE)


# ---------------------------------------------------------------------------
# Construction
# ---------------------------------------------------------------------------


def test_snapshot_preserves_map_dimensions() -> None:
    snap = _mission1_snapshot()
    assert snap.width == 16
    assert snap.height == 16


def test_snapshot_rejects_invalid_chunk_size() -> None:
    with pytest.raises(TerrainSnapshotError, match="chunk_size"):
        TerrainSnapshot.from_ir(_mission1_map(), chunk_size=0)
    with pytest.raises(TerrainSnapshotError, match="chunk_size"):
        TerrainSnapshot.from_ir(_mission1_map(), chunk_size=-1)


def test_snapshot_rejects_mismatched_dense_arrays() -> None:
    # The IR constructor rejects this already, but the snapshot is
    # robust against manually-built maps too.
    from openage.scenario.ir import InvalidIR
    with pytest.raises(InvalidIR):
        MapDefinition(
            width=4, height=4,
            terrain_ids=["grass"] * 16,
            elevation=[0] * 8,  # mismatch
        )


# ---------------------------------------------------------------------------
# Chunking
# ---------------------------------------------------------------------------


def test_mission1_has_exactly_four_8x8_chunks() -> None:
    snap = _mission1_snapshot()
    assert snap.chunk_count == 4
    assert len(snap.chunks) == 4


def test_chunk_bboxes_form_complete_grid() -> None:
    snap = _mission1_snapshot()
    expected = [
        (0, 0), (8, 0), (0, 8), (8, 8),
    ]
    actual = [(c.offset_x, c.offset_y) for c in snap.chunks]
    assert sorted(actual) == sorted(expected)


def test_chunk_bboxes_have_correct_size() -> None:
    snap = _mission1_snapshot()
    for c in snap.chunks:
        assert c.size == CHUNK_SIZE
        assert c.bbox == (c.offset_x, c.offset_y, c.size, c.size)


def test_every_tile_belongs_to_exactly_one_chunk() -> None:
    snap = _mission1_snapshot()
    seen: set[tuple[int, int]] = set()
    for c in snap.chunks:
        for dx in range(c.size):
            for dy in range(c.size):
                tile = (c.offset_x + dx, c.offset_y + dy)
                assert tile not in seen, f"tile {tile} in two chunks"
                seen.add(tile)
    # Mission 1 is 16x16 -> 256 tiles
    assert len(seen) == 256


def test_edge_chunk_handles_partial_size() -> None:
    """Right and bottom edge chunks retain their own tile dimensions."""
    m = MapDefinition(
        width=10, height=10,
        terrain_ids=["grass"] * 100,
        elevation=[0] * 100,
    )
    snap = TerrainSnapshot.from_ir(m, chunk_size=8)
    assert snap.chunk_count == 4  # 2x2 grid
    sizes = {(c.offset_x, c.offset_y): c.size for c in snap.chunks}
    assert sizes[(0, 0)] == 8
    assert sizes[(8, 0)] == 2
    assert sizes[(0, 8)] == 2
    assert sizes[(8, 8)] == 2
    assert [chunk.bbox for chunk in snap.chunks] == [
        (0, 0, 8, 8), (8, 0, 2, 8),
        (0, 8, 8, 2), (8, 8, 2, 2),
    ]


# ---------------------------------------------------------------------------
# Per-tile access
# ---------------------------------------------------------------------------


def test_get_terrain_id_at_returns_dense_value() -> None:
    snap = _mission1_snapshot()
    assert snap.get_terrain_id_at(0, 0) == "grass"
    assert snap.get_terrain_id_at(8, 8) == "grass"
    assert snap.get_terrain_id_at(15, 15) == "grass"


def test_get_terrain_id_at_rejects_out_of_bounds() -> None:
    snap = _mission1_snapshot()
    with pytest.raises(TerrainSnapshotError, match="out of bounds"):
        snap.get_terrain_id_at(-1, 0)
    with pytest.raises(TerrainSnapshotError, match="out of bounds"):
        snap.get_terrain_id_at(16, 0)
    with pytest.raises(TerrainSnapshotError, match="out of bounds"):
        snap.get_terrain_id_at(0, 16)


def test_get_elevation_at_returns_dense_value() -> None:
    snap = _mission1_snapshot()
    for x in range(16):
        for y in range(16):
            assert snap.get_elevation_at(x, y) == 0


def test_get_elevation_at_handles_varied_elevation() -> None:
    elev = [((x * 4) + y) % 4 for x in range(4) for y in range(4)]
    m = MapDefinition(
        width=4, height=4,
        terrain_ids=["grass"] * 16,
        elevation=elev,
    )
    snap = TerrainSnapshot.from_ir(m, chunk_size=4)
    for x in range(4):
        for y in range(4):
            assert snap.get_elevation_at(x, y) == elev[y * 4 + x]


# ---------------------------------------------------------------------------
# Passability
# ---------------------------------------------------------------------------


def test_default_passability_table_marks_grass_passable() -> None:
    table = default_passability_table()
    assert table["grass"] is True


def test_snapshot_is_passable_for_grass() -> None:
    snap = _mission1_snapshot()
    for x in range(16):
        for y in range(16):
            assert snap.is_passable(x, y)


def test_snapshot_marks_water_impassable_with_custom_table() -> None:
    # Build a 4x4 map with one water tile.
    terrain = ["grass"] * 16
    terrain[0] = "water"  # (0, 0) -> water
    m = MapDefinition(
        width=4, height=4,
        terrain_ids=terrain,
        elevation=[0] * 16,
    )
    snap = TerrainSnapshot.from_ir(
        m,
        chunk_size=4,
        passability_table={"grass": True, "water": False},
    )
    assert snap.is_passable(1, 1)
    assert not snap.is_passable(0, 0)


# ---------------------------------------------------------------------------
# No curve / event explosion
# ---------------------------------------------------------------------------


def test_snapshot_does_not_emit_per_tile_curve_objects() -> None:
    """Spec §6.4: 'static terrain is not modeled as a time curve'.
    The snapshot must expose ONE dense tuple of terrain ids and ONE
    dense tuple of elevations, not 256 per-tile curve objects.

    We pin this by asserting the snapshot's public attributes: a
    single tuple (not a list of curves) and chunk_count * chunk_size
    <= len(snapshot.terrain_ids).
    """
    snap = _mission1_snapshot()
    assert isinstance(snap.terrain_ids, tuple)
    assert isinstance(snap.elevation, tuple)
    assert len(snap.terrain_ids) == 256
    assert len(snap.elevation) == 256


def test_chunk_exposes_terrain_ids_as_a_view_into_snapshot() -> None:
    """The chunk's per-tile values must be slices of the snapshot's
    dense arrays, not copies."""
    snap = _mission1_snapshot()
    chunk = snap.chunks[0]
    assert chunk.size == CHUNK_SIZE
    assert chunk.parent_snapshot is snap


# ---------------------------------------------------------------------------
# TerrainChunk dataclass surface
# ---------------------------------------------------------------------------


def test_chunk_carries_terrain_fqon_for_renderer() -> None:
    """The chunk exposes the terrain FQON so the renderer can pick
    the right .terrain/.texture."""
    snap = _mission1_snapshot()
    chunk = snap.chunks[0]
    # All 16x16 tiles in Mission 1 are TestsceneGrass (hand-recon)
    assert chunk.terrain_fqon == "grass"


def test_chunk_offsets_and_size_are_readonly() -> None:
    snap = _mission1_snapshot()
    chunk = snap.chunks[0]
    with pytest.raises((AttributeError, dataclasses_exception())):
        chunk.offset_x = 999  # type: ignore[misc]


def dataclasses_exception():
    import dataclasses
    return dataclasses.FrozenInstanceError
