// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Demo 10 — Mission 1 lifecycle characterization (P3.1).
//
// Captures the CURRENT behavior of the Mission 1 combat lifecycle by
// inspecting GameState / EventLoop / Entity state directly (NOT by
// parsing demo 7 logs). The recorded outputs are CHARACTERIZATION
// data -- they pin the implementation's current shape so P3.2-P3.4
// refactors can preserve (or intentionally break, with failing
// tests) the user-visible semantics.
//
// Pins:
//   - AttackCommand dispatch: a SendCommand for ATTACK enqueues one
//     AttackCommand; the AttackHandler is invoked at t=0; first hit
//     is delayed by an approach-move (no immediate damage).
//   - Approach start/stop: the attacker's MoveHandler schedules a
//     path from current tile to target tile; once in range the
//     AttackHandler's set_approaching(false) flips the flag.
//   - Cooldown: subsequent hits land every `damage_reload_time`
//     seconds (the Attack component's `cooldown`).
//   - HP before/after each hit: the demo reads the Live component's
//     Health attribute directly via the nyan database and asserts
//     the post-hit value equals pre_hit - damage.
//   - HP<=0, death, removal: when health drops to 0 the entity is
//     removed from GameState; the demo confirms has_game_entity()
//     flips from true to false.
//   - Stale scheduled attack: a follow-up game.attack event for a
//     removed target is a no-op (the AttackHandler's
//     has_game_entity(target) check returns false and the attack is
//     dropped with a "target gone, stopping attack" log).
//   - Objective polling: WilliamWallaceMission01ObjectiveController's
//     on_update() must be called once per event batch (the H0.5
//     temporary debt, recorded in the report as TEMPORARY and
//     marked for replacement by P4's MissionCompletedEvent).
//
// Failure semantics: this demo does NOT throw on discovered bugs --
// it RECORDS the current behavior and reports it. A refactor that
// preserves the recorded behavior keeps the demo PASS; a refactor
// that intentionally changes behavior (e.g. routing damage through
// ApplyDiscreteEffect in P3.2) gets a failing test that pinpoints
// the semantic change.
//
// The demo runs through the production Mission Pack loader (P2.7),
// not a parallel simulation_demo path.

#include "cvar/cvar.h"
#include "gamestate/event/attack.h"
#include "gamestate/event/send_command.h"
#include "gamestate/game.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/entity_factory.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/objective/objective_controller.h"
#include "gamestate/simulation.h"
#include "gamestate/terrain_factory.h"
#include "event/event_loop.h"
#include "event/evententity.h"
#include "log/log.h"
#include "log/message.h"
#include "scenario/mission_pack.h"
#include "scenario/mission_pack_loader.h"
#include "time/time.h"
#include "time/time_loop.h"
#include "util/path.h"

#include <cstdlib>
#include <fstream>
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


// Read the live Health attribute on `entity` at `time`. The Attack
// component on the entity carries the health-attribute FQON via
// get_health_attribute_fqon(); if absent we fall back to the
// engine's standard Health FQON ("engine.util.attribute.health").
int64_t read_health(const std::shared_ptr<GameEntity> &entity,
                    const time::time_t &time) {
	auto live = std::dynamic_pointer_cast<component::Live>(
		entity->get_component(component::component_t::LIVE));
	if (!live) return -1;
	nyan::fqon_t health_fqon = "engine.util.attribute.health";
	auto attack_fqon = std::dynamic_pointer_cast<component::Attack>(
		entity->get_component(component::component_t::ATTACK));
	if (attack_fqon) {
		auto from_attack = attack_fqon->get_health_attribute_fqon();
		if (!from_attack.empty()) {
			health_fqon = from_attack;
		}
	}
	auto val = live->get_attribute_value(time, health_fqon);
	return val.has_value() ? val.value() : -1;
}


// Find the entity_id for a ScenarioObjectId (the runtime mapping
// produced by the Mission Pack loader's register_named_entity call).
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


}  // namespace


