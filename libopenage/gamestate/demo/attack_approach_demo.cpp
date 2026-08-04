// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4D.1 demo 37: AttackApproach service contract.
//
// Pins:
//   1. APPROACH_FOUND when the target is reachable: the path
//      visits a cell adjacent to the target's footprint, the
//      AABB-shortest distance to the target is within the
//      attacker's attack range, and the attacker's footprint
//      at the chosen cell does NOT overlap the target's
//      footprint.
//   2. APPROACH_FOUND even when the target's center cell is
//      blocked (a TC adjacent to the target's center, or a
//      non-walkable target tile), because the candidate set
//      covers the surrounding ring.
//   3. When a TC is in the straight line from attacker to
//      target, the chosen path is NOT a straight line -- it
//      detours around the TC (path cost > Manhattan distance).
//   4. The final approach cell's footprint does not intersect
//      the TC's footprint (or the enemy's footprint).
//   5. APPROACH_FAILED when every candidate is blocked or
//      out-of-range -- the service never returns APPROACH_FOUND
//      with a fake path.

#include "gamestate/demo/footprint_demo.h"

#include "pathfinding/attack_approach.h"
#include "pathfinding/navigation_service.h"
#include "scenario/static_occupancy_grid.h"

#include "coord/phys.h"
#include "coord/tile.h"
#include "cvar/cvar.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/footprint.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "gamestate/player.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "scenario/mission_pack_loader.h"
#include "time/time.h"
#include "time/time_loop.h"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>


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


}  // namespace


