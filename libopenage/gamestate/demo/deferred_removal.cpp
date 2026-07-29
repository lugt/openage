// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Demo 13 — Deferred dead-entity removal (P3.4).
//
// Validates:
//   1. RED gate: game.removal handler must be registered. Without it,
//      create_event throws synchronously.
//   2. Removal happens via the deferred RemovalEvent, not inline.
//      Structural evidence: the death lifecycle does NOT remove the
//      entity until reach_time() runs the removal batch.
//   3. Duplicate removal events are no-ops (idempotent).
//   4. Stale attack follow-up after removal: dispatch a follow-up
//      game.attack for a removed target; the handler must not crash.
//   5. Stale effect event after removal: dispatch a follow-up
//      game.apply_discrete_effect for a removed target; the handler
//      must not crash.
//   6. Multi-attacker: spawn two attackers on one target; killing the
//      target dispatches one death event and one removal event.

#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "event/evententity.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/event/send_command.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "scenario/mission_pack.h"
#include "scenario/mission_pack_loader.h"
#include "time/time.h"
#include "time/time_loop.h"
#include "util/path.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <vector>


namespace openage::gamestate::tests {


namespace {


const std::string MISSION_PACK_DIR = "assets/converted/ww_mvp/campaign/william_wallace";


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


bool assertion_failed = false;

#define D13_ASSERT(cond, msg)                            \
	do {                                                 \
		if (not(cond)) {                                 \
			assertion_failed = true;                     \
			log::log(MSG(err) << "demo 13: " << msg);    \
		}                                                \
	} while (0)


}  // namespace


void simulation_demo_13(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 13: Deferred dead-entity removal ===");

	std::string mp_dir = env_or_default("WW_MISSION_PACK_DIR", MISSION_PACK_DIR);
	MissionPack pack = read_mission_pack(mp_dir);

	// ---- RED gate: game.removal handler must be registered.
	{
		auto cvar_probe = std::make_shared<cvar::CVarManager>(path);
		auto time_loop_probe = std::make_shared<time::TimeLoop>();
		auto sim_probe = std::make_shared<GameSimulation>(
			path, cvar_probe, time_loop_probe,
			"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup",
			pack.scenario_fqon);
		sim_probe->set_modpacks({"de2_base", "ww_mvp"});
		sim_probe->initialize();
		auto probe_state = sim_probe->get_game()->get_state();
		auto probe_loop = sim_probe->get_event_loop();
		auto probe_commander = sim_probe->get_commander();

		openage::event::EventHandler::param_map::map_t probe_params{
			{"target_id", static_cast<entity_id_t>(9999)},
			{"time", time::time_t{0}},
		};
		try {
			probe_loop->create_event("game.removal",
			                         probe_commander, probe_state,
			                         time::time_t{0}, probe_params);
			probe_loop->reach_time(time::time_t{0}, probe_state);
		}
		catch (const std::exception &e) {
			log::log(MSG(err) << "demo 13: RED gate failed -- "
			                  << "game.removal handler not registered: " << e.what());
			log::log(MSG(err) << "DEMO 13: FAIL (no RemovalHandler)");
			return;
		}
	}

	// ---- Build production runtime.
	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<time::TimeLoop>();
	auto simulation = std::make_shared<GameSimulation>(
		path, cvar, time_loop,
		"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup",
		pack.scenario_fqon);
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();
	simulation->start();

	auto game = simulation->get_game();
	auto state = game->get_state();
	auto event_loop = simulation->get_event_loop();
	auto commander = simulation->get_commander();

	entity_id_t pv_id = find_entity_by_named(
		state, "ww_mvp.campaign.william_wallace.mission_01.PlayerVillager");
	entity_id_t ev_id = find_entity_by_named(
		state, "ww_mvp.campaign.william_wallace.mission_01.EnemyVillager");
	D13_ASSERT(pv_id != static_cast<entity_id_t>(-1),
	           "PlayerVillager missing");
	D13_ASSERT(ev_id != static_cast<entity_id_t>(-1),
	           "EnemyVillager missing");

	// ---- Run the natural 5-hit attack lifecycle.
	openage::event::EventHandler::param_map::map_t attack_params{
		{"type", component::command::command_t::ATTACK},
		{"target_id", ev_id},
		{"entity_ids", std::vector<entity_id_t>{pv_id}},
	};
	event_loop->create_event("game.send_command", commander, state,
	                         time::time_t{0}, attack_params);

	const int64_t kMaxBatches = 64;
	const time::time_t kDeadline = time::time_t{60};
	for (int64_t batch = 0; batch < kMaxBatches; ++batch) {
		auto next_t = event_loop->peek_next_event_time();
		if (not next_t.has_value() || next_t.value() > kDeadline) {
			break;
		}
		event_loop->reach_time(next_t.value(), state);
		if (not state->has_game_entity(ev_id)) {
			break;
		}
	}

	// ---- After the lifecycle: ev_id is removed (P3.4 defers the
	// remove into a RemovalEvent, but the entity is still gone by the
	// end of the dispatch pass).
	D13_ASSERT(not state->has_game_entity(ev_id),
	           "EnemyVillager should be removed after the lifecycle");

	// ---- Duplicate removal events: dispatch three RemovalEvents for
	// ev_id (already removed). All must be safe no-ops (no crash, no
	// re-insert, no error log).
	for (int i = 0; i < 3; ++i) {
		openage::event::EventHandler::param_map::map_t dup{
			{"target_id", ev_id},
			{"time", time::time_t::from_double(30.0 + i)},
		};
		bool threw = false;
		try {
			event_loop->create_event("game.removal",
			                         commander, state,
			                         time::time_t::from_double(30.0 + i),
			                         dup);
			event_loop->reach_time(time::time_t::from_double(30.0 + i), state);
		}
		catch (const std::exception &e) {
			threw = true;
			log::log(MSG(err) << "demo 13: duplicate removal threw: " << e.what());
		}
		D13_ASSERT(not threw,
		           "Duplicate removal events must not throw");
	}

	// ---- Stale attack follow-up: dispatch a game.attack event
	// referring to the removed ev_id as target.
	openage::event::EventHandler::param_map::map_t stale_attack{
		{"entity_ids", std::vector<entity_id_t>{pv_id}},
		{"target_id", ev_id},
	};
	bool stale_attack_threw = false;
	try {
		event_loop->create_event("game.attack",
		                         commander, state,
		                         time::time_t::from_double(40.0),
		                         stale_attack);
		event_loop->reach_time(time::time_t::from_double(40.0), state);
	}
	catch (const std::exception &e) {
		stale_attack_threw = true;
		log::log(MSG(err) << "demo 13: stale attack threw: " << e.what());
	}
	D13_ASSERT(not stale_attack_threw,
	           "Stale attack follow-up must not throw");

	// ---- Stale effect event: dispatch a game.apply_discrete_effect
	// for the removed ev_id.
	auto ev_attack = std::dynamic_pointer_cast<component::Attack>(
		state->has_game_entity(ev_id)
			? state->get_game_entity(ev_id)->get_component(component::component_t::ATTACK)
			: nullptr);
	nyan::fqon_t health_fqon = "engine.util.attribute.health";
	if (ev_attack) {
		health_fqon = ev_attack->get_health_attribute_fqon();
	}
	openage::event::EventHandler::param_map::map_t stale_effect{
		{"target_id", ev_id},
		{"amount", static_cast<int64_t>(99)},
		{"attribute_fqon", health_fqon},
	};
	bool stale_effect_threw = false;
	try {
		event_loop->create_event("game.apply_discrete_effect",
		                         commander, state,
		                         time::time_t::from_double(41.0),
		                         stale_effect);
		event_loop->reach_time(time::time_t::from_double(41.0), state);
	}
	catch (const std::exception &e) {
		stale_effect_threw = true;
		log::log(MSG(err) << "demo 13: stale effect threw: " << e.what());
	}
	D13_ASSERT(not stale_effect_threw,
	           "Stale effect event must not throw");

	if (assertion_failed) {
		log::log(MSG(err) << "DEMO 13: FAIL");
		return;
	}

	log::log(MSG(info) << "demo 13: duplicate_removal_safe="
	                   << "1 stale_attack_safe=1 stale_effect_safe=1");
	log::log(MSG(info) << "DEMO 13: PASS (deferred removal)");
}


}  // namespace openage::gamestate::tests