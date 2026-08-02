// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Demo 17 - P4.2 trigger evaluation, single-shot (TDD GREEN).
//
// See trigger_evaluation_demo.h for the pin list. The demo boots the
// production Mission 1 simulation (solely to obtain a GameState whose
// named-entity map has the EnemyVillager / PlayerVillager instances
// loaded), then constructs a TriggerRuntime on a FRESH EventLoop plus
// that GameState, hand-authors a defeat_enemy trigger
// (EntityRemoved(EnemyVillager) + DeclareVictory), and drives the
// dispatch with reach_time().
//
// The fresh loop is essential: GameSimulation::initialize() now wires
// its own production TriggerRuntime on the simulation's event loop and
// registers the "trigger.evaluate" / "set_objective_state" /
// "activate_trigger" / "declare_victory" handlers there. The event
// loop's handler store is an unordered_map keyed by handler id, so the
// FIRST registration wins and a second runtime's register_handlers()
// is silently ignored -- its scheduled events would dispatch into the
// production runtime (which does not know the demo's trigger) and the
// trigger would never fire. A private loop keeps the demo's runtime
// isolated from the production wiring.

#include "gamestate/demo/trigger_evaluation_demo.h"

#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "gamestate/game.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "scenario/mission_pack.h"
#include "scenario/trigger/trigger_definition.h"
#include "scenario/trigger/trigger_runtime.h"
#include "time/time.h"
#include "time/time_loop.h"
#include "util/path.h"

#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>


