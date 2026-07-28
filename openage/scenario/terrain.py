"""
Terrain snapshot path (P2.5).

Per spec §6.4 / plan §P2.5:

  - Map dimensions and dense tile IDs / elevation are preserved.
  - ALL tiles are populated (no holes, no overflow).
  - Terrain is split into ``chunk_size x chunk_size`` chunks with
    correct boundaries. Mission 1 (16x16) with the default
    ``chunk_size=8`` yields exactly 4 chunks.
  - The snapshot exposes passability + terrain id per (x, y).
  - Static terrain is NOT modeled as a time curve. The snapshot is a
    single allocation; chunks are views into it, not independent
    objects per tile.
  - Dynamic occupancy is the runtime's job (out of scope here).

The Python snapshot is a structural twin of the C++
``libopenage/gamestate/terrain_chunk.cpp`` API; the C++ side will
eventually consume the same dense arrays when it builds its own
chunks.
"""
from __future__ import annotations

import dataclasses
import math
import typing as T

from openage.scenario.ir import MapDefinition


class TerrainSnapshotError(ValueError):
    """Raised on terrain snapshot construction or query errors."""


# ---------------------------------------------------------------------------
# Defaults
# ---------------------------------------------------------------------------


def default_passability_table() -> dict[str, bool]:
    """Default terrain-id -> passability mapping for the MVP.

    Mission 1 only ships ``grass`` (TestsceneGrass). Future P5 work
    will replace this with real DE2 terrain ids.
    """
    return {
        "grass": True,
        "dirt": True,
        "sand": True,
        "snow": True,
        "forest": False,  # forest tiles block movement unless cleared
        "water": False,
        "mountain": False,
    }


# ---------------------------------------------------------------------------
# Chunk
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class TerrainChunk:
    """A ``chunk_size x chunk_size`` view into a :class:`TerrainSnapshot`.

    ``parent_snapshot`` is held by reference (not copy) so the chunk's
    per-tile queries are O(1) lookups into the snapshot's dense
    tuples. This is the spec §6.4 'batch/snapshot API' requirement:
    one allocation, many views.

    ``size`` is the *actual* tile count of the chunk (smaller than
    ``configured_size`` for edge chunks that do not fit a full grid).
    ``configured_size`` records the chunk dimension the snapshot was
    built with so chunk boundaries stay consistent.
    """

    offset_x: int
    offset_y: int
    size: int
    configured_size: int
    terrain_fqon: str
    parent_snapshot: "TerrainSnapshot"

    @property
    def bbox(self) -> tuple[int, int, int, int]:
        """(offset_x, offset_y, size, size) using the *actual* size."""
        return (self.offset_x, self.offset_y, self.size, self.size)


# ---------------------------------------------------------------------------
# Snapshot
# ---------------------------------------------------------------------------


@dataclasses.dataclass(frozen=True)
class TerrainSnapshot:
    """A single dense allocation of all terrain + elevation data.

    Per spec §6.4: 'static terrain is not modeled as a time curve'.
    The snapshot does NOT allocate per-tile curve / event objects;
    it stores two tuples of length width*height.
    """

    width: int
    height: int
    chunk_size: int
    terrain_ids: tuple[str, ...]
    elevation: tuple[int, ...]
    passability_table: dict[str, bool] = dataclasses.field(
        default_factory=default_passability_table
    )

    @property
    def chunks(self) -> tuple[TerrainChunk, ...]:
        """Lazily-computed chunk list. Each chunk is a view; mutating
        the snapshot (we are frozen) would invalidate them, but
        dataclass frozen=True prevents that."""
        chunks_x = math.ceil(self.width / self.chunk_size)
        chunks_y = math.ceil(self.height / self.chunk_size)
        out: list[TerrainChunk] = []
        for cy in range(chunks_y):
            for cx in range(chunks_x):
                ox = cx * self.chunk_size
                oy = cy * self.chunk_size
                # Edge chunks (rightmost / bottommost) may have fewer
                # tiles than the configured chunk_size; ``size`` is
                # the actual tile count, ``configured_size`` is the
                # chunk dimension the snapshot was built with.
                max_x = min(ox + self.chunk_size, self.width)
                max_y = min(oy + self.chunk_size, self.height)
                size = min(max_x - ox, max_y - oy)
                out.append(TerrainChunk(
                    offset_x=ox,
                    offset_y=oy,
                    size=size,
                    configured_size=self.chunk_size,
                    terrain_fqon=self._dominant_terrain_fqon(ox, oy),
                    parent_snapshot=self,
                ))
        return tuple(out)

    @property
    def chunk_count(self) -> int:
        return len(self.chunks)

    # ---- per-tile queries ---------------------------------------------

    def _index(self, x: int, y: int) -> int:
        if not (0 <= x < self.width and 0 <= y < self.height):
            raise TerrainSnapshotError(
                f"tile ({x}, {y}) is out of bounds for "
                f"{self.width}x{self.height}"
            )
        return y * self.width + x

    def get_terrain_id_at(self, x: int, y: int) -> str:
        return self.terrain_ids[self._index(x, y)]

    def get_elevation_at(self, x: int, y: int) -> int:
        return int(self.elevation[self._index(x, y)])

    def is_passable(self, x: int, y: int) -> bool:
        tid = self.get_terrain_id_at(x, y)
        return bool(self.passability_table.get(tid, False))

    # ---- chunk helpers -------------------------------------------------

    def _dominant_terrain_fqon(self, ox: int, oy: int) -> str:
        """Pick the most common terrain FQON in the chunk.

        The renderer reads one .terrain per chunk in the MVP; mixed
        chunks would force a renderer-side split, which is out of
        scope for P2.5. The reconstruction is uniform anyway.
        """
        # Count terrain ids in [ox, ox+chunk_size) x [oy, oy+chunk_size).
        # Iterate only over the in-bounds rectangle (edge chunks are
        # smaller; they are NOT padded with bogus terrain ids).
        max_x = min(ox + self.chunk_size, self.width)
        max_y = min(oy + self.chunk_size, self.height)
        counts: dict[str, int] = {}
        for y in range(oy, max_y):
            for x in range(ox, max_x):
                tid = self.terrain_ids[y * self.width + x]
                counts[tid] = counts.get(tid, 0) + 1
        if not counts:
            return ""
        return max(counts.items(), key=lambda kv: kv[1])[0]

    # ---- factory -------------------------------------------------------

    @classmethod
    def from_ir(
        cls,
        map_def: MapDefinition,
        chunk_size: int = 8,
        passability_table: dict[str, bool] | None = None,
    ) -> "TerrainSnapshot":
        if chunk_size <= 0:
            raise TerrainSnapshotError(
                f"chunk_size must be positive, got {chunk_size}"
            )
        return cls(
            width=map_def.width,
            height=map_def.height,
            chunk_size=chunk_size,
            terrain_ids=tuple(map_def.terrain_ids),
            elevation=tuple(int(e) for e in map_def.elevation),
            passability_table=(
                passability_table
                if passability_table is not None
                else default_passability_table()
            ),
        )