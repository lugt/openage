// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4A: read-only ViewModel for the currently selected entity.
//
// Pulls every field from the real simulation components (Live/Health
// Curve, component::Attack, Ownership, Position). The ViewModel never
// mutates the simulation; the QML/IngameHud binds to it as a property
// source. This is the contract the GUI depends on: if a field here
// is hard-coded or "demo" data, the HUD would display values that do
// not match the gameplay state -- which the spec explicitly forbids.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

#include "coord/phys.h"
#include "gamestate/types.h"
#include "time/time.h"


namespace openage {
namespace gamestate {
class GameEntity;
class GameState;
}
}


namespace openage::presenter::viewmodel {


/**
 * Snapshot of one selected entity. All fields are pulled from
 * authoritative simulation components at construction time; refresh
 * the snapshot (rebuild) to pick up changes.
 */
class SelectedEntityView {
public:
	/**
	 * Build a view for the given entity. If the entity is unknown or
	 * has no Live component, the view's fields are nullopt / 0.
	 */
	SelectedEntityView(
		const std::shared_ptr<openage::gamestate::GameState> &state,
		openage::gamestate::entity_id_t entity_id);

	openage::gamestate::entity_id_t entity_id() const { return this->entity_id_; }

	/**
	 * Short ScenarioObjectId (e.g. "PlayerVillager"), resolved from
	 * GameState's named-entity map. nullopt if the entity is not a
	 * placed scenario object.
	 */
	std::optional<std::string> scenario_object_id() const { return this->scenario_object_id_; }

	/**
	 * Entity definition FQON (e.g. ...Villager).
	 */
	std::optional<std::string> entity_fqon() const { return this->entity_fqon_; }

	/**
	 * Owner player id (0/1/...), from the Ownership component. -1 if
	 * the entity has no owner.
	 */
	int64_t owner_player_id() const { return this->owner_player_id_; }

	/**
	 * Current and maximum HP. Both are read from the Live component's
	 * Health attribute curve. The maximum is the curve's initial
	 * value (queried at t=0); the current is the value at "now".
	 */
	std::optional<int64_t> hp_current() const { return this->hp_current_; }
	std::optional<int64_t> hp_max() const { return this->hp_max_; }

	/**
	 * True iff hp_current() > 0. Convenience for the HUD.
	 */
	bool is_alive() const {
		return this->hp_current().has_value() && *this->hp_current() > 0;
	}

	/**
	 * Attack data, read from component::Attack. Entities without an
	 * Attack component (e.g. a non-combat building) return nullopt /
	 * 0.
	 */
	std::optional<int64_t> attack_damage() const { return this->attack_damage_; }
	std::optional<double> attack_range() const { return this->attack_range_; }
	std::optional<double> attack_cooldown() const { return this->attack_cooldown_; }

	/**
	 * True if the AttackSystem has this entity in the active attack
	 * state. The current target and "is_approaching" flags drive the
	 * Move/Attack indicator in the HUD.
	 */
	bool attack_is_active() const { return this->attack_is_active_; }
	std::optional<openage::gamestate::entity_id_t> attack_target_id() const {
		return this->attack_target_id_;
	}
	bool attack_is_approaching() const { return this->attack_is_approaching_; }

	/**
	 * Current world position (phys3). Pulled from the Position
	 * component's Curve at the construction time. nullopt if the
	 * entity has no Position component.
	 */
	std::optional<openage::coord::phys3> position() const { return this->position_; }

private:
	openage::gamestate::entity_id_t entity_id_{0};
	std::optional<std::string> scenario_object_id_;
	std::optional<std::string> entity_fqon_;
	int64_t owner_player_id_{-1};
	std::optional<int64_t> hp_current_;
	std::optional<int64_t> hp_max_;
	std::optional<int64_t> attack_damage_;
	std::optional<double> attack_range_;
	std::optional<double> attack_cooldown_;
	bool attack_is_active_{false};
	std::optional<openage::gamestate::entity_id_t> attack_target_id_;
	bool attack_is_approaching_{false};
	std::optional<openage::coord::phys3> position_;
};


} // namespace openage::presenter::viewmodel