namespace openage::gamestate::tests {


namespace {

const std::string MISSION_PACK_DIR = "assets/converted/ww_mvp/campaign/william_wallace";

const std::string ENEMY_VILLAGER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.EnemyVillager";
const std::string PLAYER_VILLAGER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerVillager";

const std::string DEFEAT_ENEMY_TRIGGER_ID = "mission_01.defeat_enemy";


std::string env_or_default(const char *name, const std::string &fallback) {
	const char *v = std::getenv(name);
	if (v && v[0]) return v;
	return fallback;
}


entity_id_t find_entity_by_named(
	const std::shared_ptr<GameState> &state,
	const std::string &fqon) {
	auto &named = state->get_named_entities();
	auto it = named.find(fqon);
	if (it == named.end()) {
		return static_cast<entity_id_t>(-1);
	}
	return it->second;
}


// Hand-author the mission-1 defeat_enemy trigger IR: one
// EntityRemoved(EnemyVillager) condition, one DeclareVictory effect.
// The Mission Pack currently carries zero triggers (the scenario is a
// reconstruction); P4.3 will feed this same shape from data.
gamestate::MissionTriggerDefinition make_defeat_enemy_ir() {
	gamestate::MissionTriggerDefinition def;
	def.trigger_id = DEFEAT_ENEMY_TRIGGER_ID;
	def.initially_enabled = true;
	def.looping = false;
	def.delay = 0.0;

	gamestate::MissionTriggerCondition cond;
	cond.kind = "EntityDestroyed";
	cond.target_scenario_object_id = ENEMY_VILLAGER_FQON;
	def.conditions.push_back(cond);

	gamestate::MissionTriggerEffect eff;
	eff.kind = "DeclareVictory";
	eff.target_scenario_object_id = "enemy_removed";
	def.effects.push_back(eff);

	return def;
}

}  // namespace


void simulation_demo_17(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 17: P4.2 trigger evaluation (single-shot) ===");

	std::string mp_dir = env_or_default("WW_MISSION_PACK_DIR", MISSION_PACK_DIR);
	MissionPack pack = read_mission_pack(mp_dir);

	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<time::TimeLoop>();
	auto simulation = std::make_shared<GameSimulation>(
		path, cvar, time_loop,
		"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup",
		pack.scenario_fqon);
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();
	simulation->start();

	auto loop = std::make_shared<openage::event::EventLoop>();
	auto state = simulation->get_game()->get_state();

	entity_id_t ev_id = find_entity_by_named(state, ENEMY_VILLAGER_FQON);
	entity_id_t pv_id = find_entity_by_named(state, PLAYER_VILLAGER_FQON);
	if (ev_id == static_cast<entity_id_t>(-1) ||
	    pv_id == static_cast<entity_id_t>(-1)) {
		throw std::runtime_error{
			"demo 17: ScenarioObjectId mapping missing; "
			"loader wiring is broken"
		};
	}

	// Construct the runtime on the simulation's loop + state. The
	// runtime registers its own event handlers (trigger.evaluate,
	// set_objective_state, activate_trigger, declare_victory).
	auto runtime = std::make_shared<scenario::trigger::TriggerRuntime>(
		loop, state);
	runtime->register_handlers();
	auto tid = runtime->add_trigger(make_defeat_enemy_ir());
	auto trig = runtime->trigger(tid);
	if (not trig) {
		throw std::runtime_error{
			"demo 17: add_trigger returned a trigger_id that does not "
			"resolve back to an instance"
		};
	}

	// ---- Pin 1: starts ARMED, not victorious -----------------------
	if (trig->state() != scenario::trigger::TriggerState::ARMED) {
		throw std::runtime_error{
			"demo 17 pin 1 FAILED: trigger did not start ARMED"
		};
	}
	if (runtime->mission_state().is_victory()) {
		throw std::runtime_error{
			"demo 17 pin 1 FAILED: mission is victorious before any "
			"removal"
		};
	}
	log::log(MSG(info) << "demo 17: pin 1 PASS (ARMED, not victorious)");

	// ---- Pin 2: non-target removal does not fire -------------------
	state->remove_game_entity(pv_id);
	runtime->notify_entity_removed(pv_id, openage::time::time_t{10});
	loop->reach_time(openage::time::time_t{10}, state);
	if (trig->state() != scenario::trigger::TriggerState::ARMED) {
		throw std::runtime_error{
			"demo 17 pin 2 FAILED: non-target removal changed trigger "
			"state (false fire)"
		};
	}
	if (runtime->mission_state().is_victory()) {
		throw std::runtime_error{
			"demo 17 pin 2 FAILED: non-target removal declared victory"
		};
	}
	log::log(MSG(info) << "demo 17: pin 2 PASS (non-target ignored)");

	// ---- Pin 3: target removal fires and declares victory ----------
	state->remove_game_entity(ev_id);
	runtime->notify_entity_removed(ev_id, openage::time::time_t{20});
	loop->reach_time(openage::time::time_t{20}, state);
	if (trig->state() != scenario::trigger::TriggerState::COMPLETED) {
		throw std::runtime_error{
			"demo 17 pin 3 FAILED: trigger did not reach COMPLETED after "
			"target removal (state="
			+ std::to_string(static_cast<int>(trig->state())) + ")"
		};
	}
	if (trig->fire_count() != 1) {
		throw std::runtime_error{
			"demo 17 pin 3 FAILED: fire_count != 1 after target removal"
		};
	}
	if (not runtime->mission_state().is_victory()) {
		throw std::runtime_error{
			"demo 17 pin 3 FAILED: target removal did not declare victory"
		};
	}
	log::log(MSG(info) << "demo 17: pin 3 PASS (target removal -> COMPLETED + VICTORY)");

	// ---- Pin 4: duplicate notify does not re-fire ------------------
	runtime->notify_entity_removed(ev_id, openage::time::time_t{30});
	loop->reach_time(openage::time::time_t{30}, state);
	if (trig->fire_count() != 1) {
		throw std::runtime_error{
			"demo 17 pin 4 FAILED: duplicate notify re-fired the trigger"
		};
	}
	if (not runtime->mission_state().is_victory()) {
		throw std::runtime_error{
			"demo 17 pin 4 FAILED: duplicate notify un-victorious the mission"
		};
	}
	log::log(MSG(info) << "demo 17: pin 4 PASS (duplicate notify, no re-fire)");

	log::log(MSG(info) << "DEMO 17: PASS (trigger evaluation single-shot)");
}


} // namespace openage::gamestate::tests
