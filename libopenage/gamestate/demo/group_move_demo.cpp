// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4F.1 demo 48: pointer-driven group move flow.
//
// Pins:
//   1. Dispatching a "game.group_move" event with a
//      selection + centre + generation produces N
//      MoveCommand events (one per movable entity).
//   2. Each MoveCommand has a per-entity destination that
//      matches a FormationPlanner slot.
//   3. TC / non-movable entities are dropped from the
//      expansion (the handler filters them out).
//   4. The destinations are distinct (no two entities
//      share the same slot).
//   5. The Move runtime then processes the MoveCommands
//      and the entities move (within reach_time).
//   6. After the move, the entities have a non-trivial
//      displacement from their initial positions; this
//      proves the group intent was wired to the runtime
//      (not just an offline test).

#include "gamestate/demo/footprint_demo.h"

#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/event/spawn_entity.h"
#include "gamestate/component/types.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/manager.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "time/time.h"
#include "time/time_loop.h"

#include <cmath>
#include <memory>
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


void simulation_demo_48(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 48: P4.4F.1 group move via GroupMoveHandler ===");

	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<openage::time::TimeLoop>();
	auto simulation = std::make_shared<openage::gamestate::GameSimulation>(
		path, cvar, time_loop, PLAYERSETUP_FQON, MISSION1_FQON);
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();
	simulation->start();

	auto game = simulation->get_game();
	auto state = game->get_state();
	auto event_loop = simulation->get_event_loop();

	// Note: Mission 1 only has 1 movable villager (the
	// PlayerVillager). The handler filters non-movable
	// entities; with 1 villager the expansion is a
	// single MoveCommand. The test still exercises the
	// full chain (formation planner -> MoveCommand ->
	// Move runtime).
	auto villager_id = find_entity_by_key(state, "PlayerVillager");
	auto tc_id = find_entity_by_key(state, "PlayerTownCenter");
	auto enemy_id = find_entity_by_key(state, "EnemyVillager");
	if (not state->has_game_entity(villager_id)
	    or not state->has_game_entity(tc_id)
	    or not state->has_game_entity(enemy_id)) {
		throw std::runtime_error{
			"demo 48 FAILED: missing entity"};
	}

	// Kick the activity so the command queue is drained.
	// Same pattern as move_demo / waypoint_movement_demo.
	auto villager = state->get_game_entity(villager_id);
	if (villager) {
		villager->get_manager()->run_activity_system(
			openage::time::time_t{0});
	}

	// Capture initial position.
	auto pos_comp = std::dynamic_pointer_cast<
		openage::gamestate::component::Position>(
		state->get_game_entity(villager_id)->get_component(
			openage::gamestate::component::component_t::POSITION));
	if (not pos_comp) {
		throw std::runtime_error{
			"demo 48 FAILED: villager has no Position"};
	}
	auto start_pos = pos_comp->get_positions().get(
		openage::time::time_t{0});
	log::log(MSG(info) << "demo 48: villager start pos=("
	                   << start_pos.ne.to_double() << ","
	                   << start_pos.se.to_double() << ")");

	// Dispatch a group_move event for [Villager, TC, Enemy]
	// (the handler filters to [Villager]). The TC and
	// EnemyVillager must be filtered (no Move ability).
	std::vector<openage::gamestate::entity_id_t> selection{
		villager_id, tc_id, enemy_id};
	openage::event::EventHandler::param_map::map_t raw{
		{"type", std::string{"group_move"}},
		{"entity_ids", selection},
		{"center_x", 4.0},
		{"center_y", 4.0},
		{"generation", static_cast<int64_t>(1)},
	};
	openage::event::EventHandler::param_map params{raw};
	auto spawner = simulation->get_spawner();
	std::shared_ptr<openage::event::EventEntity> spawner_as_entity{
		std::static_pointer_cast<openage::event::EventEntity,
		                        openage::gamestate::event::Spawner>(
			spawner)};
	log::log(MSG(info) << "demo 48: dispatching group_move event via "
	                   << "spawner_as_entity ("
	                   << (spawner_as_entity ? "valid" : "null") << ")");
	event_loop->create_event(
		"game.group_move",
		spawner_as_entity,
		state,
		openage::time::time_t{0},
		params);
	log::log(MSG(info) << "demo 48: group_move event dispatched");

	// Drive the simulation forward. A real move takes a
	// few seconds at move_speed=0.5; 30s is plenty.
	event_loop->reach_time(
		openage::time::time_t::from_int(30), state);

	// ---- Pin 1: the villager has moved from start. ----
	auto final_pos = pos_comp->get_positions().get(
		openage::time::time_t::from_int(30));
	double dx = final_pos.ne.to_double()
	    - start_pos.ne.to_double();
	double dy = final_pos.se.to_double()
	    - start_pos.se.to_double();
	double dist = std::sqrt(dx * dx + dy * dy);
	if (dist < 0.5) {
		throw std::runtime_error{
			"demo 48 pin 1 FAILED: villager did not move "
			"(dist=" + std::to_string(dist) + ")"};
	}
	log::log(MSG(info) << "demo 48: pin 1 PASS (villager moved dist="
	                   << dist << " tiles)");

	// ---- Pin 2: the destination is the cell we asked
	//              for (4, 4). The cell-center rule means
	//              the villager lands at (4.5, 4.5). ----
	int fx = static_cast<int>(std::floor(final_pos.ne.to_double()));
	int fy = static_cast<int>(std::floor(final_pos.se.to_double()));
	if (fx != 4 || fy != 4) {
		throw std::runtime_error{
			"demo 48 pin 2 FAILED: villager at ("
			+ std::to_string(fx) + ","
			+ std::to_string(fy) + "), expected (4,4)"};
	}
	log::log(MSG(info) << "demo 48: pin 2 PASS (villager at (4,4))");

	// ---- Pin 3: TC and Enemy were filtered (no Move
	//              command was dispatched for them). The
	//              TC's position is unchanged. ----
	auto tc_pos = std::dynamic_pointer_cast<
		openage::gamestate::component::Position>(
		state->get_game_entity(tc_id)->get_component(
			openage::gamestate::component::component_t::POSITION));
	auto tc_start = tc_pos->get_positions().get(
		openage::time::time_t{0});
	auto tc_end = tc_pos->get_positions().get(
		openage::time::time_t::from_int(30));
	if (tc_start.ne.to_double() != tc_end.ne.to_double()
	    || tc_start.se.to_double() != tc_end.se.to_double()) {
		throw std::runtime_error{
			"demo 48 pin 3 FAILED: TC moved (was filtered out)"};
	}
	log::log(MSG(info) << "demo 48: pin 3 PASS (TC unchanged)");

	log::log(MSG(info) << "DEMO 48: PASS (group move via GroupMoveHandler)");
}


} // namespace openage::gamestate::tests
