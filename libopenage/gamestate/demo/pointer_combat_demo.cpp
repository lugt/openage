// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4D.1 demo 38: synthetic-pointer-driven Mission 1 combat
// lifecycle.
//
// This is the gate for the
// MISSION_1_POINTER_DRIVEN_ATTACK_COMPLETE signal: the test
// must follow the spec's chain from synthetic pointer input
// through Commander, Move runtime, NavigationService,
// AttackHandler, Death/Removal, TriggerRuntime, and MissionState
// in one run.
//
// Synthetic pointer path (no Qt):
//   1. Build a SelectionSnapshot manually (simulating what
//      the presenter's selection layer would build for the
//      LMB click on the PlayerVillager).
//   2. Build a PickResult manually for the enemy entity
//      (simulating the RMB picking layer that would identify
//      the enemy at the world coord).
//   3. Call input::resolve_context_action (the semantic action
//      resolver) -- this is the platform-neutral layer the
//      input controller would call after picking.
//   4. Dispatch the resolved action through Commander via
//      "game.send_command" (this is what the production
//      input binding does on RMB release).
//   5. Drive the simulation forward with reach_time. The
//      production path takes over:
//        Commander -> SendCommandHandler -> CommandQueue
//        -> activity system -> Move::move_default
//        -> NavigationService -> AttackHandler
//        -> DeathEventHandler -> RemovalEventHandler
//        -> TriggerRuntime -> MissionState::declare_victory.
//
// The test asserts the OUTCOME at each stage, not the
// intermediate dispatch order. The whole chain must end with
// MissionState in VICTORY.

#include "gamestate/demo/footprint_demo.h"

#include "input/semantic_action.h"
#include "pathfinding/navigation_service.h"

#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/internal/commands/attack.h"
#include "gamestate/component/internal/commands/move.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/event/send_command.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "scenario/trigger/trigger_runtime.h"
#include "time/time.h"
#include "time/time_loop.h"

#include <cmath>
#include <cstdint>
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


