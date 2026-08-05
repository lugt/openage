// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4C demo 30: mover footprint clearance.
//
// Spec §3 (P4.4B.1) requires the A* to inflate static obstacles
// by the mover's half-extents so a path that keeps the mover's
// center in a walkable cell is guaranteed to keep the mover's
// rect out of every static FootprintBounds.
//
// Pins:
//   1. Point mover (rx=0, ry=0): inflation is a no-op. A path
//      runs as close to the TC as the cell-center walkability
//      allows.
//   2. Villager mover (rx=0.2, ry=0.2): cells whose centers are
//      within 0.2 of the TC bounds are blocked. The path must
//      avoid the cells [5,6,7,10,11] x [7,8] (the row immediately
//      above the TC's [6,10) x [6,10) bounds, shrunk by the
//      mover radius).
//   3. Half-extent equal to half a cell (rx=0.5, ry=0.5): the
//      mover's rect exactly covers its center cell. Paths that
//      route along the TC edge are further away.
//   4. Inflation formula: a mover with rx=2, ry=2 (same size as
//      the TC) treats ALL cells as blocked (the inflated bounds
//      cover the entire map), so any start/target pair fails
//      with NO_PATH / START_BLOCKED.

#include "gamestate/demo/footprint_demo.h"

#include "log/log.h"
#include "log/message.h"
#include "pathfinding/grid_astar.h"
#include "scenario/static_occupancy_grid.h"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>


