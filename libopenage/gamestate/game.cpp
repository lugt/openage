// Copyright 2018-2024 the openage authors. See copying.md for legal info.

#include "game.h"

#include <vector>

#include <nyan/nyan.h>

#include "log/log.h"
#include "log/message.h"

#include "assets/mod_manager.h"
#include "assets/modpack.h"
#include "error/error.h"
#include "gamestate/api/scenario.h"
#include "gamestate/component/internal/activity.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/entity_factory.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "gamestate/terrain.h"
#include "gamestate/terrain_chunk.h"
#include "gamestate/terrain_factory.h"
#include "gamestate/terrain_tile.h"
#include "gamestate/universe.h"
#include "time/time.h"
#include "util/path.h"
#include "util/strings.h"
#include "util/vector.h"

#include "coord/phys.h"
#include "coord/tile.h"

namespace openage::gamestate {

Game::Game(const std::shared_ptr<openage::event::EventLoop> &event_loop,
           const std::shared_ptr<assets::ModManager> &mod_manager,
           const std::shared_ptr<EntityFactory> &entity_factory,
           const std::shared_ptr<TerrainFactory> &terrain_factory,
           const std::vector<PlayerDescriptor> &players,
           const nyan::fqon_t &scenario_fqon) :
	event_loop{event_loop},
	entity_factory{entity_factory},
	db{nyan::Database::create()},
	state{std::make_shared<GameState>(this->db, event_loop)},
	universe{std::make_shared<Universe>(state)} {
	this->load_data(mod_manager);

	// Add one Player per PlayerDescriptor. Empty FQON yields a player
	// with neither civ patches nor starting resources (debug/observer
	// slot). Order of insertion is preserved so player IDs in
	// GameState.players() correspond 1:1 with this vector.
	for (const auto &descriptor : players) {
		auto player = entity_factory->add_player(event_loop, state,
		                                         descriptor.player_setup_fqon);
		state->add_player(player);
	}

	// TODO: This lets the spawner event check which modpacks are loaded,
	//       so that it can decide which entities it can spawn.
	//       This can be removed when we spawn based on game logic rather than
	//       hardcoded entity types.
	this->state->set_mod_manager(mod_manager);

	// Branch on scenario: empty -> debug hardcoded layout, otherwise
	// resolve the nyan Scenario and build terrain + initial entities from it.
	if (scenario_fqon.empty()) {
		this->generate_terrain(terrain_factory);
	}
	else {
		this->load_scenario(terrain_factory, scenario_fqon);
	}
}

const std::shared_ptr<GameState> &Game::get_state() const {
	return this->state;
}

void Game::attach_renderer(const std::shared_ptr<renderer::RenderFactory> &render_factory) {
	this->universe->attach_renderer(render_factory);
	this->state->get_map()->get_terrain()->attach_renderer(render_factory);
}

void Game::load_data(const std::shared_ptr<assets::ModManager> &mod_manager) {
	auto load_order = mod_manager->get_load_order();

	for (auto &mod_id : load_order) {
		auto mod = mod_manager->get_modpack(mod_id);
		auto info = mod->get_info();

		auto includes = info.includes;
		for (const auto &include : includes) {
			// handle wildcards
			auto parts = util::split(include, '/');
			auto last_part = parts.back();
			bool recursive = false;
			auto search = include;
			if (last_part == "**") {
				recursive = true;
				if (parts.size() == 1) {
					// include = "**"
					// start in root directory
					search = "";
				}
				else {
					// include = "path/to/somewhere/**"
					// remove the wildcard '**' and the slash '/'
					search = include.substr(0, include.size() - 3);
				}
			}

			this->load_path(info.path.get_parent(), info.path.get_name(), search, recursive);
		}
	}
}

void Game::load_path(const util::Path &base_dir,
                     const std::string &mod_dir,
                     const std::string &search,
                     bool recursive) {
	auto base_path = base_dir.resolve_native_path();
	auto search_path = base_dir / mod_dir / search;

	auto fileload_func = [&base_path](const std::string &filename) {
		// nyan wants a string filepath, so we have to construct it from the
		// path and subpath parameters
		log::log(INFO << "Loading .nyan file: " << filename);
		auto loc = base_path + "/" + filename;
		return std::make_shared<nyan::File>(loc);
	};

	// file loading
	if (search_path.is_file() and search_path.get_suffix() == ".nyan") {
		auto loc = mod_dir + "/" + search;
		this->db->load(loc, fileload_func);
		return;
	}

	// directory loading
	if (search_path.is_dir()) {
		// load all files in a directory
		for (auto p : search_path.iterdir()) {
			if (p.is_dir() and not recursive) {
				// folders are skipped unless we read recursively
				continue;
			}

			auto new_search = search + "/" + p.get_name();
			this->load_path(base_dir, mod_dir, new_search, recursive);
		}
	}
}

void Game::generate_terrain(const std::shared_ptr<TerrainFactory> &terrain_factory) {
	auto chunk0 = terrain_factory->add_chunk(this->state,
	                                         util::Vector2s{10, 10},
	                                         coord::tile_delta{0, 0});
	auto chunk1 = terrain_factory->add_chunk(this->state,
	                                         util::Vector2s{10, 10},
	                                         coord::tile_delta{10, 0});
	auto chunk2 = terrain_factory->add_chunk(this->state,
	                                         util::Vector2s{10, 10},
	                                         coord::tile_delta{0, 10});
	auto chunk3 = terrain_factory->add_chunk(this->state,
	                                         util::Vector2s{10, 10},
	                                         coord::tile_delta{10, 10});

	auto terrain = terrain_factory->add_terrain({20, 20}, {chunk0, chunk1, chunk2, chunk3});

	auto map = std::make_shared<Map>(this->state, terrain);
	this->state->set_map(map);
}

void Game::load_scenario(const std::shared_ptr<TerrainFactory> &terrain_factory,
                         const nyan::fqon_t &scenario_fqon) {
	// 1. Resolve the scenario nyan object from the game's database view.
	nyan::Object scenario_obj;
	try {
		scenario_obj = this->state->get_db_view()->get_object(scenario_fqon);
	}
	catch (const std::exception &e) {
		throw Error{ERR << "load_scenario: failed to load scenario "
		                << scenario_fqon << ": " << e.what()};
	}

	// 2. Validate type. is_scenario() is the canonical check from
	//    the parallel APIScenario module; rejecting wrong types here
	//    surfaces authoring errors at scenario-load time rather than
	//    later as cryptic missing-field errors.
	if (not api::APIScenario::is_scenario(scenario_obj)) {
		throw Error{ERR << "load_scenario: " << scenario_fqon
		                << " is not a Scenario (parent="
		                << scenario_obj.get_parents()[0] << ")"};
	}

	// 3. Read total map size and chunk layouts from the scenario.
	auto size = api::APIScenario::get_size(scenario_obj);
	auto chunk_layouts = api::APIScenario::get_chunks(scenario_obj);

	log::log(MSG(info) << "load_scenario: " << scenario_fqon
	                   << " size=" << size[0] << "x" << size[1]
	                   << " chunks=" << chunk_layouts.size());

	// 4. Materialise each chunk layout into a TerrainChunk. We rely on
	//    APIScenario to surface per-chunk offset/size/tile_fqons on each
	//    layout object; the factory does the FQON -> TerrainTile work.
	std::vector<std::shared_ptr<TerrainChunk>> chunks;
	chunks.reserve(chunk_layouts.size());
	for (const auto &chunk_layout : chunk_layouts) {
		// Layout members follow the TerrainChunkLayout nyan schema. The
		// field names are part of the contract between APIScenario
		// (created in parallel) and this consumer; they intentionally
		// mirror APIScenario's getter names for grep-ability.
		util::Vector2s chunk_size{
			static_cast<size_t>(chunk_layout.get_int("TerrainChunkLayout.size.x")),
			static_cast<size_t>(chunk_layout.get_int("TerrainChunkLayout.size.y"))};
		coord::tile_delta chunk_offset{
			chunk_layout.get_int("TerrainChunkLayout.offset.x"),
			chunk_layout.get_int("TerrainChunkLayout.offset.y")};

		// Collect tile FQONs into a flat vector (row-major). APIScenario
		// is expected to expose the tile set as an orderedset of objects
		// each carrying a TerrainChunkLayout.tile_fqon field. If the
		// schema instead exposes an OrderedSet of fqons directly we adjust
		// here once the parallel module lands; the rest of the pipeline
		// is agnostic.
		std::vector<nyan::fqon_t> tile_fqons;
		auto raw_tiles = chunk_layout.get_set("TerrainChunkLayout.tiles");
		tile_fqons.reserve(raw_tiles.size());
		for (const auto &tile_val : raw_tiles) {
			auto tile_obj_val = std::dynamic_pointer_cast<nyan::ObjectValue>(tile_val.get_ptr());
			auto tile_obj = this->state->get_db_view()->get_object(tile_obj_val->get_name());
			tile_fqons.push_back(tile_obj.get<nyan::ObjectValue>("TerrainChunkTile.terrain")->get_name());
		}

		chunks.push_back(terrain_factory->add_chunk_with_tiles(
			this->state, chunk_size, chunk_offset, tile_fqons));
	}

	// 5. Build the Terrain and the Map. The map is what the rest of the
	//    engine (spawner, presenter, input handlers) looks up via
	//    GameState::get_map(); replacing it wholesale is fine because
	//    load_scenario is meant to be called at most once per game.
	auto terrain = terrain_factory->add_terrain(size, std::move(chunks));
	auto map = std::make_shared<Map>(this->state, terrain);
	this->state->set_map(map);

	// 6. Spawn initial entities. Each PlacedEntity nyan object carries
	//    the entity FQON, a tile position, and an owner player ID.
	//    We don't gate on whether the resulting entity is on-map here -
	//    APIScenario::get_initial_entities is expected to only return
	//    valid placements.
	auto initial = api::APIScenario::get_initial_entities(scenario_obj);
	log::log(MSG(info) << "load_scenario: " << scenario_fqon
	                   << " initial_entities=" << initial.size());

	const time::time_t now = time::TIME_MIN; // scenario spawns pre-tick

	for (const auto &placed : initial) {
		nyan::fqon_t entity_fqon = placed.get<nyan::ObjectValue>("PlacedEntity.entity")->get_name();
		int64_t owner_id = placed.get_int("PlacedEntity.owner");
		coord::phys3 pos{
			static_cast<coord::phys_t>(placed.get_int("PlacedEntity.position.x")),
			static_cast<coord::phys_t>(placed.get_int("PlacedEntity.position.y")),
			coord::phys_t::zero()};

		auto entity = this->entity_factory->add_game_entity(
			this->event_loop, this->state,
			static_cast<player_id_t>(owner_id), entity_fqon);

		// Wire position component (created with default constructor in
		// init_components; set_position inserts the first keyframe).
		auto entity_pos = std::dynamic_pointer_cast<component::Position>(
			entity->get_component(component::component_t::POSITION));
		if (entity_pos) {
			entity_pos->set_position(now, pos);
		}

		// Wire ownership component.
		auto entity_own = std::dynamic_pointer_cast<component::Ownership>(
			entity->get_component(component::component_t::OWNERSHIP));
		if (entity_own) {
			entity_own->set_owner(now, static_cast<player_id_t>(owner_id));
		}

		// Initialise the activity so the entity is ready to act on tick.
		auto activity = std::dynamic_pointer_cast<component::Activity>(
			entity->get_component(component::component_t::ACTIVITY));
		if (activity) {
			activity->init(now);
		}

		this->state->add_game_entity(entity);
	}
}

} // namespace openage::gamestate