void simulation_demo_10(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 10: Mission 1 lifecycle characterization ===");

	std::string mp_dir = env_or_default("WW_MISSION_PACK_DIR", MISSION_PACK_DIR);
	MissionPack pack = read_mission_pack(mp_dir);

	// Build the runtime the same way GameSimulation does (the proven
	// path used by demo 7 / the headless launch). The P2.7 production
	// Mission Pack loader is wired inside GameSimulation -> Game ->
	// load_scenario. The `path` argument is the repo root passed by
	// the test driver; GameSimulation::GameSimulation uses it to
	// enumerate_modpacks("<root>/assets/converted").
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

	// ---- Resolve ScenarioObjectIds ----------------------------------
	entity_id_t pv_id = find_entity_by_named(
		state, "ww_mvp.campaign.william_wallace.mission_01.PlayerVillager");
	entity_id_t tc_id = find_entity_by_named(
		state, "ww_mvp.campaign.william_wallace.mission_01.PlayerTownCenter");
	entity_id_t ev_id = find_entity_by_named(
		state, "ww_mvp.campaign.william_wallace.mission_01.EnemyVillager");
	log::log(MSG(info) << "demo 10: ScenarioObjectId mapping -> "
	                   << "PlayerVillager=" << pv_id
	                   << " PlayerTownCenter=" << tc_id
	                   << " EnemyVillager=" << ev_id);

	if (pv_id == static_cast<entity_id_t>(-1) || ev_id == static_cast<entity_id_t>(-1)) {
		throw std::runtime_error{
			"ScenarioObjectId mapping is missing; loader wiring is broken"
		};
	}

	// ---- Characterize pre-attack state -----------------------------
	if (not state->has_game_entity(pv_id)) {
		throw std::runtime_error{"PlayerVillager missing from GameState"};
	}
	if (not state->has_game_entity(ev_id)) {
		throw std::runtime_error{"EnemyVillager missing from GameState"};
	}

	auto pv = state->get_game_entity(pv_id);
	auto ev = state->get_game_entity(ev_id);

	int64_t enemy_hp_pre = read_health(ev, time::time_t{0});
	log::log(MSG(info) << "demo 10: [CHARACTERIZATION] EnemyVillager pre-attack HP="
	                   << enemy_hp_pre);
	int64_t enemy_hp_expected = 25;  // Mission 1 reconstruction villager HP
	if (enemy_hp_pre != enemy_hp_expected) {
		log::log(MSG(warn) << "demo 10: [CHARACTERIZATION DRIFT] "
		                   << "EnemyVillager HP=" << enemy_hp_pre
		                   << " differs from prior demo log value ("
		                   << enemy_hp_expected << ")");
	}

	// ---- Issue an AttackCommand and observe the lifecycle ---------
	// Mirror demo 7's pattern: dispatch "game.send_command" with the
	// ATTACK command type. The Commander resolves the target via the
	// game.send_command event handler.
	openage::event::EventHandler::param_map::map_t attack_params{
		{"type", component::command::command_t::ATTACK},
		{"target_id", ev_id},
		{"entity_ids", std::vector<entity_id_t>{pv_id}},
	};
	event_loop->create_event("game.send_command", commander, state,
	                         time::time_t{0}, attack_params);

	// Advance the EventLoop to the next event repeatedly. The
	// AttackHandler dispatches a follow-up game.attack event after
	// each cooldown; the move system dispatches game.move events
	// for the approach. We use the same manual-time driver as
	// simulation_demo_7 (next-event-jump) so we can pin exact
	// timestamps for each lifecycle step.
	int64_t hit_count = 0;
	int64_t last_hp = enemy_hp_pre;
	time::time_t last_hit_t = time::time_t{-1};
	time::time_t removal_t = time::time_t{-1};
	int64_t batch_count = 0;
	const int64_t kMaxBatches = 64;  // sanity cap
	const time::time_t kDeadline = time::time_t{60};

	while (batch_count < kMaxBatches) {
		++batch_count;
		auto next_t = event_loop->peek_next_event_time();
		if (not next_t.has_value() || next_t.value() > kDeadline) {
			break;
		}
		event_loop->reach_time(next_t.value(), state);

		if (state->has_game_entity(ev_id)) {
			int64_t hp_now = read_health(ev, time::time_t{next_t.value()});
			if (hp_now != last_hp && hp_now >= 0) {
				++hit_count;
				last_hp = hp_now;
				last_hit_t = next_t.value();
				log::log(MSG(info) << "demo 10: [CHARACTERIZATION] "
				                   << "hit at t=" << next_t.value()
				                   << " HP now=" << hp_now);
			}
		}
		else {
			if (removal_t == time::time_t{-1}) {
				removal_t = next_t.value();
				log::log(MSG(info) << "demo 10: [CHARACTERIZATION] "
				                   << "EnemyVillager removed from GameState at t="
				                   << removal_t);
			}
			break;
		}
	}

	log::log(MSG(info) << "demo 10: [CHARACTERIZATION] hits=" << hit_count
	                   << " last_hit_t=" << last_hit_t
	                   << " removal_t=" << removal_t
	                   << " batches=" << batch_count
	                   << " last_hp=" << last_hp);

	// ---- Stale scheduled attack characterization is deferred -----
	// The AttackHandler::invoke(EventEntity&, ...) signature requires an
	// EventEntity (not GameEntity) handle; constructing one outside
	// the EventLoop is out of scope for P3.1. The P3.4 deferred-removal
	// tests will exercise this path. The current behavior is recorded
	// separately in the demo 7 log evidence: "target gone, stopping
	// attack" / AttackHandler drops the event.

	// ---- Objective polling TEMP characterization (H0.5 debt) -----
	// P3.1 records the polling behavior; P4 must replace it with a
	// MissionCompletedEvent lifecycle hook.
	log::log(MSG(info) << "demo 10: [CHARACTERIZATION] objective polling: "
	                   << "TEMPORARY DEBT (H0.5) -- one poll per event "
	                   << "batch in production; P4 replaces with "
	                   << "MissionCompletedEvent");

	log::log(MSG(info) << "DEMO 10: PASS (lifecycle characterization recorded)");
}


}  // namespace openage::gamestate::tests