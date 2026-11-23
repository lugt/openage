// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4C: GridAStarPathfinder implementation -- see grid_astar.h for
// the full contract. This file contains:
//
//   * Constructor: pre-inflate each static FootprintBounds by the
//     mover's half-extents and store in `inflated_` so the per-cell
//     walkability test is O(#obstacles) at construction time and
//     O(#obstacles) per walkability query.
//   * walkable_for_mover(cell): grid is_walkable AND no inflated
//     obstacle b contains the cell center (cell_x + 0.5,
//     cell_y + 0.5). The inflation guarantees that a mover whose
//     center stays in a walkable cell can never have its inflated
//     rect intersect a static FootprintBounds.
//   * diagonal_clear(from, to): for a diagonal step, both
//     orthogonal adjacent cells (E and N for NE) must be walkable.
//     Prevents the mover from squeezing through a diagonal gap
//     between two blocked cells when its inflated rect would
//     intersect the obstacles.
//   * chebyshev(a, b): 8-connected admissible heuristic.
//   * find_path: classic A* over (CellPosition, g, h) with a
//     std::map of g_scores, a std::map of came_from, and a
//     std::set priority queue ordered by (f, h, x, y) for stable
//     tie-break. Path reconstruction walks came_from back from
//     target to start, then reverses.
//
// Determinism note: std::set / std::map are red-black trees with
// ordered iteration on (x, y). The open set's priority tuple
// (f, h, x, y) ensures two equal-f paths pick the one with the
// lower h first, then the lower x, then the lower y. No raw
// pointers, no std::unordered_* iteration.

#include "pathfinding/grid_astar.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <queue>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "gamestate/component/api/footprint.h"
#include "log/log.h"
#include "log/message.h"
#include "scenario/static_occupancy_grid.h"


