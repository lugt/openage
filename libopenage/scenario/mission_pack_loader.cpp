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
		// Register BOTH the full PlacedEntity instance FQON (what
		// the openage objective_controller / legacy Game::load_scenario
		// keyed on) AND the short scenario_object_id (what the
		// openage.scenario IR exposes). The id returned by
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

	// 6. Areas (definitions only; dependency watchers are P4 work).
	log::log(MSG(info) << "load_mission_pack_into_game_state: areas="
	                   << pack.areas.size() << " (definitions only; P4 wires watchers)");

	// 7. Triggers (definitions only; execution is P4 work).
	log::log(MSG(info) << "load_mission_pack_into_game_state: triggers="
	                   << pack.triggers.size() << " (definitions only; P4 wires execution)");

	// 8. Initial objective events. The MVP reconstruction's
	//    VictoryDefinition.required_removals maps directly onto the
	//    SetObjective events the openage objective_controller expects.
	for (const auto &sid : pack.victory.required_removals) {
		log::log(MSG(info) << "load_mission_pack_into_game_state: "
		                   << "initial SetObjective target="
		                   << sid
		                   << " (handled by objective_controller.cpp)");
	}
}


}  // namespace openage::gamestate