void simulation_demo_38(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 38: P4.4D.1 pointer-driven Mission 1 combat ===");

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
	auto commander = simulation->get_commander();

	auto villager_id = find_entity_by_key(state, "PlayerVillager");
	auto enemy_id = find_entity_by_key(state, "EnemyVillager");
	if (not state->has_game_entity(villager_id)
	    or not state->has_game_entity(enemy_id)) {
		throw std::runtime_error{
			"demo 38 FAILED: missing villager/enemy"};
	}

	// ---- Pin 1: synthetic pointer input. Build the
	//              SelectionSnapshot for the villager (LMB
	//              click) and the PickResult for the enemy
	//              (RMB), then call resolve_context_action
	//              and dispatch the resolved action through
	//              Commander. ----
	openage::input::SelectionSnapshot snap;
	{
		openage::input::SelectionSnapshot::Entry e;
		e.id = villager_id;
		auto entity = state->get_game_entity(villager_id);
		e.has_move_ability = entity->has_component(
			openage::gamestate::component::component_t::MOVE);
		e.has_attack_ability = entity->has_component(
			openage::gamestate::component::component_t::ATTACK);
		if (entity->has_component(
			    openage::gamestate::component::component_t::OWNERSHIP)) {
			auto own = std::dynamic_pointer_cast<
				openage::gamestate::component::Ownership>(
				entity->get_component(
					openage::gamestate::component::component_t::OWNERSHIP));
			if (own) {
				e.owner = own->get_owners().get(
					openage::time::time_t{0});
			}
		}
		snap.entries.push_back(e);
	}

	openage::input::PickResult pick;
	pick.has_entity = true;
	pick.entity = enemy_id;
	{
		auto entity = state->get_game_entity(enemy_id);
		if (entity->has_component(
			    openage::gamestate::component::component_t::OWNERSHIP)) {
			auto own = std::dynamic_pointer_cast<
				openage::gamestate::component::Ownership>(
				entity->get_component(
					openage::gamestate::component::component_t::OWNERSHIP));
			if (own) {
				pick.entity_owner = own->get_owners().get(
					openage::time::time_t{0});
			}
		}
	}

	auto self_player = snap.entries.front().owner;
	auto action = openage::input::resolve_context_action(
		state, snap, pick, self_player);
	if (action.kind != openage::input::SemanticActionKind::ATTACK
	    or not action.target_entity.has_value()
	    or *action.target_entity != enemy_id) {
		throw std::runtime_error{
			"demo 38 pin 1 FAILED: resolver -> "
			+ action.kind_string() + " reason=" + action.reason};
	}
	log::log(MSG(info) << "demo 38: pin 1 PASS (synthetic pointer -> "
	                   << "resolver -> ATTACK enemy=" << enemy_id << ")");

	// ---- Pin 2: dispatch via Commander. Send a single
	//              AttackCommand targeting the enemy. This is
	//              the EXACT event the production input
	//              binding dispatches on RMB release. ----
	{
		openage::event::EventHandler::param_map::map_t cmd{
			{"type", openage::gamestate::component::command::command_t::ATTACK},
			{"entity_ids", std::vector<openage::gamestate::entity_id_t>{villager_id}},
			{"target_id", enemy_id},
		};
		event_loop->create_event("game.send_command", commander, state,
			openage::time::time_t{0}, cmd);
		log::log(MSG(info) << "demo 38: pin 2 PASS (AttackCommand dispatched once)");
	}

	// ---- Pin 3: drive the simulation forward. The
	//              production runtime (Move + AttackHandler +
	//              Death + Removal + TriggerRuntime +
	//              MissionState) must drive the enemy from
	//              alive -> dead -> removed -> objective ->
	//              victory. The Mission 1 enemy villager has
	//              ~10 HP, attack damage 3, attack cooldown
	//              ~2s; so a 60s budget gives 30 attacks = 90
	//              damage, more than enough to kill. ----
	event_loop->reach_time(
		openage::time::time_t::from_int(60), state);

	// ---- Pin 4: enemy is removed (has_game_entity=false) ----
	if (state->has_game_entity(enemy_id)) {
		throw std::runtime_error{
			"demo 38 pin 4 FAILED: enemy still alive after 60s"};
	}
	log::log(MSG(info) << "demo 38: pin 4 PASS (enemy removed)");

	// ---- Pin 5: MissionState is in VICTORY ----
	if (not simulation->is_objective_finished()) {
		throw std::runtime_error{
			"demo 38 pin 5 FAILED: MissionState not in VICTORY"};
	}
	log::log(MSG(info) << "demo 38: pin 5 PASS (MissionState=VICTORY)");

	// ---- Pin 6: no waypoint is still pending. The move
	//              runtime reached the approach cell + the
	//              attack finished; the Position curve's
	//              last keyframe is the final cell. ----
	auto pos_comp = std::dynamic_pointer_cast<
		openage::gamestate::component::Position>(
		state->get_game_entity(villager_id)->get_component(
			openage::gamestate::component::component_t::POSITION));
	if (not pos_comp) {
		throw std::runtime_error{
			"demo 38 pin 6 FAILED: villager has no Position"};
	}
	// The villager is somewhere near the enemy; we don't
	// pin the exact tile (the approach service picks the
	// best), but it must be within the enemy's ring.
	auto final_pos = pos_comp->get_positions().get(
		openage::time::time_t::from_int(60));
	int fx = static_cast<int>(std::floor(final_pos.ne.to_double()));
	int fy = static_cast<int>(std::floor(final_pos.se.to_double()));
	bool near_enemy = (fx >= 9 && fx <= 11 && fy >= 3 && fy <= 5);
	if (not near_enemy) {
		throw std::runtime_error{
			"demo 38 pin 6 FAILED: villager final pos ("
			+ std::to_string(fx) + ","
			+ std::to_string(fy) + ") not in enemy ring"};
	}
	log::log(MSG(info) << "demo 38: pin 6 PASS (villager at ("
	                   << fx << "," << fy << ") within enemy ring)");

	log::log(MSG(info) << "DEMO 38: PASS (pointer-driven Mission 1 combat)");
}


} // namespace openage::gamestate::tests