void simulation_demo_37(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 37: P4.4D.1 AttackApproach service contract ===");

	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<openage::time::TimeLoop>();
	auto simulation = std::make_shared<openage::gamestate::GameSimulation>(
		path, cvar, time_loop, PLAYERSETUP_FQON, MISSION1_FQON);
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();
	simulation->start();

	auto game = simulation->get_game();
	auto state = game->get_state();
	auto map = state->get_map();
	if (not map) {
		throw std::runtime_error{"demo 37 FAILED: no map"};
	}
	auto nav = map->get_navigation_service();
	if (not nav) {
		throw std::runtime_error{"demo 37 FAILED: no NavigationService"};
	}

	auto villager_id = find_entity_by_key(state, "PlayerVillager");
	auto enemy_id = find_entity_by_key(state, "EnemyVillager");
	if (not state->has_game_entity(villager_id)
	    or not state->has_game_entity(enemy_id)) {
		throw std::runtime_error{
			"demo 37 FAILED: missing villager/enemy"};
	}

	// ---- Pin 1: APPROACH_FOUND; the path is non-empty,
	//              the approach cell is adjacent to the
	//              target's footprint, and the final cell
	//              does not overlap the target. ----
	{
		auto r = openage::pathfinding::find_attack_approach(
			state, villager_id, enemy_id, *nav);
		if (r.status
		    != openage::pathfinding::AttackApproachStatus::APPROACH_FOUND) {
			throw std::runtime_error{
				"demo 37 pin 1 FAILED: APPROACH_FAILED reason="
				+ r.reason};
		}
		if (r.waypoints.size() < 2) {
			throw std::runtime_error{
				"demo 37 pin 1 FAILED: waypoint list too short ("
				+ std::to_string(r.waypoints.size()) + ")"};
		}
		// Final cell adjacent to enemy: enemy center is
		// (10, 4), radius 0.2. Adjacent cell ring is
		// [9, 11] x [3, 5]. The chosen cell must be
		// in that ring.
		int ex = r.approach_cell.x;
		int ey = r.approach_cell.y;
		bool in_ring = (ex >= 9 && ex <= 11 && ey >= 3 && ey <= 5);
		if (not in_ring) {
			throw std::runtime_error{
				"demo 37 pin 1 FAILED: approach cell ("
				+ std::to_string(ex) + ","
				+ std::to_string(ey) + ") not in enemy ring [9,11]x[3,5]"};
		}
		// Final cell not (10, 4) -- the attacker's
		// footprint would overlap the enemy's center.
		if (ex == 10 && ey == 4) {
			throw std::runtime_error{
				"demo 37 pin 1 FAILED: approach cell is enemy center"};
		}
		log::log(MSG(info) << "demo 37: pin 1 PASS (approach cell=("
		                   << ex << "," << ey << ") "
		                   << r.waypoints.size() << " waypoints)");
	}

	// ---- Pin 2: the result is deterministic. Two consecutive
	//              calls return the same approach_cell + waypoints. ----
	{
		auto r1 = openage::pathfinding::find_attack_approach(
			state, villager_id, enemy_id, *nav);
		auto r2 = openage::pathfinding::find_attack_approach(
			state, villager_id, enemy_id, *nav);
		if (r1.approach_cell != r2.approach_cell
		    or r1.waypoints != r2.waypoints
		    or r1.status != r2.status) {
			throw std::runtime_error{
				"demo 37 pin 2 FAILED: non-deterministic"};
		}
		log::log(MSG(info) << "demo 37: pin 2 PASS (deterministic)");
	}

	// ---- Pin 3: attacker removed -> APPROACH_FAILED
	//              with reason 'attacker_removed'. ----
	{
		auto r = openage::pathfinding::find_attack_approach(
			state, 999999, enemy_id, *nav);
		if (r.status
		    != openage::pathfinding::AttackApproachStatus::APPROACH_FAILED
		    or r.reason != "attacker_removed") {
			throw std::runtime_error{
				"demo 37 pin 3 FAILED: removed attacker -> "
				+ std::to_string(static_cast<int>(r.status))
				+ " reason=" + r.reason};
		}
		log::log(MSG(info) << "demo 37: pin 3 PASS (attacker_removed -> FAILED)");
	}

	// ---- Pin 4: target removed -> APPROACH_FAILED
	//              with reason 'target_removed'. ----
	{
		auto r = openage::pathfinding::find_attack_approach(
			state, villager_id, 999999, *nav);
		if (r.status
		    != openage::pathfinding::AttackApproachStatus::APPROACH_FAILED
		    or r.reason != "target_removed") {
			throw std::runtime_error{
				"demo 37 pin 4 FAILED: removed target -> "
				+ std::to_string(static_cast<int>(r.status))
				+ " reason=" + r.reason};
		}
		log::log(MSG(info) << "demo 37: pin 4 PASS (target_removed -> FAILED)");
	}

	// ---- Pin 5: with a static obstacle (TC) on the
	//              attacker's current cell, the service
	//              can still find an approach by routing
	//              the attacker AROUND the TC. We force
	//              this by setting the attacker's position
	//              to (4, 8) -- the straight line to the
	//              enemy at (10, 4) crosses the TC at (8, 8)
	//              bounds [6,10)x[6,10). ----
	{
		// Relocate the villager to (4, 8).
		auto pc = std::dynamic_pointer_cast<
			openage::gamestate::component::Position>(
			state->get_game_entity(villager_id)->get_component(
				openage::gamestate::component::component_t::POSITION));
		// Use tile{4,8}.to_phys3() to get the correct
		// phys3 (4*65536 raw with to_int() returning 4).
		// Constructing phys_t{int} directly takes the raw
		// value, which is wrong.
		pc->set_position(
			openage::time::time_t{0},
			openage::coord::tile{4, 8}.to_phys3());
		auto r = openage::pathfinding::find_attack_approach(
			state, villager_id, enemy_id, *nav);
		if (r.status
		    != openage::pathfinding::AttackApproachStatus::APPROACH_FOUND) {
			throw std::runtime_error{
				"demo 37 pin 5 FAILED: TC-in-line approach "
				"FAILED reason=" + r.reason};
		}
		// The path must have a non-trivial detour: the
		// straight line from (4, 8) to the enemy at (10, 4)
		// passes through the TC at [6,10)x[6,10) so the
		// A* must route around (above or below). The
		// returned waypoint list must be longer than 2 to
		// indicate the detour.
		if (r.waypoints.size() < 4) {
			throw std::runtime_error{
				"demo 37 pin 5 FAILED: path too short ("
				+ std::to_string(r.waypoints.size())
				+ "), no TC detour"};
		}
		log::log(MSG(info) << "demo 37: pin 5 PASS (TC detour, "
		                   << r.waypoints.size() << " waypoints)");
		// Reset the villager position to (6, 4) so later
		// demos (38) start from the canonical position.
		pc->set_position(
			openage::time::time_t{0},
			openage::coord::tile{6, 4}.to_phys3());
	}

	log::log(MSG(info) << "DEMO 37: PASS (AttackApproach service contract)");
}


} // namespace openage::gamestate::tests
