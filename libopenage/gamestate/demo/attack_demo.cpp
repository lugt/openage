// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "attack_demo.h"

#include <memory>
#include <optional>
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
#include "gamestate/entity_factory.h"
#include "gamestate/manager.h"
#include "gamestate/simulation.h"
#include "gamestate/types.h"
#include "gamestate/objective/objective_controller.h"
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
		"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup",
		"ww_mvp.campaign.william_wallace.mission_01.Mission");
	simulation->set_modpacks({"de2_base", "ww_mvp"});

	log::log(MSG(info) << "Initializing simulation (loads Mission 1 scenario)...");
	simulation->initialize();
	simulation->start();

	auto game = simulation->get_game();
	auto state = game->get_state();
	auto event_loop = simulation->get_event_loop();
	auto commander = simulation->get_commander();

	// Find the player villager by its PlacedEntity instance FQON. The
	// scenario spawns PlayerVillager + EnemyVillager (both movable), so
	// iterating the entity map and picking "the first movable" is order-
	// dependent and previously picked the EnemyVillager -- inverting the
	// attack direction (enemy attacking player). Resolving by instance FQON
	// makes the attacker unambiguously the player's villager.
	entity_id_t villager_id = 0;
	bool found = false;
	auto player_id_opt = state->get_entity_id_by_instance(
		"ww_mvp.campaign.william_wallace.mission_01.PlayerVillager");
	if (player_id_opt.has_value()) {
		villager_id = *player_id_opt;
		found = state->has_game_entity(villager_id);
	}
	if (not found) {
		log::log(MSG(err) << "FAIL: PlayerVillager instance not found. "
		                  << "Cannot test ATTACK dispatch.");
		log::log(MSG(info) << "========== END ATTACK TEST (no villager) ==========");
		return;
	}
	log::log(MSG(info) << "Found player villager id=" << villager_id
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
	// Increment 5 note: only advance to t=1 (one hit, HP 13->7) and then
	// stop_attack() -- the self-attack follow-up chain would otherwise keep
	// landing hits (t=2,4,...) and, now that HP<=0 removes the entity, kill
	// the villager mid-test. Stopping here leaves the attacker idle for the
	// Part C re-target against the real enemy.
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

	log::log(MSG(info) << "Advancing simulation to t=1s (one self-hit)...");
	event_loop->reach_time(time::time_t::from_int(1), state);

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

	// Terminate the self-attack follow-up chain so it cannot kill the
	// villager across later reach_time() calls (HP<=0 now removes the
	// entity, per increment 5). The queued game.attack follow-ups still
	// fire but exit via the AttackHandler "no active attack" guard.
	attack_comp->stop_attack();

	// Part F prep: build the mission_1 trigger now, while the enemy is
	// still alive. create_mission_1_trigger resolves the EnemyVillager
	// instance FQON to its entity ID via GameState::get_entity_id_by_instance
	// and snapshots that target set; on_update() is polled in Part F after
	// the enemy has been killed. Only the enemy instance is tracked, so the
	// player villager (the attacker here) dying could not satisfy it.
	auto mission_trigger = create_mission_1_trigger(state);

	// ---- Part C: approach a distant target (increment 3). ----
	// The scenario spawns an EnemyVillager at (1,1); the player villager at
	// (8,8) is ~10 tiles away (range=1), so an ATTACK on it must reuse
	// Move::move_default to approach. Verify the villager closes distance.
	// Resolve the enemy by PlacedEntity instance FQON (same reason as Part B:
	// map iteration order is not a reliable identity).
	entity_id_t enemy_id = 0;
	bool found_enemy = false;
	auto enemy_id_opt = state->get_entity_id_by_instance(
		"ww_mvp.campaign.william_wallace.mission_01.EnemyVillager");
	if (enemy_id_opt.has_value()) {
		enemy_id = *enemy_id_opt;
		found_enemy = state->has_game_entity(enemy_id);
	}
	if (not found_enemy) {
		log::log(MSG(err) << "FAIL (Part C): EnemyVillager instance not found for approach test");
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

		// ---- Part E: death + entity removal (increment 5). ----
		// Part D drove enemy Health to <= 0 on the first lethal hit (well
		// before t=45). The attack hit path must (a) remove the dead entity
		// from the state index and (b) stop the attacker's follow-up loop.
		// Verify the enemy is gone from the index and the attacker is idle.
		bool enemy_present = state->has_game_entity(enemy_id);
		size_t enemy_in_index = 0;
		for (const auto &[eid, ent] : state->get_game_entities()) {
			(void)ent;
			if (eid == enemy_id) {
				enemy_in_index++;
			}
		}
		bool attacker_idle = not attack_comp->is_active();
		if (not enemy_present and enemy_in_index == 0 and attacker_idle) {
			log::log(MSG(info) << "PASS (Part E): enemy " << enemy_id
			                   << " removed on death; attacker stopped "
			                   << "(present=" << enemy_present
			                   << " in_index=" << enemy_in_index
			                   << " attacker_active=" << (not attacker_idle) << ")");
		}
		else {
			log::log(MSG(err) << "FAIL (Part E): death/removal failed "
			                  << "(present=" << enemy_present
			                  << " in_index=" << enemy_in_index
			                  << " attacker_active=" << (not attacker_idle) << ")");
		}

		// ---- Part F: mission victory (increment 6). ----
		// The mission_1 trigger's defeat_enemy step was snapshotted in
		// Part B (enemy still alive). Now that Part E confirmed the
		// enemy is gone from the index, on_update() must see a missing
		// movable id, complete the step, and flip is_finished().
		mission_trigger->on_update(time::time_t::from_int(45));
		if (mission_trigger->is_finished()) {
			log::log(MSG(info) << "PASS (Part F): mission_1 trigger "
			                   << "finished after enemy defeated "
			                   << "(step=" << mission_trigger->current_step()
			                   << " steps=" << mission_trigger->step_count() << ")");
		}
		else {
			log::log(MSG(err) << "FAIL (Part F): mission_1 trigger "
			                  << "not finished (step="
			                  << mission_trigger->current_step()
			                  << " finished=" << mission_trigger->is_finished()
			                  << " steps=" << mission_trigger->step_count() << ")");
		}
	}

	log::log(MSG(info) << "========== END ATTACK TEST ==========");
}


