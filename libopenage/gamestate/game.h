// Copyright 2018-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <nyan/nyan.h>

namespace openage {

namespace assets {
class ModManager;
}

namespace event {
class EventLoop;
}

namespace renderer {
class RenderFactory;
}

namespace util {
class Path;
}

namespace gamestate {
class GameState;
class EntityFactory;
class TerrainFactory;
class Universe;
struct PlayerDescriptor;

/**
 * Manages a game session (settings, win conditions, etc.).
 *
 * TODO: Create sensible structure of gamestate classes. Right now it's
 *
 *       Game->Universe|->World->GameEntity
 *                     |->Terrain
 *
 * 		which can be confusing.
 */
class Game {
public:
	/**
	 * Create a new game.
	 *
	 * @param event_loop Event simulation loop for the gamestate.
	 * @param mod_manager Mod manager.
	 * @param entity_factory Factory for creating entities. Used for creating the players.
	 * @param terrain_factory Factory for creating terrain chunks.
	 * @param players Per-slot configuration; one Player is created per
	 *                entry in the order given. Empty means no players are
	 *                added (debug modes only).
	 * @param scenario_fqon Optional FQON of a nyan Scenario object. When
	 *                      non-empty, the terrain and initial entities
	 *                      are loaded from the scenario via load_scenario().
	 *                      When empty, falls back to the hardcoded
	 *                      generate_terrain() layout (debug / no-scenario
	 *                      paths).
	 */
	Game(const std::shared_ptr<openage::event::EventLoop> &event_loop,
	     const std::shared_ptr<assets::ModManager> &mod_manager,
	     const std::shared_ptr<EntityFactory> &entity_factory,
	     const std::shared_ptr<TerrainFactory> &terrain_factory,
	     const std::vector<PlayerDescriptor> &players,
	     const nyan::fqon_t &scenario_fqon = "");
	~Game() = default;

	/**
	 * Get the current game state.
	 */
	const std::shared_ptr<GameState> &get_state() const;

	/**
	 * Attach a renderer to the game which enables graphical display options for
	 * all ingame entities.
	 *
	 * @param render_factory Factory for creating connector objects for gamestate->renderer
	 *                       communication.
	 */
	void attach_renderer(const std::shared_ptr<renderer::RenderFactory> &render_factory);

	/**
	 * Load a scenario by FQON.
	 *
	 * Resolves the Scenario nyan object, builds a Terrain from its chunk
	 * layouts, creates a Map for the game state, and spawns any initial
	 * entities declared by the scenario.
	 *
	 * Used both by the Game constructor when a scenario_fqon is supplied
	 * and can be invoked separately to swap scenarios on the fly (the
	 * map is replaced wholesale; existing terrain/entities are not
	 * preserved).
	 *
	 * @param terrain_factory Factory for creating terrain chunks.
	 * @param scenario_fqon FQON of the Scenario nyan object to load.
	 */
	void load_scenario(const std::shared_ptr<TerrainFactory> &terrain_factory,
	                   const nyan::fqon_t &scenario_fqon);

private:
	/**
	 * Load game data from the filesystem.
	 *
	 * @param mod_manager Mod manager.
	 */
	void load_data(const std::shared_ptr<assets::ModManager> &mod_manager);

	/**
	 * Load game data from the filesystem recursively.
	 *
	 * TODO: Move this into nyan.
	 *
	 * @param base_dir Base directory where mods are stored.
	 * @param mod_dir Name of the mod directory.
	 * @param search Search path relative to the mod directory.
	 * @param recursive if true, recursively search subfolders if the the search path is a directory.
	 */
	void load_path(const util::Path &base_dir,
	               const std::string &mod_dir,
	               const std::string &search,
	               bool recursive = false);

	/**
	 * Generate the terrain for the current game.
	 *
	 * TODO: Use a real map generator.
	 *
	 * @param terrain_factory Factory for creating terrain objects.
	 */
	void generate_terrain(const std::shared_ptr<TerrainFactory> &terrain_factory);

	/**
	 * Event loop passed into Game. Held to give scenario-spawned entities
	 * the same loop as entities created via the EntityFactory.
	 */
	std::shared_ptr<openage::event::EventLoop> event_loop;

	/**
	 * Factory used to spawn initial entities when loading a scenario.
	 */
	std::shared_ptr<EntityFactory> entity_factory;

	/**
	 * Nyan game data database.
	 */
	std::shared_ptr<nyan::Database> db;

	/**
	 * State of the current game.
	 */
	std::shared_ptr<GameState> state;

	/**
	 * Object that controls entities in the game world.
	 */
	std::shared_ptr<Universe> universe;
};

} // namespace gamestate
} // namespace openage