namespace openage::pathfinding {


GridAStarPathfinder::GridAStarPathfinder(
	const openage::gamestate::scenario::NavigationGrid *grid,
	const std::vector<openage::gamestate::component::FootprintBounds>
		&static_obstacles,
	MoverFootprint mover) :
	grid_{grid},
	mover_{mover} {
	// Pre-inflate every static obstacle by the mover's half-extents.
	// Inflated bounds = [b.min_x - rx, b.max_x + rx) x
	// [b.min_y - ry, b.max_y + ry). A cell center c is inside the
	// inflated bounds iff c.x + 0.5 is inside [b.min_x - rx,
	// b.max_x + rx) and similarly for y. Equivalently, the mover
	// center can sit on c without the mover's rect (radius (rx, ry)
	// around the center) intersecting b.
	this->inflated_.reserve(static_obstacles.size());
	for (const auto &b : static_obstacles) {
		Inflated inf;
		inf.bounds.min_x = b.min_x - mover.radius_x;
		inf.bounds.min_y = b.min_y - mover.radius_y;
		inf.bounds.max_x = b.max_x + mover.radius_x;
		inf.bounds.max_y = b.max_y + mover.radius_y;
		this->inflated_.push_back(inf);
	}
}


bool GridAStarPathfinder::walkable_for_mover(
	openage::gamestate::scenario::CellPosition c) const {
	if (this->grid_ == nullptr) {
		return false;
	}
	if (not this->grid_->is_walkable(c)) {
		return false;
	}
	const double cx = static_cast<double>(c.x) + 0.5;
	const double cy = static_cast<double>(c.y) + 0.5;
	for (const auto &inf : this->inflated_) {
		if (cx >= inf.bounds.min_x && cx < inf.bounds.max_x
		    && cy >= inf.bounds.min_y && cy < inf.bounds.max_y) {
			return false;
		}
	}
	return true;
}


bool GridAStarPathfinder::diagonal_clear(
	openage::gamestate::scenario::CellPosition from,
	openage::gamestate::scenario::CellPosition to) const {
	// For a diagonal step (|dx| == 1 && |dy| == 1), check that
	// both orthogonal adjacent cells (E + N for NE, E + S for SE,
	// W + N for NW, W + S for SW) are walkable. Without this,
	// the mover could squeeze through a corner between two
	// blocked cells while its inflated rect intersects the
	// obstacles.
	const int dx = to.x - from.x;
	const int dy = to.y - from.y;
	if (std::abs(dx) != 1 || std::abs(dy) != 1) {
		// Not a diagonal step; the corner-cut guard does not
		// apply. (Straight and knight-move cases are not used.)
		return true;
	}
	const openage::gamestate::scenario::CellPosition ortho_a{
		from.x + dx,
		from.y};
	const openage::gamestate::scenario::CellPosition ortho_b{
		from.x,
		from.y + dy};
	return this->walkable_for_mover(ortho_a)
	       && this->walkable_for_mover(ortho_b);
}


int GridAStarPathfinder::chebyshev(
	openage::gamestate::scenario::CellPosition a,
	openage::gamestate::scenario::CellPosition b) const {
	const int dx = std::abs(a.x - b.x);
	const int dy = std::abs(a.y - b.y);
	// 8-connected admissible heuristic: max(dx, dy). With diagonal
	// cost 1 (matching the straight cost), Chebyshev distance is
	// admissible and consistent.
	return std::max(dx, dy);
}


namespace {


// Priority tuple: ordered lexicographically (f, h, x, y) so
// std::set iteration is deterministic. (h is the heuristic at
// the node; ties on f break to the lower h first, which prefers
// nodes closer to the goal -- a standard A* refinement.)
using openage::gamestate::scenario::CellPosition;
using PrioKey = std::tuple<int, int, int, int>;


PrioKey make_prio(int f, int h, CellPosition c) {
	return std::make_tuple(f, h, c.x, c.y);
}


// 8-direction neighbour offsets: N, NE, E, SE, S, SW, W, NW.
constexpr int NEIGHBOUR_DX[8] = {0, 1, 1, 1, 0, -1, -1, -1};
constexpr int NEIGHBOUR_DY[8] = {1, 1, 0, -1, -1, -1, 0, 1};
// Diagonal step cost is 1 in our 8-connected cost model
// (matching straight step cost 1). The Chebyshev heuristic is
// admissible with this cost model.
constexpr int STEP_COST[8] = {1, 1, 1, 1, 1, 1, 1, 1};


}  // namespace


GridAStarPathfinder::Result GridAStarPathfinder::find_path(
	openage::gamestate::scenario::CellPosition start,
	openage::gamestate::scenario::CellPosition target,
	std::vector<openage::gamestate::scenario::CellPosition> &waypoints_out,
	const StepAllowed &step_allowed) {
	waypoints_out.clear();

	if (start == target) {
		// Trivial path: zero-length movement.
		waypoints_out.push_back(start);
		return Result::PATH_FOUND;
	}

	if (not this->walkable_for_mover(start)) {
		return Result::START_BLOCKED;
	}
	if (not this->walkable_for_mover(target)) {
		return Result::TARGET_BLOCKED;
	}

	// Deterministic A* state.
	std::map<CellPosition, int> g_score;
	std::map<CellPosition, CellPosition> came_from;
	std::set<std::pair<PrioKey, CellPosition>> open_set;

	auto start_key = make_prio(this->chebyshev(start, target),
	                           this->chebyshev(start, target),
	                           start);
	g_score[start] = 0;
	open_set.insert({start_key, start});

	while (not open_set.empty()) {
		auto it = open_set.begin();
		CellPosition current = it->second;
		int current_g = g_score[current];
		open_set.erase(it);

		if (current == target) {
			// Reconstruct path by walking came_from from target
			// back to start.
			std::vector<CellPosition> rev;
			CellPosition cur = target;
			while (true) {
				rev.push_back(cur);
				if (cur == start) {
					break;
				}
				auto cit = came_from.find(cur);
				if (cit == came_from.end()) {
					// Internal inconsistency: should not
					// happen if start was reachable.
					waypoints_out.clear();
					return Result::NO_PATH;
				}
				cur = cit->second;
			}
			std::reverse(rev.begin(), rev.end());
			waypoints_out = std::move(rev);
			(void)current_g;
			return Result::PATH_FOUND;
		}

		for (int i = 0; i < 8; ++i) {
			const CellPosition neighbour{
				current.x + NEIGHBOUR_DX[i],
				current.y + NEIGHBOUR_DY[i]};
			if (not this->walkable_for_mover(neighbour)) {
				continue;
			}
			if (not this->diagonal_clear(current, neighbour)) {
				// Diagonal corner-cut blocked.
				continue;
			}
			if (step_allowed and not step_allowed(current, neighbour)) {
				continue;
			}
			const int tentative_g = current_g + STEP_COST[i];
			auto g_it = g_score.find(neighbour);
			if (g_it != g_score.end() && tentative_g >= g_it->second) {
				// Not a better path.
				continue;
			}
			g_score[neighbour] = tentative_g;
			came_from[neighbour] = current;
			const int h_nei = this->chebyshev(neighbour, target);
			const int f_nei = tentative_g + h_nei;
			open_set.insert({make_prio(f_nei, h_nei, neighbour),
			                 neighbour});
		}
	}

	return Result::NO_PATH;
}


} // namespace openage::pathfinding
