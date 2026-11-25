// Copyright 2018-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "gamestate/types.h"
#include "nyan/nyan.h"


namespace nyan {
class View;
} // namespace nyan

namespace openage::gamestate {

/**
 * Entity for managing a player inside the game world.
 */
class Player {
public:
	/**
	 * Create a new player with the given id.
	 *
	 * @param id Unique player ID.
	 * @param db_view View of the nyan database used for the player.
	 */
	Player(player_id_t id,
	       const std::shared_ptr<nyan::View> &db_view);

	Player(Player &&) = default;
	Player &operator=(Player &&) = default;

	~Player() = default;

	/**
	 * Copy this player.
	 *
	 * @param id Unique identifier.
	 *
	 * @return Copy of this player.
	 */
	std::shared_ptr<Player> copy(entity_id_t id);

	/**
	 * Get the unique ID of the player.
	 *
	 * @return Unique player ID.
	 */
	player_id_t get_id() const;

	/**
	 * Get the nyan game database view for the player.
	 *
	 * @return nyan database view.
	 */
	const std::shared_ptr<nyan::View> &get_db_view() const;

	/**
	 * Add a positive amount of a resource to the player's stockpile.
	 *
	 * Increments the existing counter or inserts a new entry. Negative
	 * amounts are not allowed here; use \p spend() to deduct. Calling this
	 * with amount <= 0 is a no-op so that villager "harvest finished but
	 * nothing in hand" or empty-resource-spot returns do not poison the
	 * counter.
	 *
	 * @param resource_fqon Fully-qualified nyan object name of the resource
	 *                      type (e.g. "util.resource.types.Wood").
	 * @param amount Units to add (must be > 0).
	 */
	void add_resource(const nyan::fqon_t &resource_fqon, int64_t amount);

	/**
	 * Get the current amount of a resource the player owns.
	 *
	 * Missing entries read as 0 so callers do not have to populate the map
	 * defensively. The reference is invalidated by any subsequent
	 * add_resource / spend call; copy if you need to keep the value.
	 *
	 * @param resource_fqon Fully-qualified nyan object name of the resource
	 *                      type.
	 * @return Units in the stockpile (0 if absent).
	 */
	int64_t get_resource(const nyan::fqon_t &resource_fqon) const;

	/**
	 * Check whether the player can afford to spend \p amount of a resource.
	 *
	 * @param resource_fqon Fully-qualified nyan object name of the resource
	 *                      type.
	 * @param amount Units required (must be >= 0).
	 * @return True if current stockpile >= amount.
	 */
	bool has_enough(const nyan::fqon_t &resource_fqon, int64_t amount) const;

	/**
	 * Try to deduct \p amount of a resource from the player's stockpile.
	 *
	 * Returns false without mutating state when the player cannot afford
	 * the spend, so callers (production queues, construction sites, etc.)
	 * must not assume success and should queue or cancel the action
	 * accordingly. amount=0 always succeeds (idempotent reservation
	 * release, etc.).
	 *
	 * @param resource_fqon Fully-qualified nyan object name of the resource
	 *                      type.
	 * @param amount Units to deduct (must be >= 0).
	 * @return True if the deduction succeeded; false if insufficient funds.
	 */
	bool spend(const nyan::fqon_t &resource_fqon, int64_t amount);

	/**
	 * Read-only snapshot of the player's resource stockpile.
	 *
	 * Used by the tutorial trigger system and the HUD resource bar to
	 * iterate every non-zero counter without poking at each known FQON.
	 *
	 * @return Reference to the internal resource map.
	 */
	const std::unordered_map<nyan::fqon_t, int64_t> &get_resources() const;

    void disable_action(const nyan::fqon_t &subject) { this->disabled_actions.insert(subject); }
    bool allows_action(const nyan::fqon_t &subject) const {
		return not this->disabled_actions.contains(subject)
		    and (not this->allowed_actions or this->allowed_actions->contains(subject));
	}
	/** Civilization availability is shared by AI, UI and production validation. */
	void set_allowed_actions(std::unordered_set<nyan::fqon_t> subjects) { this->allowed_actions = std::move(subjects); }
	bool has_researched(const nyan::fqon_t &tech) const { return this->researched.contains(tech); }
	void record_research(const nyan::fqon_t &tech) { this->researched.insert(tech); }
	/** Event handlers update these counters only after successful completion. */
	ScoreLedger score_ledger;
	bool is_gaia = false;
	const std::unordered_set<nyan::fqon_t> &get_researched() const { return this->researched; }
	void set_resource(const nyan::fqon_t &resource_fqon, int64_t amount) {
		this->resources[resource_fqon] = amount;
	}

protected:
	/**
	 * A player cannot be default copied because of their unique ID.
	 *
	 * \p copy() must be used instead.
	 */
	Player(const Player &) = default;
	Player &operator=(const Player &) = default;

private:
	/**
	 * Set the unique identifier of this player.
	 *
	 * Only called by \p copy().
	 *
	 * @param id New ID.
	 */
	void set_id(entity_id_t id);

	/**
	 * Player ID. Must be unique.
	 */
	player_id_t id;

	/**
	 * Player view of the nyan game data database.
	 */
	std::shared_ptr<nyan::View> db_view;

	/**
	 * Per-resource stockpile keyed by nyan FQON (e.g.
	 * "util.resource.types.Wood").
	 *
	 * Owned by the Player because the natural consumer / producer of
	 * resources is the player-level system (DropResources /
	 * ResourceStorage / market / tribute). It would also fit on a
	 * per-entity ResourceStorage component, but the tutorial keeps the
	 * model player-flavoured to start: villagers carry as travelling
	 * cargo (not yet modelled) and unload straight into player.resources.
	 */
	std::unordered_map<nyan::fqon_t, int64_t> resources;
	std::unordered_set<nyan::fqon_t> researched;
    std::unordered_set<nyan::fqon_t> disabled_actions;
	std::optional<std::unordered_set<nyan::fqon_t>> allowed_actions;
};

} // namespace openage::gamestate
