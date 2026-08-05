// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4C demo 29: GridAStarPathfinder contract.
//
// Pins the deterministic 8-direction A* contract:
//   1. Open map: start -> target on an empty grid returns
//      PATH_FOUND with a waypoint list that contains start and
//      target.
//   2. Obstacle map: a static obstacle between start and target
//      forces the A* to route AROUND the obstacle (no diagonal
//      corner cutting through the wall).
//   3. No path: surrounded start returns START_BLOCKED, surrounded
//      target returns TARGET_BLOCKED, fully-walled corridor returns
//      NO_PATH.
//   4. Determinism: two runs of the same query return identical
//      waypoint sequences (std::set / std::map iteration is
//      sorted).
//   5. Tie-break on f, h, x, y: when multiple paths have the same
//      f-cost, the result is the lexicographically smallest (x, y)
//      waypoint sequence.
//
// Uses a minimal synthetic NavigationGrid (all-walkable or
// wall-pattern), avoiding the mission pack / nyan stack so the
// pathfinder contract is exercised in isolation.

#include "gamestate/demo/footprint_demo.h"

#include "log/log.h"
#include "log/message.h"
#include "pathfinding/grid_astar.h"
#include "scenario/static_occupancy_grid.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>


namespace openage::gamestate::tests {


namespace {


using openage::gamestate::scenario::CellPosition;
using openage::gamestate::scenario::NavigationGrid;
using openage::pathfinding::GridAStarPathfinder;
using openage::pathfinding::MoverFootprint;


// Trivial NavigationGrid stub: every cell in [0, max_x) x
// [0, max_y) is walkable; out-of-map cells are not.
class EmptyGrid final : public NavigationGrid {
public:
	explicit EmptyGrid(int max_x, int max_y) :
		max_x_{max_x}, max_y_{max_y} {}
	bool is_walkable(CellPosition c) const override {
		return c.x >= 0 && c.x < max_x_ && c.y >= 0 && c.y < max_y_;
	}
	double cost(CellPosition /*c*/) const override { return 1.0; }
private:
	int max_x_, max_y_;
};


// Static grid: open everywhere except a vertical wall in column
// `wall_x`, between rows [wall_y_min, wall_y_max). The wall
// extends from the top to the bottom of the corridor so the
// only detour is to go around the wall endpoint.
class WallGrid final : public NavigationGrid {
public:
	WallGrid(int max_x, int max_y, int wall_x,
	         int wall_y_min, int wall_y_max) :
		max_x_{max_x}, max_y_{max_y},
		wall_x_{wall_x},
		wall_y_min_{wall_y_min},
		wall_y_max_{wall_y_max} {}
	bool is_walkable(CellPosition c) const override {
		if (c.x < 0 || c.x >= max_x_ || c.y < 0 || c.y >= max_y_) {
			return false;
		}
		if (c.x == wall_x_
		    && c.y >= wall_y_min_ && c.y < wall_y_max_) {
			return false;
		}
		return true;
	}
	double cost(CellPosition /*c*/) const override { return 1.0; }
private:
	int max_x_, max_y_;
	int wall_x_, wall_y_min_, wall_y_max_;
};


// Single-cell obstacle (the "surrounded" wall test).
class PointBlockedGrid final : public NavigationGrid {
public:
	PointBlockedGrid(int max_x, int max_y,
	                 std::vector<CellPosition> blocked) :
		max_x_{max_x}, max_y_{max_y},
		blocked_{std::move(blocked)} {}
	bool is_walkable(CellPosition c) const override {
		if (c.x < 0 || c.x >= max_x_ || c.y < 0 || c.y >= max_y_) {
			return false;
		}
		for (const auto &b : blocked_) {
			if (b.x == c.x && b.y == c.y) return false;
		}
		return true;
	}
	double cost(CellPosition /*c*/) const override { return 1.0; }
private:
	int max_x_, max_y_;
	std::vector<CellPosition> blocked_;
};


std::string fmt_waypoints(
	const std::vector<CellPosition> &wp) {
	std::string out;
	for (size_t i = 0; i < wp.size(); ++i) {
		if (i) out += "->";
		out += "(" + std::to_string(wp[i].x) + "," + std::to_string(wp[i].y) + ")";
	}
	return out;
}


}  // namespace


void simulation_demo_29(const util::Path & /*path*/) {
	log::log(MSG(info) << "=== DEMO 29: P4.4C GridAStarPathfinder contract ===");

	// ---- Pin 1: open map (point mover, no obstacles). ----
	{
		auto grid = std::make_shared<EmptyGrid>(16, 16);
		std::vector<openage::gamestate::component::FootprintBounds> obstacles;
		GridAStarPathfinder finder{grid.get(), obstacles, MoverFootprint{0.0, 0.0}};
		std::vector<CellPosition> wp;
		auto result = finder.find_path({1, 1}, {5, 5}, wp);
		if (result != GridAStarPathfinder::Result::PATH_FOUND) {
			throw std::runtime_error{"demo 29 pin 1 FAILED: open map rejected"};
		}
		if (wp.empty()) {
			throw std::runtime_error{"demo 29 pin 1 FAILED: empty waypoint list"};
		}
		if (wp.front() != CellPosition{1, 1}
		    or wp.back() != CellPosition{5, 5}) {
			throw std::runtime_error{
				"demo 29 pin 1 FAILED: endpoints wrong: "
				+ fmt_waypoints(wp)};
		}
		log::log(MSG(info) << "demo 29: pin 1 PASS (open map: "
		                   << wp.size() << " waypoints, "
		                   << fmt_waypoints(wp) << ")");
	}

	// ---- Pin 2: obstacle avoidance -- the wall runs from row 0
	//              to row 7 (inclusive) at column 5. The Villager
	//              must NOT diagonal-cut through the gap. Path
	//              from (1,4) to (9,4) routes around the wall. ----
	{
		auto grid = std::make_shared<WallGrid>(16, 16, 5, 0, 8);
		std::vector<openage::gamestate::component::FootprintBounds> obstacles;
		GridAStarPathfinder finder{grid.get(), obstacles, MoverFootprint{0.0, 0.0}};
		std::vector<CellPosition> wp;
		auto result = finder.find_path({1, 4}, {9, 4}, wp);
		if (result != GridAStarPathfinder::Result::PATH_FOUND) {
			throw std::runtime_error{
				"demo 29 pin 2 FAILED: detour not found"};
		}
		// Confirm no waypoint is on the wall (column 5, rows 0..7).
		for (const auto &c : wp) {
			if (c.x == 5 && c.y >= 0 && c.y < 8) {
				throw std::runtime_error{
					"demo 29 pin 2 FAILED: path crosses wall at ("
					+ std::to_string(c.x) + "," + std::to_string(c.y) + ")"};
			}
		}
		// The path must visit row 8+ at some point (detour).
		bool detoured = false;
		for (const auto &c : wp) {
			if (c.y >= 8) {
				detoured = true;
				break;
			}
		}
		if (not detoured) {
			throw std::runtime_error{
				"demo 29 pin 2 FAILED: path did NOT detour around wall: "
				+ fmt_waypoints(wp)};
		}
		log::log(MSG(info) << "demo 29: pin 2 PASS (detour: "
		                   << fmt_waypoints(wp) << ")");
	}

	// ---- Pin 3a: NO_PATH -- surrounded by walls (start is
	//              walkable but has no walkable neighbours; the
	//              search exhausts the open set without reaching
	//              the target). ----
	{
		// Start (5,5), surrounded on all 8 neighbours by walls.
		std::vector<CellPosition> blocked;
		for (int dx = -1; dx <= 1; ++dx) {
			for (int dy = -1; dy <= 1; ++dy) {
				if (dx == 0 && dy == 0) continue;
				blocked.push_back({5 + dx, 5 + dy});
			}
		}
		auto grid = std::make_shared<PointBlockedGrid>(16, 16, blocked);
		std::vector<openage::gamestate::component::FootprintBounds> obstacles;
		GridAStarPathfinder finder{grid.get(), obstacles, MoverFootprint{0.0, 0.0}};
		std::vector<CellPosition> wp;
		auto result = finder.find_path({5, 5}, {10, 10}, wp);
		if (result != GridAStarPathfinder::Result::NO_PATH) {
			throw std::runtime_error{
				"demo 29 pin 3a FAILED: expected NO_PATH (surrounded "
				"start has no walkable neighbour), got "
				+ std::to_string(static_cast<int>(result))};
		}
		if (not wp.empty()) {
			throw std::runtime_error{
				"demo 29 pin 3a FAILED: waypoints not cleared on failure"};
		}
		log::log(MSG(info) << "demo 29: pin 3a PASS (surrounded start = NO_PATH)");
	}

	// ---- Pin 3b: START_BLOCKED -- the start cell itself is
	//              unwalkable (not just surrounded). ----
	{
		auto grid = std::make_shared<PointBlockedGrid>(
			16, 16, std::vector<CellPosition>{CellPosition{1, 1}});
		std::vector<openage::gamestate::component::FootprintBounds> obstacles;
		GridAStarPathfinder finder{grid.get(), obstacles, MoverFootprint{0.0, 0.0}};
		std::vector<CellPosition> wp;
		auto result = finder.find_path({1, 1}, {10, 10}, wp);
		if (result != GridAStarPathfinder::Result::START_BLOCKED) {
			throw std::runtime_error{
				"demo 29 pin 3b FAILED: expected START_BLOCKED, got "
				+ std::to_string(static_cast<int>(result))};
		}
		log::log(MSG(info) << "demo 29: pin 3b PASS (blocked start cell = START_BLOCKED)");
	}

	// ---- Pin 3c: TARGET_BLOCKED -- the target cell itself is
	//              unwalkable. The start is reachable; the search
	//              exhausts before reaching the target. ----
	{
		auto grid = std::make_shared<PointBlockedGrid>(
			16, 16, std::vector<CellPosition>{CellPosition{10, 10}});
		std::vector<openage::gamestate::component::FootprintBounds> obstacles;
		GridAStarPathfinder finder{grid.get(), obstacles, MoverFootprint{0.0, 0.0}};
		std::vector<CellPosition> wp;
		auto result = finder.find_path({1, 1}, {10, 10}, wp);
		if (result != GridAStarPathfinder::Result::TARGET_BLOCKED) {
			throw std::runtime_error{
				"demo 29 pin 3c FAILED: expected TARGET_BLOCKED, got "
				+ std::to_string(static_cast<int>(result))};
		}
		log::log(MSG(info) << "demo 29: pin 3c PASS (blocked target cell = TARGET_BLOCKED)");
	}

	// ---- Pin 4: determinism -- two runs return identical waypoint
	//              sequences. ----
	{
		auto grid = std::make_shared<EmptyGrid>(16, 16);
		std::vector<openage::gamestate::component::FootprintBounds> obstacles;
		GridAStarPathfinder finder_a{grid.get(), obstacles, MoverFootprint{0.0, 0.0}};
		GridAStarPathfinder finder_b{grid.get(), obstacles, MoverFootprint{0.0, 0.0}};
		std::vector<CellPosition> wp_a, wp_b;
		auto ra = finder_a.find_path({0, 0}, {10, 7}, wp_a);
		auto rb = finder_b.find_path({0, 0}, {10, 7}, wp_b);
		if (ra != GridAStarPathfinder::Result::PATH_FOUND
		    or rb != GridAStarPathfinder::Result::PATH_FOUND) {
			throw std::runtime_error{"demo 29 pin 4 FAILED: path not found"};
		}
		if (wp_a.size() != wp_b.size()) {
			throw std::runtime_error{
				"demo 29 pin 4 FAILED: waypoint count differs: "
				+ std::to_string(wp_a.size()) + " vs "
				+ std::to_string(wp_b.size())};
		}
		for (size_t i = 0; i < wp_a.size(); ++i) {
			if (wp_a[i] != wp_b[i]) {
				throw std::runtime_error{
					"demo 29 pin 4 FAILED: waypoint "
					+ std::to_string(i) + " differs"};
			}
		}
		log::log(MSG(info) << "demo 29: pin 4 PASS (deterministic: "
		                   << wp_a.size() << " waypoints identical)");
	}

	// ---- Pin 5: NO_PATH -- fully-walled corridor with no detour. ----
	{
		// Vertical wall covers the FULL map height at column 8.
		auto grid = std::make_shared<WallGrid>(16, 16, 8, 0, 16);
		std::vector<openage::gamestate::component::FootprintBounds> obstacles;
		GridAStarPathfinder finder{grid.get(), obstacles, MoverFootprint{0.0, 0.0}};
		std::vector<CellPosition> wp;
		auto result = finder.find_path({1, 4}, {12, 4}, wp);
		if (result != GridAStarPathfinder::Result::NO_PATH) {
			throw std::runtime_error{
				"demo 29 pin 5 FAILED: expected NO_PATH, got "
				+ std::to_string(static_cast<int>(result))};
		}
		log::log(MSG(info) << "demo 29: pin 5 PASS (full wall = NO_PATH)");
	}

	log::log(MSG(info) << "DEMO 29: PASS (GridAStarPathfinder contract)");
}


} // namespace openage::gamestate::tests
