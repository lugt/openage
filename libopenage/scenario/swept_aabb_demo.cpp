// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4F.1 demo 42: DynamicOccupancy runtime + swept AABB
// contract.
//
// Pins:
//   1. swept_aabb(start, end) = union of the two AABBs.
//   2. segment_overlaps returns true iff the swept AABB
//      intersects obstacle.
//   3. A diagonal pass-through (e.g. A -> B with C in
//      between) is detected by the swept AABB (not
//      just endpoint check).
//   4. Two entities moving the same direction do not
//      produce a false positive (swept AABB only flags
//      actual overlap).
//   5. The DynamicOccupancy can_occupy check respects the
//      swept AABB -- a fast-moving unit's path through
//      another unit's cell is blocked even if the
//      endpoints don't overlap.

#include "gamestate/demo/footprint_demo.h"

#include "scenario/dynamic_occupancy.h"
#include "scenario/swept_aabb.h"

#include "log/log.h"
#include "log/message.h"

#include <cmath>
#include <stdexcept>
#include <string>


namespace openage::gamestate::tests {


namespace {


openage::gamestate::component::FootprintBounds
rect(double cx, double cy, double rx, double ry) {
	openage::gamestate::component::FootprintBounds b;
	b.min_x = cx - rx;
	b.min_y = cy - ry;
	b.max_x = cx + rx;
	b.max_y = cy + ry;
	return b;
}


}  // namespace


void simulation_demo_42(const util::Path & /*path*/) {
	log::log(MSG(info) << "=== DEMO 42: P4.4F.1 swept AABB + DynamicOccupancy runtime ===");

	using openage::gamestate::component::FootprintBounds;
	using openage::scenario::swept_aabb;
	using openage::scenario::segment_overlaps;

	// --- Pin 1: swept_aabb = union of two AABBs. ---
	{
		auto a = rect(0.0, 0.0, 0.5, 0.5);
		auto b = rect(2.0, 0.0, 0.5, 0.5);
		auto s = swept_aabb(a, b);
		// The union spans x = -0.5..2.5, y = -0.5..0.5.
		if (std::abs(s.min_x - (-0.5)) > 1e-9
		    or std::abs(s.max_x - 2.5) > 1e-9
		    or std::abs(s.min_y - (-0.5)) > 1e-9
		    or std::abs(s.max_y - 0.5) > 1e-9) {
			throw std::runtime_error{
				"demo 42 pin 1 FAILED: swept_aabb wrong"};
		}
		log::log(MSG(info) << "demo 42: pin 1 PASS (swept_aabb = union)");
	}

	// --- Pin 2: segment_overlaps basic positive case. ---
	{
		auto a = rect(0.0, 0.0, 0.4, 0.4);
		auto b = rect(1.0, 0.0, 0.4, 0.4);
		auto obs = rect(0.5, 0.0, 0.1, 0.1);
		if (not segment_overlaps(a, b, obs)) {
			throw std::runtime_error{
				"demo 42 pin 2 FAILED: should overlap"};
		}
		log::log(MSG(info) << "demo 42: pin 2 PASS (overlap detected)");
	}

	// --- Pin 3: pass-through case. A unit moves from
	//              (0,0) to (2,0); an obstacle at (1, 0)
	//              with rx=ry=0.1 is on the path. The
	//              swept AABB (x = -0.4..2.4) contains the
	//              obstacle, so segment_overlaps must
	//              return true. An endpoint-only check
	//              (a vs b) would NOT detect the obstacle
	//              because the obstacle's x = 0.9..1.1
	//              does not overlap a (x=-0.4..0.4) or b
	//              (x=1.6..2.4). ----
	{
		auto a = rect(0.0, 0.0, 0.4, 0.4);
		auto b = rect(2.0, 0.0, 0.4, 0.4);
		auto obs = rect(1.0, 0.0, 0.1, 0.1);
		// Verify endpoint-only: a and b don't overlap obs.
		if (a.min_x < obs.max_x && obs.min_x < a.max_x) {
			throw std::runtime_error{
				"demo 42 pin 3 FAILED: endpoint a should "
				"not overlap obs"};
		}
		if (b.min_x < obs.max_x && obs.min_x < b.max_x) {
			throw std::runtime_error{
				"demo 42 pin 3 FAILED: endpoint b should "
				"not overlap obs"};
		}
		// Swept should overlap.
		if (not segment_overlaps(a, b, obs)) {
			throw std::runtime_error{
				"demo 42 pin 3 FAILED: swept AABB should "
				"catch pass-through"};
		}
		log::log(MSG(info) << "demo 42: pin 3 PASS (pass-through detected)");
	}

	// --- Pin 4: no false positive when entities move the
	//              same direction with non-overlapping
	//              paths. A at (0,0) moving to (1,0); B
	//              at (3,0) stationary. A's swept AABB
	//              does NOT touch B. ----
	{
		auto a = rect(0.0, 0.0, 0.4, 0.4);
		auto b = rect(1.0, 0.0, 0.4, 0.4);
		auto obs = rect(3.0, 0.0, 0.4, 0.4);
		if (segment_overlaps(a, b, obs)) {
			throw std::runtime_error{
				"demo 42 pin 4 FAILED: false positive"};
		}
		log::log(MSG(info) << "demo 42: pin 4 PASS (no false positive)");
	}

	// --- Pin 5: DynamicOccupancy runtime check. A is
	//              registered; B's swept AABB would
	//              pass through A; DynamicOccupancy should
	//              report B as a blocker. ----
	{
		openage::scenario::DynamicOccupancy occ;
		auto a_start = rect(2.0, 0.0, 0.4, 0.4);
		occ.register_entity(100u, a_start);
		// B starts at (0, 0) and moves to (3, 0). Its
		// swept AABB overlaps A's bounds.
		auto b_start = rect(0.0, 0.0, 0.4, 0.4);
		auto b_end = rect(3.0, 0.0, 0.4, 0.4);
		auto b_swept = swept_aabb(b_start, b_end);
		// Check the occupancy at the swept bounds, not
		// just the endpoint.
		auto blockers = occ.query(200u, b_swept);
		if (std::find(blockers.begin(), blockers.end(), 100u)
		    == blockers.end()) {
			throw std::runtime_error{
				"demo 42 pin 5 FAILED: A not in blockers "
				"for B's swept AABB"};
		}
		log::log(MSG(info) << "demo 42: pin 5 PASS "
		                   << "(swept query catches pass-through)");
	}

	// --- Pin 6: half-open AABB semantics. Adjacent
	//              bounds with disjoint interiors do NOT
	//              overlap. (Use a third AABB that is
	//              outside the swept segment.) ----
	{
		auto a = rect(0.0, 0.0, 0.5, 0.5);
		auto b = rect(1.0, 0.0, 0.5, 0.5);
		auto obs = rect(5.0, 5.0, 0.5, 0.5);
		// The swept AABB of (a, b) is [-0.5, 1.5) x
		// [-0.5, 0.5). The obstacle at (5, 5) is well
		// outside this range. segment_overlaps must
		// return false.
		if (segment_overlaps(a, b, obs)) {
			throw std::runtime_error{
				"demo 42 pin 6 FAILED: false positive "
				"on far-away obstacle"};
		}
		log::log(MSG(info) << "demo 42: pin 6 PASS (no false positive)");
	}

	log::log(MSG(info) << "DEMO 42: PASS (swept AABB + DynamicOccupancy runtime)");
}


}  // namespace openage::gamestate::tests
