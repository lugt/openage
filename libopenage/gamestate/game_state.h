// Copyright 2023-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <memory>
#include <unordered_map>

#include "event/state.h"
#include "gamestate/types.h"


namespace nyan {
class Database;
class View;
} // namespace nyan

namespace openage {

namespace assets {
class ModManager;
}

namespace event {
class EventLoop;
}

namespace gamestate {
class GameEntity;
class Map;
class Player;

/**
 * State of the game.
 *
 * Contains index structures for looking up game entities and other
 * information for the current game.
 */
class GameState : public openage::event::State {
public:
	/**
	 * Create a new game state.
	 *
	 * @param db Nyan game data database.
	 * @param event_loop Event loop for the game state.
	 */
	explicit GameState(const std::shared_ptr<nyan::Database> &db,
	                   const std::shared_ptr<openage::event::EventLoop> &event_loop);

	/**
	 * Get the nyan database view for the whole game.
	 *
	 * Players have individual views for their own data.
	 *
	 * @return nyan database view.
	 */
	const std::shared_ptr<nyan::View> &get_db_view();

	/**
	 * Add a new game entity to the index.
	 *
	 * @param entity New game entity.
	 */
	void add_game_entity(const std::shared_ptr<GameEntity> &entity);

	/**
	 * Add a new player to the index.
	 *
	 * @param player New player.
	 */
	void add_player(const std::shared_ptr<Player> &player);

	/**
	 * Get the player index (read-only).
	 *
	 * Exposed so other subsystems (e.g. the input controller's
	 * controlled-factions set in Presenter) can iterate over existing
	 * players without joining the friend chain through Game.
	 *
	 * @return Map of player ID -> Player shared pointer.
	 */
	const std::unordered_map<player_id_t, std::shared_ptr<Player>> &get_players() const;

	/**
	 * Set the map of the current game.
	 *
	 * @param terrain Map object.
	 */
	void set_map(const std::shared_ptr<Map> &map);

	/**
	 * Get a game entity by its ID.
	 *
	 * @param id ID of the game entity.
	 *
	 * @return Game entity with the given ID.
	 */
	const std::shared_ptr<GameEntity> &get_game_entity(entity_id_t id) const;

	/**
	 * Check whether a game entity with the given ID is still in the index.
	 *
	 * Combat systems need a non-throwing lookup: once an entity is killed
	 * (HP <= 0) it is removed from the index by \p remove_game_entity, and
	 * any follow-up attack event scheduled against it must detect the gap
	 * and stop the attacker's loop rather than throwing on \p get_game_entity.
	 *
	 * @param id ID of the game entity.
	 *
	 * @return true if the entity is present in the index.
	 */
	bool has_game_entity(entity_id_t id) const;

	/**
	 * Remove a game entity from the index.
	 *
	 * Called when an entity dies (Health drops to <= 0 in the attack hit
	 * path). The entity's components may still be alive through outstanding
	 * shared_ptr references (e.g. an in-flight AttackHandler holding the
	 * target), but it is no longer discoverable via \p get_game_entity or
	 * \p get_game_entities, so subsequent attack re-evaluations against it
	 * stop cleanly. No-op (with a warning) if the ID is not present.
	 *
	 * @param id ID of the game entity to remove.
	 */
	void remove_game_entity(entity_id_t id);

	/**
	 * Get all game entities in the current game.
	 *
	 * @return Map of all game entities in the current game by their ID.
	 */
	const std::unordered_map<entity_id_t, std::shared_ptr<GameEntity>> &get_game_entities() const;

	/**
	 * Get a player by its ID.
	 *
	 * @param id ID of the player.
	 *
	 * @return Player with the given ID.
	 */
	const std::shared_ptr<Player> &get_player(player_id_t id) const;

	/**
	 * Get the map of the current game.
	 *
	 * @return Map object.
	 */
	const std::shared_ptr<Map> &get_map() const;

	/**
	 * TODO: Only for testing.
	 */
	const std::shared_ptr<assets::ModManager> &get_mod_manager() const;
	void set_mod_manager(const std::shared_ptr<assets::ModManager> &mod_manager);

private:
	/**
	 * View for the nyan game data database.
	 */
	std::shared_ptr<nyan::View> db_view;

	/**
	 * Map of all game entities in the current game by their ID.
	 */
	std::unordered_map<entity_id_t, std::shared_ptr<GameEntity>> game_entities;

	/**
	 * Map of all players in the current game by their ID.
	 */
	std::unordered_map<player_id_t, std::shared_ptr<Player>> players;

	/**
	 * Map of the current game.
	 */
	std::shared_ptr<Map> map;

	/**
	 * TODO: Only for testing
	 */
	std::shared_ptr<assets::ModManager> mod_manager;
};
} // namespace gamestate
} // namespace openage
