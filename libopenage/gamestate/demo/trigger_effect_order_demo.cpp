// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Demo 19 - P4.2 effect ordering (TDD GREEN).
//
// See trigger_effect_order_demo.h for the pin list. The demo boots the
// production Mission 1 simulation (solely to obtain a GameState whose
// named-entity map has EnemyVillager loaded), then constructs a
// TriggerRuntime on a FRESH EventLoop plus that GameState, hand-authors
// a trigger whose condition is EntityRemoved(EnemyVillager) and whose
// effect list is
//   0: SetObjectiveState(primary, ACTIVE)
//   1: SetObjectiveState(primary, COMPLETED)
//   2: DeclareVictory(enemy_removed)
// then drives the dispatch with reach_time() and asserts that all
// three effects fired in stable effect_index order.
//
// The runtime lives on a fresh loop, not the simulation's, so its
// handler registrations do not collide with the production
// TriggerRuntime that GameSimulation::initialize() wires on the
// simulation's event loop (the loop's handler store is a map keyed by
// handler id; the first registration wins, so a second runtime on the
// same loop would be silently ignored).

#include "gamestate/demo/trigger_effect_order_demo.h"

#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "gamestate/game.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "scenario/mission_pack.h"
#include "scenario/trigger/mission_state.h"
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

const std::string PRIMARY_OBJECTIVE_ID = "mission_01.defeat_enemy";
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


// Hand-author a multi-effect trigger IR: one EntityRemoved(EnemyVillager)
// condition, THREE effects -- SetObjectiveState(ACTIVE),
// SetObjectiveState(COMPLETED), DeclareVictory -- in that exact order.
// The order is what the demo pins: if the runtime dispatched the two
// SetObjectiveState effects in reverse, the objective's final state
// would be ACTIVE, not COMPLETED.
gamestate::MissionTriggerDefinition make_multi_effect_defeat_enemy_ir() {
	gamestate::MissionTriggerDefinition def;
	def.trigger_id = DEFEAT_ENEMY_TRIGGER_ID;
	def.initially_enabled = true;
	def.looping = false;
	def.delay = 0.0;

	gamestate::MissionTriggerCondition cond;
	cond.kind = "EntityDestroyed";
	cond.target_scenario_object_id = ENEMY_VILLAGER_FQON;
	def.conditions.push_back(cond);

	// Effect 0: SetObjective(primary, ACTIVE).
	// IR convention: target_scenario_object_id = objective_id,
	//                target_area_id           = target_state string.
	gamestate::MissionTriggerEffect eff_active;
	eff_active.kind = "SetObjective";
	eff_active.target_scenario_object_id = PRIMARY_OBJECTIVE_ID;
	eff_active.target_area_id = "ACTIVE";
	def.effects.push_back(eff_active);

	// Effect 1: CompleteObjective(primary) -> COMPLETED.
	gamestate::MissionTriggerEffect eff_completed;
	eff_completed.kind = "CompleteObjective";
	eff_completed.target_scenario_object_id = PRIMARY_OBJECTIVE_ID;
	def.effects.push_back(eff_completed);

	// Effect 2: DeclareVictory(enemy_removed).
	gamestate::MissionTriggerEffect eff_victory;
	eff_victory.kind = "DeclareVictory";
	eff_victory.target_scenario_object_id = "enemy_removed";
	def.effects.push_back(eff_victory);

	return def;
}

}  // namespace