// ---------------------------------------------------------------------------
// demo 6 -- victory-semantics integration test.
//
// The mission_1 trigger's defeat_enemy step must fire only when the explicit
// enemy target set is fully eliminated. Each scenario loads a fresh Mission 1
// (so entity removals don't bleed across cases) and resolves the enemy by
// PlacedEntity instance FQON via GameState::get_entity_id_by_instance -- no
// spawn-order / "all movables" heuristics.
// ---------------------------------------------------------------------------

namespace {

// PlacedEntity instance FQONs declared in mission_1.nyan. These are the
// placed-instance FQONs (their own object names), not the referenced
// GameEntity FQONs.
constexpr const char *PLAYER_VILLAGER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerVillager";
constexpr const char *ENEMY_VILLAGER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.EnemyVillager";
constexpr const char *PLAYER_TC_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerTownCenter";
// A bystander spawned by the test (not in the scenario file) to represent
// an unrelated movable. Registered under a synthetic instance FQON so it
// is distinguishable from the objective targets.
constexpr const char *BYSTANDER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.Bystander";
// Real GameEntity FQON for spawning a villager (reused from mission_1.nyan).
constexpr const char *VILLAGER_ENTITY_FQON =
	"de2_base.data.game_entity.generic.villager.villager.Villager";

// One fresh Mission 1 load. Holds the simulation alive so the state stays
// valid for the scenario's duration.
struct Mission1Fixture {
	std::shared_ptr<GameSimulation> sim;
	std::shared_ptr<GameState> state;
	std::shared_ptr<openage::event::EventLoop> event_loop;
	std::shared_ptr<EntityFactory> factory;
	// optionals because entity id 0 is a valid id (the first spawned entity
	// is PlayerVillager with id 0); a truthy id check would misread it as
	// "not found". has_value() is the real lookup-success signal.
	std::optional<entity_id_t> enemy_id;
	std::optional<entity_id_t> player_id;
	std::optional<entity_id_t> tc_id;
};

Mission1Fixture load_mission1(const util::Path &path) {
	Mission1Fixture f;
	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<time::TimeLoop>();
	f.sim = std::make_shared<GameSimulation>(
		path,
		cvar,
		time_loop,
		"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup",
		"ww_mvp.campaign.william_wallace.mission_01.Mission");
	f.sim->set_modpacks({"de2_base", "ww_mvp"});
	f.sim->initialize();
	f.sim->start();

	auto game = f.sim->get_game();
	f.state = game->get_state();
	f.event_loop = f.sim->get_event_loop();
	f.factory = f.sim->get_entity_factory();
	f.enemy_id = f.state->get_entity_id_by_instance(ENEMY_VILLAGER_FQON);
	f.player_id = f.state->get_entity_id_by_instance(PLAYER_VILLAGER_FQON);
	f.tc_id = f.state->get_entity_id_by_instance(PLAYER_TC_FQON);
	return f;
}

} // namespace

