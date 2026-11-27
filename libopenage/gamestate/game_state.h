// Copyright 2023-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <limits>
#include <memory>
#include <map>
#include <shared_mutex>
#include <string>
#include <optional>
#include <unordered_map>

#include "event/state.h"
#include "gamestate/types.h"
#include "gamestate/visibility.h"
#include "time/time.h"


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
enum class DiplomaticStance { Self, Ally, Neutral, Enemy };

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
	/** Original build-page categories keyed by the producible building definition. */
	std::map<std::string, std::string> command_categories;
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
	DiplomaticStance get_stance(player_id_t source, player_id_t target) const;
	bool set_stance(player_id_t source, player_id_t target, DiplomaticStance stance, bool reciprocal = false);
	void set_player_name(player_id_t player, std::string name);
	std::string get_player_name(player_id_t player) const;

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
	 * P4.4C: expose the EventLoop so subsystems that hold only a
	 * GameState (e.g. system::Move) can dispatch observation
	 * events such as WaypointReachedEvent / MovementArrivedEvent.
	 * Stored as a weak_ptr because GameState is conceptually a
	 * child of the simulation that owns the loop; the loop
	 * outlives any single GameState, but a stale pointer would
	 * still be wrong if the loop is rebuilt. Returning the
	 * shared_ptr of the live loop is safe in the current
	 * ownership model (the loop is constructed in
	 * GameSimulation::GameSimulation and never destroyed until
	 * after the GameState).
	 */
	const std::shared_ptr<openage::event::EventLoop> &get_event_loop() const {
		return this->event_loop;
	}

	/**
	 * Set the map of the current game.
	 *
	 * @param terrain Map object.
	 */
	void set_map(const std::shared_ptr<Map> &map);
	void enable_fog(bool share_allied_vision = false) {
		this->fog_of_war = true;
		this->share_allied_vision = share_allied_vision;
	}
	void set_fog_enabled(bool enabled) { this->fog_of_war = enabled; }
	bool is_fog_enabled() const { return this->fog_of_war; }
	void set_reveal_map_cheat(bool enabled) { this->reveal_map_cheat = enabled; }
	bool is_reveal_map_cheat() const { return this->reveal_map_cheat; }
	void set_no_fog_cheat(bool enabled) { this->no_fog_cheat = enabled; }
	bool is_no_fog_cheat() const { return this->no_fog_cheat; }
	void set_fast_actions_cheat(bool enabled) { this->fast_actions_cheat = enabled; }
	bool fast_actions() const { return this->fast_actions_cheat; }
	/** Replace explored history on load; current visibility is recomputed afterwards. */
	void restore_exploration(std::map<player_id_t, VisibilitySnapshot> snapshots);
	void update_visibility(time::time_t at);
	std::shared_ptr<const VisibilitySnapshot> visibility(player_id_t player) const;
	bool can_see(player_id_t player, entity_id_t entity) const;
	/** Match-wide limit is applied to every producer, including human players. */
	void set_population_limit(int64_t limit);
	int64_t get_population_limit() const { return this->population_limit; }
	void update_player_economies(time::time_t at);
	PlayerEconomy player_economy(player_id_t player) const;
	/** Sorted, thread-safe leaderboard snapshot, highest score first. */
	std::vector<PlayerScore> player_scores() const;
	/** Resource value of an entity in its owner's current technology view. */
	int64_t entity_score_value(const GameEntity &entity, player_id_t owner);
	/** Credit a destroyed or converted enemy; the caller gates duplicate events. */
	void record_enemy_loss(entity_id_t target, player_id_t killer, time::time_t at);
	struct ActionFeedback {
		std::string message;
		uint64_t seq = 0;
		bool is_error = false;
	};
	std::string action_feedback(player_id_t player) const;
	ActionFeedback action_feedback_event(player_id_t player) const;
	void set_action_feedback(player_id_t player, std::string message, bool is_error = false);

	/**
	 * P4.4C: set the EventLoop handle. The constructor sets it
	 * from the EventLoop passed in by GameSimulation, but tests
	 * that build a GameState directly may want to inject a
	 * different one (or none). Idempotent: only the first set
	 * wins (a re-set logs a warning and keeps the original).
	 */
	void set_event_loop(const std::shared_ptr<openage::event::EventLoop> &loop);

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
	 * Register a named scenario instance -> spawned entity ID mapping.
	 *
	 * When the scenario loader spawns the initial entities from a Scenario's
	 * initial_entities set, each PlacedEntity nyan object has its own FQON
	 * (e.g. "ww_mvp.campaign.william_wallace.mission_01.EnemyVillager"). The loader
	 * registers that instance FQON against the runtime entity ID it just
	 * created, so objective/trigger systems can resolve "the EnemyVillager
	 * instance" to a concrete entity without relying on spawn order, max
	 * ID, or "all movables" heuristics. This is the minimal, generic
	 * instance->ID surface; a full owner/faction system is deliberately
	 * out of scope.
	 *
	 * @param instance_fqon FQON of the PlacedEntity nyan instance.
	 * @param id Runtime entity ID assigned to the spawned entity.
	 */
	void register_named_entity(const nyan::fqon_t &instance_fqon,
	                           entity_id_t id);

	/**
	 * Look up the runtime entity ID spawned for a named scenario instance.
	 *
	 * @param instance_fqon FQON of the PlacedEntity nyan instance.
	 *
	 * @return The entity ID, or std::nullopt if no instance with that FQON
	 *         was registered (e.g. the scenario never spawned one).
	 */
	std::optional<entity_id_t> get_entity_id_by_instance(
		const nyan::fqon_t &instance_fqon) const;

	/**
	 * Read-only access to the full instance->ID map. Used by trigger
	 * factories to snapshot the objective targets at creation time.
	 *
	 * @return Map of PlacedEntity instance FQON -> entity ID.
	 */
	const std::unordered_map<nyan::fqon_t, entity_id_t> &get_named_entities() const;

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
	 * Map of named scenario instances (PlacedEntity FQON) -> entity ID,
	 * populated by the scenario loader at spawn time. Lets objective/
	 * trigger systems resolve a specific placed instance to its runtime
	 * entity without spawn-order assumptions.
	 */
	std::unordered_map<nyan::fqon_t, entity_id_t> named_entities;

	/**
	 * Map of all players in the current game by their ID.
	 */
	std::unordered_map<player_id_t, std::shared_ptr<Player>> players;
	std::unordered_map<player_id_t, std::string> player_names;
	std::map<std::pair<player_id_t, player_id_t>, DiplomaticStance> diplomacy;
	mutable std::shared_mutex diplomacy_mutex;

	/**
	 * Map of the current game.
	 */
	std::shared_ptr<Map> map;
	int64_t population_limit = std::numeric_limits<int64_t>::max();
	bool fog_of_war = false;
	bool share_allied_vision = false;
	bool reveal_map_cheat = false;
	bool no_fog_cheat = false;
	bool fast_actions_cheat = false;
	std::unordered_map<player_id_t, std::shared_ptr<const VisibilitySnapshot>> visibility_by_player;
	mutable std::shared_mutex visibility_mutex;
	std::unordered_map<player_id_t, PlayerEconomy> player_economies;
	std::vector<PlayerScore> scores;
	std::unordered_map<nyan::fqon_t, nyan::fqon_t> score_costs;
	bool score_costs_initialized = false;
	std::unordered_map<player_id_t, ActionFeedback> economy_feedback;
	uint64_t next_feedback_seq = 1;
	mutable std::shared_mutex economy_mutex;

	/**
	 * TODO: Only for testing
	 */
	std::shared_ptr<assets::ModManager> mod_manager;

	/**
	 * P4.4C: handle to the EventLoop the GameState lives on.
	 * Stored so Move runtime (which only receives the GameState)
	 * can dispatch WaypointReachedEvent / MovementArrivedEvent
	 * without joining the friend chain through GameSimulation.
	 */
	std::shared_ptr<openage::event::EventLoop> event_loop;
};
} // namespace gamestate
} // namespace openage
