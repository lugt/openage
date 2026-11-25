// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <nyan/nyan.h>

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
	 * Reads the first FlatAttributeChangeDecrease effect from the
	 * ApplyDiscreteEffect batches' EffectBatch.effects lists and returns
	 * its change_value.amount. The villager's Melee effect carries
	 * amount=3 for Health, so villager-vs-villager hits deal 3 damage.
	 *
	 * Armor-aware batch selection (matching attacker batch to target
	 * armour class) is out of scope for the MVP; the first decrease is
	 * the right answer for the villager's melee attack, which is the
	 * only attacker in Mission 1.
	 *
	 * Returns 0 if no FlatAttributeChangeDecrease batch effect is found
	 * (defensive fallback so a malformed ability cannot crash the loop).
	 */
	int64_t get_damage() const;
	int64_t get_damage_against(const nyan::Object &resistance) const;

	/**
	 * FQON of the attribute damaged by this attack (e.g. the Health
	 * attribute).
	 *
	 * Resolved from the first FlatAttributeChangeDecrease batch effect's
	 * change_value.type. AttackHandler uses this to write the post-damage
	 * value via Live::set_attribute on the target.
	 *
	 * Returns an empty FQON if no FlatAttributeChangeDecrease batch
	 * effect is found (defensive fallback; callers should treat empty
	 * FQON as "do not apply damage").
	 */
	nyan::fqon_t get_health_attribute_fqon() const;

	// --- Runtime attack state (set by the AttackSystem) ---

	bool is_active() const;
	entity_id_t get_target_id() const;
	time::time_t get_next_attack_time() const;
	bool is_approaching() const;
	uint64_t get_order_id() const { return this->order_id; }
	bool is_convert() const { return this->get_ability().get_name().ends_with(".Convert"); }
	uint64_t convert_rounds{0};

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
	uint64_t order_id{0};

	/** Entity being attacked. */
	entity_id_t target_id{0};

	/** Time at which the next hit may land. */
	time::time_t next_attack_time{};

	/** Whether the entity is currently moving to close with the target. */
	bool approaching{false};
};

} // namespace openage::gamestate::component
