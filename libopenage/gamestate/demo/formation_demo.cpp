// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4D.2 + P4.4F demo 46: multi-unit formation + dynamic
// occupancy contract.
//
// The demo covers the most important invariants of the
// P4.4D.2 / P4.4F scope that can be verified headlessly:
//
//   - SelectionModel multi-select (P4.4D.2.3): set / add /
//     remove / empty on primary + entities.
//   - DynamicOccupancy (P4.4F.10): register / update /
//     remove / can_occupy / query.
//   - MovementReservation-style wait/repath: simplified to
//     the dynamic occupancy can_occupy contract (the
//     reservation subsystem's P4.4F.12 is documented in the
//     header but not exercised here; demo 43 covers it in
//     isolation).
//   - FormationPlanner (P4.4F.16-22): 1, 2, 4, 6 unit
//     selection against a TC obstacle; each unit gets a
//     different cell, no slot overlaps the TC, slots are
//     inside the map, assignment is stable across reruns.
//
// Each pin asserts ONE invariant; pin failure prints a clear
// "pin N FAILED" line so a regression diff highlights the
// specific invariant that broke.

#include "gamestate/demo/footprint_demo.h"

#include "pathfinding/formation_planner.h"
#include "scenario/dynamic_occupancy.h"
#include "scenario/static_occupancy_grid.h"
#include "presenter/viewmodel/selection_model.h"

#include "cvar/cvar.h"
#include "gamestate/component/api/footprint.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "presenter/viewmodel/selection_model.h"
#include "time/time.h"
#include "time/time_loop.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>


