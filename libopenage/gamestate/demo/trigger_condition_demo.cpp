// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Demo 16 - P4.2 EntityRemovedCondition (TDD GREEN).
//
// See trigger_condition_demo.h for the pin list. The demo boots the
// production Mission 1 simulation (same bootstrap as demos 7-14) so the
// EntityRemovedCondition is resolved against the REAL GameState
// named-entity index (PlayerVillager / EnemyVillager), not a mock.

#include "gamestate/demo/trigger_condition_demo.h"

#include "cvar/cvar.h"
#include "gamestate/game.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "scenario/mission_pack.h"
#include "scenario/trigger/trigger_condition.h"
#include "scenario/trigger/trigger_definition.h"
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


// Build an EntityDestroyed IR condition record. The runtime's
// make_trigger_condition() dispatches on the `kind` string (the
// CamelCase token validated by validate_mission_pack); the
// target_scenario_object_id is the placed-instance FQON the
// condition binds to.
gamestate::MissionTriggerCondition make_entity_removed_ir(
	const std::string &target_fqon) {
	gamestate::MissionTriggerCondition ir;
	ir.kind = "EntityDestroyed";
	ir.target_scenario_object_id = target_fqon;
	return ir;
}

}  // namespace


void simulation_demo_16(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 16: P4.2 EntityRemovedCondition ===");

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

	auto state = simulation->get_game()->get_state();

	entity_id_t ev_id = find_entity_by_named(state, ENEMY_VILLAGER_FQON);
	entity_id_t pv_id = find_entity_by_named(state, PLAYER_VILLAGER_FQON);
	log::log(MSG(info) << "demo 16: ScenarioObjectId mapping -> "
	                   << "EnemyVillager=" << ev_id
	                   << " PlayerVillager=" << pv_id);

	if (ev_id == static_cast<entity_id_t>(-1) ||
	    pv_id == static_cast<entity_id_t>(-1)) {
		throw std::runtime_error{
			"demo 16: ScenarioObjectId mapping missing; "
			"loader wiring is broken"
		};
	}

	const openage::time::time_t t{0};

	// ---- Pin 1: condition for an existing entity is false ------------
	auto cond = scenario::trigger::make_trigger_condition(
		*state, openage::time::time_t{0},
		make_entity_removed_ir(ENEMY_VILLAGER_FQON));
	if (not cond) {
		throw std::runtime_error{
			"demo 16 pin 1 FAILED: make_trigger_condition returned "
			"nullptr for kind 'entity_removed'"
		};
	}
	if (cond->evaluate(*state, t)) {
		throw std::runtime_error{
			"demo 16 pin 1 FAILED: EntityRemovedCondition is true while "
			"the target entity still exists"
		};
	}
	log::log(MSG(info) << "demo 16: pin 1 PASS (condition false while exists)");

	// ---- Pin 4: dependencies() expose the resolved entity key -------
	auto deps = cond->dependencies();
	if (deps.size() != 1 ||
	    deps[0].kind != scenario::trigger::DependencyKind::ENTITY ||
	    deps[0].key != static_cast<uint64_t>(ev_id)) {
		throw std::runtime_error{
			"demo 16 pin 4 FAILED: EntityRemovedCondition dependencies "
			"do not match the resolved entity id"
		};
	}
	log::log(MSG(info) << "demo 16: pin 4 PASS (dependencies expose entity key)");

	// ---- Pin 2: after removal, the condition is true ----------------
	state->remove_game_entity(ev_id);
	if (not cond->evaluate(*state, t)) {
		throw std::runtime_error{
			"demo 16 pin 2 FAILED: EntityRemovedCondition is false after "
			"the target entity was removed"
		};
	}
	log::log(MSG(info) << "demo 16: pin 2 PASS (condition true after removal)");

	// ---- Pin 5: removing PlayerVillager does not affect the already-
	// resolved EnemyVillager condition's truth (it stays true because
	// EV is gone; the point is the condition is identity-bound). We
	// re-build a fresh condition bound to PlayerVillager and confirm
	// removing EnemyVillager (already gone) did not make IT true.
	auto pv_cond = scenario::trigger::make_trigger_condition(
		*state, openage::time::time_t{0},
		make_entity_removed_ir(PLAYER_VILLAGER_FQON));
	if (pv_cond->evaluate(*state, t)) {
		throw std::runtime_error{
			"demo 16 pin 5 FAILED: PlayerVillager condition is true "
			"before PlayerVillager is removed (condition is not "
			"identity-bound)"
		};
	}
	state->remove_game_entity(pv_id);
	if (not pv_cond->evaluate(*state, t)) {
		throw std::runtime_error{
			"demo 16 pin 5 FAILED: PlayerVillager condition is false "
			"after PlayerVillager was removed"
		};
	}
	log::log(MSG(info) << "demo 16: pin 5 PASS (identity-bound condition)");

	// ---- Pin 3: unresolved FQON -> permanently false ----------------
	auto unresolved = scenario::trigger::make_trigger_condition(
		*state, openage::time::time_t{0},
		make_entity_removed_ir("ww_mvp.campaign.william_wallace."
		                       "mission_01.NoSuchInstance"));
	if (not unresolved) {
		throw std::runtime_error{
			"demo 16 pin 3 FAILED: make_trigger_condition returned "
			"nullptr for an unresolved FQON (should still construct)"
		};
	}
	if (unresolved->evaluate(*state, t)) {
		throw std::runtime_error{
			"demo 16 pin 3 FAILED: unresolved-FQON condition is true"
		};
	}
	if (not unresolved->dependencies().empty()) {
		throw std::runtime_error{
			"demo 16 pin 3 FAILED: unresolved-FQON condition has "
			"non-empty dependencies"
		};
	}
	log::log(MSG(info) << "demo 16: pin 3 PASS (unresolved FQON permanently false)");

	log::log(MSG(info) << "DEMO 16: PASS (EntityRemovedCondition)");
}


} // namespace openage::gamestate::tests
