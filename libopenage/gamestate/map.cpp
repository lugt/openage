// Copyright 2024-2024 the openage authors. See copying.md for legal info.

#include "map.h"

#include "gamestate/api/terrain.h"
#include "gamestate/component/api/footprint.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/terrain.h"
#include "gamestate/terrain_chunk.h"
#include "pathfinding/cost_field.h"
#include "pathfinding/definitions.h"
#include "pathfinding/grid.h"
#include "pathfinding/navigation_service.h"
#include "pathfinding/pathfinder.h"
#include "pathfinding/sector.h"
#include "scenario/static_occupancy_grid.h"

import nyan;


namespace openage::gamestate {
Map::Map(const std::shared_ptr<GameState> &state,
         const std::shared_ptr<Terrain> &terrain) :
	terrain{terrain},
	pathfinder{std::make_shared<path::Pathfinder>()},
	dynamic_occupancy{std::make_shared<
		openage::scenario::DynamicOccupancy>()},
	grid_lookup{} {
	// Create a grid for each path type
	// TODO: This is non-deterministic because of the unordered set. Is this a problem?
	auto nyan_db = state->get_db_view();
	std::unordered_set<nyan::fqon_t> path_types = nyan_db->get_obj_children_all("engine.util.path_type.PathType");
	size_t grid_idx = 0;
	auto chunk_size = this->terrain->get_chunk(0)->get_size();
	auto side_length = std::max(chunk_size[0], chunk_size[1]);
	auto grid_size = this->terrain->get_chunks_size();
	for (const auto &path_type : path_types) {
		auto grid = std::make_shared<path::Grid>(grid_idx, grid_size, side_length);
		this->pathfinder->add_grid(grid);

		this->grid_lookup.emplace(path_type, grid_idx);
		grid_idx += 1;
	}

	// Set path costs
	for (size_t chunk_idx = 0; chunk_idx < this->terrain->get_chunks().size(); ++chunk_idx) {
		auto chunk_terrain = this->terrain->get_chunk(chunk_idx);
		const auto extent = chunk_terrain->get_size();
		if (extent[0] != side_length or extent[1] != side_length) {
			for (const auto &[path_type, grid_id] : this->grid_lookup) {
				auto sector = this->pathfinder->get_grid(grid_id)->get_sector(chunk_idx);
				sector->get_cost_field()->set_costs(
					std::vector<path::cost_t>(side_length * side_length, path::COST_IMPASSABLE),
					time::TIME_ZERO);
			}
		}
		for (size_t tile_idx = 0; tile_idx < chunk_terrain->get_tiles().size(); ++tile_idx) {
			auto tile = chunk_terrain->get_tile(tile_idx);
			auto path_costs = api::APITerrain::get_path_costs(tile.terrain);

			for (const auto &path_cost : path_costs) {
				auto grid_id = this->grid_lookup.at(path_cost.first);
				auto grid = this->pathfinder->get_grid(grid_id);

				auto sector = grid->get_sector(chunk_idx);
				auto cost_field = sector->get_cost_field();
				cost_field->set_cost(tile_idx % extent[0], tile_idx / extent[0],
				                     path_cost.second, time::TIME_ZERO);
			}
		}
	}

	// Connect sectors with portals
	for (const auto &path_type : this->grid_lookup) {
		auto grid = this->pathfinder->get_grid(path_type.second);
		grid->init_portals();
		grid->init_portal_nodes();
	}
}

const util::Vector2s &Map::get_size() const {
	return this->terrain->get_size();
}

const std::shared_ptr<Terrain> &Map::get_terrain() const {
	return this->terrain;
}

const std::shared_ptr<path::Pathfinder> &Map::get_pathfinder() const {
	return this->pathfinder;
}

path::grid_id_t Map::get_grid_id(const nyan::fqon_t &path_grid) const {
	return this->grid_lookup.at(path_grid);
}


void Map::set_navigation_service(
	std::shared_ptr<openage::gamestate::scenario::NavigationGrid> grid,
	std::vector<openage::gamestate::component::FootprintBounds> static_obstacles) {
	this->navigation_service =
		std::make_shared<openage::pathfinding::NavigationService>(
			std::move(grid),
			std::move(static_obstacles));
}


// P4.4F Task 3: register an entity in the Map's shared
// DynamicOccupancy at spawn time. See map.h for the full
// contract. The default radius (0.4) applies when the entity
// has no footprint; otherwise the footprint's radius_x/y is
// used (matching move.cpp's occ_bounds_at lambda). Static
// buildings (fp.is_static() == true) are SKIPPED -- they are
// tracked in StaticOccupancyGrid + NavigationService, and
// duplicating them in DynamicOccupancy would double-count
// blockers and break demo pin counts. The DynamicOccupancy is
// always non-null (constructed in the Map ctor), but we
// null-check defensively.
void register_entity_in_dynamic_occupancy(
	Map &map,
	const GameEntity &entity,
	double center_x,
	double center_y) {
	auto occ = map.get_dynamic_occupancy();
	if (not occ) {
		return;
	}
	double rx = 0.4;
	double ry = 0.4;
	if (not entity.has_footprint()) {
		return;
	}
	if (entity.has_footprint()) {
		auto fp = entity.footprint();
		// Buildings (is_static=true) are tracked in
		// StaticOccupancyGrid + NavigationService; do NOT
		// duplicate them in DynamicOccupancy. Only dynamic
		// units (villagers, enemies, etc.) go in the
		// dynamic table.
		if (fp.is_static()) {
			return;
		}
		rx = fp.radius_x();
		ry = fp.radius_y();
	}
	openage::gamestate::component::FootprintBounds bounds;
	bounds.min_x = center_x - rx;
	bounds.min_y = center_y - ry;
	bounds.max_x = center_x + rx;
	bounds.max_y = center_y + ry;
	occ->register_entity(entity.get_id(), bounds);
}

} // namespace openage::gamestate
