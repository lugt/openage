// Copyright 2022-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "gamestate/component/types.h"
#include "gamestate/component/api/footprint.h"
#include "gamestate/types.h"
#include "time/time.h"


namespace openage {

namespace renderer::world {
class RenderEntity;
}

namespace gamestate {
class GameEntityManager;

namespace component {
class Component;
class Footprint;
}

/**
 * Entity for a "physical" thing (unit, building, etc.) in the game world.
 */
class GameEntity {
public:
	/**
	 * Create a new game entity.
	 *
	 * @param id Unique identifier.
	 */
	GameEntity(entity_id_t id);

	~GameEntity() = default;

	GameEntity(GameEntity &&) = default;
	GameEntity &operator=(GameEntity &&) = default;

	/**
	 * Copy this game entity.
	 *
	 * @param id Unique identifier.
	 *
	 * @return Copy of this game entity.
	 */
	std::shared_ptr<GameEntity> copy(entity_id_t id);

	/**
	 * Get the unique identifier of this entity.
	 *
	 * @return Unique identifier.
	 */
	entity_id_t get_id() const;

	/**
	 * Set the current render entity.
	 *
	 * @param entity New render entity.
	 */
	void set_render_entity(const std::shared_ptr<renderer::world::RenderEntity> &entity);

	/**
	 * Set the event manager of this entity.
	 *
	 * @param manager Event manager.
	 */
	void set_manager(const std::shared_ptr<GameEntityManager> &manager);

	/**
	 * Get the event manager of this entity.
	 *
	 * @return Event manager.
	 */
	const std::shared_ptr<GameEntityManager> &get_manager() const;

	/**
	 * Get a component of this entity.
	 *
	 * @param type Component type.
	 */
	const std::shared_ptr<component::Component> &get_component(component::component_t type);

	/**
	 * Add a component to this entity.
	 *
	 * @param component Component to add.
	 */
	void add_component(const std::shared_ptr<component::Component> &component);

	/**
	 * Check if this entity has a component of the given type.
	 *
	 * @param type Component type.
	 */
	bool has_component(component::component_t type);

	// --- P4.4B: gameplay footprint ---
	// Footprint is plain data (NOT an openage Component), stored
	// as a field on the entity. See component/api/footprint.h.
	// The default-constructed (empty) footprint reports
	// radius=0 and the entity is a point with no collision.
	void set_footprint(const component::Footprint &fp) {
		this->footprint_ = fp;
		this->has_footprint_ = true;
	}
	bool has_footprint() const { return this->has_footprint_; }
	const component::Footprint &footprint() const { return this->footprint_; }

	/**
	 * Death-state flag.
	 *
	 * Set by the DeathEvent handler (P3.3) once a generic threshold
	 * listener has derived an HP<=0 transition. Idempotent: subsequent
	 * calls are no-ops. Used by AttackHandler (and any future
	 * damage-source) to detect "target already dead" without
	 * depending on GameState membership alone -- a target that has
	 * not yet been removed from the GameState container (P3.4
	 * defers removal) but is logically dead still rejects new
	 * damage and the attacker stops cleanly.
	 *
	 * P3.4 replaces the logical-dead check with a stricter "removed
	 * from GameState" check once the deferred RemovalEvent lands.
	 */
	bool is_dead() const { return this->dead; }

	/**
	 * Mark the entity dead. Idempotent -- if already dead, this is a
	 * no-op (returns false); on the first transition returns true.
	 * Callers can use the return value to log "death fired" lines
	 * exactly once per entity.
	 */
	bool mark_dead() {
		if (this->dead) {
			return false;
		}
		this->dead = true;
		return true;
	}

	/**
	 * Update the render entity.
	 *
	 * @param time Simulation time of the update.
	 * @param animation_path Path to the animation definition used at \p time.
	 */
	void render_update(const time::time_t &time,
	                   const std::string &animation_path);

protected:
	/**
	 * A game entity cannot be default copied because of their unique ID.
	 *
	 * \p copy() must be used instead.
	 */
	GameEntity(const GameEntity &) = default;
	GameEntity &operator=(const GameEntity &) = default;

private:
	/**
	 * Set the unique identifier of this game entity.
	 *
	 * Only called by \p copy().
	 *
	 * @param id New ID.
	 */
	void set_id(entity_id_t id);

	/**
	 * Unique identifier.
	 */
	entity_id_t id;

	/**
	 * Data components.
	 *
	 * TODO: Multiple components of the same type.
	 */
	std::unordered_map<component::component_t, std::shared_ptr<component::Component>> components;

	// P4.4B: gameplay footprint (plain data, not a Component).
	component::Footprint footprint_{};
	bool has_footprint_{false};

	/**
	 * Render entity for pushing updates to the renderer. Can be \p nullptr.
	 */
	std::shared_ptr<renderer::world::RenderEntity> render_entity;

	/**
	 * Event manager.
	 */
	std::shared_ptr<GameEntityManager> manager;

	/**
	 * Whether this entity has been declared dead. See is_dead() /
	 * mark_dead() for the lifecycle.
	 */
	bool dead{false};
};

} // namespace gamestate
} // namespace openage
