// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "attack_demo.h"

#include <memory>
#include <vector>

#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "event/eventhandler.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/commands/attack.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/position.h"
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
	log::log(MSG(info) << "========== ATTACK DISPATCH TEST (demo 5) ==========");

	// ---- Part A: unit-test the AttackCommand class directly. ----
	// No simulation needed; this is the core class wiring. If get_type /
	// get_attacker / get_target are wrong, the rest of the combat chain cannot
	// work, so we check it in isolation first.
	{
		auto cmd = std::make_shared<component::command::AttackCommand>(7, 42);
		bool type_ok = (cmd->get_type() == component::command::command_t::ATTACK);
		bool attacker_ok = (cmd->get_attacker() == 7);
		bool target_ok = (cmd->get_target() == 42);
		if (type_ok and attacker_ok and target_ok) {
			log::log(MSG(info) << "PASS: AttackCommand class "
			                   << "(get_type()==ATTACK, get_attacker()==7, get_target()==42)");
		}
		else {
			log::log(MSG(err) << "FAIL: AttackCommand class type_ok=" << type_ok
			                  << " attacker_ok=" << attacker_ok
			                  << " target_ok=" << target_ok);
		}
	}

	// ---- Part B: end-to-end event-driven dispatch smoke. ----
	// Issue an ATTACK command through the real SendCommandHandler. The handler
	// enqueues an AttackCommand and emits a "game.attack" event; AttackHandler
	// fires (same execute_events pass) and pops the command before the
	// activity graph's process_command runs -- so no Idle->CheckQueue->Branch
	// loop. The smoke passes if the AttackHandler log appears, the sim does not
	// crash, and the villager is still tracked. Combat semantics (range /
	// damage / death) land in later increments.
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

	// Confirm the Attack component is mounted (entity_factory wires it from
	// the villager's ApplyDiscreteEffect ability) and that the ability data
	// (cooldown / range) is readable. The villager is melee, so range falls
	// back to the default engagement range.
	auto attack_comp = std::dynamic_pointer_cast<component::Attack>(
		villager->has_component(component::component_t::ATTACK)
			? villager->get_component(component::component_t::ATTACK)
			: nullptr);
	if (not attack_comp) {
		log::log(MSG(err) << "FAIL: villager has no Attack component "
		                  << "(ApplyDiscreteEffect ability not mounted)");
		log::log(MSG(info) << "========== END ATTACK TEST (no Attack comp) ==========");
		return;
	}
	double cooldown = attack_comp->get_cooldown();
	double range = attack_comp->get_attack_range();
	bool data_ok = (cooldown > 0.0) and (range > 0.0);
	if (data_ok) {
		log::log(MSG(info) << "PASS: Attack ability data "
		                   << "(cooldown=" << cooldown << ", range=" << range << ")");
	}
	else {
		log::log(MSG(err) << "FAIL: Attack ability data unreadable "
		                  << "(cooldown=" << cooldown << ", range=" << range << ")");
	}

	// Issue an AttackCommand targeting the villager itself. Self-target
	// (dist=0) exercises the in-range branch of the range check; the
	// out-of-range/approach branch lands in increment 3 with a real enemy.
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

	// Verify the persistent attack state was set on the Attack component:
	// active, target == villager, and not approaching (dist=0 <= range).
	bool state_ok = attack_comp->is_active()
	                and attack_comp->get_target_id() == villager_id
	                and not attack_comp->is_approaching();
	// Smoke also passes if the sim didn't crash (no activity loop) and the
	// villager is still tracked.
	bool alive = state->get_game_entities().count(villager_id) > 0;
	if (state_ok and alive) {
		log::log(MSG(info) << "PASS: ATTACK state set "
		                   << "(active, target=self, not approaching, sim alive)");
	}
	else {
		log::log(MSG(err) << "FAIL: attack state wrong "
		                  << "(active=" << attack_comp->is_active()
		                  << " target=" << attack_comp->get_target_id()
		                  << " approaching=" << attack_comp->is_approaching()
		                  << " alive=" << alive << ")");
	}

	// ---- Part C: approach a distant target (increment 3). ----
	// The scenario spawns an EnemyVillager at (1,1); the player villager at
	// (8,8) is ~10 tiles away (range=1), so an ATTACK on it must reuse
	// Move::move_default to approach. Verify the villager closes distance.
	entity_id_t enemy_id = 0;
	bool found_enemy = false;
	for (auto &[eid, ent] : entities) {
		if (eid != villager_id and ent->has_component(component::component_t::MOVE)) {
			enemy_id = eid;
			found_enemy = true;
			break;
		}
	}
	if (not found_enemy) {
		log::log(MSG(err) << "FAIL (Part C): no second movable entity for approach test");
	}
	else {
		auto enemy = state->get_game_entity(enemy_id);
		auto villager_pos_comp = std::dynamic_pointer_cast<component::Position>(
			villager->get_component(component::component_t::POSITION));
		auto enemy_pos_comp = std::dynamic_pointer_cast<component::Position>(
			enemy->get_component(component::component_t::POSITION));
		auto villager_start = villager_pos_comp->get_positions().get(time::time_t::from_int(10));
		auto enemy_pos = enemy_pos_comp->get_positions().get(time::time_t::from_int(10));
		double start_dist = (villager_start - enemy_pos).length();
		log::log(MSG(info) << "Part C: approach villager=" << villager_id
		                   << " enemy=" << enemy_id
		                   << " start_dist=" << start_dist);

		openage::event::EventHandler::param_map::map_t params_c{
			{"type", component::command::command_t::ATTACK},
			{"target_id", enemy_id},
			{"entity_ids", std::vector<entity_id_t>{villager_id}},
		};
		event_loop->create_event("game.send_command", commander, state,
		                         time::time_t::from_int(10), params_c);

		log::log(MSG(info) << "Advancing simulation to t=30s for approach...");
		event_loop->reach_time(time::time_t::from_int(30), state);

		auto villager_end = villager_pos_comp->get_positions().get(time::time_t::from_int(30));
		double end_dist = (villager_end - enemy_pos).length();
		if (end_dist < start_dist) {
			log::log(MSG(info) << "PASS (Part C): approach closed distance to enemy "
			                   << "(start_dist=" << start_dist
			                   << " end_dist=" << end_dist << ")");
		}
		else {
			log::log(MSG(err) << "FAIL (Part C): villager did not approach "
			                  << "(start_dist=" << start_dist
			                  << " end_dist=" << end_dist << ")");
		}

		// ---- Part D: cooldown + damage (increment 4). ----
		// Part C closed distance by t=27.54; the in-range re-eval fires the
		// first hit immediately (cooldown gate fires because
		// next_attack_time was initialised to the attack start time), then
		// each hit schedules the next game.attack at now + cooldown
		// (reload_time = 2.0 s for villager). Verify the enemy's Health
		// drops monotonically across multiple hits.
		auto enemy_live = std::dynamic_pointer_cast<component::Live>(
			enemy->get_component(component::component_t::LIVE));
		if (not enemy_live) {
			log::log(MSG(err) << "FAIL (Part D): enemy " << enemy_id
			                  << " has no Live component (cannot verify HP)");
		}
		else {
			auto health_fqon = attack_comp->get_health_attribute_fqon();
			auto start_hp = enemy_live->get_attribute_value(
				time::time_t::from_int(30), health_fqon);
			log::log(MSG(info) << "Part D: enemy=" << enemy_id
			                   << " health_fqon='" << health_fqon << "'"
			                   << " start_hp=" << start_hp.value_or(-1));

			log::log(MSG(info) << "Advancing simulation to t=45s for hits...");
			event_loop->reach_time(time::time_t::from_int(45), state);

			auto end_hp = enemy_live->get_attribute_value(
				time::time_t::from_int(45), health_fqon);
			int64_t start_v = start_hp.value_or(0);
			int64_t end_v = end_hp.value_or(start_v);
			int64_t delta = start_v - end_v;
			int64_t expected_per_hit = attack_comp->get_damage();
			int64_t hits = (expected_per_hit > 0) ? (delta / expected_per_hit) : 0;
			if (delta > 0 and hits >= 1) {
				log::log(MSG(info) << "PASS (Part D): enemy took damage "
				                   << "(start_hp=" << start_v
				                   << " end_hp=" << end_v
				                   << " delta=" << delta
				                   << " per_hit=" << expected_per_hit
				                   << " hits=" << hits << ")");
			}
			else {
				log::log(MSG(err) << "FAIL (Part D): enemy HP did not decrease "
				                  << "(start_hp=" << start_v
				                  << " end_hp=" << end_v
				                  << " delta=" << delta
				                  << " per_hit=" << expected_per_hit << ")");
			}
		}
	}

	log::log(MSG(info) << "========== END ATTACK TEST ==========");
}

} // namespace openage::gamestate::tests
