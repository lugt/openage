// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#pragma once

#include "gamestate/component/api_component.h"
#include "gamestate/component/types.h"
#include "gamestate/types.h"
#include "time/time.h"


namespace openage::gamestate::component {

/**
 * Holds the Attack (ApplyDiscreteEffect) ability data plus the per-entity
 * runtime attack state for the event-driven AttackSystem (Phase 2, Option 1).
 *
 * Ability data (immutable, read from nyan): cooldown (reload_time), range.
 * Runtime state (mutable, set by AttackHandler/AttackSystem): target_id,
 * next_attack_time, is_approaching, active.
 *
 * Why a single component holds both: the user spec allows "boundary-clear
 * state/component" with exactly these fields, and explicitly forbids a full
 * Combat ECS. The ability data and the runtime state are both small and only
 * ever touched by the AttackSystem, so collocating them keeps the surface
 * area minimal.
 *
 * Migration note (Activity Task Node): when Attack moves out of the stopgap
 * AttackSystem into an activity task node, the ability-data getters stay; the
 * runtime-state fields move into the task node's blackboard. The AttackCommand
 * input and the ApplyDiscreteEffect output are unchanged, so the migration
 * needs no data-format change -- only where the state machine lives.
 */
class Attack final : public APIComponent {
public:
	using APIComponent::APIComponent;

	component_t get_type() const override;

	// --- Ability data (read from the nyan ApplyDiscreteEffect ability) ---

	/**
	 * Cooldown (seconds) between hits.
	 *
	 * Reads ApplyDiscreteEffect.reload_time.
	 */
	double get_cooldown() const;

	/**
	 * Attack range in phys/tiles.
	 *
	 * RangedDiscreteEffect (a subclass of ApplyDiscreteEffect) reads
	 * max_range. The base ApplyDiscreteEffect (melee, e.g. the villager) has
	 * no range field in nyan, so a default engagement range is used so that
	 * adjacent units can hit each other.
	 */
	double get_attack_range() const;

	/**
	 * Damage per hit.
	 *
	 * Returns 0 in increment 2; the flat damage amount is read from the
	 * ability's effect batch in increment 4 (cooldown + ApplyDiscreteEffect),
	 * where the damage-application path is wired. Armor-aware selection is
	 * out of scope for Phase 2.
	 */
	int64_t get_damage() const;

	// --- Runtime attack state (set by the AttackSystem) ---

	bool is_active() const;
	entity_id_t get_target_id() const;
	time::time_t get_next_attack_time() const;
	bool is_approaching() const;

	/**
	 * Begin attacking \p target at \p now. Sets active, target, and the
	 * first next_attack_time to now (the AttackSystem decides whether the
	 * first hit lands immediately or after one cooldown).
	 */
	void start_attack(entity_id_t target, const time::time_t &now);

	/** End the attack (target dead/gone, command replaced, attacker dead). */
	void stop_attack();

	void set_next_attack_time(const time::time_t &t);
	void set_approaching(bool approaching);

private:
	/**
	 * Default melee engagement range (phys/tiles) for base
	 * ApplyDiscreteEffect abilities that have no range field.
	 */
	static constexpr double DEFAULT_MELEE_RANGE = 1.0;

	/** Whether this entity is currently in an attack state. */
	bool active{false};

	/** Entity being attacked. */
	entity_id_t target_id{0};

	/** Time at which the next hit may land. */
	time::time_t next_attack_time{};

	/** Whether the entity is currently moving to close with the target. */
	bool approaching{false};
};

} // namespace openage::gamestate::component
