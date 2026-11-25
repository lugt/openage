// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <algorithm>
#include <memory>
#include <sstream>
#include <vector>

#include "config.h"
#include "coord/tile.h"
#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/event/death.h"
#include "gamestate/event/send_command.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/player.h"
#include "gamestate/save_load.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "gamestate/simulation.h"
#include "gamestate/system/move.h"
#include "scenario/dynamic_occupancy.h"
#include "scenario/static_occupancy_grid.h"
#include "testing/testing.h"
#include "time/time_loop.h"
#include "util/fslike/directory.h"

export module openage.gamestate.movement_tests;

export namespace openage::gamestate::tests {

/** Dynamic obstacles must permit detours, resumed orders and explicit cancellation. */
void movement_replanning() {
	class OpenGrid final : public scenario::NavigationGrid {
	public:
		bool is_walkable(scenario::CellPosition cell) const override {
			return cell.x >= 0 and cell.y >= 0 and cell.x < 12 and cell.y < 8;
		}
		double cost(scenario::CellPosition) const override {
			return 1;
		}
	};
	auto directory = std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir);
	const auto root = directory->root();
	auto simulation = std::make_shared<GameSimulation>(root, std::make_shared<cvar::CVarManager>(root), std::make_shared<time::TimeLoop>(), "ww_mvp.campaign.william_wallace.mission_01.PlayerSetup", "ww_mvp.campaign.william_wallace.mission_01.Mission");
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();
	const auto state = simulation->get_game()->get_state();
	std::vector<entity_id_t> ids;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->has_component(component::component_t::MOVE)) {
			ids.push_back(id);
		}
	}
	std::ranges::sort(ids);
	TESTEQUALS(ids.empty(), false);
	const auto entity = state->get_game_entity(ids.front());
	const auto position = entity->try_get_component_as<component::Position>(component::component_t::POSITION);
	const auto map = state->get_map();
	map->set_navigation_service(std::make_shared<OpenGrid>(), {});
	const auto occupancy = map->get_dynamic_occupancy();
	for (const auto &record : occupancy->records()) {
		occupancy->remove_entity(record.entity);
	}
	const auto start = coord::tile{2, 4}.to_phys3_center();
	const auto goal = coord::tile{9, 4}.to_phys3_center();
	const entity_id_t blocker = ids.back() + 100000;
	position->set_position(0, start);
	occupancy->register_entity(blocker, {5.1, 4.1, 5.9, 4.9});
	const auto duration = system::Move::move_default(entity, state, goal, 0, true);
	TESTEQUALS(position->get_positions().get(duration).to_tile() == goal.to_tile(), true);
	for (double t = 0; t <= duration.to_double(); t += 0.125) {
		const auto point = position->get_positions().get(time::time_t::from_double(t));
		auto footprint = entity->footprint();
		footprint.set_center(point.ne.to_double(), point.se.to_double());
		TESTEQUALS(footprint.bounds().overlaps({5.1, 4.1, 5.9, 4.9}), false);
	}

	// Occupied destinations wait for a later simulation event, without UI input.
	auto at = duration + time::time_t::from_int(1);
	position->set_position(at, start);
	occupancy->register_entity(blocker, {9.1, 4.1, 9.9, 4.9});
	const auto blocked = system::Move::move_default(entity, state, goal, at, true);
	TESTEQUALS(position->get_positions().get(at + blocked).to_tile() == goal.to_tile(), false);
	const auto loop = simulation->get_event_loop();
	loop->reach_time(at + blocked, state);
	occupancy->remove_entity(blocker);
	at += blocked + time::time_t::from_int(30);
	loop->reach_time(at, state);
	TESTEQUALS(position->get_positions().get(at).to_tile() == goal.to_tile(), true);

	// Stop invalidates the pending replan even when the blocker subsequently leaves.
	at += time::time_t::from_int(1);
	position->set_position(at, start);
	occupancy->register_entity(blocker, {9.1, 4.1, 9.9, 4.9});
	system::Move::move_default(entity, state, goal, at, true);
	system::Move::halt(entity, state, at);
	occupancy->remove_entity(blocker);
	loop->reach_time(at + time::time_t::from_int(60), state);
	TESTEQUALS(position->get_positions().get(at + time::time_t::from_int(60)) == start, true);

	// A replacement destination must also supersede a blocked order.
	at += time::time_t::from_int(61);
	position->set_position(at, start);
	occupancy->register_entity(blocker, {9.1, 4.1, 9.9, 4.9});
	system::Move::move_default(entity, state, goal, at, true);
	const auto replacement = coord::tile{2, 1}.to_phys3_center();
	system::Move::move_default(entity, state, replacement, at, true);
	occupancy->remove_entity(blocker);
	loop->reach_time(at + time::time_t::from_int(60), state);
	TESTEQUALS(position->get_positions().get(at + time::time_t::from_int(60)).to_tile() == replacement.to_tile(), true);

	// Two live movers issuing orders at the same time both retain progress.
	TESTEQUALS(ids.size() >= 2, true);
	const auto other = state->get_game_entity(ids[1]);
	const auto other_position = other->try_get_component_as<component::Position>(component::component_t::POSITION);
	at += time::time_t::from_int(61);
	position->set_position(at, start);
	other_position->set_position(at, coord::tile{5, 4}.to_phys3_center());
	system::Move::halt(other, state, at);
	system::Move::move_default(entity, state, goal, at, true);
	system::Move::move_default(other, state, start, at, true);
	loop->reach_time(at + time::time_t::from_int(60), state);
	TESTEQUALS(position->get_positions().get(at + time::time_t::from_int(60)).to_tile() == goal.to_tile(), true);
	TESTEQUALS(other_position->get_positions().get(at + time::time_t::from_int(60)).to_tile() == start.to_tile(), true);
}

