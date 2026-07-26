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

namespace {

/**
 * Find the first FlatAttributeChangeDecrease effect in the attacker's
 * ApplyDiscreteEffect batches and invoke \p visit on it. Returns true
 * if one was found, false otherwise.
 *
 * The villager's Attack ability wires a single UnorderedBatch (Batch)
 * whose EffectBatch.effects set contains six decrease effects
 * (BuildingNoPort, Melee, StoneDefense, AnimalPredator, Ram,
 * SiegeBallista); the first decrease is the right answer for any
 * attacker type we have in Mission 1, since the MVP scope does not
 * pick the batch by armour class.
 *
 * Iteration uses the same pattern as entity_factory for set-of-object
 * members: SetValue yields ValueHolder, which we downcast to
 * ObjectValue and resolve through the same View the ability lives in.
 */
template <typename Visitor>
bool visit_first_decrease_effect(const nyan::Object &ability, Visitor &&visit) {
	if (not ability.has("ApplyDiscreteEffect.batches")) {
		return false;
	}
	auto view = ability.get_view();
	auto batches = ability.get_set("ApplyDiscreteEffect.batches");
	for (auto &batch_holder : batches) {
		auto batch_val = std::dynamic_pointer_cast<nyan::ObjectValue>(batch_holder.get_ptr());
		if (not batch_val) {
			continue;
		}
		auto batch = view->get_object(batch_val->get_name());
		if (not batch.has("EffectBatch.effects")) {
			continue;
		}
		auto effects = batch.get_set("EffectBatch.effects");
		for (auto &effect_holder : effects) {
			auto effect_val = std::dynamic_pointer_cast<nyan::ObjectValue>(effect_holder.get_ptr());
			if (not effect_val) {
				continue;
			}
			auto effect = view->get_object(effect_val->get_name());
			// The Decrease/Increase subclasses live in
			// engine.effect.discrete.flat_attribute_change.type (note the
			// ".type" suffix -- the base FlatAttributeChange class lives
			// in the parent flat_attribute_change file). The villager's
			// nyan imports it as 'edisc_flac' and references it via that
			// alias, so we match the full type.nyan FQON here.
			if (effect.extends(
				"engine.effect.discrete.flat_attribute_change.type.FlatAttributeChangeDecrease")) {
				visit(effect);
				return true;
			}
		}
	}
	return false;
}

} // anonymous namespace


int64_t Attack::get_damage() const {
	int64_t damage = 0;
	visit_first_decrease_effect(
		this->get_ability(),
		[&](const nyan::Object &effect) {
			if (effect.has("FlatAttributeChange.change_value")) {
				auto change_value = effect.get_object("FlatAttributeChange.change_value");
				if (change_value.has("AttributeAmount.amount")) {
					damage = change_value.get_int("AttributeAmount.amount");
				}
			}
		});
	return damage;
}

nyan::fqon_t Attack::get_health_attribute_fqon() const {
	nyan::fqon_t fqon;
	visit_first_decrease_effect(
		this->get_ability(),
		[&](const nyan::Object &effect) {
			if (effect.has("FlatAttributeChange.change_value")) {
				auto change_value = effect.get_object("FlatAttributeChange.change_value");
				if (change_value.has("AttributeAmount.type")) {
					fqon = change_value.get_object("AttributeAmount.type").get_name();
				}
			}
		});
	return fqon;
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
