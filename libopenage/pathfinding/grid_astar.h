// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4C: GridAStarPathfinder -- deterministic 8-directional A* with
// mover footprint clearance (obstacle inflation).
//
// Per P4.4B.1 §3, the cell-center blocked test is NOT the final
// collision guarantee. The A* applies obstacle inflation by the
// mover's half-extents: a cell (gx, gy) is walkable for the mover
// iff
//   1. NavigationGrid::is_walkable(gx, gy) is true (terrain +
//      static grid not blocked), AND
//   2. no static FootprintBounds b has the mover's bounds
//      [gx - rx, gx + rx) x [gy - ry, gy + ry) overlapping b
//      (equivalently, b inflated by (rx, ry) contains the cell
//      center (gx + 0.5, gy + 0.5)).
//
// 8-direction movement, NO diagonal corner cutting: a diagonal
// step (e.g. NE) is only allowed if the two orthogonal adjacent
// cells (E and N) are both walkable. This prevents the mover
// from squeezing through a diagonal gap between two blocked
// cells while its inflated rect would intersect the obstacles.
//
// Determinism: the open/closed sets are std::set / std::map
// (red-black tree, iteration is sorted by key). The priority
// queue uses std::tuple-ordered entries (f, h, x, y) so ties
// break deterministically on (h, then x, then y). No raw
// pointers, no std::unordered_* iteration.
//
// Result: PATH_FOUND + waypoints (start..target), NO_PATH (no
// walkable route), START_BLOCKED / TARGET_BLOCKED (the
// respective cell is not walkable for this mover -- e.g., a
// Villager spawned on top of the TC would have a blocked start
// cell and the A* reports it; the production load rejects this
// earlier via the PlacementValidator, but the A* guards the
// runtime case too).

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <set>
#include <utility>
#include <vector>

#include "gamestate/component/api/footprint.h"
#include "gamestate/types.h"
#include "scenario/static_occupancy_grid.h"


namespace openage::gamestate::scenario {
class NavigationGrid;
}


namespace openage::pathfinding {


/**
 * Mover half-extents in tile units. Used to inflate static
 * obstacles so a path that keeps the mover's center in a
 * walkable cell is guaranteed to keep the mover's rect out of
 * every static FootprintBounds.
 */
struct MoverFootprint {
	double radius_x{0.0};
	double radius_y{0.0};
};


class GridAStarPathfinder {
public:
	using StepAllowed = std::function<bool(gamestate::scenario::CellPosition,
	                                      gamestate::scenario::CellPosition)>;
	enum class Result : uint8_t {
		PATH_FOUND = 0,
		NO_PATH,
		START_BLOCKED,
		TARGET_BLOCKED,
	};

	/**
	 * Build a pathfinder for the given static-obstacle set and
	 * mover. The grid pointer must outlive the pathfinder
	 * (the pathfinder does NOT own the grid). The obstacles
	 * reference is copied into the pathfinder so the caller
	 * may release the source vector after construction.
	 */
	GridAStarPathfinder(
		const openage::gamestate::scenario::NavigationGrid *grid,
		const std::vector<openage::gamestate::component::FootprintBounds>
			&static_obstacles,
		MoverFootprint mover);

	/**
	 * Find a path from start to target. On PATH_FOUND, the
	 * waypoints (start..target inclusive) are written to
	 * waypoints_out. On any other result, waypoints_out is
	 * cleared.
	 *
	 * The start and target cells MUST be inside the grid's
	 * extent (otherwise PATH_FOUND is never returned). The
	 * grid is queried via NavigationGrid::is_walkable; the
	 * pathfinder inflates static obstacles by the mover's
	 * half-extents.
	 */
	Result find_path(
		openage::gamestate::scenario::CellPosition start,
		openage::gamestate::scenario::CellPosition target,
		std::vector<openage::gamestate::scenario::CellPosition>
		&waypoints_out, const StepAllowed &step_allowed = {});

private:
	// Inflated static obstacles: pre-computed at construction.
	struct Inflated {
		openage::gamestate::component::FootprintBounds bounds;
	};
	std::vector<Inflated> inflated_;

	const openage::gamestate::scenario::NavigationGrid *grid_;
	MoverFootprint mover_;

	bool walkable_for_mover(openage::gamestate::scenario::CellPosition c) const;
	bool diagonal_clear(openage::gamestate::scenario::CellPosition from,
	                    openage::gamestate::scenario::CellPosition to) const;
	int chebyshev(openage::gamestate::scenario::CellPosition a,
	              openage::gamestate::scenario::CellPosition b) const;
};


} // namespace openage::pathfinding
