// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Demo 20 - P4.3 Mission 1 data-driven victory (TDD GREEN).
//
// See trigger_data_driven_victory_demo.h for the pin list. The demo
// boots the production Mission 1 GameSimulation (which wires the
// TriggerRuntime + synthesizes the defeat_enemy trigger from the
// Mission Pack's required_removals), drives the production removal
// chain via game.removal events, and asserts victory through the
// public is_objective_finished() contract.

#include "gamestate/demo/trigger_data_driven_victory_demo.h"

#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "event/eventhandler.h"
#include "gamestate/event/send_command.h"  // Commander : public EventEntity
#include "gamestate/game.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "time/time.h"
#include "time/time_loop.h"
#include "util/path.h"

#include <memory>
#include <stdexcept>
#include <string>


namespace openage::gamestate::tests {


namespace {

// Stable FQONs declared by mission_01.nyan (same as demo 7).
constexpr const char *MISSION1_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.Mission";
constexpr const char *PLAYERSETUP_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup";
constexpr const char *PLAYERVILLAGER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerVillager";
constexpr const char *ENEMYVILLAGER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.EnemyVillager";


// Drive the production removal chain: create a game.removal event for
// the given entity at time `at`. The RemovalEventHandler removes the
// entity from the GameState and fires the TriggerRuntime observer,
// which schedules trigger.evaluate. The commander is used as the
// event target (the handler ignores the target; it reads target_id
// from params and the entity from the state).
void remove_via_event(
	const std::shared_ptr<openage::event::EventLoop> &loop,
	const std::shared_ptr<GameState> &state,
	const std::shared_ptr<openage::event::EventEntity> &target,
	openage::gamestate::entity_id_t entity_id,
	openage::time::time_t at) {

	openage::event::EventHandler::param_map::map_t params{
		{"target_id", entity_id},
		{"time", at},
	};
	loop->create_event("game.removal", target, state, at, params);
}

}  // namespace


void simulation_demo_20(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 20: P4.3 Mission 1 data-driven victory ===");

	// Boot the PRODUCTION Mission 1 simulation. initialize() constructs
	// the TriggerRuntime and (via wire_trigger_data) synthesizes the
	// defeat_enemy trigger from the Mission Pack's
	// VictoryDefinition.required_removals. The demo does NOT hand-author
	// any trigger -- the trigger comes from the production wiring.
	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<time::TimeLoop>();
	auto simulation = std::make_shared<GameSimulation>(
		path, cvar, time_loop, PLAYERSETUP_FQON, MISSION1_FQON);
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();
	simulation->start();

	auto game = simulation->get_game();
	auto state = game->get_state();
	auto loop = simulation->get_event_loop();
	auto commander = simulation->get_commander();

	auto opt_enemy = state->get_entity_id_by_instance(ENEMYVILLAGER_FQON);
	auto opt_pv = state->get_entity_id_by_instance(PLAYERVILLAGER_FQON);
	if (not opt_enemy.has_value() or not opt_pv.has_value()) {
		throw std::runtime_error{
			"demo 20: ScenarioObjectId mapping missing; "
			"loader wiring is broken"
		};
	}
	auto enemy_id = *opt_enemy;
	auto pv_id = *opt_pv;

	// ---- Pin 1: not finished, enemy present -------------------------
	if (simulation->is_objective_finished()) {
		throw std::runtime_error{
			"demo 20 pin 1 FAILED: mission finished before any removal"
		};
	}
	if (not state->has_game_entity(enemy_id)) {
		throw std::runtime_error{
			"demo 20 pin 1 FAILED: EnemyVillager not present at init"
		};
	}
	log::log(MSG(info) << "demo 20: pin 1 PASS (not finished, enemy present)");

	// ---- Pin 2: non-target removal does NOT finish ------------------
	// Remove PlayerVillager through the production game.removal event.
	// The defeat_enemy trigger watches only EnemyVillager, so the
	// TriggerRuntime observer's notify_entity_removed schedules a
	// trigger.evaluate that finds the condition false (enemy still
	// alive) and does not fire.
	remove_via_event(loop, state, commander, pv_id, openage::time::time_t{10});
	loop->reach_time(openage::time::time_t{10}, state);
	if (simulation->is_objective_finished()) {
		throw std::runtime_error{
			"demo 20 pin 2 FAILED: non-target removal declared victory"
		};
	}
	log::log(MSG(info) << "demo 20: pin 2 PASS (non-target removal ignored)");

	// ---- Pin 3: target removal finishes the mission -----------------
	// Remove EnemyVillager through the production game.removal event.
	// RemovalEventHandler fires the TriggerRuntime observer ->
	// trigger.evaluate -> defeat_enemy fires -> DeclareVictory ->
	// MissionState::VICTORY -> is_objective_finished() == true.
	remove_via_event(loop, state, commander, enemy_id, openage::time::time_t{20});
	loop->reach_time(openage::time::time_t{20}, state);
	if (not simulation->is_objective_finished()) {
		throw std::runtime_error{
			"demo 20 pin 3 FAILED: target removal did not finish the mission"
		};
	}
	if (state->has_game_entity(enemy_id)) {
		throw std::runtime_error{
			"demo 20 pin 3 FAILED: EnemyVillager not removed from state"
		};
	}
	log::log(MSG(info) << "demo 20: pin 3 PASS (target removal -> VICTORY)");

	// ---- Pin 4: idempotent -- advancing time keeps it finished ------
	loop->reach_time(openage::time::time_t{30}, state);
	if (not simulation->is_objective_finished()) {
		throw std::runtime_error{
			"demo 20 pin 4 FAILED: mission regressed to not-finished"
		};
	}
	log::log(MSG(info) << "demo 20: pin 4 PASS (idempotent, stays finished)");

	log::log(MSG(info) << "DEMO 20: PASS (Mission 1 data-driven victory)");
}


} // namespace openage::gamestate::tests
