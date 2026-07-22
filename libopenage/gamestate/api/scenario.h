// Copyright 2023-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <string>
#include <vector>

#include <nyan/nyan.h>

#include "util/vector.h"


namespace openage::gamestate::api {

/**
 * Helper class for API scenarios (a Scenario nyan object whose name/type is
 * \p engine.util.scenario.Scenario).
 *
 * Scenarios describe a complete playable setup: total map size, the list
 * of terrain chunks laid out across that map, and the entities that get
 * spawned at scenario load time (player units, scripted enemies, ...).
 */
class APIScenario {
public:
	/**
	 * Check if a nyan object is a scenario (type == \p engine.util.scenario.Scenario).
	 *
	 * @param obj nyan object handle.
	 *
	 * @return true if the object is a scenario, else false.
	 */
	static bool is_scenario(const nyan::Object &obj);

	/**
	 * Get the playable map size of a scenario.
	 *
	 * Reads the \p Scenario.size member (type \p engine.util.terrain.Size),
	 * which is a nested object with \p x and \p y int members giving tile
	 * counts.
	 *
	 * @param scenario nyan object (type == \p engine.util.scenario.Scenario).
	 *
	 * @return Map size in tiles (x = columns, y = rows).
	 */
	static const util::Vector2s get_size(const nyan::Object &scenario);

	/**
	 * Get the terrain chunks of a scenario.
	 *
	 * Reads the \p Scenario.terrain_chunks ordered set. Each element is
	 * a \p TerrainChunkLayout nyan object (type ==
	 * \p engine.util.terrain.TerrainChunkLayout) holding the chunk's
	 * offset, size, and tile FQONs.
	 *
	 * @param scenario nyan object (type == \p engine.util.scenario.Scenario).
	 *
	 * @return \p TerrainChunkLayout nyan objects in the order they were
	 *         declared in the scenario file.
	 */
	static const std::vector<nyan::Object> get_chunks(const nyan::Object &scenario);

	/**
	 * Get the initial entities of a scenario.
	 *
	 * Reads the \p Scenario.initial_entities ordered set. Each element
	 * is a \p PlacedEntity nyan object (type ==
	 * \p engine.util.game_entity.PlacedEntity) wrapping a game entity
	 * plus its spawn position on the map.
	 *
	 * @param scenario nyan object (type == \p engine.util.scenario.Scenario).
	 *
	 * @return \p PlacedEntity nyan objects in spawn order.
	 */
	static const std::vector<nyan::Object> get_initial_entities(const nyan::Object &scenario);

private:
	/**
	 * Read a 2D coordinate from an \p engine.util.terrain.Size nyan
	 * object by reading its \p x and \p y int members.
	 *
	 * Shared helper between get_size() and any code that needs to read
	 * a coordinate out of a Size-typed nyan field.
	 *
	 * @param size_obj nyan object of type \p engine.util.terrain.Size.
	 *
	 * @return Coordinate as a Vector2s (x, y).
	 */
	static const util::Vector2s read_size(const nyan::Object &size_obj);
};

} // namespace openage::gamestate::api