void simulation_demo_6(const util::Path &path) {
	log::log(MSG(info) << "========== VICTORY SEMANTICS TEST (demo 6) ==========");

	// ---- Part A: removing the EnemyVillager completes the trigger. ----
	{
		auto f = load_mission1(path);
		log::log(MSG(info) << "Part A: enemy=" << f.enemy_id.value_or((entity_id_t)-1)
		                   << " player=" << f.player_id.value_or((entity_id_t)-1)
		                   << " tc=" << f.tc_id.value_or((entity_id_t)-1));
		if (not(f.enemy_id and f.player_id and f.tc_id)) {
			log::log(MSG(err) << "FAIL (Part A): instance->ID lookup incomplete");
		}
		else {
			auto trigger = create_mission_1_trigger(f.state);
			f.state->remove_game_entity(*f.enemy_id);
			trigger->on_update(time::time_t::from_int(1));
			if (trigger->is_finished()) {
				log::log(MSG(info) << "PASS (Part A): enemy removed -> victory");
			}
			else {
				log::log(MSG(err) << "FAIL (Part A): enemy removed but trigger "
				                  << "not finished (finished="
				                  << trigger->is_finished() << ")");
			}
		}
	}

	// ---- Part B: removing the PlayerVillager does NOT complete it. ----
	{
		auto f = load_mission1(path);
		auto trigger = create_mission_1_trigger(f.state);
		f.state->remove_game_entity(*f.player_id);
		trigger->on_update(time::time_t::from_int(1));
		if (not trigger->is_finished()) {
			log::log(MSG(info) << "PASS (Part B): player removed -> no victory "
			                   << "(enemy still alive)");
		}
		else {
			log::log(MSG(err) << "FAIL (Part B): player removed but trigger "
			                  << "finished (false victory)");
		}
	}

	// ---- Part C: removing an unrelated bystander movable does NOT complete. ----
	{
		auto f = load_mission1(path);
		// Spawn a bystander villager (has MOVE) under a synthetic instance
		// FQON so it is not an objective target.
		auto bystander = f.factory->add_game_entity(
			f.event_loop, f.state, 0, VILLAGER_ENTITY_FQON);
		f.state->add_game_entity(bystander);
		f.state->register_named_entity(BYSTANDER_FQON, bystander->get_id());

		auto trigger = create_mission_1_trigger(f.state);
		f.state->remove_game_entity(bystander->get_id());
		trigger->on_update(time::time_t::from_int(1));
		if (not trigger->is_finished()) {
			log::log(MSG(info) << "PASS (Part C): bystander removed -> no victory");
		}
		else {
			log::log(MSG(err) << "FAIL (Part C): bystander removed but trigger "
			                  << "finished (false victory)");
		}
	}

	// ---- Part D: multiple targets -- ALL must be removed to complete. ----
	{
		auto f = load_mission1(path);
		// Use enemy + town center as a two-target objective. remove one at a
		// time: after the first removal the trigger must NOT fire; only after
		// both are gone does it complete.
		auto trigger = make_defeat_enemy_objective(
			f.state, std::vector<entity_id_t>{*f.enemy_id, *f.tc_id});

		f.state->remove_game_entity(*f.tc_id);
		trigger->on_update(time::time_t::from_int(1));
		bool still_active = not trigger->is_finished();

		f.state->remove_game_entity(*f.enemy_id);
		trigger->on_update(time::time_t::from_int(2));
		bool now_done = trigger->is_finished();

		if (still_active and now_done) {
			log::log(MSG(info) << "PASS (Part D): multi-target needs ALL removed "
			                   << "(after 1: finished=" << (not still_active)
			                   << " after 2: finished=" << now_done << ")");
		}
		else {
			log::log(MSG(err) << "FAIL (Part D): multi-target AND semantics wrong "
			                  << "(after 1: finished=" << (not still_active)
			                  << " after 2: finished=" << now_done << ")");
		}
	}

	log::log(MSG(info) << "========== END VICTORY SEMANTICS TEST ==========");
}

} // namespace openage::gamestate::tests