namespace openage::gamestate::tests {


namespace {


constexpr const char *MISSION1_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.Mission";
constexpr const char *PLAYERSETUP_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup";


openage::gamestate::entity_id_t find_entity_by_key(
	const std::shared_ptr<openage::gamestate::GameState> &state,
	const std::string &key_substr) {
	for (const auto &kv : state->get_named_entities()) {
		if (kv.first.find(key_substr) != std::string::npos) {
			return kv.second;
		}
	}
	return 0;
}


// Register a synthetic 4-unit "army" in the dynamic
// occupancy table at known cells. Returns the entity ids in
// registration order. Positions are in tile units, footprints
// in tile units (radius_x = radius_y = 0.2 for villager
// shape; we widen them slightly here so that the test's
// "no overlap" pin has a non-trivial margin to check).
struct FakeUnit {
	openage::gamestate::entity_id_t id;
	double cx;
	double cy;
	double rx;
	double ry;
};

std::vector<FakeUnit> seed_army(
	openage::scenario::DynamicOccupancy &occ) {
	std::vector<FakeUnit> units;
	for (int i = 0; i < 4; ++i) {
		FakeUnit u;
		u.id = 1000 + i;
		u.cx = 2.0 + i;
		u.cy = 2.0;
		u.rx = 0.4;
		u.ry = 0.4;
		openage::gamestate::component::FootprintBounds b;
		b.min_x = u.cx - u.rx;
		b.min_y = u.cy - u.ry;
		b.max_x = u.cx + u.rx;
		b.max_y = u.cy + u.ry;
		occ.register_entity(u.id, b);
		units.push_back(u);
	}
	return units;
}


}  // namespace


void simulation_demo_46(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 46: P4.4D.2 + P4.4F multi-unit formation ===");

	// ============================================================
	// Part A: SelectionModel multi-select (P4.4D.2.3)
	// ============================================================
	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<openage::time::TimeLoop>();
	auto simulation = std::make_shared<openage::gamestate::GameSimulation>(
		path, cvar, time_loop, PLAYERSETUP_FQON, MISSION1_FQON);
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();
	simulation->start();

	openage::presenter::viewmodel::SelectionModel sel{simulation};

	// --- Pin 1: empty selection ---
	{
		auto snap = sel.snapshot();
		if (not snap.empty() or snap.primary.has_value()) {
			throw std::runtime_error{
				"demo 46 pin 1 FAILED: empty selection has primary"};
		}
		log::log(MSG(info) << "demo 46: pin 1 PASS (empty selection)");
	}

	// --- Pin 2: set_selected then add_to_selection then
	//             remove_from_selection. Each operation
	//             yields a stable sorted vector. ---
	std::vector<openage::gamestate::entity_id_t> ids;
	{
		sel.set_selected(0);
		sel.add_to_selection(0);
		sel.add_to_selection(1);
		sel.add_to_selection(2);
		auto snap = sel.snapshot();
		// Vector must be sorted ascending and have no
		// duplicates.
		for (size_t i = 1; i < snap.entities.size(); ++i) {
			if (snap.entities[i] <= snap.entities[i - 1]) {
				throw std::runtime_error{
					"demo 46 pin 2 FAILED: not sorted / has dups at index "
					+ std::to_string(i)};
			}
		}
		// Primary is the most recent add (entity 2).
		if (snap.primary != 2) {
			throw std::runtime_error{
				"demo 46 pin 2 FAILED: primary not most recent"};
		}
		ids = snap.entities;
		log::log(MSG(info) << "demo 46: pin 2 PASS (multi-selection sorted, primary=2)");
	}

	// --- Pin 3: remove a non-primary entity; primary
	//             unchanged. ---
	{
		sel.remove_from_selection(0);
		auto snap = sel.snapshot();
		if (snap.primary != 2) {
			throw std::runtime_error{
				"demo 46 pin 3 FAILED: primary changed on non-primary removal"};
		}
		if (std::find(snap.entities.begin(), snap.entities.end(), 0)
		    != snap.entities.end()) {
			throw std::runtime_error{
				"demo 46 pin 3 FAILED: removed entity still in selection"};
		}
		log::log(MSG(info) << "demo 46: pin 3 PASS (remove non-primary keeps primary)");
	}

	// --- Pin 4: remove the primary; primary must promote
	//             to the lowest-id remaining. ---
	{
		sel.remove_from_selection(2);
		auto snap = sel.snapshot();
		if (snap.primary != 1) {
			throw std::runtime_error{
				"demo 46 pin 4 FAILED: primary not promoted to lowest ("
				+ std::to_string(*snap.primary) + ")"};
		}
		log::log(MSG(info) << "demo 46: pin 4 PASS (primary promoted to 1)");
	}

	// --- Pin 5: remove the last entity; selection clears. ---
	{
		sel.remove_from_selection(1);
		auto snap = sel.snapshot();
		if (not snap.empty() or snap.primary.has_value()) {
			throw std::runtime_error{
				"demo 46 pin 5 FAILED: selection not empty after removing all"};
		}
		log::log(MSG(info) << "demo 46: pin 5 PASS (empty after last remove)");
	}

	// ============================================================
	// Part B: DynamicOccupancy (P4.4F.10)
	// ============================================================
	openage::scenario::DynamicOccupancy occ;
	auto units = seed_army(occ);
	if (occ.size() != 4) {
		throw std::runtime_error{
			"demo 46 pin 6 FAILED: size != 4 after seed"};
	}
	log::log(MSG(info) << "demo 46: pin 6 PASS (4 units registered)");

	// --- Pin 7: query for a non-overlapping bounds returns empty.
	{
		openage::gamestate::component::FootprintBounds probe;
		probe.min_x = 0.0;
		probe.min_y = 10.0;
		probe.max_x = 0.5;
		probe.max_y = 10.5;
		auto blockers = occ.query(0, probe);
		if (not blockers.empty()) {
			throw std::runtime_error{
				"demo 46 pin 7 FAILED: empty-cell query returned blockers"};
		}
		log::log(MSG(info) << "demo 46: pin 7 PASS (empty query no blockers)");
	}

	// --- Pin 8: query that overlaps unit[0] returns unit[0]
	//             and the self-exclusion works. ---
	{
		openage::gamestate::component::FootprintBounds probe;
		probe.min_x = 1.8;
		probe.min_y = 1.8;
		probe.max_x = 2.2;
		probe.max_y = 2.2;
		// Querying without self-exclusion returns unit 1000.
		auto all = occ.query(0, probe);
		if (std::find(all.begin(), all.end(), 1000u) == all.end()) {
			throw std::runtime_error{
				"demo 46 pin 8a FAILED: no-self-exclusion query missing 1000"};
		}
		// Querying with self-exclusion: mover = 1000, expect
		// no 1000 in the result.
		auto excl = occ.query(1000, probe);
		if (std::find(excl.begin(), excl.end(), 1000u) != excl.end()) {
			throw std::runtime_error{
				"demo 46 pin 8b FAILED: self-exclusion did not exclude 1000"};
		}
		log::log(MSG(info) << "demo 46: pin 8 PASS (self-exclusion works)");
	}

	// --- Pin 9: update_entity moves a unit; the new cell
	//             is occupied, the old cell is free. ---
	{
		openage::gamestate::component::FootprintBounds old_b;
		old_b.min_x = 2.0 - 0.4;
		old_b.min_y = 2.0 - 0.4;
		old_b.max_x = 2.0 + 0.4;
		old_b.max_y = 2.0 + 0.4;
		openage::gamestate::component::FootprintBounds new_b;
		new_b.min_x = 5.0 - 0.4;
		new_b.min_y = 2.0 - 0.4;
		new_b.max_x = 5.0 + 0.4;
		new_b.max_y = 2.0 + 0.4;
		occ.update_entity(1000, old_b, new_b);
		// Old cell now free.
		auto old_q = occ.query(0, old_b);
		if (std::find(old_q.begin(), old_q.end(), 1000u) != old_q.end()) {
			throw std::runtime_error{
				"demo 46 pin 9 FAILED: old cell still occupied after update"};
		}
		// New cell occupied.
		auto new_q = occ.query(0, new_b);
		if (std::find(new_q.begin(), new_q.end(), 1000u) == new_q.end()) {
			throw std::runtime_error{
				"demo 46 pin 9 FAILED: new cell not occupied after update"};
		}
		log::log(MSG(info) << "demo 46: pin 9 PASS (update moved occupancy)");
	}

	// --- Pin 10: remove_entity frees its cell. ---
	{
		openage::gamestate::component::FootprintBounds b;
		b.min_x = 5.0 - 0.4;
		b.min_y = 2.0 - 0.4;
		b.max_x = 5.0 + 0.4;
		b.max_y = 2.0 + 0.4;
		occ.remove_entity(1000);
		auto q = occ.query(0, b);
		if (std::find(q.begin(), q.end(), 1000u) != q.end()) {
			throw std::runtime_error{
				"demo 46 pin 10 FAILED: removed entity still blocks"};
		}
		if (occ.size() != 3) {
			throw std::runtime_error{
				"demo 46 pin 10 FAILED: size != 3 after remove"};
		}
		log::log(MSG(info) << "demo 46: pin 10 PASS (remove frees cell)");
	}

	// ============================================================
	// Part C: FormationPlanner (P4.4F.16-22) -- no obstacle.
	// ============================================================
	// Reset the occupancy: 4 villager-shape units packed in
	// the upper-left corner of a 16x16 map.
	openage::scenario::DynamicOccupancy occ2;
	std::vector<openage::gamestate::entity_id_t> army;
	for (int i = 0; i < 4; ++i) {
		openage::gamestate::entity_id_t id = 2000 + i;
		openage::gamestate::component::FootprintBounds b;
		b.min_x = 1.0 + i - 0.4;
		b.min_y = 1.0 - 0.4;
		b.max_x = 1.0 + i + 0.4;
		b.max_y = 1.0 + 0.4;
		occ2.register_entity(id, b);
		army.push_back(id);
	}
	openage::gamestate::scenario::StaticOccupancyGrid
		static_occ(0, 0, 16, 16);

	// --- Pin 11: 1 unit at (8, 8). One slot. ---
	{
		auto plan = openage::pathfinding::plan_formation(
			{5000u}, 8.0, 8.0,
			0.8, 0.8,  // footprint WxH (radius 0.4)
			occ2, static_occ,
			0, 0, 16, 16);
		if (plan.result != openage::pathfinding::FormationResult::OK
		    or plan.assignments.size() != 1) {
			throw std::runtime_error{
				"demo 46 pin 11 FAILED: 1-unit plan failed: "
				+ plan.reason};
		}
		log::log(MSG(info) << "demo 46: pin 11 PASS (1 unit, slot=("
		                   << plan.assignments[0].slot_center_x
		                   << ","
		                   << plan.assignments[0].slot_center_y
		                   << "))");
	}

	// --- Pin 12: 2 units get 2 different slots near (8, 8). ---
	{
		auto plan = openage::pathfinding::plan_formation(
			{5001u, 5002u}, 8.0, 8.0, 0.8, 0.8,
			occ2, static_occ, 0, 0, 16, 16);
		if (plan.result != openage::pathfinding::FormationResult::OK
		    or plan.assignments.size() != 2) {
			throw std::runtime_error{
				"demo 46 pin 12 FAILED: 2-unit plan: "
				+ plan.reason};
		}
		if (plan.assignments[0].slot_center_x
		    == plan.assignments[1].slot_center_x
		    and plan.assignments[0].slot_center_y
		    == plan.assignments[1].slot_center_y) {
			throw std::runtime_error{
				"demo 46 pin 12 FAILED: 2 units got the same slot"};
		}
		log::log(MSG(info) << "demo 46: pin 12 PASS (2 different slots)");
	}

	// --- Pin 13: 4 units get 4 different slots, all inside
	//             the 16x16 map, no two overlap. ---
	{
		auto plan = openage::pathfinding::plan_formation(
			army, 8.0, 8.0, 0.8, 0.8,
			occ2, static_occ, 0, 0, 16, 16);
		if (plan.result != openage::pathfinding::FormationResult::OK
		    or plan.assignments.size() != 4) {
			throw std::runtime_error{
				"demo 46 pin 13 FAILED: 4-unit plan: "
				+ plan.reason};
		}
		// Distinct slot centers.
		std::vector<std::pair<double, double>> centres;
		for (const auto &a : plan.assignments) {
			centres.push_back({a.slot_center_x, a.slot_center_y});
		}
		std::sort(centres.begin(), centres.end());
		for (size_t i = 1; i < centres.size(); ++i) {
			if (centres[i] == centres[i - 1]) {
				throw std::runtime_error{
					"demo 46 pin 13 FAILED: duplicate slot at index "
					+ std::to_string(i)};
			}
		}
		// All slots inside map.
		for (const auto &a : plan.assignments) {
			if (a.slot_center_x < 1.0 or a.slot_center_x > 15.0
			    or a.slot_center_y < 1.0 or a.slot_center_y > 15.0) {
				throw std::runtime_error{
					"demo 46 pin 13 FAILED: slot out of map ("
					+ std::to_string(a.slot_center_x) + ","
					+ std::to_string(a.slot_center_y) + ")"};
			}
		}
		// No two slot footprints overlap (AABB vs AABB).
		for (size_t i = 0; i < plan.assignments.size(); ++i) {
			for (size_t j = i + 1; j < plan.assignments.size(); ++j) {
				if (plan.assignments[i].bounds.min_x
				        < plan.assignments[j].bounds.max_x
				    && plan.assignments[j].bounds.min_x
				        < plan.assignments[i].bounds.max_x
				    && plan.assignments[i].bounds.min_y
				        < plan.assignments[j].bounds.max_y
				    && plan.assignments[j].bounds.min_y
				        < plan.assignments[i].bounds.max_y) {
					throw std::runtime_error{
						"demo 46 pin 13 FAILED: slot footprints overlap ("
						+ std::to_string(i) + ","
						+ std::to_string(j) + ")"};
				}
			}
		}
		log::log(MSG(info) << "demo 46: pin 13 PASS (4 distinct non-overlapping slots)");
	}

	// --- Pin 14: stable assignment. Two consecutive
	//             plan_formation calls on the same input
	//             return identical assignment vectors. ---
	{
		auto p1 = openage::pathfinding::plan_formation(
			army, 8.0, 8.0, 0.8, 0.8,
			occ2, static_occ, 0, 0, 16, 16);
		auto p2 = openage::pathfinding::plan_formation(
			army, 8.0, 8.0, 0.8, 0.8,
			occ2, static_occ, 0, 0, 16, 16);
		if (p1.assignments.size() != p2.assignments.size()) {
			throw std::runtime_error{
				"demo 46 pin 14 FAILED: size mismatch"};
		}
		for (size_t i = 0; i < p1.assignments.size(); ++i) {
			if (p1.assignments[i].entity != p2.assignments[i].entity
			    or p1.assignments[i].slot_center_x
			        != p2.assignments[i].slot_center_x
			    or p1.assignments[i].slot_center_y
			        != p2.assignments[i].slot_center_y) {
				throw std::runtime_error{
					"demo 46 pin 14 FAILED: non-deterministic at index "
					+ std::to_string(i)};
			}
		}
		log::log(MSG(info) << "demo 46: pin 14 PASS (deterministic across runs)");
	}

	// --- Pin 15: 6 units; the planner lays out a 2x3
	//             (cols=ceil(sqrt(6))=3, rows=2) grid. ---
	{
		std::vector<openage::gamestate::entity_id_t> six;
		for (int i = 0; i < 6; ++i) six.push_back(6000 + i);
		auto plan = openage::pathfinding::plan_formation(
			six, 8.0, 8.0, 0.8, 0.8,
			occ2, static_occ, 0, 0, 16, 16);
		if (plan.result != openage::pathfinding::FormationResult::OK
		    or plan.assignments.size() != 6) {
			throw std::runtime_error{
				"demo 46 pin 15 FAILED: 6-unit plan: "
				+ plan.reason};
		}
		log::log(MSG(info) << "demo 46: pin 15 PASS (6-unit formation)");
	}

	// --- Pin 16: TC obstacle in the centre. The planner
	//             must avoid it. We register the TC at
	//             (8, 8) with bounds [6, 10) x [6, 10) in
	//             the static grid, then ask the planner to
	//             place 4 units at (8, 8). The translation
	//             fallback must move the formation to a
	//             non-TC quadrant. ---
	{
		openage::gamestate::scenario::StaticOccupancyGrid
			occ_with_tc(0, 0, 16, 16);
		// Block cells (6..9, 6..9) -- the TC footprint.
		for (int x = 6; x <= 9; ++x) {
			for (int y = 6; y <= 9; ++y) {
				openage::gamestate::component::FootprintBounds b;
				b.min_x = x;
				b.min_y = y;
				b.max_x = x + 1;
				b.max_y = y + 1;
				occ_with_tc.occupy(9999, b);
			}
		}
		auto plan = openage::pathfinding::plan_formation(
			army, 8.0, 8.0, 0.8, 0.8,
			occ2, occ_with_tc, 0, 0, 16, 16);
		if (plan.result != openage::pathfinding::FormationResult::OK
		    or plan.assignments.size() != 4) {
			throw std::runtime_error{
				"demo 46 pin 16 FAILED: TC-obstacle plan: "
				+ plan.reason};
		}
		// None of the slot footprints may overlap the
		// TC cells (6..9, 6..9).
		for (const auto &a : plan.assignments) {
			double bmin_x = std::max(a.bounds.min_x, 6.0);
			double bmin_y = std::max(a.bounds.min_y, 6.0);
			double bmax_x = std::min(a.bounds.max_x, 10.0);
			double bmax_y = std::min(a.bounds.max_y, 10.0);
			if (bmax_x > bmin_x and bmax_y > bmin_y) {
				throw std::runtime_error{
					"demo 46 pin 16 FAILED: slot overlaps TC at ("
					+ std::to_string(a.slot_center_x) + ","
					+ std::to_string(a.slot_center_y) + ")"};
			}
		}
		log::log(MSG(info) << "demo 46: pin 16 PASS (TC obstacle avoided)");
	}

	// --- Pin 17: empty selection -> OK with 0 assignments
	//             (special case). ---
	{
		auto plan = openage::pathfinding::plan_formation(
			{}, 8.0, 8.0, 0.8, 0.8,
			occ2, static_occ, 0, 0, 16, 16);
		if (plan.result != openage::pathfinding::FormationResult::OK
		    or not plan.assignments.empty()) {
			throw std::runtime_error{
				"demo 46 pin 17 FAILED: empty plan not OK"};
		}
		log::log(MSG(info) << "demo 46: pin 17 PASS (empty selection -> empty plan)");
	}

	// --- Pin 18: NO_SPACE when no quadrant has room. We
	//             block the entire map with the static
	//             grid and expect NO_SPACE. ---
	{
		openage::gamestate::scenario::StaticOccupancyGrid
			full_block(0, 0, 16, 16);
		for (int x = 0; x < 16; ++x) {
			for (int y = 0; y < 16; ++y) {
				openage::gamestate::component::FootprintBounds b;
				b.min_x = x;
				b.min_y = y;
				b.max_x = x + 1;
				b.max_y = y + 1;
				full_block.occupy(9999, b);
			}
		}
		auto plan = openage::pathfinding::plan_formation(
			army, 8.0, 8.0, 0.8, 0.8,
			occ2, full_block, 0, 0, 16, 16);
		if (plan.result != openage::pathfinding::FormationResult::NO_SPACE) {
			throw std::runtime_error{
				"demo 46 pin 18 FAILED: full-block plan should NO_SPACE"};
		}
		log::log(MSG(info) << "demo 46: pin 18 PASS (full-block -> NO_SPACE)");
	}

	log::log(MSG(info) << "DEMO 46: PASS (multi-unit formation + dynamic occupancy)");
}


} // namespace openage::gamestate::tests
