// Copyright 2022-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "error/error.h"
#include "gamestate/component/base_component.h"
#include "gamestate/component/types.h"
#include "gamestate/component/api/footprint.h"
#include "gamestate/types.h"
#include "gamestate/action_option.h"
#include "log/message.h"
#include "time/time.h"
#include "util/compiler.h"

#include <cassert>


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
	void remove_from_renderer();

	void set_death_animation(std::string path, time::time_t duration);
	void set_corpse_animation(std::string path, time::time_t duration);
	void render_death(const time::time_t &time);
	struct ResourceSpot {
		nyan::fqon_t definition;
		nyan::fqon_t resource;
		int64_t remaining;
		bool requires_hunting;
	};
	std::optional<ResourceSpot> resource_spot;
	std::vector<nyan::fqon_t> drop_containers;
	bool is_harvestable_carcass() const {
		return this->is_dead() and resource_spot and resource_spot->requires_hunting and resource_spot->remaining > 0;
	}
	int64_t genie_id = -1;
	double line_of_sight = 0;
	int64_t population_used = 0;
	int64_t population_provided = 0;
	nyan::fqon_t definition;
	std::vector<ActionOption> action_options;
	std::optional<nyan::Object> construct_ability;
	uint64_t construction_order = 0;

	void add_damage_animation(double max_health_percent, std::string path);
	void render_damage(const time::time_t &time, int64_t previous_hp, int64_t current_hp, int64_t max_hp);
	bool play_animation(const nyan::Object &ability, const time::time_t &time);

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
	 * Throws if the entity does not have that component. Use this for
	 * components the entity cannot work without.
	 *
	 * @param type Component type.
	 */
	const std::shared_ptr<component::Component> &get_component(component::component_t type);

	/**
	 * Get a component of this entity, or \p nullptr if it has none.
	 *
	 * Use this for components that are legitimately optional: a
	 * TownCenter has no Attack component (it has no attack ability,
	 * so the entity factory never mounts one), and the presenter view
	 * models must be able to ask "does this entity attack?" without
	 * crashing on the answer "no".
	 *
	 * @param type Component type.
	 * @return The component, or nullptr if the entity has no such component.
	 */
	std::shared_ptr<component::Component> try_get_component(component::component_t type);

	/**
	 * Get a component as a concrete type, or \p nullptr if absent.
	 *
	 * This is the accessor the presenter layer should use. It closes a
	 * whole class of latent crashes that the raw pair
	 * try_get_component() + dynamic_pointer_cast() left open:
	 *
	 *   auto attack = entity->try_get_component(component_t::ATTACK);
	 *   if (attack) {                              // only checks the BASE ptr
	 *       auto typed = std::dynamic_pointer_cast<Attack>(attack);
	 *       typed->get_damage();                   // <-- typed may be nullptr
	 *   }
	 *
	 * The `if` above tests the base-class pointer, which is non-null as
	 * soon as *some* component is mounted, so a failed cast reaches the
	 * dereference and segfaults. This accessor checks the cast result and
	 * the component's own get_type() instead.
	 *
	 * The two failure modes are deliberately treated differently:
	 *
	 *  - The component is simply not mounted (e.g. a TownCenter has no
	 *    Attack component). That is a legitimate state, so nullptr is
	 *    returned and the caller is expected to handle it.
	 *
	 *  - The component IS mounted but is not of type \p T. That is always
	 *    a programming error -- the enum and the concrete class disagree,
	 *    which cannot happen through correct construction. An
	 *    openage::error::Error is thrown in both debug and release builds,
	 *    allowing callers and regression tests to diagnose the mismatch.
	 *
	 * @param type Component type, must match T.
	 * @return The typed component, or nullptr if this entity has no such
	 *         component.
	 * @throws openage::error::Error if the component is mounted but is
	 *         not of type \p T.
	 */
	template <typename T>
	std::shared_ptr<T> try_get_component_as(component::component_t type) {
		auto base = this->try_get_component(type);
		if (base == nullptr) {
			// Legitimately absent -- not an error.
			return nullptr;
		}

		auto typed = std::dynamic_pointer_cast<T>(base);

		if (typed == nullptr) {
			// Mounted under this enum, but it is not a T. The mapping
			// between component_t and the concrete class is supposed to
			// be 1:1; a mismatch means the two were changed out of sync.
			//
			// Both halves of the message are needed: to_string(type) names
			// the enum that was asked for, typestring<T>() names the C++
			// class that was requested. Reporting only the enum produces
			// "has a Attack component where a Attack component was
			// requested", which reads as nonsense -- the enum alone cannot
			// tell the caller which cast to fix.
			throw openage::error::Error{
				MSG(err) << "entity " << this->id << " has a "
				         << component::to_string(base->get_type())
				         << " component, which is not a "
				         << util::typestring<T>()
				         << " (requested as component_t::"
				         << component::to_string(type) << ")"};
		}

		// get_type() is the enum the factory actually mounted. It must be
		// the one that was asked for; assert cheaply catches an off-by-one
		// in the enum ordering that a cast alone would not.
		assert(base->get_type() == type && "component enum mismatch");
		return typed;
	}

	/**
	 * try_get_component_as(), plus a hard assert on a type mismatch.
	 *
	 * Use this at a call site where a mismatch means a genuine invariant
	 * violation you would rather trap during development than handle at
	 * runtime. Debug builds abort with SIGABRT; release builds fall
	 * through to the same thrown error as try_get_component_as().
	 *
	 * Prefer plain try_get_component_as() in engine code: it survives
	 * into release builds and can be exercised by a test.
	 *
	 * @param type Component type, must match T.
	 * @return The typed component, or nullptr if this entity has no such
	 *         component.
	 */
	template <typename T>
	std::shared_ptr<T> try_get_component_as_strict(component::component_t type) {
		auto base = this->try_get_component(type);
		if (base != nullptr) {
			assert(base->get_type() == type
			       && "component type / concrete class mismatch");
		}
		return this->try_get_component_as<T>(type);
	}

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
	bool has_component(component::component_t type) const;

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
	std::optional<time::time_t> animation_removal_time;
	std::map<double, std::string> damage_animations;
	std::string death_animation;
	std::string corpse_animation;
	time::time_t death_duration = 0;
	time::time_t corpse_duration = 0;

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