void simulation_demo_19(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 19: P4.2 effect ordering ===");

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
			"demo 19: ScenarioObjectId mapping missing; "
			"loader wiring is broken"
		};
	}

	auto runtime = std::make_shared<scenario::trigger::TriggerRuntime>(
		loop, state);
	runtime->register_handlers();
	// The objective must be registered (INACTIVE) BEFORE the
	// SetObjectiveState effects fire, otherwise the handler's
	// set_objective_state() returns false and the state stays unset.
	runtime->add_objective(PRIMARY_OBJECTIVE_ID);
	auto tid = runtime->add_trigger(make_multi_effect_defeat_enemy_ir());
	auto trig = runtime->trigger(tid);
	if (not trig) {
		throw std::runtime_error{
			"demo 19: add_trigger returned a trigger_id that does not "
			"resolve back to an instance"
		};
	}

	// ---- Pin 1: objective starts INACTIVE, mission not victorious ----
	if (runtime->mission_state().objective_state(PRIMARY_OBJECTIVE_ID)
	    != scenario::trigger::ObjectiveState::INACTIVE) {
		throw std::runtime_error{
			"demo 19 pin 1 FAILED: objective did not start INACTIVE"
		};
	}
	if (runtime->mission_state().is_victory()) {
		throw std::runtime_error{
			"demo 19 pin 1 FAILED: mission is victorious before fire"
		};
	}
	log::log(MSG(info) << "demo 19: pin 1 PASS (objective INACTIVE, not victorious)");

	// ---- Pin 2: fire dispatches all three effects in order ---------
	// Remove the target entity, notify the runtime, and advance the
	// clock. The trigger.evaluate event fires, which calls fire(),
	// which enqueues the three effects at the same simulation time.
	// reach_time() dispatches them in (time, ..., creation_seq) order,
	// i.e. effect_index order: ACTIVE -> COMPLETED -> DeclareVictory.
	state->remove_game_entity(ev_id);
	runtime->notify_entity_removed(ev_id, openage::time::time_t{20});
	loop->reach_time(openage::time::time_t{20}, state);

	if (trig->state() != scenario::trigger::TriggerState::COMPLETED) {
		throw std::runtime_error{
			"demo 19 pin 2 FAILED: trigger did not reach COMPLETED "
			"(state="
			+ std::to_string(static_cast<int>(trig->state())) + ")"
		};
	}
	if (trig->fire_count() != 1) {
		throw std::runtime_error{
			"demo 19 pin 2 FAILED: fire_count != 1 (got "
			+ std::to_string(trig->fire_count()) + ")"
		};
	}
	// Ordering oracle: the final objective state is COMPLETED only if
	// the ACTIVE effect fired BEFORE the COMPLETED effect. If the
	// runtime had dispatched them in reverse, the final state would be
	// ACTIVE.
	if (not runtime->mission_state().is_objective_completed(PRIMARY_OBJECTIVE_ID)) {
		throw std::runtime_error{
			"demo 19 pin 2 FAILED: objective not COMPLETED after fire "
			"(effect order broken: final state="
			+ std::to_string(static_cast<int>(
				runtime->mission_state().objective_state(PRIMARY_OBJECTIVE_ID)))
			+ ")"
		};
	}
	log::log(MSG(info) << "demo 19: pin 2 PASS (3 effects dispatched in order)");

	// ---- Pin 3: DeclareVictory effect fired -> mission victorious --
	if (not runtime->mission_state().is_victory()) {
		throw std::runtime_error{
			"demo 19 pin 3 FAILED: DeclareVictory effect did not fire"
		};
	}
	if (runtime->mission_state().victory_reason() != "enemy_removed") {
		throw std::runtime_error{
			"demo 19 pin 3 FAILED: victory reason mismatch (got '"
			+ runtime->mission_state().victory_reason() + "')"
		};
	}
	log::log(MSG(info) << "demo 19: pin 3 PASS (DeclareVictory fired, reason=enemy_removed)");

	// ---- Pin 4: single fire dispatched all effects; no re-fire -----
	// A duplicate notify must not re-fire (the trigger is COMPLETED),
	// so the objective stays COMPLETED and the mission stays victorious
	// with fire_count still 1.
	runtime->notify_entity_removed(ev_id, openage::time::time_t{30});
	loop->reach_time(openage::time::time_t{30}, state);
	if (trig->fire_count() != 1) {
		throw std::runtime_error{
			"demo 19 pin 4 FAILED: duplicate notify re-fired the trigger"
		};
	}
	if (not runtime->mission_state().is_objective_completed(PRIMARY_OBJECTIVE_ID)
	    or not runtime->mission_state().is_victory()) {
		throw std::runtime_error{
			"demo 19 pin 4 FAILED: duplicate notify regressed state"
		};
	}
	log::log(MSG(info) << "demo 19: pin 4 PASS (no re-fire, state stable)");

	log::log(MSG(info) << "DEMO 19: PASS (effect ordering)");
}


} // namespace openage::gamestate::tests
