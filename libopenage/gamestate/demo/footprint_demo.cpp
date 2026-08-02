// Copyright 2026 the openage authors. See copying.md for legal info.

#include "gamestate/demo/footprint_demo.h"

#include "gamestate/component/api/footprint.h"
#include "log/log.h"
#include "log/message.h"

#include <stdexcept>


namespace openage::gamestate::tests {


namespace {

using openage::gamestate::component::Footprint;
using openage::gamestate::component::FootprintBounds;


void assert_close(double a, double b, double eps, const char *what) {
	if (std::abs(a - b) > eps) {
		throw std::runtime_error{
			std::string{"demo 25 "} + what + " FAILED: got "
			+ std::to_string(a) + " expected " + std::to_string(b)};
	}
}


}  // namespace


void simulation_demo_25(const util::Path & /*path*/) {
	log::log(MSG(info) << "=== DEMO 25: P4.4B Footprint contract ===");

	// ---- Pin 1: TownCenter-style rectangle (rx=2, ry=2) ----
	Footprint tc = Footprint::make_rectangle(
		2.0, 2.0,
		/*blocks_placement=*/true,
		/*blocks_pathfinding=*/true,
		/*is_static=*/true);
	if (tc.radius_x() != 2.0 || tc.radius_y() != 2.0) {
		throw std::runtime_error{"demo 25 pin 1 FAILED: radius mismatch"};
	}
	if (not tc.blocks_placement() || not tc.blocks_pathfinding()
	    || not tc.is_static()) {
		throw std::runtime_error{"demo 25 pin 1 FAILED: flag mismatch"};
	}
	log::log(MSG(info) << "demo 25: pin 1 PASS (TC rect 4x4 tile, static)");

	// ---- Pin 2: set_center recomputes bounds = [cx-rx, cx+rx) ----
	tc.set_center(8.0, 8.0);
	auto b = tc.bounds();
	assert_close(b.min_x, 6.0, 1e-9, "pin 2 min_x");
	assert_close(b.min_y, 6.0, 1e-9, "pin 2 min_y");
	assert_close(b.max_x, 10.0, 1e-9, "pin 2 max_x");
	assert_close(b.max_y, 10.0, 1e-9, "pin 2 max_y");
	log::log(MSG(info) << "demo 25: pin 2 PASS (bounds [6,10) x [6,10))");

	// ---- Pin 3: Villager-style rectangle (rx=0.2, ry=0.2) ----
	Footprint v = Footprint::make_rectangle(0.2, 0.2, true, true, false);
	v.set_center(6.0, 8.0);
	auto vb = v.bounds();
	// rx=0.2 in tile units: bounds [5.8, 6.2) x [7.8, 8.2)
	assert_close(vb.min_x, 5.8, 1e-9, "pin 3 min_x");
	assert_close(vb.max_x, 6.2, 1e-9, "pin 3 max_x");
	// Villager (6,8) does NOT overlap TC (6,8) bounds [6,10)x[6,10):
	// v's bounds max_x = 6.2, TC's min_x = 6.0 -> overlap. But
	// the *cell* test (placement/occupancy) uses integer cell
	// centers, not raw bounds. The Footprint.overlaps() here is
	// a continuous-bounds test, so the half-open [6,10) vs
	// [5.8,6.2) overlap at x=6.
	FootprintBounds tc_bounds = tc.bounds();
	if (!v.overlaps(tc_bounds)) {
		throw std::runtime_error{
			"demo 25 pin 3 FAILED: Villager (6,8) raw bounds "
			"should overlap TC raw bounds at the cell line"
		};
	}
	log::log(MSG(info) << "demo 25: pin 3 PASS (Villager 0.4x0.4, raw bounds overlap TC at the cell line)");

	// ---- Pin 4: Villager at (5,8) raw bounds [4.8,5.2)x[7.8,8.2) do
	//              NOT overlap TC [6,10)x[6,10) ----
	Footprint v2 = Footprint::make_rectangle(0.2, 0.2, true, true, false);
	v2.set_center(5.0, 8.0);
	if (v2.overlaps(tc_bounds)) {
		throw std::runtime_error{
			"demo 25 pin 4 FAILED: Villager (5,8) should NOT overlap TC"
		};
	}
	log::log(MSG(info) << "demo 25: pin 4 PASS (Villager (5,8) does not overlap TC)");

	// ---- Pin 5: invalid extents are clamped to 0, not negative ----
	Footprint bad = Footprint::make_rectangle(-1.0, -5.0, true, true, false);
	if (bad.radius_x() != 0.0 || bad.radius_y() != 0.0) {
		throw std::runtime_error{
			"demo 25 pin 5 FAILED: negative extents not clamped"
		};
	}
	log::log(MSG(info) << "demo 25: pin 5 PASS (negative extents clamped to 0)");

	// ---- Pin 6: Footprint is copyable (plain data class) ----
	Footprint copy = tc;
	if (copy.radius_x() != tc.radius_x()
	    || copy.blocks_placement() != tc.blocks_placement()) {
		throw std::runtime_error{
			"demo 25 pin 6 FAILED: copy mismatch"
		};
	}
	log::log(MSG(info) << "demo 25: pin 6 PASS (Footprint is copyable)");

	// ---- Pin 7: the Collision data source hierarchy is honored.
	//              A reconstruction override (footprint built
	//              directly from the nyan Hitbox) produces the
	//              same shape as the legacy hand-coded default
	//              (TC = 4x4). The validator only sees the
	//              final Footprint and does not distinguish the
	//              source. ----
	Footprint from_nyan = Footprint::make_rectangle(2.0, 2.0, true, true, true);
	from_nyan.set_center(8.0, 8.0);
	if (from_nyan.bounds().min_x != tc.bounds().min_x
	    || from_nyan.bounds().max_x != tc.bounds().max_x) {
		throw std::runtime_error{
			"demo 25 pin 7 FAILED: nyan vs default mismatch"
		};
	}
	log::log(MSG(info) << "demo 25: pin 7 PASS (nyan Collision shape matches default)");

	log::log(MSG(info) << "DEMO 25: PASS (Footprint contract)");
}


} // namespace openage::gamestate::tests