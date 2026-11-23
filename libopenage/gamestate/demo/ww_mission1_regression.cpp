// Copyright 2026 the openage authors. See copying.md for legal info.
//
// William Wallace Mission 1 — Phase H0 minimal headless regression.
//
// Goal of this demo / regression harness:
//   1. Initialise the runtime exactly the way Engine::initialize_runtime()
//      does on the main thread, without spawning any worker.
//   2. Locate the three initial entities by their PlacedEntity instance
//      FQONs (PlayerVillager / PlayerTownCenter / EnemyVillager) --
//      "stable name" lookup, immune to spawn-order or std::unordered_map
//      iteration order.
//   3. Issue a single ATTACK via the existing send_command event path,
//      then advance the kernel explicitly through reach_time() calls
//      (NO fixed tick, NO sleep, NO wall-clock) until one of three
//      stop conditions is hit:
//          * enemy removed (HP drained to 0)
//          * objective controller reported finished
//          * deadline reached before either
//   4. Emit a stable, deterministically-ordered failure snapshot
//      whenever any of the three assertions fail. The snapshot never
//      contains pointers, thread IDs, or unordered-iteration artefacts.
//
// This is the minimum Phase D gate: prove the William Wallace
// Mission 1 scenario, once the runtime is initialised, can be
// driven to completion in headless mode using manual time.

#include "demo_0.h"  // for log::log convenience macros

#include <algorithm>
#include <array>
#include <cmath>
#include <sstream>
#include <unordered_set>
#include <vector>

#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/api/build.h"
#include "gamestate/component/internal/activity.h"
#include "gamestate/component/internal/commands/attack.h"
#include "gamestate/component/internal/commands/move.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/event/send_command.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/player.h"
#include "gamestate/manager.h"
#include "gamestate/map.h"
#include "gamestate/objective/objective_controller.h"
#include "gamestate/simulation.h"
#include "gamestate/terrain.h"
#include "gamestate/terrain_chunk.h"
#include "gamestate/terrain_factory.h"
#include "log/log.h"
#include "pathfinding/cost_field.h"
#include "pathfinding/definitions.h"
#include "pathfinding/grid.h"
#include "pathfinding/pathfinder.h"
#include "pathfinding/sector.h"
#include "scenario/static_occupancy_grid.h"
#include "renderer/stages/world/render_entity.h"
#include "time/clock.h"
#include "time/time.h"
#include "time/time_loop.h"