/** Duplicate death dispatch must not award a second military score. */
void campaign_scoring() {
	auto directory = std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir);
	const auto root = directory->root();
	auto simulation = std::make_shared<GameSimulation>(root, std::make_shared<cvar::CVarManager>(root), std::make_shared<time::TimeLoop>(), "ww_mvp.campaign.william_wallace.mission_01.PlayerSetup", "ww_mvp.campaign.william_wallace.mission_01.Mission");
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();
	const auto state = simulation->get_game()->get_state();
	const auto at = time::time_t::zero();
	std::shared_ptr<GameEntity> victim;
	player_id_t owner = 0;
	int64_t value = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		const auto ownership = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		if (not ownership or entity->is_dead()) {
			continue;
		}
		owner = ownership->get_owners().get(at);
		if (not state->get_player(owner)->is_gaia) {
			value = state->entity_score_value(*entity, owner);
			if (value > 0) {
				victim = entity;
				break;
			}
		}
	}
	TESTEQUALS(static_cast<bool>(victim), true);
	std::shared_ptr<Player> killer;
	for (const auto &[id, player] : state->get_players()) {
		if (id != owner and not player->is_gaia) {
			killer = player;
			break;
		}
	}
	TESTEQUALS(static_cast<bool>(killer), true);
	state->set_stance(killer->get_id(), owner, DiplomaticStance::Enemy);
	const auto before = killer->score_ledger.destroyed_value;
	event::DeathEventHandler death;
	const openage::event::EventHandler::param_map params{
		{"target_id", victim->get_id()}, {"killer", killer->get_id()},
	};
	for (int attempt = 0; attempt < 2; ++attempt) {
		death.invoke(*simulation->get_event_loop(), simulation->get_commander(), state, at, params);
	}
	TESTEQUALS(killer->score_ledger.destroyed_value, before + value);
	killer->score_ledger.research_value = 50;
	state->update_visibility(at);
	const auto saved_view = state->visibility(killer->get_id());
	const auto saved = GameSaveManager::serialize(*simulation);
	std::istringstream records{saved};
	std::string line;
	std::string metadata;
	std::string legacy;
	while (std::getline(records, line)) {
		if (not line.starts_with("ENTITY|")) {
			metadata += line + "\n";
			if (not line.starts_with("SCORE|") and not line.starts_with("EXPLORED|")) {
				legacy += line + "\n";
			}
		}
	}
	killer->score_ledger = {};
	std::string error;
	TESTEQUALS(GameSaveManager::deserialize(*simulation, metadata, &error), true);
	TESTEQUALS(killer->score_ledger.destroyed_value, before + value);
	TESTEQUALS(killer->score_ledger.research_value, 50);
	if (saved_view) {
		const auto restored = state->visibility(killer->get_id());
		TESTEQUALS(static_cast<bool>(restored), true);
		TESTEQUALS(std::ranges::count_if(restored->tiles, [](auto tile) { return tile != TileVisibility::Unexplored; }),
		           std::ranges::count_if(saved_view->tiles, [](auto tile) { return tile != TileVisibility::Unexplored; }));
	}
	TESTEQUALS(GameSaveManager::deserialize(*simulation, legacy, &error), true);
	TESTEQUALS(killer->score_ledger.destroyed_value, 0);
	TESTEQUALS(killer->score_ledger.research_value, 0);
	TESTEQUALS(GameSaveManager::deserialize(*simulation, metadata + "SCORE|999999|1|1\n", &error), false);
	state->update_player_economies(at);
	const auto rows = state->player_scores();
	TESTEQUALS(std::ranges::none_of(rows, [&](const auto &row) { return state->get_player(row.player)->is_gaia; }), true);
	TESTEQUALS(std::ranges::is_sorted(rows, [](const auto &a, const auto &b) {
		return a.total != b.total ? a.total > b.total : a.player < b.player;
	}), true);
}

} // namespace openage::gamestate::tests
