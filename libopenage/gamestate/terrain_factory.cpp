// Copyright 2023-2024 the openage authors. See copying.md for legal info.

#include "terrain_factory.h"

#include <mutex>

#include <nyan/nyan.h>

#include "gamestate/api/terrain.h"
#include "gamestate/terrain.h"
#include "gamestate/terrain_chunk.h"
#include "gamestate/terrain_tile.h"
#include "renderer/render_factory.h"
#include "renderer/stages/terrain/render_entity.h"
#include "time/time.h"

#include "assets/mod_manager.h"
#include "gamestate/game_state.h"


namespace openage::gamestate {

// TODO: Remove test terrain references
static const std::vector<std::string> test_terrain_paths = {
	"../test/textures/test_terrain.terrain",
	"../test/textures/test_terrain2.terrain",
};

static const std::vector<nyan::fqon_t> aoe1_test_terrain = {
	"aoe1_base.data.terrain.forest.forest.Forest",
	"aoe1_base.data.terrain.grass.grass.Grass",
	"aoe1_base.data.terrain.dirt.dirt.Dirt",
	"aoe1_base.data.terrain.water.water.Water",
};
static const std::vector<nyan::fqon_t> de1_test_terrain = {
	"aoe1_base.data.terrain.desert.desert.Desert",
	"aoe1_base.data.terrain.grass.grass.Grass",
	"aoe1_base.data.terrain.dirt.dirt.Dirt",
	"aoe1_base.data.terrain.water.water.Water",
};
static const std::vector<nyan::fqon_t> aoe2_test_terrain = {
	"aoe2_base.data.terrain.foundation.foundation.Foundation",
	"aoe2_base.data.terrain.grass.grass.Grass",
	"aoe2_base.data.terrain.dirt.dirt.Dirt",
	"aoe2_base.data.terrain.water.water.Water",
};
static const std::vector<nyan::fqon_t> de2_test_terrain = {
	"de2_base.data.terrain.foundation.foundation.Foundation",
	"de2_base.data.terrain.grass.grass.Grass",
	"de2_base.data.terrain.dirt.dirt.Dirt",
	"de2_base.data.terrain.water.water.Water",
};
static const std::vector<nyan::fqon_t> hd_test_terrain = {
	"hd_base.data.terrain.foundation.foundation.Foundation",
	"hd_base.data.terrain.grass.grass.Grass",
	"hd_base.data.terrain.dirt.dirt.Dirt",
	"hd_base.data.terrain.water.water.Water",
};
static const std::vector<nyan::fqon_t> swgb_test_terrain = {
	"swgb_base.data.terrain.foundation.foundation.Foundation",
	"swgb_base.data.terrain.grass2.grass2.Grass2",
	"swgb_base.data.terrain.desert0.desert0.Desert0",
	"swgb_base.data.terrain.water1.water1.Water1",
};
static const std::vector<nyan::fqon_t> trial_test_terrain = {
	"trial_base.data.terrain.foundation.foundation.Foundation",
	"trial_base.data.terrain.grass.grass.Grass",
	"trial_base.data.terrain.dirt.dirt.Dirt",
	"trial_base.data.terrain.water.water.Water",
};

// TODO: Remove hardcoded test texture references
static std::vector<nyan::fqon_t> test_terrains; // declare static so we only have to do this once
static bool has_graphics = false;

void build_test_terrains(const std::shared_ptr<GameState> &gstate) {
	auto modpack_ids = gstate->get_mod_manager()->get_load_order();
	for (auto &modpack_id : modpack_ids) {
		if (modpack_id == "aoe1_base") {
			test_terrains.insert(test_terrains.end(),
			                     aoe1_test_terrain.begin(),
			                     aoe1_test_terrain.end());
			has_graphics = false;
		}
		else if (modpack_id == "de1_base") {
			test_terrains.insert(test_terrains.end(),
			                     de1_test_terrain.begin(),
			                     de1_test_terrain.end());
			has_graphics = false;
		}
		else if (modpack_id == "aoe2_base") {
			test_terrains.insert(test_terrains.end(),
			                     aoe2_test_terrain.begin(),
			                     aoe2_test_terrain.end());
			has_graphics = true;
		}
		else if (modpack_id == "de2_base") {
			test_terrains.insert(test_terrains.end(),
			                     de2_test_terrain.begin(),
			                     de2_test_terrain.end());
			has_graphics = false;
		}
		else if (modpack_id == "hd_base") {
			test_terrains.insert(test_terrains.end(),
			                     hd_test_terrain.begin(),
			                     hd_test_terrain.end());
			has_graphics = true;
		}
		else if (modpack_id == "swgb_base") {
			test_terrains.insert(test_terrains.end(),
			                     swgb_test_terrain.begin(),
			                     swgb_test_terrain.end());
			has_graphics = true;
		}
		else if (modpack_id == "trial_base") {
			test_terrains.insert(test_terrains.end(),
			                     trial_test_terrain.begin(),
			                     trial_test_terrain.end());
			has_graphics = true;
		}
	}
}

// Layout of terrain tiles on chunk 0
// values are the terrain index
static const std::array<size_t, 100> layout_chunk0{
	// clang-format off
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 1, 1, 1, 1, 1, 1, 1, 1, 0,
    0, 0, 1, 1, 1, 1, 1, 1, 0, 0,
    0, 0, 0, 1, 1, 1, 1, 0, 0, 0,
    0, 0, 0, 0, 1, 1, 0, 0, 0, 2,
    0, 0, 0, 0, 1, 1, 0, 0, 0, 2,
    0, 0, 0, 1, 3, 3, 1, 0, 0, 0,
    0, 0, 1, 1, 3, 3, 1, 1, 0, 0,
    0, 1, 1, 1, 3, 3, 1, 1, 1, 0,
    0, 0, 1, 1, 3, 3, 1, 0, 0, 0,
	// clang-format on
};

// Layout of terrain tiles on chunk 1
// values are the terrain index
static const std::array<size_t, 100> layout_chunk1{
	// clang-format off
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    2, 2, 2, 2, 0, 0, 0, 0, 0, 0,
    2, 2, 2, 2, 2, 0, 0, 0, 0, 0,
    0, 0, 0, 2, 2, 2, 0, 0, 0, 0,
    0, 0, 0, 0, 2, 2, 0, 0, 0, 0,
    0, 0, 0, 0, 2, 2, 0, 0, 0, 0,
    0, 0, 0, 0, 2, 2, 0, 0, 0, 0,
	// clang-format on
};

// Layout of terrain tiles on chunk 2
// values are the terrain index
static const std::array<size_t, 100> layout_chunk2{
	// clang-format off
    1, 1, 1, 1, 3, 3, 1, 0, 0, 0,
    1, 1, 1, 1, 3, 3, 1, 1, 0, 0,
    1, 1, 1, 1, 3, 3, 1, 1, 0, 0,
    1, 1, 1, 3, 3, 3, 3, 1, 0, 0,
    1, 3, 3, 3, 3, 3, 3, 3, 1, 2,
    1, 3, 3, 3, 2, 2, 2, 3, 1, 2,
    3, 3, 3, 3, 2, 2, 2, 3, 1, 2,
    1, 3, 3, 3, 2, 2, 2, 3, 1, 1,
    1, 3, 3, 3, 3, 3, 3, 3, 3, 1,
    1, 3, 3, 3, 3, 3, 3, 3, 1, 0,
	// clang-format on
};

// Layout of terrain tiles on chunk 3
// values are the terrain index
static const std::array<size_t, 100> layout_chunk3{
	// clang-format off
    0, 0, 0, 0, 2, 2, 0, 0, 0, 0,
    0, 0, 0, 2, 2, 2, 0, 0, 0, 0,
    0, 0, 2, 2, 2, 2, 1, 1, 2, 0,
    0, 2, 2, 2, 2, 2, 1, 2, 0, 0,
    2, 2, 2, 2, 2, 2, 2, 0, 0, 0,
    2, 2, 2, 2, 2, 2, 2, 0, 0, 0,
    0, 0, 2, 2, 2, 2, 2, 0, 0, 0,
    0, 0, 0, 2, 2, 2, 0, 0, 0, 0,
    0, 0, 0, 2, 2, 2, 0, 0, 0, 0,
    0, 0, 0, 2, 2, 2, 0, 0, 0, 0,
	// clang-format on
};


static const std::vector<std::array<size_t, 100>> layout_chunks{
	layout_chunk0,
	layout_chunk1,
	layout_chunk2,
	layout_chunk3,
};


std::shared_ptr<Terrain> TerrainFactory::add_terrain(const util::Vector2s &size,
                                                     std::vector<std::shared_ptr<TerrainChunk>> &&chunks) {
	// TODO: Replace this with a proper terrain generator.
	auto terrain = std::make_shared<Terrain>(size, std::move(chunks));

	return terrain;
}

std::shared_ptr<TerrainChunk> TerrainFactory::add_chunk(const std::shared_ptr<GameState> &gstate,
                                                        const util::Vector2s size,
                                                        const coord::tile_delta offset) {
	std::string terrain_info_path;

	// TODO: Remove test texture references
	// ==========
	nyan::Object terrain_obj;
	if (test_terrains.empty()) {
		build_test_terrains(gstate);
	}

	// fill the chunk with tiles
	std::vector<TerrainTile> tiles{};
	tiles.reserve(size[0] * size[1]);

	static size_t test_chunk_index = 0;
	if (not test_terrains.empty()) {
		// use one of the modpack terrain textures
		if (test_chunk_index >= layout_chunks.size()) {
			test_chunk_index = 0;
		}

		for (size_t i = 0; i < size[0] * size[1]; ++i) {
			size_t terrain_index = layout_chunks.at(test_chunk_index).at(i);
			terrain_obj = gstate->get_db_view()->get_object(test_terrains.at(terrain_index));

			if (has_graphics) {
				terrain_info_path = api::APITerrain::get_terrain_path(terrain_obj);
			}
			else {
				terrain_info_path = test_terrain_paths.at(test_chunk_index % test_terrain_paths.size());
			}

			tiles.push_back({terrain_obj, terrain_info_path, terrain_elevation_t::zero()});
		}

		test_chunk_index += 1;
	}
	// ==========

	auto chunk = std::make_shared<TerrainChunk>(size, offset, std::move(tiles));

	if (this->render_factory) {
		auto render_entity = this->render_factory->add_terrain_render_entity(size, offset);
		chunk->set_render_entity(render_entity);

		chunk->render_update(time::TIME_ZERO);
	}

	return chunk;
}

std::shared_ptr<TerrainChunk> TerrainFactory::add_chunk_with_tiles(
	const std::shared_ptr<GameState> &gstate,
	const util::Vector2s size,
	const coord::tile_delta offset,
	const std::vector<nyan::fqon_t> &tile_fqons) {
	// Ensure test_terrains list is populated so we can fall back when a tile
	// FQON is missing or doesn't resolve to a terrain. Mirrors add_chunk().
	if (test_terrains.empty()) {
		build_test_terrains(gstate);
	}

	const size_t expected = size[0] * size[1];
	// nyan orderedset(text) de-duplicates, so a chunk whose every tile
	// shares one terrain (the common tutorial case: an all-grass map)
	// collapses to a single FQON. Accept that and broadcast the lone FQON
	// across the whole chunk; any other count must still be per-tile.
	std::vector<nyan::fqon_t> effective_fqons;
	if (tile_fqons.size() == 1) {
		effective_fqons.assign(expected, tile_fqons[0]);
	}
	else {
		effective_fqons = tile_fqons;
		if (effective_fqons.size() != expected) {
			throw Error(MSG(err) << "add_chunk_with_tiles: tile_fqons count "
			                     << effective_fqons.size()
			                     << " does not match chunk size "
			                     << size[0] << "x" << size[1]
			                     << " (" << expected << " tiles expected; "
			                     << "use a single FQON for a uniform chunk)");
		}
	}

	auto db_view = gstate->get_db_view();

	std::vector<TerrainTile> tiles{};
	tiles.reserve(expected);

	for (const auto &tile_fqon : effective_fqons) {
		nyan::Object terrain_obj;
		std::string terrain_info_path;
		bool resolved = false;

		// Try to resolve the FQON through the nyan database first. Missing
		// FQONs (e.g. placeholder in a WIP scenario) fall back to a default
		// test terrain so the chunk still renders something.
		if (not tile_fqon.empty()) {
			try {
				terrain_obj = db_view->get_object(tile_fqon);
				if (api::APITerrain::is_terrain(terrain_obj)) {
					if (has_graphics) {
						terrain_info_path = api::APITerrain::get_terrain_path(terrain_obj);
					}
					else {
						// No graphical assets in this modpack (e.g. the
						// schema-only 'engine' modpack used by the tutorial):
						// fall back to the bundled test terrain texture, which
						// the renderer resolves relative to assets/converted
						// (-> assets/test/textures/test_terrain.terrain). Mirrors
						// the legacy add_chunk() path for non-graphical modpacks.
						terrain_info_path = test_terrain_paths.front();
					}
					resolved = true;
				}
			}
			catch (const std::exception &) {
				// FQON didn't resolve; fall through to default.
			}
		}

		if (not resolved) {
			// Fallback to first available test terrain (matches behaviour of
			// the legacy add_chunk when no nyan tile data is available).
			if (test_terrains.empty()) {
				throw Error(MSG(err) << "add_chunk_with_tiles: no fallback terrain available for tile "
				                     << tile_fqon);
			}
			terrain_obj = db_view->get_object(test_terrains.front());
			if (has_graphics) {
				terrain_info_path = api::APITerrain::get_terrain_path(terrain_obj);
			}
			else {
				terrain_info_path = test_terrain_paths.front();
			}
		}

		tiles.push_back({terrain_obj, terrain_info_path, terrain_elevation_t::zero()});
	}

	auto chunk = std::make_shared<TerrainChunk>(size, offset, std::move(tiles));

	if (this->render_factory) {
		auto render_entity = this->render_factory->add_terrain_render_entity(size, offset);
		chunk->set_render_entity(render_entity);

		chunk->render_update(time::TIME_ZERO);
	}

	return chunk;
}

void TerrainFactory::attach_renderer(const std::shared_ptr<renderer::RenderFactory> &render_factory) {
	std::unique_lock lock{this->mutex};

	this->render_factory = render_factory;
}

} // namespace openage::gamestate
