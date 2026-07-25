// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "attack.h"

#include <nyan/nyan.h>

#include "gamestate/component/types.h"


namespace openage::gamestate::component {

component_t Attack::get_type() const {
	return component_t::ATTACK;
}

double Attack::get_cooldown() const {
	return this->get_ability().get_float("ApplyDiscreteEffect.reload_time");
}

double Attack::get_attack_range() const {
	const auto &ability = this->get_ability();
	// RangedDiscreteEffect (subclass) carries min/max_range. The base
	// ApplyDiscreteEffect (melee) has no range field, so fall back to the
	// default engagement range.
	if (ability.extends("engine.ability.type.RangedDiscreteEffect")) {
		return static_cast<double>(ability.get_int("RangedDiscreteEffect.max_range"));
	}
	return DEFAULT_MELEE_RANGE;
}

int64_t Attack::get_damage() const {
	// Increment 4 reads the flat Health damage from the ability's effect
	// batch. Returning 0 here is intentional -- increment 2 only tracks
	// range, and the AttackSystem does not apply damage yet.
	return 0;
}

bool Attack::is_active() const {
	return this->active;
}

entity_id_t Attack::get_target_id() const {
	return this->target_id;
}

time::time_t Attack::get_next_attack_time() const {
	return this->next_attack_time;
}

bool Attack::is_approaching() const {
	return this->approaching;
}

void Attack::start_attack(entity_id_t target, const time::time_t &now) {
	this->active = true;
	this->target_id = target;
	this->next_attack_time = now;
	this->approaching = false;
}

void Attack::stop_attack() {
	this->active = false;
	this->target_id = 0;
	this->next_attack_time = time::time_t{};
	this->approaching = false;
}

void Attack::set_next_attack_time(const time::time_t &t) {
	this->next_attack_time = t;
}

void Attack::set_approaching(bool approaching) {
	this->approaching = approaching;
}

} // namespace openage::gamestate::component
