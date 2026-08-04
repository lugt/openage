// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4D demo 36: Windowed-style Move routing via the
// ContextActionResolver binding.
//
// Pins:
//   1. resolve_context_action with ground pick + mover in
//      selection returns MOVE with the world position intact.
//   2. The same input with NO mover in selection returns
//      REJECTED (and the integration layer's binding is a
//      no-op for the resolver's REJECTED/UNSUPPORTED/NO_OP
//      outcomes -- this is the test for the integration
//      layer's switch statement, not just the resolver).
//   3. Enemy pick + attacker in selection returns ATTACK with
//      the target entity id intact.
//
// This demo exercises the resolver directly (P4.4D §11 says
// "synthetic pointer event -> ... -> semantic action resolver
// -> Commander"). The resolver is the platform-neutral layer
// that the Qt input layer feeds; the demo does not need Qt.

#include "gamestate/demo/footprint_demo.h"

#include "input/semantic_action.h"

#include "cvar/cvar.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "time/time.h"
#include "time/time_loop.h"

#include <memory>
#include <string>


namespace openage::gamestate::tests {


namespace {


constexpr const char *MISSION1_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.Mission";
constexpr const char *PLAYERSETUP_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup";


openage::gamestate::entity_id_t find_entity_by_key(
	const std::shared_ptr<openage::gamestate::GameState> &state,
	const std::string &key_substr) {
	for (const auto &kv : state->get_named_entities()) {
		if (kv.first.find(key_substr) != std::string::npos) {
			return kv.second;
		}
	}
	return 0;
}


openage::input::SelectionSnapshot snapshot_from_ids(
	const std::shared_ptr<openage::gamestate::GameState> &state,
	const std::vector<openage::gamestate::entity_id_t> &ids) {
	openage::input::SelectionSnapshot snap;
	for (auto id : ids) {
		openage::input::SelectionSnapshot::Entry e;
		e.id = id;
		auto entity = state->get_game_entity(id);
		e.has_move_ability = entity->has_component(
			openage::gamestate::component::component_t::MOVE);
		e.has_attack_ability = entity->has_component(
			openage::gamestate::component::component_t::ATTACK);
		auto own = std::dynamic_pointer_cast<
			openage::gamestate::component::Ownership>(
			entity->get_component(
				openage::gamestate::component::component_t::OWNERSHIP));
		if (own) {
			e.owner = own->get_owners().get(
				openage::time::time_t{0});
		}
		snap.entries.push_back(e);
	}
	return snap;
}


}  // namespace


void simulation_demo_36(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 36: P4.4D windowed-style move routing ===");

	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<openage::time::TimeLoop>();
	auto simulation = std::make_shared<openage::gamestate::GameSimulation>(
		path, cvar, time_loop, PLAYERSETUP_FQON, MISSION1_FQON);
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();
	simulation->start();

	auto game = simulation->get_game();
	auto state = game->get_state();

	auto player_villager_id = find_entity_by_key(state, "PlayerVillager");
	auto player_tc_id = find_entity_by_key(state, "PlayerTownCenter");
	auto enemy_villager_id = find_entity_by_key(state, "EnemyVillager");
	if (not state->has_game_entity(player_villager_id)
	    or not state->has_game_entity(player_tc_id)
	    or not state->has_game_entity(enemy_villager_id)) {
		throw std::runtime_error{"demo 36 FAILED: missing entity"};
	}

	auto villager_own = std::dynamic_pointer_cast<
		openage::gamestate::component::Ownership>(
		state->get_game_entity(player_villager_id)->get_component(
			openage::gamestate::component::component_t::OWNERSHIP));
	auto enemy_own = std::dynamic_pointer_cast<
		openage::gamestate::component::Ownership>(
		state->get_game_entity(enemy_villager_id)->get_component(
			openage::gamestate::component::component_t::OWNERSHIP));
	auto self_player = villager_own->get_owners().get(
		openage::time::time_t{0});
	auto enemy_player = enemy_own->get_owners().get(
		openage::time::time_t{0});

	// ---- Pin 1: ground pick + villager selection -> MOVE
	//              with (target_x, target_y) preserved. ----
	{
		openage::input::PickResult p;
		p.has_entity = false;
		p.world_x = 4.0;
		p.world_y = 4.0;
		auto snap = snapshot_from_ids(state, {player_villager_id});
		auto a = openage::input::resolve_context_action(
			state, snap, p, self_player);
		if (a.kind != openage::input::SemanticActionKind::MOVE
		    or not a.world_x.has_value()
		    or not a.world_y.has_value()
		    or *a.world_x != 4.0
		    or *a.world_y != 4.0) {
			throw std::runtime_error{
				"demo 36 pin 1 FAILED: ground+selection -> "
				+ a.kind_string() + " reason=" + a.reason};
		}
		log::log(MSG(info) << "demo 36: pin 1 PASS (ground+selection -> MOVE 4.0,4.0)");
	}

	// ---- Pin 2: ground pick + TC-only selection -> REJECTED
	//              (no Move ability on TC). The integration
	//              layer's binding switches on REJECTED and
	//              returns no event; verify the resolver
	//              returns the right kind for that path. ----
	{
		openage::input::PickResult p;
		p.has_entity = false;
		p.world_x = 4.0;
		p.world_y = 4.0;
		auto snap = snapshot_from_ids(state, {player_tc_id});
		auto a = openage::input::resolve_context_action(
			state, snap, p, self_player);
		if (a.kind != openage::input::SemanticActionKind::REJECTED
		    or a.reason.find("no_move_ability") == std::string::npos) {
			throw std::runtime_error{
				"demo 36 pin 2 FAILED: ground+TC -> "
				+ a.kind_string() + " reason=" + a.reason};
		}
		log::log(MSG(info) << "demo 36: pin 2 PASS (ground+TC -> REJECTED)");
	}

	// ---- Pin 3: enemy pick + villager selection -> ATTACK
	//              with target_entity == enemy_villager_id. ----
	{
		openage::input::PickResult p;
		p.has_entity = true;
		p.entity = enemy_villager_id;
		p.entity_owner = enemy_player;
		auto snap = snapshot_from_ids(state, {player_villager_id});
		auto a = openage::input::resolve_context_action(
			state, snap, p, self_player);
		if (a.kind != openage::input::SemanticActionKind::ATTACK
		    or not a.target_entity.has_value()
		    or *a.target_entity != enemy_villager_id) {
			throw std::runtime_error{
				"demo 36 pin 3 FAILED: enemy+villager -> "
				+ a.kind_string() + " reason=" + a.reason};
		}
		log::log(MSG(info) << "demo 36: pin 3 PASS (enemy+villager -> ATTACK enemy)");
	}

	// ---- Pin 4: enemy pick + TC-only selection -> REJECTED
	//              (TC has no Attack ability). This is the
	//              critical pin that proves ATTACK does NOT
	//              degenerate to MOVE: a TC selected and
	//              right-clicking an enemy is REJECTED, not
	//              "MoveCommand to enemy position". ----
	{
		openage::input::PickResult p;
		p.has_entity = true;
		p.entity = enemy_villager_id;
		p.entity_owner = enemy_player;
		auto snap = snapshot_from_ids(state, {player_tc_id});
		auto a = openage::input::resolve_context_action(
			state, snap, p, self_player);
		if (a.kind != openage::input::SemanticActionKind::REJECTED
		    or a.reason.find("no_attack_ability") == std::string::npos
		    or a.target_entity.has_value()
		    or a.world_x.has_value()) {
			throw std::runtime_error{
				"demo 36 pin 4 FAILED: enemy+TC -> "
				+ a.kind_string() + " reason=" + a.reason};
		}
		log::log(MSG(info) << "demo 36: pin 4 PASS (enemy+TC -> REJECTED, not MOVE)");
	}

	log::log(MSG(info) << "DEMO 36: PASS (windowed-style move routing)");
}


} // namespace openage::gamestate::tests