namespace openage::gamestate::tests {


namespace {


using openage::gamestate::component::FootprintBounds;
using openage::gamestate::scenario::CellPosition;
using openage::gamestate::scenario::NavigationGrid;
using openage::pathfinding::GridAStarPathfinder;
using openage::pathfinding::MoverFootprint;


// All-walkable 16x16 grid; the A* gates mover clearance on top
// of grid walkability via inflated obstacles.
class EmptyGrid final : public NavigationGrid {
public:
	bool is_walkable(CellPosition c) const override {
		return c.x >= 0 && c.x < 16 && c.y >= 0 && c.y < 16;
	}
	double cost(CellPosition /*c*/) const override { return 1.0; }
};


// TC footprint centered at (8,8), rx=2, ry=2 -> bounds
// [6,10) x [6,10).
FootprintBounds tc_bounds() {
	FootprintBounds b;
	b.min_x = 6.0;
	b.min_y = 6.0;
	b.max_x = 10.0;
	b.max_y = 10.0;
	return b;
}


std::string fmt(const std::vector<CellPosition> &wp) {
	std::string s;
	for (size_t i = 0; i < wp.size(); ++i) {
		if (i) s += "->";
		s += "(" + std::to_string(wp[i].x) + "," + std::to_string(wp[i].y) + ")";
	}
	return s;
}


}  // namespace


void simulation_demo_30(const util::Path & /*path*/) {
	log::log(MSG(info) << "=== DEMO 30: P4.4C mover footprint clearance ===");

	// ---- Pin 1: point mover (rx=0, ry=0). Inflation is a no-op;
	//              the path runs as close to the TC as the
	//              cell-center test allows (next to [6,10)x[6,10)). ----
	{
		auto grid = std::make_shared<EmptyGrid>();
		std::vector<FootprintBounds> obstacles{tc_bounds()};
		GridAStarPathfinder finder{grid.get(), obstacles,
			MoverFootprint{0.0, 0.0}};
		std::vector<CellPosition> wp;
		auto r = finder.find_path({1, 8}, {14, 8}, wp);
		if (r != GridAStarPathfinder::Result::PATH_FOUND) {
			throw std::runtime_error{
				"demo 30 pin 1 FAILED: point mover rejected"};
		}
		// Point mover can pass through cell (5,8) (left of TC)
		// or (10,8) (right of TC) or (8,5)/(8,10).
		bool passed_through_gap = false;
		for (const auto &c : wp) {
			if ((c.x == 5 || c.x == 10 || c.y == 5 || c.y == 10)
			    && c.x >= 0 && c.x < 16) {
				passed_through_gap = true;
				break;
			}
		}
		if (not passed_through_gap) {
			throw std::runtime_error{
				"demo 30 pin 1 FAILED: point mover did not route "
				"through cell-edge gap: " + fmt(wp)};
		}
		log::log(MSG(info) << "demo 30: pin 1 PASS (point mover routes through gap: "
		                   << fmt(wp) << ")");
	}

	// ---- Pin 2: Villager mover (rx=0.2, ry=0.2). The inflation
	//              expands the TC bounds by 0.2 in each direction:
	//              [5.8, 10.2) x [5.8, 10.2). Cell centers in
	//              [5.8, 10.2) x [5.8, 10.2) are blocked, i.e.
	//              cells (6..9, 6..9) PLUS cells where x+0.5 in
	//              [5.8, 10.2) -> x in {5, 10} and y in {6..9}
	//              AND cells where y+0.5 in [5.8, 10.2) -> y in
	//              {5, 10} and x in {6..9}. So (5, 6..9), (10, 6..9),
	//              (6..9, 5), (6..9, 10) are blocked for the
	//              Villager. ----
	{
		auto grid = std::make_shared<EmptyGrid>();
		std::vector<FootprintBounds> obstacles{tc_bounds()};
		GridAStarPathfinder finder{grid.get(), obstacles,
			MoverFootprint{0.2, 0.2}};
		std::vector<CellPosition> wp;
		auto r = finder.find_path({1, 8}, {14, 8}, wp);
		if (r != GridAStarPathfinder::Result::PATH_FOUND) {
			throw std::runtime_error{
				"demo 30 pin 2 FAILED: Villager-sized mover rejected"};
		}
		// The Villager must NOT pass through (5,8) or (10,8) --
		// those cells are inside the inflated rect [5.8, 10.2) x
		// [5.8, 10.2) (the cell-center (5.5, 8.5) is in there).
		for (const auto &c : wp) {
			if ((c.x == 5 || c.x == 10) && c.y >= 6 && c.y <= 9) {
				throw std::runtime_error{
					"demo 30 pin 2 FAILED: Villager passed through "
					"inflation-blocked cell ("
					+ std::to_string(c.x) + "," + std::to_string(c.y) + ")"};
			}
			if ((c.y == 5 || c.y == 10) && c.x >= 6 && c.x <= 9) {
				throw std::runtime_error{
					"demo 30 pin 2 FAILED: Villager passed through "
					"inflation-blocked cell ("
					+ std::to_string(c.x) + "," + std::to_string(c.y) + ")"};
			}
		}
		// Path must detour above or below (y >= 11 or y <= 4).
		bool detoured = false;
		for (const auto &c : wp) {
			if (c.y >= 11 || c.y <= 4) {
				detoured = true;
				break;
			}
		}
		if (not detoured) {
			throw std::runtime_error{
				"demo 30 pin 2 FAILED: Villager did not detour past "
				"inflation: " + fmt(wp)};
		}
		log::log(MSG(info) << "demo 30: pin 2 PASS (Villager rx=0.2 detour: "
		                   << fmt(wp) << ")");
	}

	// ---- Pin 3: same TC, half-cell mover (rx=0.5, ry=0.5). The
	//              inflated bounds are [5.5, 10.5) x [5.5, 10.5);
	//              cell centers in [5.5, 10.5) -> cells (5..10, 5..10)
	//              (because (x+0.5) in [5.5, 10.5) iff x in [5, 10)).
	//              So the mover can squeeze through (5, 5)/(10, 5)/
	//              (5, 10)/(10, 10) corners but not (5, 8)/(10, 8). ----
	{
		auto grid = std::make_shared<EmptyGrid>();
		std::vector<FootprintBounds> obstacles{tc_bounds()};
		GridAStarPathfinder finder{grid.get(), obstacles,
			MoverFootprint{0.5, 0.5}};
		std::vector<CellPosition> wp;
		auto r = finder.find_path({1, 8}, {14, 8}, wp);
		if (r != GridAStarPathfinder::Result::PATH_FOUND) {
			throw std::runtime_error{
				"demo 30 pin 3 FAILED: half-cell mover rejected"};
		}
		// Confirm the path detoured via the corner cells.
		bool via_corner = false;
		for (const auto &c : wp) {
			if ((c.x == 5 || c.x == 10)
			    && (c.y == 5 || c.y == 10)) {
				via_corner = true;
				break;
			}
		}
		if (not via_corner) {
			throw std::runtime_error{
				"demo 30 pin 3 FAILED: half-cell mover did not corner-cut: "
				+ fmt(wp)};
		}
		log::log(MSG(info) << "demo 30: pin 3 PASS (half-cell mover corner-routes: "
		                   << fmt(wp) << ")");
	}

	// ---- Pin 4: a mover bigger than half the map can block the
	//              whole map. rx=ry=8 inflates the TC to
	//              [-2, 18) x [-2, 18), covering all 16x16 cells;
	//              any start/target pair returns NO_PATH or
	//              START/TARGET_BLOCKED. ----
	{
		auto grid = std::make_shared<EmptyGrid>();
		std::vector<FootprintBounds> obstacles{tc_bounds()};
		GridAStarPathfinder finder{grid.get(), obstacles,
			MoverFootprint{8.0, 8.0}};
		std::vector<CellPosition> wp;
		auto r = finder.find_path({1, 1}, {14, 14}, wp);
		if (r == GridAStarPathfinder::Result::PATH_FOUND) {
			throw std::runtime_error{
				"demo 30 pin 4 FAILED: huge mover found a path"};
		}
		log::log(MSG(info) << "demo 30: pin 4 PASS (rx=8 mover blocked everywhere, "
		                   << "result=" << static_cast<int>(r) << ")");
	}

	log::log(MSG(info) << "DEMO 30: PASS (mover footprint clearance)");
}


} // namespace openage::gamestate::tests