namespace openage::gamestate::tests {

namespace {

// Stable FQONs declared by mission_01.nyan
constexpr const char *MISSION1_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.Mission";
constexpr const char *PLAYERSETUP_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup";
constexpr const char *PLAYERVILLAGER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerVillager";
constexpr const char *PLAYERTOWNCENTER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerTownCenter";
constexpr const char *ENEMYVILLAGER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.EnemyVillager";

// Phase H0 deadline: if Mission 1 cannot be won within 5 simulated
// minutes we have a regression against the legacy attack pipeline.
constexpr time::time_t WW_MISSION1_DEADLINE = time::time_t::from_int(300);
// Phase H0.5 event-batch budget. We track EVENT BATCHES, not actual
// dispatched events (the EventLoop reach_time() return value is
// currently void; the implemented dispatcher returns a count but it
// is private to the loop). Phases H1/H2+ are planned to expose a
// real executed_event counter; until then we use event_batches as
// the explicit counter and let a spike in batches fail the test.
constexpr size_t WW_MISSION1_MAX_EVENT_BATCHES = 4096;

bool assertion_failed = false;
std::ostringstream failure_log;

#define WW1_ASSERT(cond, msg)                              \
	do {                                                   \
		if (not(cond)) {                                   \
			assertion_failed = true;                       \
			failure_log << "ASSERT FAIL: " << msg          \
			            << " (at " << __FILE__ << ":"     \
			            << __LINE__ << ")\n";              \
			log::log(MSG(err) << "WW1: " << msg);          \
		}                                                  \
	} while (0)

// Phase H0 minimal snapshot: stable, sorted, pointer-free. We avoid
// pulling component internals into the test until the
// component-level accessors (Live::get_attribute_value /
// Activity::get_current_state) stabilize. The minimum a reviewer
// needs to triage a regression is: time, objective state, the set
// of registered entity stable names, and the failure reasons.
std::string snapshot_failure_state(
	const std::shared_ptr<GameState> &state,
	const std::shared_ptr<GameSimulation> &simulation,
	time::time_t now) {
	std::ostringstream out;
	out << "snapshot_failure_state {\n";
	out << "  simulation_time=" << now << "\n";
	out << "  objective_finished=" << simulation->is_objective_finished() << "\n";

	std::vector<std::string> names;
	for (const auto &kv : state->get_named_entities()) {
		names.push_back(kv.first);
	}
	std::sort(names.begin(), names.end());
	out << "  registered_entities=" << names.size() << " (sorted): ";
	for (size_t i = 0; i < names.size(); ++i) {
		if (i) out << ",";
		out << names[i];
	}
	out << "\n";
	out << "  failure_reasons=" << failure_log.str();
	out << "}\n";
	return out.str();
}

} // namespace

void simulation_demo_7(const util::Path &path) {
	log::log(MSG(info) << "========== WW MISSION 1 HEADLESS REGRESSION (demo 7) ==========");
	assertion_failed = false;
	failure_log.str(std::string{});
	failure_log.clear();

	// ---- Phase 1: initialise the runtime on the main thread, mirroring
	// Engine::initialize_runtime() so the test and the real launch
	// path share the same boundary. NO worker is spawned here.
	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<time::TimeLoop>();
	auto simulation = std::make_shared<GameSimulation>(
		path, cvar, time_loop, PLAYERSETUP_FQON, MISSION1_FQON);
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();
	simulation->start();

	auto game = simulation->get_game();
	auto state = game->get_state();
	auto event_loop = simulation->get_event_loop();
	auto commander = simulation->get_commander();

	// Exercise a rectangular edge and an asymmetric water tile. Their
	// coordinates must agree in the terrain, the flow-field grid and A*.
	TerrainFactory terrain_factory;
	const nyan::fqon_t grass = "de2_base.data.terrain.grass.grass.Grass";
	const nyan::fqon_t water = "de2_base.data.terrain.water.water.Water";
	std::vector<nyan::fqon_t> edge_tiles(16, grass);
	std::vector<uint8_t> edge_elevations(16, 0);
	edge_tiles[13] = water;
	edge_elevations[13] = 2;
	auto terrain = terrain_factory.add_terrain({10, 8}, {
		terrain_factory.add_chunk_with_tiles(state, {8, 8}, {0, 0}, {grass}),
		terrain_factory.add_chunk_with_tiles(state, {2, 8}, {8, 0}, edge_tiles, edge_elevations),
	});
	const auto &water_tile = terrain->get_chunk(coord::chunk{1, 0})->get_tile(coord::tile{1, 6});
	auto occupancy = std::make_shared<scenario::StaticOccupancyGrid>(0, 0, 10, 8);
	scenario::TerrainNavigationGrid nav{occupancy, terrain};
	WW1_ASSERT(water_tile.terrain.get_name() == water && water_tile.elevation.to_float() == 2,
	           "rectangular chunk did not retain water and height at (9, 6)");
	WW1_ASSERT(not nav.is_walkable({9, 6}) && nav.is_walkable({8, 6}),
	           "land navigation did not distinguish water and grass");
	Map edge_map{state, terrain};
	auto grid_id = edge_map.get_grid_id("de2_base.data.util.path_type.types.Land");
	auto costs = edge_map.get_pathfinder()->get_grid(grid_id)->get_sector(1)->get_cost_field();
	WW1_ASSERT(costs->get_cost(1, 6) == path::COST_IMPASSABLE
	           && costs->get_cost(0, 6) == path::COST_MIN
	           && costs->get_cost(2, 6) == path::COST_IMPASSABLE,
	           "flow-field costs transposed the edge chunk or left padding walkable");

	if (state->get_entity_id_by_instance("GatheringTownCenter")) {
		const std::array<std::pair<const char *, const char *>, 3> orders{{
			{"FoodGatherer", "ForageBush"}, {"WoodGatherer", "Oak"}, {"GoldGatherer", "GoldDeposit"},
		}};
		for (const auto &[worker_name, spot_name] : orders) {
			auto worker = state->get_entity_id_by_instance(worker_name);
			auto spot = state->get_entity_id_by_instance(spot_name);
			WW1_ASSERT(worker and spot, "Feeding the Army is missing required harvest entities");
			if (not worker or not spot) {
				return;
			}
			openage::event::EventHandler::param_map::map_t params{
				{"type", component::command::command_t::GATHER},
				{"target_id", *spot}, {"entity_ids", std::vector<entity_id_t>{*worker}},
			};
			event_loop->create_event("game.send_command", commander, state, time::time_t::zero(), params);
		}

		auto player = state->get_player(0);
		const auto starting_economy = state->player_economy(0);
		WW1_ASSERT(starting_economy.population == 3 and starting_economy.population_capacity == 5,
		           "Three villagers should use three of the Town Center's five population spaces");
		const auto food = "de2_base.data.util.resource.types.Food";
		const auto wood = "de2_base.data.util.resource.types.Wood";
		const auto gold = "de2_base.data.util.resource.types.Gold";
		event_loop->reach_time(time::time_t{5}, state);
		state->update_player_economies(time::time_t{5});
		const auto before_delivery = state->player_economy(0);
		WW1_ASSERT(before_delivery.food == 0 and before_delivery.wood == 0 and before_delivery.gold == 0,
		           "The HUD stockpiles should remain empty until villagers deliver resources");
		WW1_ASSERT(player->get_resource(food) == 0 and player->get_resource(wood) == 0
		           and player->get_resource(gold) == 0 and not simulation->is_objective_finished(),
		           "Gathering credited resources before delivery");
		auto now = time::time_t{5};
		for (size_t batch = 0; batch < WW_MISSION1_MAX_EVENT_BATCHES; ++batch) {
			const auto next = event_loop->peek_next_event_time();
			if (not next or *next > time::time_t{500}) {
				break;
			}
			now = *next;
			event_loop->reach_time(now, state);
			if (simulation->is_objective_finished()) {
				state->update_player_economies(*next);
				break;
			}
		}
		const auto delivered_economy = state->player_economy(0);
		WW1_ASSERT(delivered_economy.food == player->get_resource(food)
		           and delivered_economy.wood == player->get_resource(wood)
		           and delivered_economy.gold == player->get_resource(gold)
		           and delivered_economy.stone == 0,
		           "The HUD stockpiles should match the player's credited deliveries");
		WW1_ASSERT(simulation->is_objective_finished() and player->has_enough(food, 50)
		           and player->has_enough(wood, 50) and player->has_enough(gold, 50),
		           "Feeding the Army did not complete after deliveries: food=" << player->get_resource(food)
		           << " wood=" << player->get_resource(wood) << " gold=" << player->get_resource(gold));
		if (assertion_failed) {
			return;
		}
		const auto worker = *state->get_entity_id_by_instance("FoodGatherer");
		const auto town_center = *state->get_entity_id_by_instance("GatheringTownCenter");
		auto issue_action = [&](entity_id_t actor, ActionKind kind, const std::string &name,
		                        std::optional<coord::phys3> position = std::nullopt) {
			const auto entity = state->get_game_entity(actor);
			const auto option = std::ranges::find_if(entity->action_options, [&](const auto &action) {
				return action.kind == kind and action.subject.ends_with("." + name);
			});
			if (option == entity->action_options.end()) {
				throw std::runtime_error{"Missing imported action: " + name};
			}
			openage::event::EventHandler::param_map::map_t order{
				{"actor", actor}, {"option", option->id}, {"issuer", player_id_t{0}},
			};
			if (position) {
				order.emplace("position", *position);
			}
			event_loop->create_event("game.economy_action", commander, state, now, order);
			event_loop->reach_time(now, state);
			return *option;
		};
		auto advance = [&](int seconds) {
			now += time::time_t::from_int(seconds);
			event_loop->reach_time(now, state);
			state->update_player_economies(now);
		};
		event_loop->create_event("game.send_command", commander, state, now, {
			{"type", component::command::command_t::IDLE},
			{"entity_ids", std::vector<entity_id_t>{
				worker, *state->get_entity_id_by_instance("WoodGatherer"), *state->get_entity_id_by_instance("GoldGatherer")}},
			{"issuer", player_id_t{0}},
		});
		event_loop->reach_time(now, state);
		issue_action(town_center, ActionKind::Research, "FeudalAge");
		WW1_ASSERT(state->action_feedback(0) == "Prerequisites are missing.", "Feudal Age ignored its building requirement");
		const auto gold_before = player->get_resource(gold);
		const auto loom = issue_action(town_center, ActionKind::Research, "Loom");
		WW1_ASSERT(loom.seconds == 25 and player->get_resource(gold) == gold_before - 50
		           and not player->has_researched(loom.subject), "Loom did not charge its original cost or wait for research");
		const auto wood_before = player->get_resource(wood);
		const auto house = issue_action(worker, ActionKind::Build, "House", coord::tile{5, 10}.to_phys3());
		WW1_ASSERT(player->get_resource(wood) == wood_before - 25, "House did not charge its original wood cost");
		const auto site = std::ranges::find_if(state->get_game_entities(), [&](const auto &entry) {
			return entry.second->definition == house.subject;
		});
		if (site == state->get_game_entities().end()) {
			throw std::runtime_error{"House order did not create a foundation"};
		}
		const auto site_id = site->first;
		const auto build = site->second->template try_get_component_as<component::Build>(component::component_t::BUILD);
		advance(16);
		const auto paused_progress = build->get_progress();
		event_loop->create_event("game.send_command", commander, state, now, {
			{"type", component::command::command_t::IDLE},
			{"entity_ids", std::vector<entity_id_t>{worker}}, {"issuer", player_id_t{0}},
		});
		advance(30);
		WW1_ASSERT(paused_progress > 0 and paused_progress < 100 and paused_progress == build->get_progress(),
		           "Stopping the worker did not pause construction");
		event_loop->create_event("game.economy_action", commander, state, now, {
			{"actor", worker}, {"site", site_id}, {"issuer", player_id_t{0}},
		});
		advance(45);
		const auto live = state->get_game_entity(worker)->try_get_component_as<component::Live>(component::component_t::LIVE);
		const nyan::fqon_t health = "de2_base.data.util.attribute.types.Health";
		WW1_ASSERT(player->has_researched(loom.subject) and live->get_attribute_value(now, health) == 40,
		           "Loom did not apply the original health upgrade to the existing villager");
		WW1_ASSERT(state->player_economy(0).population_capacity == 10, "The finished house did not provide five population spaces");
		const auto food_before = player->get_resource(food);
		issue_action(town_center, ActionKind::Train, "Villager");
		WW1_ASSERT(player->get_resource(food) == food_before - 50, "Training did not charge the original villager price");
		advance(30);
		WW1_ASSERT(state->player_economy(0).population == 4, "Town Center did not train a villager");
		for (const auto *animal_name : {"Sheep", "Boar"}) {
			const auto animal = state->get_entity_id_by_instance(animal_name);
			if (not animal) {
				continue;
			}
			const auto harvest_before = player->get_resource(food);
			event_loop->create_event("game.send_command", commander, state, now, {
				{"type", component::command::command_t::GATHER},
				{"target_id", *animal}, {"entity_ids", std::vector<entity_id_t>{worker}}, {"issuer", player_id_t{0}},
			});
			advance(180);
			WW1_ASSERT(player->get_resource(food) > harvest_before
			           and (not state->has_game_entity(*animal) or state->get_game_entity(*animal)->is_dead()),
			           "Villager did not hunt and return food from " << animal_name);
		}
		if (const auto fish = state->get_entity_id_by_instance("ShoreFish")) {
			const auto before_fishing = player->get_resource(food);
			const auto fishing_since = now;
			auto fishing_render = std::make_shared<renderer::world::RenderEntity>();
			state->get_game_entity(worker)->set_render_entity(fishing_render);
			event_loop->create_event("game.send_command", commander, state, now, {
				{"type", component::command::command_t::GATHER},
				{"target_id", *fish}, {"entity_ids", std::vector<entity_id_t>{worker}}, {"issuer", player_id_t{0}},
			});
			advance(90);
			WW1_ASSERT(player->get_resource(food) > before_fishing, "Villager did not fish from shore and deliver food");
			bool played_fishing = false;
			for (auto sample = fishing_since; sample < now; sample += time::time_t{1}) {
				if (fishing_render->get_animation_path().get(sample).ends_with("/shore_fishing_graphics/fish.sprite")) {
					played_fishing = true;
					break;
				}
			}
			WW1_ASSERT(played_fishing, "Shore fishing did not play the imported DE2 animation");
		}
		player->add_resource(wood, 400);
		player->add_resource(food, 500);
		issue_action(worker, ActionKind::Build, "Barracks", coord::tile{5, 2}.to_phys3());
		WW1_ASSERT(state->action_feedback(0) == "Construction ordered.", "Barracks order: " << state->action_feedback(0));
		advance(80);
		issue_action(worker, ActionKind::Build, "MiningCamp", coord::tile{11, 2}.to_phys3());
		WW1_ASSERT(state->action_feedback(0) == "Construction ordered.", "Mining camp order: " << state->action_feedback(0));
		advance(80);
		const auto feudal = issue_action(town_center, ActionKind::Research, "FeudalAge");
		WW1_ASSERT(state->action_feedback(0) == "Research in progress.",
		           "Feudal Age should unlock after completing the two required buildings: " << state->action_feedback(0));
		advance(140);
		WW1_ASSERT(player->has_researched(feudal.subject), "Feudal Age did not finish after its imported research time");
		if (not assertion_failed) {
			log::log(MSG(info) << "WW MISSION 2 HEADLESS REGRESSION: PASS");
		}
		return;
	}

	// The marching profile uses the same mission entry point with three
	// required targets. Exercise the command and removal handlers in order.
	if (const auto outpost = state->get_entity_id_by_instance("EnglishOutpost")) {
		const auto starting_economy = state->player_economy(0);
		WW1_ASSERT(starting_economy.population == 1 and starting_economy.population_capacity == 5,
		           "Only the player's scout should count before the reinforcements join");
		const auto initial_view = state->visibility(0);
		WW1_ASSERT(initial_view and state->get_stance(0, 2) == DiplomaticStance::Ally
		           and state->get_stance(0, 1) == DiplomaticStance::Enemy,
		           "Mission Pack did not initialize the diplomacy matrix and fog");
		if (not initial_view) {
			return;
		}
		const auto enemy_view = state->visibility(1);
		WW1_ASSERT(initial_view->at(1, 2) == TileVisibility::Visible
		           and initial_view->at(0, 6) == TileVisibility::Unexplored
		           and initial_view->at(15, 15) == TileVisibility::Visible
		           and enemy_view and enemy_view->at(0, 0) == TileVisibility::Unexplored,
		           "Scout and Town Center sight should be circular and tracked separately per player");
		WW1_ASSERT(enemy_view and enemy_view->at(1, 8) == TileVisibility::Visible
		           and initial_view->at(1, 8) == TileVisibility::Unexplored,
		           "The English outpost revealed ground to the Scots");
		openage::event::EventHandler::param_map::map_t change{
			{"source", player_id_t{0}}, {"target", player_id_t{1}}, {"stance", DiplomaticStance::Ally},
		};
		event_loop->create_event("game.diplomacy", commander, state, time::time_t::zero(), change);
		event_loop->reach_time(time::time_t::zero(), state);
		const auto private_view = state->visibility(0);
		WW1_ASSERT(state->get_stance(1, 0) == DiplomaticStance::Ally
		           and private_view and private_view->tiles == initial_view->tiles,
		           "An alliance should not share sight by default");

		state->enable_fog(true);
		state->update_visibility(time::time_t::zero());
		const auto allied_view = state->visibility(0);
		WW1_ASSERT(allied_view and allied_view->at(1, 8) == TileVisibility::Visible,
		           "Explicit allied vision did not reveal the ally's surroundings");
		if (not allied_view) {
			return;
		}

		change["stance"] = DiplomaticStance::Neutral;
		event_loop->create_event("game.diplomacy", commander, state, time::time_t::zero(), change);
		event_loop->reach_time(time::time_t::zero(), state);
		const auto neutral_view = state->visibility(0);
		if (not neutral_view) {
			WW1_ASSERT(false, "Neutral diplomacy event lost the visibility snapshot");
			return;
		}
		bool retained_exploration = false;
		for (size_t index = 0; index < initial_view->tiles.size(); ++index) {
			if (initial_view->tiles[index] == TileVisibility::Unexplored
			    and allied_view->tiles[index] == TileVisibility::Visible
			    and neutral_view->tiles[index] == TileVisibility::Explored) {
				retained_exploration = true;
			}
		}
		WW1_ASSERT(retained_exploration and state->get_stance(1, 0) == DiplomaticStance::Neutral,
		           "Ally sight was not shared or explored tiles were lost when the alliance ended");
		state->enable_fog();

		const auto attacker = state->get_entity_id_by_instance("ScottishReinforcement");
		const auto soldier = state->get_entity_id_by_instance("ScottishSoldier");
		WW1_ASSERT(attacker and soldier, "Marching profile is missing a Scottish soldier");
		if (not attacker or not soldier) {
			return;
		}

		const auto scout = state->get_entity_id_by_instance("ScottishScout");
		WW1_ASSERT(scout.has_value(), "Marching profile is missing the scout");
		if (not scout) {
			return;
		}

		const auto scout_attack = state->get_game_entity(*scout)->try_get_component_as<component::Attack>(
			component::component_t::ATTACK);
		openage::event::EventHandler::param_map::map_t neutral_attack{
			{"type", component::command::command_t::ATTACK},
			{"target_id", *outpost}, {"entity_ids", std::vector<entity_id_t>{*scout}},
		};
		event_loop->create_event("game.send_command", commander, state, time::time_t::zero(), neutral_attack);
		event_loop->reach_time(time::time_t::zero(), state);
		WW1_ASSERT(scout_attack and not scout_attack->is_active(), "Attack accepted a neutral target");

		change["stance"] = DiplomaticStance::Enemy;
		event_loop->create_event("game.diplomacy", commander, state, time::time_t::zero(), change);
		event_loop->reach_time(time::time_t::zero(), state);

		const auto foundation_id = state->get_entity_id_by_instance("ScottishConstruction");
		WW1_ASSERT(foundation_id.has_value(), "Marching profile is missing the scripted foundation");
		if (not foundation_id) {
			return;
		}
		auto foundation = state->get_game_entity(*foundation_id);
		auto foundation_render = std::make_shared<renderer::world::RenderEntity>();
		foundation->set_render_entity(foundation_render);
		foundation->get_manager()->run_activity_system(time::time_t{0});
		const auto construction = foundation->try_get_component_as<component::Build>(component::component_t::BUILD);
		const auto construction_live = foundation->try_get_component_as<component::Live>(component::component_t::LIVE);
		const std::string health = "de2_base.data.util.attribute.types.Health";
		WW1_ASSERT(construction and construction_live
		           and construction->get_progress() == 0
		           and construction_live->get_attribute_value(time::time_t{0}, health) == 1
		           and foundation_render->get_animation_path().get(time::time_t{0}).ends_with(
		                   "/idle_construct0_override_outpost.sprite"),
		           "Foundation did not start with construction graphics and 1 HP"
		               << " progress=" << (construction ? construction->get_progress() : -1)
		               << " hp=" << (construction_live ? construction_live->get_attribute_value(time::time_t{0}, health).value_or(-1) : -1)
		               << " sprite=" << foundation_render->get_animation_path().get(time::time_t{0})
		               << " build sprite=" << (construction ? construction->get_construction_animation() : ""));
		if (assertion_failed) {
			return;
		}
		const bool fire_installed =
			(path / "assets/converted/de2_base/data/game_entity/generic/outpost/graphics/outpost_fire_75.texture").is_file();

		// Attach the production render entities without opening a window.
		auto ally_owner = state->get_game_entity(*attacker)->try_get_component_as<component::Ownership>(
			component::component_t::OWNERSHIP);
		WW1_ASSERT(ally_owner and ally_owner->get_owners().get(time::time_t{0}) == 2,
		           "Allied reinforcement was controllable before the road checkpoint");

		auto scout_render = std::make_shared<renderer::world::RenderEntity>();
		auto attacker_render = std::make_shared<renderer::world::RenderEntity>();
		auto soldier_render = std::make_shared<renderer::world::RenderEntity>();
		state->get_game_entity(*scout)->set_render_entity(scout_render);
		state->get_game_entity(*attacker)->set_render_entity(attacker_render);
		state->get_game_entity(*soldier)->set_render_entity(soldier_render);
		state->get_game_entity(*scout)->get_manager()->run_activity_system(time::time_t{0});
		const auto road_tile = coord::tile{5, 4};
		openage::event::EventHandler::param_map::map_t move_params{
			{"type", component::command::command_t::MOVE},
			{"target", road_tile.to_phys3_center()},
			{"entity_ids", std::vector<entity_id_t>{*scout}},
		};
		event_loop->create_event("game.send_command", commander, state,
		                         time::time_t::from_int(0), move_params);
		auto now = time::time_t::from_int(0);
		event_loop->reach_time(now, state);
		WW1_ASSERT(scout_render->get_animation_path().get(now).ends_with("/move_militia.sprite"),
		           "Scout did not start the walk animation");
		const auto move_deadline = time::time_t::from_int(30);
		for (size_t batch = 0; batch < WW_MISSION1_MAX_EVENT_BATCHES; ++batch) {
			auto next = event_loop->peek_next_event_time();
			if (not next or *next > move_deadline) {
				break;
			}
			now = *next;
			event_loop->reach_time(now, state);
			if (now == time::time_t{6}) {
				WW1_ASSERT(construction->get_progress() == 50
				           and construction_live->get_attribute_value(now, health) == 250,
				           "Foundation did not reach half progress and health");
			}
		}
		WW1_ASSERT(construction->get_progress() == 100
		           and construction_live->get_attribute_value(now, health) == 500
		           and foundation_render->get_animation_path().get(now).ends_with("/idle_outpost.sprite"),
		           "Completed construction did not switch to the finished outpost");
		const auto position = state->get_game_entity(*scout)->try_get_component_as<component::Position>(
			component::component_t::POSITION);
		WW1_ASSERT(position and position->get_positions().get(move_deadline).to_tile() == road_tile,
		           "Scout did not reach the road waypoint");
		WW1_ASSERT(ally_owner and ally_owner->get_owners().get(move_deadline) == 0,
		           "The road checkpoint did not transfer control of the allied soldier");
		state->update_visibility(now);
		state->update_player_economies(now);
		WW1_ASSERT(state->player_economy(0).population == 3 and state->player_economy(2).population == 0,
		           "The reinforcements should count for their new owner after the road checkpoint");
		const auto marched_view = state->visibility(0);
		WW1_ASSERT(marched_view and marched_view->at(1, 2) == TileVisibility::Explored
		           and marched_view->at(0, 6) == TileVisibility::Visible
		           and marched_view->at(14, 2) == TileVisibility::Unexplored,
		           "Marching away should dim old ground, reveal ground near recruits, and leave distant ground unexplored");
		WW1_ASSERT(scout_render->get_animation_path().get(move_deadline).ends_with("/idle_militia.sprite"),
		           "Scout did not return to idle");
		if (position) {
			const auto before = time::time_t::from_double(0.05);
			const auto after = time::time_t::from_double(0.10);
			auto displacement = position->get_positions().get(after) - position->get_positions().get(before);
			const auto actual = position->get_angles().get(before).to_double();
			const auto expected = displacement.to_angle().to_double();
			WW1_ASSERT(displacement.length() > 0 and std::abs(std::remainder(actual - expected, 360.0)) < 1.0,
			           "Scout did not face its first walking segment: " << actual << " vs " << expected);
		}
		if (assertion_failed) {
			return;
		}

		const std::vector<std::string> targets{
			"EnglishOutpost", "EnglishSoldier", "EnglishReinforcement",
		};
		for (size_t index = 0; index < targets.size(); ++index) {
			const auto target = state->get_entity_id_by_instance(targets[index]);
			WW1_ASSERT(target.has_value(), "Missing marching target " << targets[index]);
			if (not target) {
				return;
			}

			auto target_render = std::make_shared<renderer::world::RenderEntity>();
			state->get_game_entity(*target)->set_render_entity(target_render);

			const auto live = state->get_game_entity(*target)->try_get_component_as<component::Live>(
				component::component_t::LIVE);
			const int64_t expected_health = index == 0 ? 500 : 40;
			WW1_ASSERT(live and live->get_attribute_value(now, health) == expected_health,
			           "Unexpected starting health for " << targets[index]);
			if (assertion_failed) {
				return;
			}

			bool first_hit_checked = false;
			std::unordered_set<int> fire_stages;
			const std::vector<entity_id_t> attackers = index == 0
				? std::vector<entity_id_t>{*scout, *soldier, *attacker}
				: std::vector<entity_id_t>{*scout};
			const std::array attack_renders{scout_render, soldier_render, attacker_render};
			std::unordered_set<entity_id_t> landed_hits;
			std::vector<component::FootprintBounds> occupied_attack_positions;
			openage::event::EventHandler::param_map::map_t attack_params{
				{"type", component::command::command_t::ATTACK},
				{"target_id", *target},
				{"entity_ids", attackers},
				{"issuer", player_id_t{0}},
			};
			event_loop->create_event("game.send_command", commander, state, now, attack_params);
			const auto deadline = now + WW_MISSION1_DEADLINE;
			for (size_t batch = 0; batch < WW_MISSION1_MAX_EVENT_BATCHES; ++batch) {
				auto next = event_loop->peek_next_event_time();
				if (not next or *next > deadline) {
					break;
				}
				now = *next;
				event_loop->reach_time(now, state);
				for (size_t i = 0; i < attackers.size(); ++i) {
					const auto entity = state->get_game_entity(attackers[i]);
					const auto attack = entity->try_get_component_as<component::Attack>(component::component_t::ATTACK);
					if (attack and attack->is_active() and attack->get_next_attack_time() > now
					    and landed_hits.insert(attackers[i]).second) {
						WW1_ASSERT(attack_renders[i]->get_animation_path().get(now).ends_with("/attack_militia.sprite"),
						           "Attacker did not play the attack animation: " << attackers[i]);
						const auto pos = entity->try_get_component_as<component::Position>(component::component_t::POSITION);
						const auto point = pos->get_positions().get(now);
						auto footprint = entity->footprint();
						footprint.set_center(point.ne.to_double(), point.se.to_double());
						for (const auto &other : occupied_attack_positions) {
							WW1_ASSERT(not footprint.overlaps(other), "Two units attacked from overlapping positions");
						}
						occupied_attack_positions.push_back(footprint.bounds());
					}
				}
				if (not first_hit_checked and live->get_attribute_value(now, health) != expected_health) {
					WW1_ASSERT(not landed_hits.empty()
					           and live->get_attribute_value(now, health) == expected_health - 4 * static_cast<int64_t>(landed_hits.size())
					           and state->has_game_entity(*target),
					           "Unexpected first hit against " << targets[index]);
					first_hit_checked = true;
				}
				if (fire_installed and index == 0) {
					const auto hp = live->get_attribute_value(now, health).value_or(0);
					for (const auto threshold : {25, 50, 75}) {
						if (hp > 0 and hp <= expected_health * threshold / 100) {
							const auto suffix = "/idle_damage_override_" + std::to_string(threshold) + "_outpost.sprite";
							WW1_ASSERT(target_render->get_animation_path().get(now).ends_with(suffix),
							           "Outpost did not play the damage animation below " << threshold << "%");
							fire_stages.insert(threshold);
							break;
						}
					}
				}
				if (not state->has_game_entity(*target)) {
					break;
				}
			}

			WW1_ASSERT(landed_hits.size() == attackers.size(),
			           "Only " << landed_hits.size() << " of " << attackers.size()
			                   << " assigned units hit " << targets[index]);
			WW1_ASSERT(not fire_installed or index != 0 or fire_stages.size() == 3,
			           "The outpost did not pass through all three fire stages");
			WW1_ASSERT(not state->has_game_entity(*target), "Attack did not remove " << targets[index]);
			WW1_ASSERT(target_render->get_animation_path().get(now).ends_with(
			               index == 0 ? "/death_outpost.sprite" : "/death_militia.sprite")
			           and not target_render->is_removed(now) and not target_render->is_selectable(),
			           "Death animation did not survive gameplay removal for " << targets[index]);
			if (index > 0) {
				WW1_ASSERT(target_render->get_animation_path().get(now + 2).ends_with("/despawn_militia.sprite")
				           and not target_render->is_removed(now + 2) and target_render->is_removed(now + 302),
				           "Militia corpse was missing or never expired");
			}
			WW1_ASSERT(simulation->is_objective_finished() == (index + 1 == targets.size()),
			           "Unexpected objective state after attacking " << targets[index]);
			if (assertion_failed) {
				log::log(MSG(err) << snapshot_failure_state(state, simulation, now));
				return;
			}
			if (index == 0) {
				openage::event::EventHandler::param_map::map_t approach_village{
					{"type", component::command::command_t::MOVE},
					{"target", coord::tile{9, 8}.to_phys3_center()},
					{"entity_ids", std::vector<entity_id_t>{*scout}},
				};
				event_loop->create_event("game.send_command", commander, state, now, approach_village);
				const auto village_deadline = now + time::time_t{30};
				while (const auto next = event_loop->peek_next_event_time()) {
					if (*next > village_deadline) {
						break;
					}
					now = *next;
					event_loop->reach_time(now, state);
				}
				WW1_ASSERT(position->get_positions().get(now).to_tile() == coord::tile(9, 8),
				           "Scout did not reach the village checkpoint");
				if (assertion_failed) {
					return;
				}
			}
		}
		log::log(MSG(info) << "WW MISSION 1 HEADLESS REGRESSION: PASS (marching profile)");
		return;
	}

	// ---- Phase 2: assert the three initial entities are present and
	// resolve them by their PlacedEntity instance FQON.
	WW1_ASSERT(state->get_game_entities().size() == 3,
	          "Mission 1 must spawn exactly 3 entities (got "
	              << state->get_game_entities().size() << ")");

	auto opt_villager = state->get_entity_id_by_instance(PLAYERVILLAGER_FQON);
	auto opt_tc = state->get_entity_id_by_instance(PLAYERTOWNCENTER_FQON);
	auto opt_enemy = state->get_entity_id_by_instance(ENEMYVILLAGER_FQON);
	WW1_ASSERT(opt_villager.has_value(), "PlayerVillager instance not registered");
	WW1_ASSERT(opt_tc.has_value(), "PlayerTownCenter instance not registered");
	WW1_ASSERT(opt_enemy.has_value(), "EnemyVillager instance not registered");

	if (assertion_failed or not opt_villager.has_value()
	    or not opt_enemy.has_value()) {
		log::log(MSG(err) << snapshot_failure_state(state, simulation,
		                                              time_loop->get_clock()->get_time()));
		log::log(MSG(err) << "WW MISSION 1 HEADLESS REGRESSION: FAIL (phase 2)");
		return;
	}
	auto player_villager_id = *opt_villager;
	auto enemy_id = *opt_enemy;

	// ---- Phase 3: drive the manual-time loop. We issue one ATTACK
	// on PlayerVillager at t=0, then advance the simulation tick by
	// tick, dispatching any events whose scheduled time falls within
	// the tick.
	openage::event::EventHandler::param_map::map_t attack_params{
		{"type", component::command::command_t::ATTACK},
		{"target_id", enemy_id},
		{"entity_ids", std::vector<entity_id_t>{player_villager_id}},
	};
	event_loop->create_event("game.send_command", commander, state,
	                         time::time_t::from_int(0), attack_params);

	// Phase H0.5 manual-time loop. We advance by scheduled event time,
	// not by a fixed `+1`. Each loop iteration:
	//   1. Peek next pending event time (no pop, no reorder).
	//   2. If no event is pending or its time exceeds the deadline, exit.
	//   3. reach_time(next_event_time) so the kernel can dispatch the
	//      batch of events scheduled at that exact time.
	//   4. Poll the objective controller exactly once per event batch
	//      (Phase D temporary compatibility hook; will be removed when
	//      the controller consumes a dedicated event).
	//   5. Check the stop conditions (enemy removed, objective finished).
	//
	// Counters tracked explicitly so a future regression that spikes
	// e.g. on event-batches visibly fails. We name them precisely:
	//   time_advances        -- number of peek_next_event_time() queries
	//   event_batches        -- number of reach_time() calls
	size_t time_advances = 0;
	size_t event_batches = 0;
	time::time_t final_dispatched_time = time::time_t::from_int(0);

	bool enemy_removed = false;
	bool objective_finished = false;
	while (event_batches < WW_MISSION1_MAX_EVENT_BATCHES) {
		// Step 1: peek next event time without consuming it.
		auto next_opt = event_loop->peek_next_event_time();
		++time_advances;
		if (not next_opt.has_value()) {
			// Queue exhausted -- no more events to dispatch.
			break;
		}
		time::time_t next = *next_opt;

		// Step 2: stop if the next event time would exceed the deadline.
		if (next > WW_MISSION1_DEADLINE) {
			break;
		}

		// Step 3: dispatch the event(s) scheduled at `next`. The
		// game.removal event handler is now subscribed to the
		// ObjectiveRemovalHandler (P4.1), so dispatching the
		// removal event is sufficient to complete the objective --
		// no per-batch tick_objective_controller() call is needed.
		event_loop->reach_time(next, state);
		++event_batches;
		final_dispatched_time = next;

		enemy_removed = not state->has_game_entity(enemy_id);
		objective_finished = simulation->is_objective_finished();
		if (enemy_removed or objective_finished) {
			break;
		}
	}

	// ---- Phase 4: stop-condition diagnostics + assertions.
	WW1_ASSERT(enemy_removed,
	          "EnemyVillager not removed before deadline "
	              "(time_advances=" << time_advances
	              << " event_batches=" << event_batches
	              << " last_dispatched_t=" << final_dispatched_time << ")");
	WW1_ASSERT(objective_finished,
	          "Mission 1 objective not finished (Victory not reached; "
	              "time_advances=" << time_advances
	              << " event_batches=" << event_batches
	              << " last_dispatched_t=" << final_dispatched_time << ")");

	if (assertion_failed) {
		log::log(MSG(err) << snapshot_failure_state(state, simulation, final_dispatched_time));
		log::log(MSG(err) << "WW MISSION 1 HEADLESS REGRESSION: FAIL");
		return;
	}

	log::log(MSG(info) << "WW MISSION 1 HEADLESS REGRESSION: PASS"
	                       " (enemy_removed=" << enemy_removed
	                       << " objective_finished=" << objective_finished
	                       << " time_advances=" << time_advances
	                       << " event_batches=" << event_batches
	                       << " last_dispatched_t=" << final_dispatched_time << ")");
}

} // namespace openage::gamestate::tests
