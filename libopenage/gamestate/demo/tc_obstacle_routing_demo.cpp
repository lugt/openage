// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4C demo 32: Mission 1 TownCenter obstacle integration.
//
// Spec §VII: the tc_obstacle fixture must produce a Villager
// path that ROUTES AROUND the TownCenter (not through it). The
// A* uses obstacle inflation: a Villager with rx=0.2 cannot
// enter cells whose center is inside the TC's inflated rect
// [5.8, 10.2) x [5.8, 10.2). Forcing a path that visually
// crosses the TC at cell (8,8) would have failed the inflation
// gate.
//
// We don't actually drive the Villager in a full simulation
// (that is demo 31); this demo verifies the A*'s behaviour
// against the production layout: a Villager at (2,8) trying to
// reach (14,8) must route above or below the TC, not through
// the wall of blocked cells the inflation creates.
//
// Pins:
//   1. The straight line from (2,8) to (14,8) crosses the TC
//      footprint. A point mover cannot pass through (8,8); a
//      Villager must route around (rx=0.2 inflation).
//   2. A path is FOUND (not NO_PATH) -- the open terrain is
//      wide enough for a detour.
//   3. NO waypoint in the path is inside the TC footprint
//      [6,10) x [6,10) OR inside the Villager-inflated rect
//      [5.8, 10.2) x [5.8, 10.2) -- the cell-center test must
//      reject those cells.
//   4. The path visits at least one cell with y <= 4 or y >= 11
//      (the detour went above or below).
//
// This test uses a synthetic NavigationGrid + obstacle list that
// mirrors the Mission 1 layout, so it doesn't depend on the full
// simulation runtime.

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


// 16x16 all-walkable grid (Mission 1 terrain default).
class EmptyGrid final : public NavigationGrid {
public:
	bool is_walkable(CellPosition c) const override {
		return c.x >= 0 && c.x < 16 && c.y >= 0 && c.y < 16;
	}
	double cost(CellPosition /*c*/) const override { return 1.0; }
};


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


void simulation_demo_32(const util::Path & /*path*/) {
	log::log(MSG(info) << "=== DEMO 32: P4.4C Mission 1 TC obstacle routing ===");

	// ---- Pin 1: straight-line is blocked. The straight line
	//              between (2,8) and (14,8) crosses the TC at
	//              (8,8). ----
	{
		auto grid = std::make_shared<EmptyGrid>();
		std::vector<FootprintBounds> obstacles{tc_bounds()};
		GridAStarPathfinder finder{grid.get(), obstacles,
			MoverFootprint{0.0, 0.0}};
		std::vector<CellPosition> wp;
		auto r = finder.find_path({2, 8}, {14, 8}, wp);
		if (r != GridAStarPathfinder::Result::PATH_FOUND) {
			throw std::runtime_error{
				"demo 32 pin 1 FAILED: straight-line detour not found"};
		}
		// Confirm no waypoint is inside the TC [6,10)x[6,10)
		// (point mover, no inflation).
		for (const auto &c : wp) {
			if (c.x >= 6 && c.x < 10 && c.y >= 6 && c.y < 10) {
				throw std::runtime_error{
					"demo 32 pin 1 FAILED: point mover entered TC at ("
					+ std::to_string(c.x) + "," + std::to_string(c.y) + "): "
					+ fmt(wp)};
			}
		}
		log::log(MSG(info) << "demo 32: pin 1 PASS (point mover detoured TC: "
		                   << fmt(wp) << ")");
	}

	// ---- Pin 2: Villager-sized mover (rx=0.2). The Villager
	//              must route around the inflation. ----
	{
		auto grid = std::make_shared<EmptyGrid>();
		std::vector<FootprintBounds> obstacles{tc_bounds()};
		GridAStarPathfinder finder{grid.get(), obstacles,
			MoverFootprint{0.2, 0.2}};
		std::vector<CellPosition> wp;
		auto r = finder.find_path({2, 8}, {14, 8}, wp);
		if (r != GridAStarPathfinder::Result::PATH_FOUND) {
			throw std::runtime_error{
				"demo 32 pin 2 FAILED: Villager detour not found"};
		}
		// Cell-center test against the Villager-inflated bounds
		// [5.8, 10.2) x [5.8, 10.2):
		//   (cx+0.5, cy+0.5) in [5.8, 10.2) x [5.8, 10.2)
		// iff cx in [5, 10] AND cy in [5, 10] AND (cx,cy) != (5,5)
		// and (10,10) corners?  The cell center (5.5, 5.5) IS in
		// [5.8, 10.2)x[5.8,10.2), so cell (5,5) is blocked.
		// Similarly cell center (10.5, 10.5) is in the inflated
		// rect, so cell (10,10) is blocked.
		// Cell (5, 10): center (5.5, 10.5); y=10.5 not in
		// [5.8, 10.2) -- so (5, 10) is walkable.
		// Cell (10, 5): center (10.5, 5.5); x=10.5 not in
		// [5.8, 10.2) -- so (10, 5) is walkable.
		for (const auto &c : wp) {
			bool inside_inflated =
				(static_cast<double>(c.x) + 0.5) >= 5.8
				&& (static_cast<double>(c.x) + 0.5) < 10.2
				&& (static_cast<double>(c.y) + 0.5) >= 5.8
				&& (static_cast<double>(c.y) + 0.5) < 10.2;
			if (inside_inflated) {
				throw std::runtime_error{
					"demo 32 pin 2 FAILED: Villager entered inflated "
					"rect at ("
					+ std::to_string(c.x) + "," + std::to_string(c.y)
					+ "): " + fmt(wp)};
			}
		}
		log::log(MSG(info) << "demo 32: pin 2 PASS (Villager detoured inflated TC: "
		                   << fmt(wp) << ")");
	}

	// ---- Pin 3: detour reached an above-or-below cell. ----
	// (Pin 2 already checked the inflated rect; this verifies the
	//  path didn't squeeze between the wall and the map edge.)
	{
		auto grid = std::make_shared<EmptyGrid>();
		std::vector<FootprintBounds> obstacles{tc_bounds()};
		GridAStarPathfinder finder{grid.get(), obstacles,
			MoverFootprint{0.2, 0.2}};
		std::vector<CellPosition> wp;
		auto r = finder.find_path({2, 8}, {14, 8}, wp);
		if (r != GridAStarPathfinder::Result::PATH_FOUND) {
			throw std::runtime_error{
				"demo 32 pin 3 FAILED: detour not found"};
		}
		bool detoured = false;
		for (const auto &c : wp) {
			if (c.y <= 4 || c.y >= 11) {
				detoured = true;
				break;
			}
		}
		if (not detoured) {
			throw std::runtime_error{
				"demo 32 pin 3 FAILED: Villager did not detour above "
				"or below TC: " + fmt(wp)};
		}
		log::log(MSG(info) << "demo 32: pin 3 PASS (detour reached y<=4 or y>=11)");
	}

	log::log(MSG(info) << "DEMO 32: PASS (Mission 1 TC obstacle routing)");
}


} // namespace openage::gamestate::tests
