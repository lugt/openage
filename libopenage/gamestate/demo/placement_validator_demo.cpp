// Copyright 2026 the openage authors. See copying.md for legal info.

#include "gamestate/demo/footprint_demo.h"

#include "log/log.h"
#include "log/message.h"
#include "scenario/placement_validator.h"
#include "scenario/static_occupancy_grid.h"

#include <memory>
#include <stdexcept>
#include <string>


namespace openage::gamestate::tests {


namespace {

using openage::gamestate::scenario::CellPosition;
using openage::gamestate::scenario::PlacementFailureReason;
using openage::gamestate::scenario::PlacementRequest;
using openage::gamestate::scenario::PlacementValidator;
using openage::gamestate::scenario::StaticOccupancyGrid;


std::shared_ptr<StaticOccupancyGrid> make_grid(int x0, int y0, int x1, int y1) {
	return std::make_shared<StaticOccupancyGrid>(x0, y0, x1, y1);
}


PlacementRequest mk_request(entity_id_t id, double cx, double cy,
                            double rx, double ry, bool blocks = true) {
	PlacementRequest r;
	r.entity = id;
	r.entity_fqon = "de2_base.data.game_entity.generic.villager.villager.Villager";
	r.scenario_object_id = "TestEntity";
	r.center_x = cx;
	r.center_y = cy;
	r.radius_x = rx;
	r.radius_y = ry;
	r.blocks_placement = blocks;
	return r;
}


}  // namespace


void simulation_demo_26(const util::Path & /*path*/) {
	log::log(MSG(info) << "=== DEMO 26: P4.4B PlacementValidator contract ===");

	auto grid = make_grid(0, 0, 16, 16);
	PlacementValidator v{grid, 0, 0, 16, 16};

	// ---- Pin 1: valid placement is accepted ----
	auto r1 = mk_request(1, 6.0, 8.0, 0.2, 0.2);
	auto out1 = v.validate(r1);
	if (not out1.ok()) {
		throw std::runtime_error{
			"demo 26 pin 1 FAILED: " + out1.reason_string()
			+ " " + out1.context
		};
	}
	log::log(MSG(info) << "demo 26: pin 1 PASS (Villager (6,8) accepted)");

	// ---- Pin 2: invalid footprint (negative radius) is rejected ----
	auto r2 = mk_request(2, 8.0, 8.0, -1.0, 0.2);
	auto out2 = v.validate(r2);
	if (out2.reason != PlacementFailureReason::INVALID_FOOTPRINT) {
		throw std::runtime_error{
			"demo 26 pin 2 FAILED: reason=" + out2.reason_string()
		};
	}
	log::log(MSG(info) << "demo 26: pin 2 PASS (negative radius rejected)");

	// ---- Pin 3: out-of-map placement is rejected ----
	auto r3 = mk_request(3, -2.0, 8.0, 0.2, 0.2);
	auto out3 = v.validate(r3);
	if (out3.reason != PlacementFailureReason::OUT_OF_MAP) {
		throw std::runtime_error{
			"demo 26 pin 3 FAILED: reason=" + out3.reason_string()
		};
	}
	log::log(MSG(info) << "demo 26: pin 3 PASS (off-map rejected)");

	// ---- Pin 4: occupier rejected (with blocker id) ----
	// First accept TC at (8,8), then attempt a Villager whose
	// bounds cross the TC cell. The Villager is small (0.4x0.4)
	// so we must use a 0.5 radius to actually cross cells.
	auto r_tc = mk_request(10, 8.0, 8.0, 2.0, 2.0);
	auto out_tc = v.validate_and_occupy(r_tc);
	if (not out_tc.ok()) {
		throw std::runtime_error{
			"demo 26 pin 4 pre FAILED: " + out_tc.reason_string()
		};
	}
	auto r4 = mk_request(4, 8.0, 8.0, 0.5, 0.5);  // overlaps TC
	auto out4 = v.validate(r4);
	if (out4.reason != PlacementFailureReason::STATIC_OBSTACLE) {
		throw std::runtime_error{
			"demo 26 pin 4 FAILED: reason=" + out4.reason_string()
		};
	}
	if (out4.blocking_entity != 10) {
		throw std::runtime_error{
			"demo 26 pin 4 FAILED: blocker=" + std::to_string(out4.blocking_entity)
		};
	}
	log::log(MSG(info) << "demo 26: pin 4 PASS (Villager (8,8) rejected by TC, blocker=10)");

	// ---- Pin 5: entity overlap (a second TC after the first) ----
	auto r5 = mk_request(5, 8.0, 8.0, 2.0, 2.0);
	auto out5 = v.validate(r5);
	if (out5.reason != PlacementFailureReason::STATIC_OBSTACLE) {
		throw std::runtime_error{
			"demo 26 pin 5 FAILED: reason=" + out5.reason_string()
		};
	}
	log::log(MSG(info) << "demo 26: pin 5 PASS (overlapping TC rejected)");

	// ---- Pin 6: tangent placement is accepted (no overlap) ----
	// TC covers [6,10)x[6,10); Villager(0.2 rx) at (4.8, 8.0) has
	// bounds [4.6, 5.0) x [7.8, 8.2). max_x = 5.0 < min_x_tc = 6.0
	// so the half-open bounds do not overlap.
	auto r6 = mk_request(6, 4.8, 8.0, 0.2, 0.2);
	auto out6 = v.validate(r6);
	if (not out6.ok()) {
		throw std::runtime_error{
			"demo 26 pin 6 FAILED: tangent rejected (" + out6.reason_string() + ")"
		};
	}
	log::log(MSG(info) << "demo 26: pin 6 PASS (tangent placement accepted)");

	// ---- Pin 7: context string includes the entity FQON + ID ----
	auto r7 = mk_request(7, 0.0, 0.0, 0.2, 0.2);
	auto out7 = v.validate(r7);
	if (out7.context.find("TestEntity") == std::string::npos
	    || out7.context.find("Villager") == std::string::npos) {
		throw std::runtime_error{
			"demo 26 pin 7 FAILED: context missing FQON/SOID: "
			+ out7.context
		};
	}
	log::log(MSG(info) << "demo 26: pin 7 PASS (error context includes FQON+SOID)");

	// ---- Pin 8: find_nearest_valid finds a stable resolution ----
	// Take the (8,8) TC (already occupied) and ask for a nearby
	// free placement within radius 3. The stable ring scan must
	// find SOME free cell.
	PlacementRequest near = mk_request(99, 8.0, 8.0, 0.2, 0.2);
	auto found = v.find_nearest_valid(near, 3.0);
	if (not found.has_value()) {
		throw std::runtime_error{
			"demo 26 pin 8 FAILED: no nearest-valid found within r=3"
		};
	}
	// The nearest-valid must NOT overlap the (8,8) TC.
	double ncx = found->first;
	double ncy = found->second;
	// The cell (floor(ncx), floor(ncy)) must NOT be (8,8).
	int ncx_i = static_cast<int>(ncx);
	int ncy_i = static_cast<int>(ncy);
	if (ncx_i == 8 && ncy_i == 8) {
		throw std::runtime_error{
			"demo 26 pin 8 FAILED: nearest-valid resolved inside TC"
		};
	}
	log::log(MSG(info) << "demo 26: pin 8 PASS (nearest-valid=("
	                   << ncx << "," << ncy << "), stable)");

	// ---- Pin 9: same input -> same output (determinism) ----
	auto again = v.find_nearest_valid(near, 3.0);
	if (not again.has_value()
	    || again->first != found->first
	    || again->second != found->second) {
		throw std::runtime_error{
			"demo 26 pin 9 FAILED: nearest-valid non-deterministic"
		};
	}
	log::log(MSG(info) << "demo 26: pin 9 PASS (nearest-valid deterministic)");

	// ---- Pin 10: find_nearest_valid outside max radius returns nullopt ----
	auto too_far = mk_request(98, 0.0, 0.0, 0.2, 0.2);
	// Fill the map so there is no free cell within radius 1.
	for (int y = 0; y < 16; ++y) {
		for (int x = 0; x < 16; ++x) {
			if (std::abs(x) + std::abs(y) <= 1) {
				grid->occupy(1000, openage::gamestate::component::FootprintBounds{
					static_cast<double>(x) - 0.5,
					static_cast<double>(y) - 0.5,
					static_cast<double>(x) + 0.5,
					static_cast<double>(y) + 0.5});
			}
		}
	}
	auto none = v.find_nearest_valid(too_far, 0.5);
	if (none.has_value()) {
		throw std::runtime_error{
			"demo 26 pin 10 FAILED: expected nullopt when no free cell"
		};
	}
	log::log(MSG(info) << "demo 26: pin 10 PASS (no free cell within r=0.5)");

	log::log(MSG(info) << "DEMO 26: PASS (PlacementValidator contract)");
}


} // namespace openage::gamestate::tests