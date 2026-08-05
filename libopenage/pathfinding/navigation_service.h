// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4C: NavigationService -- the runtime wrapper around the
// static obstacle set and the deterministic GridAStarPathfinder.
//
// The service is owned by Game (or GameSimulation, depending on
// the bootstrap path the spec lands on) and is queried by the
// Move runtime (system/move.cpp) to resolve a tile-cell start +
// tile-cell target into a list of waypoint cells. It is NOT
// aware of nyan, placements, or entity ids: the Move runtime
// resolves entities to footprints and then to (cell_x, cell_y)
// before calling request_path().
//
// Construction:
//   NavigationService(
//       std::shared_ptr<NavigationGrid> grid,
//       std::vector<FootprintBounds> static_obstacles)
//
// The static_obstacles vector is COPIED into the service (the
// caller may release its own copy). Subsequent changes to the
// caller's vector do not affect the service; if the static
// obstacle set changes at runtime, build a new NavigationService
// (rebuild_navigation() helper).
//
// Per-mover semantics:
//   request_path(start, target, mover) builds a fresh
//   GridAStarPathfinder (a tiny POD that only inflates obstacles
//   and runs A*) so we don't have to mutate state when different
//   units ask for paths with different mover half-extents.
//
// Failure mode: NO_PATH / START_BLOCKED / TARGET_BLOCKED are
// surfaced verbatim; the Move runtime MUST NOT fall back to a
// straight-line traversal on any failure (spec §VI).

#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "gamestate/component/api/footprint.h"
#include "pathfinding/grid_astar.h"
#include "scenario/static_occupancy_grid.h"


namespace openage::pathfinding {


class NavigationService {
public:
	NavigationService(
		std::shared_ptr<openage::gamestate::scenario::NavigationGrid> grid,
		std::vector<openage::gamestate::component::FootprintBounds>
			static_obstacles);

	/**
	 * Result of a request_path call: the pathfinder outcome plus
	 * the waypoint list. On PATH_FOUND, waypoints is a non-empty
	 * list (start..target inclusive). On any other outcome,
	 * waypoints is empty.
	 */
	struct Path {
		GridAStarPathfinder::Result result{
			GridAStarPathfinder::Result::NO_PATH};
		std::vector<openage::gamestate::scenario::CellPosition> waypoints;
	};

	/**
	 * Resolve a path between two cells for a mover with the given
	 * half-extents. The NavigationGrid (terrain + static
	 * occupancy) gates walkability; the inflated obstacle set
	 * gates mover clearance.
	 */
	Path request_path(
		openage::gamestate::scenario::CellPosition start,
		openage::gamestate::scenario::CellPosition target,
		MoverFootprint mover) const;

	/**
	 * Read-only access to the underlying grid (for diagnostics /
	 * tests).
	 */
	const std::shared_ptr<openage::gamestate::scenario::NavigationGrid>
	&grid() const {
		return this->grid_;
	}

	/**
	 * Number of static obstacles currently registered.
	 */
	size_t static_obstacle_count() const {
		return this->static_obstacles_.size();
	}

private:
	std::shared_ptr<openage::gamestate::scenario::NavigationGrid> grid_;
	std::vector<openage::gamestate::component::FootprintBounds>
		static_obstacles_;
};


} // namespace openage::pathfinding
