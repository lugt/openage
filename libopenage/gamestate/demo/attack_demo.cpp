// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "attack_demo.h"

#include <memory>
#include <vector>

#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "event/eventhandler.h"
#include "gamestate/component/internal/commands/attack.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/types.h"
#include "gamestate/event/send_command.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/manager.h"
#include "gamestate/simulation.h"
#include "gamestate/types.h"
#include "log/log.h"
#include "log/message.h"
#include "time/time.h"
#include "time/time_loop.h"


namespace openage::gamestate::tests {

void simulation_demo_5(const util::Path &path) {
	log::log(MSG(info) << "========== ATTACK COMMAND TEST (demo 5) ==========");

	// ---- Part A: unit-test the AttackCommand class directly. ----
	// No simulation needed; this is the core Phase 1 deliverable. If the
	// class wiring (command_t::ATTACK, get_type, get_target) is wrong, the
	// rest of the combat chain cannot work, so we check it in isolation first.
	{
		auto cmd = std::make_shared<component::command::AttackCommand>(42);
		bool type_ok = (cmd->get_type() == component::command::command_t::ATTACK);
		bool target_ok = (cmd->get_target() == 42);
		if (type_ok and target_ok) {
			log::log(MSG(info) << "PASS: AttackCommand class "
			                   << "(get_type()==ATTACK, get_target()==42)");
		}
		else {
			log::log(MSG(err) << "FAIL: AttackCommand class type_ok=" << type_ok
			                  << " target_ok=" << target_ok);
		}
	}

	// ---- Part B: end-to-end dispatch smoke. ----
	// Issue an ATTACK command through the real SendCommandHandler and advance
	// the sim. AttackSystem is not wired into the activity graph yet (that is
	// Phase 2), so the Move node will pop the AttackCommand and drop it with a
	// warn log. The smoke passes as long as the SendCommandHandler::ATTACK
	// branch executes and the sim does not crash.
	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<time::TimeLoop>();
	auto simulation = std::make_shared<GameSimulation>(
		path,
		cvar,
		time_loop,
		"engine.util.setup.tutorial_setup.TutorialPlayerSetup",
		"engine.util.scenario.mission_1.Mission1");
	simulation->set_modpacks({"engine"});

	log::log(MSG(info) << "Starting simulation (loads Mission 1 scenario)...");
	simulation->start();

	auto game = simulation->get_game();
	auto state = game->get_state();
	auto event_loop = simulation->get_event_loop();
	auto commander = simulation->get_commander();

	// Find the player villager (the scenario spawns one with a Move ability).
	auto &entities = state->get_game_entities();
	entity_id_t villager_id = 0;
	bool found = false;
	for (auto &[id, entity] : entities) {
		if (entity->has_component(component::component_t::MOVE)) {
			villager_id = id;
			found = true;
			break;
		}
	}
	if (not found) {
		log::log(MSG(err) << "FAIL: no movable entity was spawned. "
		                  << "Cannot test ATTACK dispatch.");
		log::log(MSG(info) << "========== END ATTACK TEST (no villager) ==========");
		return;
	}
	log::log(MSG(info) << "Found villager id=" << villager_id
	                   << " for ATTACK dispatch smoke");

	auto villager = state->get_game_entity(villager_id);
	// HEADLESS: no renderer => attach_renderer() never runs, so the activity
	// is never kicked and primer_command_in_queue never creates the
	// process_command dependent. Kick the activity once manually so the
	// command-wait primer exists (mirrors simulation_demo_1).
	villager->get_manager()->run_activity_system(time::time_t{0});

	// Issue an AttackCommand targeting the villager itself. Self-target is
	// fine for a dispatch smoke; combat semantics (range/damage/death) land
	// in later phases.
	openage::event::EventHandler::param_map::map_t params{
		{"type", component::command::command_t::ATTACK},
		{"target_id", villager_id},
		{"entity_ids", std::vector<entity_id_t>{villager_id}},
	};
	event_loop->create_event(
		"game.send_command",
		commander,
		state,
		time::time_t{0},
		params);

	log::log(MSG(info) << "Advancing simulation to t=10s...");
	event_loop->reach_time(time::time_t::from_int(10), state);

	// Smoke passes if the sim didn't crash and the villager is still tracked.
	bool alive = state->get_game_entities().count(villager_id) > 0;
	if (alive) {
		log::log(MSG(info) << "PASS: ATTACK dispatch smoke "
		                   << "(sim alive, villager still present)");
	}
	else {
		log::log(MSG(err) << "FAIL: villager vanished after ATTACK dispatch");
	}

	log::log(MSG(info) << "========== END ATTACK TEST ==========");
}

} // namespace openage::gamestate::tests
