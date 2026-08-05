// Copyright 2026 the openage authors. See copying.md for legal info.

#include "gamestate/demo/footprint_demo.h"

#include "log/log.h"
#include "log/message.h"
#include "scenario/placement_validator.h"

#include <memory>
#include <stdexcept>
#include <string>


namespace openage::gamestate::tests {


namespace {

using openage::gamestate::scenario::PlacementFailureReason;
using openage::gamestate::scenario::PlacementRequest;
using openage::gamestate::scenario::PlacementValidator;


PlacementRequest mk_request(entity_id_t id, double cx, double cy,
                            double rx, double ry, bool blocks = true,
                            bool is_static = false) {
	PlacementRequest r;
	r.entity = id;
	r.entity_fqon = "de2_base.data.game_entity.generic.villager.villager.Villager";
	r.scenario_object_id = "TestEntity";
	r.center_x = cx;
	r.center_y = cy;
	r.radius_x = rx;
	r.radius_y = ry;
	r.blocks_placement = blocks;
	r.is_static = is_static;
	return r;
}


}  // namespace


void simulation_demo_26(const util::Path & /*path*/) {
	log::log(MSG(info) << "=== DEMO 26: P4.4B PlacementValidator contract ===");

	PlacementValidator v{0, 0, 16, 16};

	// ---- Pin 1: valid placement is accepted ----
	auto r1 = mk_request(1, 5.0, 4.0, 0.2, 0.2);
	auto out1 = v.validate(r1);
	if (not out1.ok()) {
		throw std::runtime_error{
			"demo 26 pin 1 FAILED: " + out1.reason_string()
			+ " " + out1.context
		};
	}
	log::log(MSG(info) << "demo 26: pin 1 PASS (Villager (5,4) accepted)");

	// ---- Pin 2: invalid footprint (negative radius) rejected ----
	auto r2 = mk_request(2, 8.0, 8.0, -1.0, 0.2);
	auto out2 = v.validate(r2);
	if (out2.reason != PlacementFailureReason::INVALID_FOOTPRINT) {
		throw std::runtime_error{
			"demo 26 pin 2 FAILED: reason=" + out2.reason_string()
		};
	}
	log::log(MSG(info) << "demo 26: pin 2 PASS (negative radius rejected)");

	// ---- Pin 3: out-of-map rejected ----
	auto r3 = mk_request(3, -2.0, 8.0, 0.2, 0.2);
	auto out3 = v.validate(r3);
	if (out3.reason != PlacementFailureReason::OUT_OF_MAP) {
		throw std::runtime_error{
			"demo 26 pin 3 FAILED: reason=" + out3.reason_string()
		};
	}
	log::log(MSG(info) << "demo 26: pin 3 PASS (off-map rejected)");

	// ---- Pin 4: AABB overlap with a static entity -> STATIC_OBSTACLE
	//              (the TC at (8,8) with rx=2 blocks the Villager) ----
	auto r_tc = mk_request(10, 8.0, 8.0, 2.0, 2.0, true, true);
	auto out_tc = v.validate(r_tc);
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

	// ---- Pin 5: a second static (TC-shaped) placement overlapping
	//              the first -> STATIC_OBSTACLE ----
	auto r5 = mk_request(5, 8.0, 8.0, 2.0, 2.0, true, true);
	auto out5 = v.validate(r5);
	if (out5.reason != PlacementFailureReason::STATIC_OBSTACLE) {
		throw std::runtime_error{
			"demo 26 pin 5 FAILED: reason=" + out5.reason_string()
		};
	}
	log::log(MSG(info) << "demo 26: pin 5 PASS (overlapping static rejected)");

	// ---- Pin 6: unit vs unit overlap (non-static) -> ENTITY_OVERLAP ----
	PlacementValidator v2{0, 0, 16, 16};
	auto u1 = mk_request(20, 6.0, 4.0, 0.5, 0.5, true, /*is_static=*/false);
	v2.validate(u1);
	auto u2 = mk_request(21, 6.5, 4.0, 0.5, 0.5, true, /*is_static=*/false);
	auto out_u2 = v2.validate(u2);
	if (out_u2.reason != PlacementFailureReason::ENTITY_OVERLAP) {
		throw std::runtime_error{
			"demo 26 pin 6 FAILED: reason=" + out_u2.reason_string()
		};
	}
	if (out_u2.blocking_entity != 20) {
		throw std::runtime_error{
			"demo 26 pin 6 FAILED: blocker=" + std::to_string(out_u2.blocking_entity)
		};
	}
	log::log(MSG(info) << "demo 26: pin 6 PASS (unit vs unit overlap = ENTITY_OVERLAP)");

	// ---- Pin 7: tangent placement accepted ----
	// TC bounds [6,10)x[6,10); Villager rx=0.2 at (4.8,8) has
	// bounds [4.6,5.0)x[7.8,8.2). max_x=5.0 < min_x_tc=6.0
	// (half-open) -> no overlap.
	auto r7 = mk_request(7, 4.8, 8.0, 0.2, 0.2);
	auto out7 = v.validate(r7);
	if (not out7.ok()) {
		throw std::runtime_error{
			"demo 26 pin 7 FAILED: tangent rejected ("
			+ out7.reason_string() + ")"
		};
	}
	log::log(MSG(info) << "demo 26: pin 7 PASS (tangent accepted)");

	// ---- Pin 8: context string includes FQON + SOID ----
	auto r8 = mk_request(8, 0.0, 0.0, 0.2, 0.2);
	auto out8 = v.validate(r8);
	if (out8.context.find("TestEntity") == std::string::npos
	    || out8.context.find("Villager") == std::string::npos) {
		throw std::runtime_error{
			"demo 26 pin 8 FAILED: context missing FQON/SOID: "
			+ out8.context
		};
	}
	log::log(MSG(info) << "demo 26: pin 8 PASS (error context includes FQON+SOID)");

	// ---- Pin 9: find_nearest_valid is deterministic + finds a
	//              stable resolution. We block the cell (8,8) and
	//              ask for a free placement nearby. ----
	PlacementValidator v3{0, 0, 16, 16};
	v3.validate(mk_request(100, 8.0, 8.0, 2.0, 2.0, true, true));
	auto near = mk_request(99, 8.0, 8.0, 0.2, 0.2);
	auto found = v3.find_nearest_valid(near, 3.0);
	if (not found.has_value()) {
		throw std::runtime_error{
			"demo 26 pin 9 FAILED: no nearest-valid within r=3"
		};
	}
	auto again = v3.find_nearest_valid(near, 3.0);
	if (not again.has_value()
	    || again->first != found->first
	    || again->second != found->second) {
		throw std::runtime_error{
			"demo 26 pin 9 FAILED: nearest-valid non-deterministic"
		};
	}
	log::log(MSG(info) << "demo 26: pin 9 PASS (nearest-valid deterministic)");

	// ---- Pin 10: out-of-map, NOT-OK on probe radius exhaustion ----
	// Take a tight map (0..3) x (0..3) and fill (0,0) with a
	// 1.0-radius blocker. A 0.5-radius probe at (0,0) is
	// covered; a probe at (3,0) is out of map (the rect
	// extends to 3.5 which exceeds the 0..3 extent). find_nearest_valid
	// within r=0 must fail.
	PlacementValidator v4{0, 0, 3, 3};
	v4.validate(mk_request(200, 0.0, 0.0, 1.0, 1.0, true, true));
	auto none = mk_request(201, 0.0, 0.0, 0.5, 0.5);
	auto no_result = v4.find_nearest_valid(none, 0.0);
	if (no_result.has_value()) {
		throw std::runtime_error{
			"demo 26 pin 10 FAILED: expected nullopt when r=0 covers"
		};
	}
	log::log(MSG(info) << "demo 26: pin 10 PASS (r=0 returns nullopt)");

	log::log(MSG(info) << "DEMO 26: PASS (PlacementValidator contract)");
}


} // namespace openage::gamestate::tests