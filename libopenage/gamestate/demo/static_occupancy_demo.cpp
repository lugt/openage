// Copyright 2026 the openage authors. See copying.md for legal info.

#include "gamestate/demo/footprint_demo.h"

#include "gamestate/component/api/footprint.h"
#include "log/log.h"
#include "log/message.h"
#include "scenario/static_occupancy_grid.h"

#include <memory>
#include <stdexcept>
#include <string>


namespace openage::gamestate::tests {


namespace {

using openage::gamestate::component::Footprint;
using openage::gamestate::component::FootprintBounds;
using openage::gamestate::scenario::CellPosition;
using openage::gamestate::scenario::StaticOccupancyGrid;


// Build a 4x4-tile footprint centered at (cx, cy).
FootprintBounds rect4(double cx, double cy) {
	FootprintBounds b;
	b.min_x = cx - 2.0;
	b.min_y = cy - 2.0;
	b.max_x = cx + 2.0;
	b.max_y = cy + 2.0;
	return b;
}


// Build a small (rx, ry)-half-extent rectangle centered at
// (cx, cy). Used for unit footprints whose bounds cover only
// their center cell under the cell-center containment rule.
FootprintBounds rect_small(double cx, double cy, double rx, double ry) {
	FootprintBounds b;
	b.min_x = cx - rx;
	b.min_y = cy - ry;
	b.max_x = cx + rx;
	b.max_y = cy + ry;
	return b;
}


}  // namespace


void simulation_demo_27(const util::Path & /*path*/) {
	log::log(MSG(info) << "=== DEMO 27: P4.4B StaticOccupancyGrid contract ===");

	// 16x16 map.
	auto grid = std::make_shared<StaticOccupancyGrid>(0, 0, 16, 16);

	// ---- Pin 1: out-of-map = blocked ----
	if (not grid->is_blocked(CellPosition{-1, 8})) {
		throw std::runtime_error{"demo 27 pin 1 FAILED: out-of-map not blocked"};
	}
	if (not grid->is_blocked(CellPosition{16, 8})) {
		throw std::runtime_error{"demo 27 pin 1 FAILED: out-of-map high not blocked"};
	}
	log::log(MSG(info) << "demo 27: pin 1 PASS (out-of-map blocked)");

	// ---- Pin 2: TC (rx=2, ry=2) at (8,8) blocks the 4x4 cell
	//              area (6,6)..(9,9) ----
	auto conf = grid->occupy(100, rect4(8.0, 8.0));
	if (conf.has_value()) {
		throw std::runtime_error{
			"demo 27 pin 2 FAILED: TC occupy reported conflict"
		};
	}
	for (int y = 6; y <= 9; ++y) {
		for (int x = 6; x <= 9; ++x) {
			if (not grid->is_blocked(CellPosition{x, y})) {
				throw std::runtime_error{
					"demo 27 pin 2 FAILED: cell (" + std::to_string(x)
					+ "," + std::to_string(y) + ") not blocked"
				};
			}
			auto b = grid->blocker_of(CellPosition{x, y});
			if (not b.has_value() || *b != 100) {
				throw std::runtime_error{
					"demo 27 pin 2 FAILED: cell (" + std::to_string(x)
					+ "," + std::to_string(y) + ") wrong blocker"
				};
			}
		}
	}
	log::log(MSG(info) << "demo 27: pin 2 PASS (TC blocks 16 cells, source=100)");

	// ---- Pin 3: cells just outside the TC are walkable ----
	for (int y = 6; y <= 9; ++y) {
		// x = 5 (left column) and x = 10 (right column) are
		// outside the TC's [6,10) bounds.
		if (grid->is_blocked(CellPosition{5, y})) {
			throw std::runtime_error{
				"demo 27 pin 3 FAILED: (5," + std::to_string(y) + ") blocked"
			};
		}
		if (grid->is_blocked(CellPosition{10, y})) {
			throw std::runtime_error{
				"demo 27 pin 3 FAILED: (10," + std::to_string(y) + ") blocked"
			};
		}
	}
	log::log(MSG(info) << "demo 27: pin 3 PASS (adjacent cells walkable)");

	// ---- Pin 4: a second occupy with a different entity reports
	//              a conflict (rejected) ----
	auto conf2 = grid->occupy(200, rect4(8.0, 8.0));
	if (not conf2.has_value()) {
		throw std::runtime_error{
			"demo 27 pin 4 FAILED: overlap not detected"
		};
	}
	if (*conf2 != 100) {
		throw std::runtime_error{
			"demo 27 pin 4 FAILED: wrong blocker=" + std::to_string(*conf2)
		};
	}
	log::log(MSG(info) << "demo 27 pin 4 PASS (overlap rejected, blocker=100)");

	// ---- Pin 5: re-occupy with the SAME entity is a no-op ----
	auto conf3 = grid->occupy(100, rect4(8.0, 8.0));
	if (conf3.has_value()) {
		throw std::runtime_error{
			"demo 27 pin 5 FAILED: same-entity re-occupy returned conflict"
		};
	}
	log::log(MSG(info) << "demo 27 pin 5 PASS (same-entity re-occupy is no-op)");

	// ---- Pin 6: release clears the cells ----
	grid->release(100);
	for (int y = 6; y <= 9; ++y) {
		for (int x = 6; x <= 9; ++x) {
			if (grid->is_blocked(CellPosition{x, y})) {
				throw std::runtime_error{
					"demo 27 pin 6 FAILED: cell ("
					+ std::to_string(x) + "," + std::to_string(y)
					+ ") still blocked after release"
				};
			}
		}
	}
	log::log(MSG(info) << "demo 27 pin 6 PASS (release clears all 16 cells)");

	// ---- Pin 7: the tc_obstacle layout's straight line crosses
	//              the TC footprint. Walk the integer cells from
	//              Villager(2,8) to Enemy(12,8) and assert that
	//              at least one cell on the line is inside the
	//              TC's blocked area. (We re-occupy TC first.) ----
	grid->occupy(100, rect4(8.0, 8.0));
	bool found_blocked = false;
	for (int x = 2; x <= 12; ++x) {
		if (grid->is_blocked(CellPosition{x, 8})) {
			found_blocked = true;
			break;
		}
	}
	if (not found_blocked) {
		throw std::runtime_error{
			"demo 27 pin 7 FAILED: straight line Villager(2,8)->Enemy(12,8) "
			"does not cross any TC-blocked cell"
		};
	}
	// And the start/end are NOT inside TC.
	if (grid->is_blocked(CellPosition{2, 8})) {
		throw std::runtime_error{
			"demo 27 pin 7 FAILED: Villager (2,8) should be clear"
		};
	}
	if (grid->is_blocked(CellPosition{12, 8})) {
		throw std::runtime_error{
			"demo 27 pin 7 FAILED: Enemy (12,8) should be clear"
		};
	}
	log::log(MSG(info) << "demo 27: pin 7 PASS (tc_obstacle line crosses blocked cells, start+end clear)");

	// ---- Pin 8: a dynamic unit (Villager, 0.2 radius) is NOT
	//              permanently written to the static grid ----
	// Per spec §6: "不把移动单位永久写入静态导航网格" (do not
	// permanently write moving units to the static nav grid).
	// Under the cell-center containment rule, a 0.2-radius
	// Villager covers ZERO cells (its bounds are smaller than
	// half a cell, so no cell center falls inside). This is the
	// correct static-grid semantics: the grid records buildings,
	// not units. The pathfinder (P4.4C) handles dynamic
	// unit-unit avoidance.
	auto vconf = grid->occupy(300, rect_small(4.0, 8.0, 0.2, 0.2));
	if (vconf.has_value()) {
		throw std::runtime_error{
			"demo 27 pin 8 FAILED: Villager occupy reported conflict"
		};
	}
	if (grid->is_blocked(CellPosition{4, 8})) {
		throw std::runtime_error{
			"demo 27 pin 8 FAILED: sub-tile Villager (rx=0.2) should "
			"cover 0 cells under cell-center containment; cell (4,8) "
			"is incorrectly marked blocked"
		};
	}
	// For sanity: a LARGER unit (rx=0.6) at the same center
	// covers its center cell.
	auto vconf2 = grid->occupy(301, rect_small(4.0, 8.0, 0.6, 0.6));
	if (vconf2.has_value()) {
		throw std::runtime_error{
			"demo 27 pin 8 FAILED: large Villager occupy reported conflict"
		};
	}
	if (not grid->is_blocked(CellPosition{4, 8})) {
		throw std::runtime_error{
			"demo 27 pin 8 FAILED: large Villager (rx=0.6) should cover (4,8)"
		};
	}
	log::log(MSG(info) << "demo 27: pin 8 PASS (sub-tile unit: 0 cells; large unit: center cell)");

	log::log(MSG(info) << "DEMO 27: PASS (StaticOccupancyGrid contract)");
}


} // namespace openage::gamestate::tests
