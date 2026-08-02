// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Demo 18 - P4.2 trigger delay (TDD GREEN).
//
// See trigger_delay_demo.h for the pin list. The demo boots the
// production Mission 1 simulation (solely to obtain a GameState whose
// named-entity map has EnemyVillager loaded), then constructs a
// TriggerRuntime on a FRESH EventLoop plus that GameState, hand-authors
// a defeat_enemy trigger with delay=5.0 (EntityRemoved(EnemyVillager) +
// DeclareVictory), then drives the dispatch with reach_time() to pin
// the WAITING_DELAY state machine path: ARMED -> WAITING_DELAY at the
// notification, then a delayed fire at (notify_time + delay).
//
// The runtime lives on a fresh loop, not the simulation's, so its
// handler registrations do not collide with the production
// TriggerRuntime that GameSimulation::initialize() wires on the
// simulation's event loop (the loop's handler store is a map keyed by
// handler id; the first registration wins, so a second runtime on the
// same loop would be silently ignored).

#include "gamestate/demo/trigger_delay_demo.h"

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

const std::string DELAYED_DEFEAT_ENEMY_TRIGGER_ID = "mission_01.delayed_defeat_enemy";

// The dependency notification happens at simulation time 10. The
// trigger's delay is 5 seconds, so the delayed re-evaluation (and the
// fire) is due at time 15. Time 14 is one tick before the delayed fire
// and is used to prove the trigger has NOT fired yet.
const double NOTIFY_TIME = 10.0;
const double DELAY_SECONDS = 5.0;


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


// Hand-author a delayed defeat_enemy trigger IR: one
// EntityRemoved(EnemyVillager) condition, one DeclareVictory effect,
// and a non-zero delay so the runtime routes through WAITING_DELAY.
gamestate::MissionTriggerDefinition make_delayed_defeat_enemy_ir() {
	gamestate::MissionTriggerDefinition def;
	def.trigger_id = DELAYED_DEFEAT_ENEMY_TRIGGER_ID;
	def.initially_enabled = true;
	def.looping = false;
	def.delay = DELAY_SECONDS;

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


void simulation_demo_18(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 18: P4.2 trigger delay ===");

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
	if (ev_id == static_cast<entity_id_t>(-1)) {
		throw std::runtime_error{
			"demo 18: ScenarioObjectId mapping missing; "
			"loader wiring is broken"
		};
	}

	auto runtime = std::make_shared<scenario::trigger::TriggerRuntime>(
		loop, state);
	runtime->register_handlers();
	auto tid = runtime->add_trigger(make_delayed_defeat_enemy_ir());
	auto trig = runtime->trigger(tid);
	if (not trig) {
		throw std::runtime_error{
			"demo 18: add_trigger returned a trigger_id that does not "
			"resolve back to an instance"
		};
	}

	const auto notify_time = openage::time::time_t::from_double(NOTIFY_TIME);
	const auto fire_time = openage::time::time_t::from_double(NOTIFY_TIME + DELAY_SECONDS);
	const auto before_fire = openage::time::time_t::from_double(NOTIFY_TIME + DELAY_SECONDS - 1.0);

	// ---- Pin 1: notification enters WAITING_DELAY, not fired ------
	// Remove the target entity and notify the runtime at notify_time.
	// The trigger's conditions now hold, but the non-zero delay means
	// the runtime transitions ARMED -> WAITING_DELAY and schedules a
	// delayed trigger.evaluate at (notify_time + delay). The trigger
	// has NOT fired (fire_count == 0) and the mission is not yet
	// victorious.
	state->remove_game_entity(ev_id);
	runtime->notify_entity_removed(ev_id, notify_time);
	loop->reach_time(notify_time, state);

	if (trig->state() != scenario::trigger::TriggerState::WAITING_DELAY) {
		throw std::runtime_error{
			"demo 18 pin 1 FAILED: trigger did not enter WAITING_DELAY "
			"(state="
			+ std::to_string(static_cast<int>(trig->state())) + ")"
		};
	}
	if (trig->fire_count() != 0) {
		throw std::runtime_error{
			"demo 18 pin 1 FAILED: trigger fired before delay elapsed"
		};
	}
	if (runtime->mission_state().is_victory()) {
		throw std::runtime_error{
			"demo 18 pin 1 FAILED: mission victorious before delayed fire"
		};
	}
	log::log(MSG(info) << "demo 18: pin 1 PASS (WAITING_DELAY, not fired)");

	// ---- Pin 2: advancing to BEFORE fire_time does not fire -------
	// The clock is one second short of (notify_time + delay). The
	// delayed trigger.evaluate has not been reached, so the trigger
	// stays WAITING_DELAY.
	loop->reach_time(before_fire, state);
	if (trig->state() != scenario::trigger::TriggerState::WAITING_DELAY) {
		throw std::runtime_error{
			"demo 18 pin 2 FAILED: trigger left WAITING_DELAY before "
			"delay elapsed (state="
			+ std::to_string(static_cast<int>(trig->state())) + ")"
		};
	}
	if (trig->fire_count() != 0) {
		throw std::runtime_error{
			"demo 18 pin 2 FAILED: trigger fired before fire_time"
		};
	}
	log::log(MSG(info) << "demo 18: pin 2 PASS (still WAITING_DELAY at t=14)");

	// ---- Pin 3: advancing to fire_time fires and declares victory --
	// At (notify_time + delay) the delayed trigger.evaluate fires, the
	// runtime re-checks the conditions (still true), and the trigger
	// fires -> COMPLETED + VICTORY. fire_count == 1.
	loop->reach_time(fire_time, state);
	if (trig->state() != scenario::trigger::TriggerState::COMPLETED) {
		throw std::runtime_error{
			"demo 18 pin 3 FAILED: trigger did not reach COMPLETED at "
			"fire_time (state="
			+ std::to_string(static_cast<int>(trig->state())) + ")"
		};
	}
	if (trig->fire_count() != 1) {
		throw std::runtime_error{
			"demo 18 pin 3 FAILED: fire_count != 1 after delayed fire"
		};
	}
	if (not runtime->mission_state().is_victory()) {
		throw std::runtime_error{
			"demo 18 pin 3 FAILED: delayed fire did not declare victory"
		};
	}
	log::log(MSG(info) << "demo 18: pin 3 PASS (delayed fire -> COMPLETED + VICTORY)");

	log::log(MSG(info) << "DEMO 18: PASS (trigger delay)");
}


} // namespace openage::gamestate::tests
