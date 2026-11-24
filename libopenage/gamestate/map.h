// Copyright 2024-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <memory>
#include <unordered_map>
#include <vector>

#include <nyan/config.h>

#include "gamestate/component/api/footprint.h"
#include "pathfinding/navigation_service.h"
#include "scenario/dynamic_occupancy.h"
#include "pathfinding/types.h"
#include "scenario/static_occupancy_grid.h"
#include "util/vector.h"


namespace openage {
namespace path {
class Pathfinder;
} // namespace path

namespace gamestate {
class GameState;
class Terrain;
class GameEntity;

class Map {
public:
	/**
	 * Create a new map from existing terrain.
	 *
	 * Initializes the pathfinder with the terrain path costs.
	 *
	 * @param state Game state.
	 * @param terrain Terrain object.
	 */
	Map(const std::shared_ptr<GameState> &state,
	    const std::shared_ptr<Terrain> &terrain);

	~Map() = default;

	/**
	 * Get the size of the map.
	 *
	 * @return Map size (width x height).
	 */
	const util::Vector2s &get_size() const;

	/**
	 * Get the terrain of the map.
	 *
	 * @return Terrain.
	 */
	const std::shared_ptr<Terrain> &get_terrain() const;

	/**
	 * Get the pathfinder for the map.
	 *
	 * @return Pathfinder.
	 */
	const std::shared_ptr<path::Pathfinder> &get_pathfinder() const;

	/**
	 * Get the grid ID associated with a nyan path grid object.
	 *
	 * @param path_grid Path grid object fqon.
	 *
	 * @return Grid ID.
	 */
	path::grid_id_t get_grid_id(const nyan::fqon_t &path_grid) const;

	/**
	 * P4.4C: attach the deterministic A* NavigationService to this
	 * map. Built once after StaticOccupancyGrid is populated; the
	 * service owns its own copy of the static obstacle list. May
	 * be called multiple times to rebuild on layout changes.
	 */
	void set_navigation_service(
		std::shared_ptr<openage::gamestate::scenario::NavigationGrid> grid,
		std::vector<openage::gamestate::component::FootprintBounds> static_obstacles);

	/**
	 * P4.4C: get the NavigationService. Returns nullptr until
	 * set_navigation_service() has been called.
	 */
	const std::shared_ptr<openage::pathfinding::NavigationService>
	&get_navigation_service() const {
		return this->navigation_service;
	}

	/**
	 * P4.4F.1: get the shared DynamicOccupancy for the
	 * current map. The DynamicOccupancy is constructed
	 * when the Map is built (it has no construction-time
	 * data -- it's a per-entity registry). The Move
	 * runtime and the production removal handler read /
	 * write this instance.
	 */
	const std::shared_ptr<openage::scenario::DynamicOccupancy>
	&get_dynamic_occupancy() const {
		return this->dynamic_occupancy;
	}

private:
	/**
	 * Terrain.
	 */
	std::shared_ptr<Terrain> terrain;

	/**
	 * Pathfinder.
	 */
	std::shared_ptr<path::Pathfinder> pathfinder;

	/**
	 * Lookup table for mapping path grid objects in nyan to grid indices.
	 */
	std::unordered_map<nyan::fqon_t, path::grid_id_t> grid_lookup;

	/**
	 * P4.4C: deterministic A* wrapper. Built lazily by the
	 * mission_pack_loader after StaticOccupancyGrid is populated;
	 * the Move runtime prefers it over the legacy flow-field
	 * pathfinder. May be null in non-scenario builds or in tests
	 * that bypass the loader.
	 */
	std::shared_ptr<openage::pathfinding::NavigationService> navigation_service;

	/**
	 * P4.4F.1: shared DynamicOccupancy for the map. The
	 * Map owns the instance; move.cpp and the production
	 * removal handler read / write it.
	 */
	std::shared_ptr<openage::scenario::DynamicOccupancy> dynamic_occupancy;
};

/**
 * P4.4F Task 3: register an entity in the Map's shared
 * DynamicOccupancy at spawn time. Builds FootprintBounds
 * from the entity's footprint radius and the given center;
 * if the entity has no footprint, uses a small default
 * (radius 0.4 tiles) so units still block each other.
 * Idempotent for the same (entity, bounds) pair (see
 * DynamicOccupancy::register_entity).
 *
 * The center is in TILE units (1 tile = 65536 phys). Callers
 * holding a coord::phys3 should pass pos.ne.to_double() /
 * pos.se.to_double() directly -- FixedPoint::to_double already
 * returns the tile-scaled value (raw / 65536); do NOT divide
 * by 65536 again (that would double-scale). This matches the
 * conversion used in move.cpp's occ_bounds_at lambda.
 */
void register_entity_in_dynamic_occupancy(
	Map &map,
	const GameEntity &entity,
	double center_x,
	double center_y);

} // namespace gamestate
} // namespace openage

