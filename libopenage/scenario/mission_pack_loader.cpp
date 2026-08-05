// Copyright 2026 the openage authors. See copying.md for legal info.

#include "mission_pack_loader.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

#include <nyan/nyan.h>

#include "error/error.h"
#include "gamestate/component/internal/activity.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/entity_factory.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "gamestate/player.h"
#include "gamestate/terrain.h"
#include "gamestate/terrain_chunk.h"
#include "gamestate/terrain_factory.h"
#include "scenario/placement_validator.h"
#include "scenario/static_occupancy_grid.h"
#include "log/log.h"
#include "log/message.h"

#include "coord/phys.h"
#include "coord/tile.h"
#include "time/time.h"
#include "util/vector.h"


namespace openage::gamestate {


namespace {


// Resolve an entity FQON against the game state's nyan view. Raises
// MissionPackLoadError if the FQON does not resolve; this is the
// spec §10.1 'missing entity_definition references' invariant.
nyan::Object resolve_entity_fqon(
	const std::shared_ptr<GameState> &state,
	const std::string &fqon,
	const std::string &placement_id) {
	try {
		return state->get_db_view()->get_object(fqon);
	}
	catch (const std::exception &e) {
		throw MissionPackLoadError{
			"placement " + placement_id
			+ " references unresolved entity FQON " + fqon
			+ ": " + e.what()
		};
	}
}


// Spec §6.4 chunk-grid invariants. The Mission Pack already encodes
// the dense terrain FQON list per chunk; the loader verifies that
// every tile in (width*height) belongs to exactly one chunk before
// calling the factory.
void assert_chunks_cover_map(const MissionMapDefinition &map) {
	std::vector<bool> seen(map.terrain_ids_count, false);
	for (const auto &chunk : map.chunks) {
		for (int64_t dy = 0; dy < chunk.size_y; ++dy) {
			for (int64_t dx = 0; dx < chunk.size_x; ++dx) {
				int64_t x = chunk.offset_x + dx;
				int64_t y = chunk.offset_y + dy;
				int64_t idx = y * map.width + x;
				seen[idx] = true;
			}
		}
	}
	for (size_t i = 0; i < seen.size(); ++i) {
		if (!seen[i]) {
			throw MissionPackLoadError{
				"terrain tile "
				+ std::to_string(static_cast<int64_t>(i) % map.width)
				+ ","
				+ std::to_string(static_cast<int64_t>(i) / map.width)
				+ " has no chunk coverage"
			};
		}
	}
}


// Compute the full PlacedEntity instance FQON from the Mission Pack's
// scenario_fqon and a placement's short ScenarioObjectId. For the
// Mission 1 reconstruction, scenario_fqon is
//   "ww_mvp.campaign.william_wallace.mission_01.Mission"
// and the corresponding instance FQON is
//   "ww_mvp.campaign.william_wallace.mission_01.EnemyVillager"
// i.e. everything before the trailing ".Mission" (or ".mission_NN")
// plus a '.' plus the ScenarioObjectId. This matches what
// GameState::register_named_entity expects; the legacy nyan loader
// registered the same key via placed.get_name().
std::string compute_instance_fqon(
	const std::string &scenario_fqon,
	const std::string &scenario_object_id) {
	auto pos = scenario_fqon.rfind(".Mission");
	if (pos == std::string::npos) {
		// Fall back to the file-name-level prefix (without the
		// mission_XX discriminator). The Mission 1 case never
		// hits this branch.
		pos = scenario_fqon.rfind('.');
	}
	if (pos == std::string::npos) {
		return scenario_object_id;
	}
	return scenario_fqon.substr(0, pos) + "." + scenario_object_id;
}


}  // namespace


void load_mission_pack_into_game_state(
	const std::shared_ptr<openage::event::EventLoop> &event_loop,
	const std::shared_ptr<GameState> &state,
	const std::shared_ptr<EntityFactory> &entity_factory,
	const std::shared_ptr<TerrainFactory> &terrain_factory,
	const MissionPack &pack) {

	if (!state) {
		throw MissionPackLoadError{"load_mission_pack_into_game_state: state is null"};
	}
	if (!entity_factory) {
		throw MissionPackLoadError{"load_mission_pack_into_game_state: entity_factory is null"};
	}
	if (!terrain_factory) {
		throw MissionPackLoadError{"load_mission_pack_into_game_state: terrain_factory is null"};
	}

	// P4.4B: build the StaticOccupancyGrid + PlacementValidator.
	// The map extent is derived from the pack's terrain chunks
	// (the runtime map is not yet attached to state at this
	// point in the load order; the loader is the one that
	// builds it). The grid and validator are local to this load
	// and are not stored on the GameState; the runtime
	// NavigationGrid (consumed by P4.4C's A*) is constructed
	// in a follow-up that owns the StaticOccupancyGrid
	// instance.
	int map_min_x = 0;
	int map_min_y = 0;
	int map_max_x = 0;
	int map_max_y = 0;
	bool first_chunk = true;
	for (const auto &chunk : pack.map.chunks) {
		const int ox = static_cast<int>(chunk.offset_x);
		const int oy = static_cast<int>(chunk.offset_y);
		const int sx = static_cast<int>(chunk.size_x);
		const int sy = static_cast<int>(chunk.size_y);
		if (first_chunk) {
			map_min_x = ox;
			map_min_y = oy;
			map_max_x = ox + sx;
			map_max_y = oy + sy;
			first_chunk = false;
		}
		else {
			if (ox < map_min_x) map_min_x = ox;
			if (oy < map_min_y) map_min_y = oy;
			if (ox + sx > map_max_x) map_max_x = ox + sx;
			if (oy + sy > map_max_y) map_max_y = oy + sy;
		}
	}
	if (first_chunk) {
		throw MissionPackLoadError{
			"load_mission_pack_into_game_state: no terrain chunks"
		};
	}
	auto occ = std::make_shared<openage::gamestate::scenario::StaticOccupancyGrid>(
		map_min_x, map_min_y, map_max_x, map_max_y);
	openage::gamestate::scenario::PlacementValidator validator{
		map_min_x, map_min_y, map_max_x, map_max_y};

	// P4.4C: the list of static obstacle FootprintBounds to feed
	// to the NavigationService. Accumulated in the placement loop
	// below so we don't have to re-derive each building's bounds
	// from the StaticOccupancyGrid (which only knows cell ids).
	std::vector<openage::gamestate::component::FootprintBounds>
		static_obstacles;

	log::log(MSG(info) << "load_mission_pack_into_game_state: placement "
	                   << "validator ready (map=[" << map_min_x
	                   << "," << map_max_x << ") x ["
	                   << map_min_y << "," << map_max_y << "))");

	log::log(MSG(info) << "load_mission_pack_into_game_state: scenario_kind="
	                   << pack.scenario_kind
	                   << " fqon=" << pack.scenario_fqon
	                   << " players=" << pack.players.size()
	                   << " placements=" << pack.placements.size()
	                   << " chunks=" << pack.map.chunks.size());

	// 1. Terrain and chunks.
	assert_chunks_cover_map(pack.map);
	std::vector<std::shared_ptr<TerrainChunk>> chunks;
	chunks.reserve(pack.map.chunks.size());
	for (const auto &chunk : pack.map.chunks) {
		std::vector<nyan::fqon_t> tile_fqons;
		tile_fqons.reserve(chunk.tile_fqons.size());
		for (const auto &fqon : chunk.tile_fqons) {
			tile_fqons.push_back(nyan::fqon_t{fqon});
		}
		chunks.push_back(terrain_factory->add_chunk_with_tiles(
			state,
			util::Vector2s{
				static_cast<size_t>(chunk.size_x),
				static_cast<size_t>(chunk.size_y),
			},
			coord::tile_delta{
				static_cast<coord::tile_t>(chunk.offset_x),
				static_cast<coord::tile_t>(chunk.offset_y),
			},
			tile_fqons));
	}
	auto terrain = terrain_factory->add_terrain(
		util::Vector2s{
			static_cast<size_t>(pack.map.width),
			static_cast<size_t>(pack.map.height),
		},
		std::move(chunks));
	auto map = std::make_shared<Map>(state, terrain);
	state->set_map(map);

	// 2. Players.
	// The Mission Pack's PlayerDefinition does not yet carry a
	// player_setup FQON (the openage/scenario IR rounds through a
	// separate PlayerSetup loader). For the MVP reconstruction we
	// use an empty FQON, which produces a player with neither civ
	// patches nor starting resources — exactly what the demo 7 log
	// observes today. P5 / P7 will wire the PlayerSetup FQON back in
	// once the IR exposes it on PlayerDefinition.
	std::unordered_map<int64_t, std::shared_ptr<Player>> players_by_id;
	for (const auto &p : pack.players) {
		nyan::fqon_t player_setup_fqon{};
		auto player = entity_factory->add_player(event_loop, state, player_setup_fqon);
		players_by_id[p.player_id] = player;
		state->add_player(player);
	}

	// 3. Diplomacy. Empty matrix for the MVP reconstruction (spec
	//    §6.1 — the IR's VictoryDefinition drives completion; the
	//    openage objective_controller reads required_removals and
	//    checks player removals).
	log::log(MSG(info) << "load_mission_pack_into_game_state: diplomacy: "
	                   << "(empty matrix; MVP reconstruction)");

	// 4. Placements.
	// Spawn at sim_time=0, matching Game::load_scenario's old behavior
	// (every curve's KeyframeContainer seeds a default keyframe at
	// TIME_MIN = -INF; spawning at TIME_MIN underflows the size_t
	// hint in Continuous::set_last).
	const time::time_t now = time::time_t{0};

	for (size_t i = 0; i < pack.placements.size(); ++i) {
		const auto &ep = pack.placements[i];

		// Resolve the entity FQON. Failure here is the spec §10.1
		// 'invalid FQON' invariant.
		resolve_entity_fqon(state, ep.entity_definition_fqon, ep.scenario_object_id);

		auto player_it = players_by_id.find(ep.player_id);
		if (player_it == players_by_id.end()) {
			throw MissionPackLoadError{
				"placement " + ep.scenario_object_id
				+ " has unknown owner player_id "
				+ std::to_string(ep.player_id)
			};
		}
		(void)player_it;  // ownership is wired via add_game_entity below

		auto entity = entity_factory->add_game_entity(
			event_loop, state,
			static_cast<player_id_t>(ep.player_id),
			nyan::fqon_t{ep.entity_definition_fqon});

		// Wire position component.
		auto entity_pos = std::dynamic_pointer_cast<component::Position>(
			entity->get_component(component::component_t::POSITION));
		if (entity_pos) {
			coord::phys3 pos = coord::tile{
				static_cast<coord::tile_t>(ep.pos_x),
				static_cast<coord::tile_t>(ep.pos_y),
			}.to_phys3();
			entity_pos->set_position(now, pos);
		}

		// P4.4B.1: validate the placement (pure AABB overlap
		// against the validator's accepted list) and, if the
		// entity is a static building, register it in the
		// StaticOccupancyGrid. Moving units (Villager, Enemy)
		// are validated for AABB overlap but NOT written to
		// the static grid -- their own tile stays walkable
		// for the pathfinder. The entity's Footprint was
		// mounted by the factory from the nyan Collision
		// ability; we set its center here.
		if (entity->has_footprint()) {
			auto fp = entity->footprint();
			fp.set_center(static_cast<double>(ep.pos_x),
			             static_cast<double>(ep.pos_y));
			entity->set_footprint(fp);
			openage::gamestate::scenario::PlacementRequest req;
			req.entity = entity->get_id();
			req.entity_fqon = ep.entity_definition_fqon;
			req.scenario_object_id = ep.scenario_object_id;
			req.center_x = static_cast<double>(ep.pos_x);
			req.center_y = static_cast<double>(ep.pos_y);
			req.radius_x = fp.radius_x();
			req.radius_y = fp.radius_y();
			req.blocks_placement = fp.blocks_placement();
			// Per spec §VIII, units are dynamic (not static).
			// Buildings are static. The default in the
			// factory is is_static=false; the loader
			// classifies a static-occupying entity by
			// blocks_pathfinding + a future Mission Pack
			// override flag. For the P4.4B MVP, the
			// TownCenter is the only static entity; we
			// detect it by fqon (TownCenter is in the
			// entity_definition_fqon). The pathfinder
			// never sees Villager/Enemy as a static
			// obstacle.
			req.is_static = (ep.entity_definition_fqon.find(
				"town_center") != std::string::npos);
			auto pf = validator.validate(req);
			if (not pf.ok()) {
				throw MissionPackLoadError{
					"load_mission_pack_into_game_state: "
					"placement REJECTED for scenario_object_id="
					+ ep.scenario_object_id
					+ " entity_fqon=" + ep.entity_definition_fqon
					+ " reason=" + pf.reason_string()
					+ " context=" + pf.context};
			}
			// Static occupancy: only static+blocks_pathfinding
			// entities write to the grid. Units (Villager,
			// Enemy) do NOT permanently block any cell.
			if (req.is_static && fp.blocks_pathfinding()) {
				auto conflict = occ->occupy(req.entity, pf.requested_bounds);
				if (conflict.has_value()) {
					throw MissionPackLoadError{
						"load_mission_pack_into_game_state: "
						"static occupy conflict for entity "
						+ std::to_string(req.entity)
						+ " blocker=" + std::to_string(*conflict)};
				}
				// P4.4C: record the bounds for the NavigationService
				// so the A* can inflate them per-mover.
				static_obstacles.push_back(pf.requested_bounds);
			}
		}

		// Wire ownership component.
		auto entity_own = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (entity_own) {
			entity_own->set_owner(now, static_cast<player_id_t>(ep.player_id));
		}

		// Initialise activity.
		auto activity = std::dynamic_pointer_cast<component::Activity>(
			entity->get_component(component::component_t::ACTIVITY));
		if (activity) {
			activity->init(now);
		}

		state->add_game_entity(entity);

		// 5. ScenarioObjectId -> GameEntityId mapping.
		// Register BOTH the full PlacedEntity instance FQON (what the
		// TriggerRuntime's EntityRemovedCondition resolves via
		// GameState::get_entity_id_by_instance, and what the legacy
		// Game::load_scenario / objective_controller keyed on) AND the
		// short scenario_object_id (what the openage.scenario IR
		// exposes). The id returned by
		// get_next_entity_id is monotonically increasing in
		// placement-list order (see
		// EntityFactory::get_next_entity_id), which matches the
		// openage/scenario loader's "scenario_object_to_entity"
		// contract from P2.4. The mapping is independent of nyan
		// resolution timing -- it is the canonical source of truth.
		std::string instance_fqon = compute_instance_fqon(
			pack.scenario_fqon, ep.scenario_object_id);
		state->register_named_entity(
			nyan::fqon_t{instance_fqon},
			entity->get_id());
		state->register_named_entity(
			nyan::fqon_t{ep.scenario_object_id},
			entity->get_id());

		log::log(MSG(info) << "load_mission_pack_into_game_state: "
		                   << "spawned placed_instance=" << instance_fqon
		                   << " (short=" << ep.scenario_object_id << ")"
		                   << " entity_id=" << entity->get_id()
		                   << " entity_fqon=" << ep.entity_definition_fqon);
	}

	// P4.4C: build the deterministic NavigationService now that
	// StaticOccupancyGrid + placements are settled. The
	// TerrainNavigationGrid composes the StaticOccupancyGrid and
	// the Terrain into the walkability view that the A* queries.
	// The static obstacle list is the FootprintBounds we recorded
	// in the placement loop above; the loader is the single
	// source of truth for which placements blocked which cells.
	{
		auto nav_grid = std::make_shared<
			openage::gamestate::scenario::TerrainNavigationGrid>(
				occ, map->get_terrain());
		map->set_navigation_service(nav_grid,
			std::move(static_obstacles));
		log::log(MSG(info) << "load_mission_pack_into_game_state: "
		                   << "NavigationService ready (static_obstacles="
		                   << map->get_navigation_service()->static_obstacle_count()
		                   << ")");
	}

	// 6. Areas (definitions only; dependency watchers are P4 work).
	log::log(MSG(info) << "load_mission_pack_into_game_state: areas="
	                   << pack.areas.size() << " (definitions only; P4 wires watchers)");

	// 7. Triggers (definitions only; execution is P4 work).
	log::log(MSG(info) << "load_mission_pack_into_game_state: triggers="
	                   << pack.triggers.size() << " (definitions only; P4 wires execution)");

	// 8. Initial objective targets. The MVP reconstruction's
	//    VictoryDefinition.required_removals lists the entities whose
	//    removal completes the mission. GameSimulation::wire_trigger_data
	//    synthesizes one defeat_enemy trigger per required_removal
	//    (EntityDestroyed condition + SetObjective/CompleteObjective/
	//    DeclareVictory effects) on the TriggerRuntime, so this list is
	//    the data source for the production objective lifecycle -- the
	//    legacy ObjectiveController is no longer on the production path.
	for (const auto &sid : pack.victory.required_removals) {
		log::log(MSG(info) << "load_mission_pack_into_game_state: "
		                   << "victory required_removal target="
		                   << sid
		                   << " (synthesized into a defeat_enemy trigger "
		                   << "by GameSimulation::wire_trigger_data)");
	}
}


}  // namespace openage::gamestate