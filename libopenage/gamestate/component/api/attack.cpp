// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "attack.h"

#include <nyan/nyan.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

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
 * P4.1.1: deterministic ordered iteration over a nyan set of
 * object-valued holders. nyan::Set is backed by
 * std::unordered_set (see nyan/value/set.h); iteration order is
 * platform-dependent (libc++ vs libstdc++ hash + seed differences)
 * and pointer-dependent. Application code that takes "first
 * matching" or otherwise assumes a deterministic order MUST run
 * the set through this helper. The order is by FQON of the
 * contained ObjectValue, ascending.
 *
 * This is the application-side deterministic-iteration helper. The
 * nyan library's Set remains unordered by design; relying on
 * pointer order in the event loop or the entity factory would
 * reintroduce the platform-dependent behavior we are removing.
 */
std::vector<nyan::fqon_t> stable_object_fqons(const nyan::set_t &set) {
	std::vector<nyan::fqon_t> out;
	out.reserve(set.size());
	for (auto &holder : set) {
		auto val = std::dynamic_pointer_cast<nyan::ObjectValue>(holder.get_ptr());
		if (val) {
			out.push_back(val->get_name());
		}
	}
	std::sort(out.begin(), out.end());
	return out;
}


/**
 * Find the first FlatAttributeChangeDecrease effect in the attacker's
 * ApplyDiscreteEffect batches and invoke \p visit on it. Returns true
 * if one was found, false otherwise.
 *
 * P4.1.1: batch and effect set iteration go through
 * stable_object_fqons() so the same input produces the same
 * dispatch order on every platform and on every run. The first
 * matching NON-FALLBACK decrease effect is the answer for
 * Mission 1 (the MVP scope does not pick the batch by armour
 * class).
 *
 * The de2_base converter writes a fallback effect
 * (AoE2AttackFallback, with the conservative 1-damage
 * change_value) into the same EffectBatch as the per-armour
 * effects. The fallback is identifiable by name suffix
 * ("AoE2AttackFallback") and by FQON prefix
 * ("...effect.discrete.flat_attribute_change.fallback."). We
 * skip the fallback in favour of a real effect because the
 * fallback's 1-damage change_value exists exactly to ensure
 * any attacker can deal at least 1 damage; for the primary
 * read (the attacker's per-hit damage) the fallback is the
 * wrong answer. This is the platform-independent equivalent of
 * the converter-side ordering that produced the Ubuntu
 * baseline.
 */
template <typename Visitor>
bool visit_first_decrease_effect(const nyan::Object &ability, Visitor &&visit) {
	if (not ability.has("ApplyDiscreteEffect.batches")) {
		return false;
	}
	auto view = ability.get_view();
	auto batches_fqons = stable_object_fqons(ability.get_set("ApplyDiscreteEffect.batches"));
	for (auto &batch_fqon : batches_fqons) {
		auto batch = view->get_object(batch_fqon);
		if (not batch.has("EffectBatch.effects")) {
			continue;
		}
		auto effects_fqons = stable_object_fqons(batch.get_set("EffectBatch.effects"));
		for (auto &effect_fqon : effects_fqons) {
			auto effect = view->get_object(effect_fqon);
			// The Decrease/Increase subclasses live in
			// engine.effect.discrete.flat_attribute_change.type (note the
			// ".type" suffix -- the base FlatAttributeChange class lives
			// in the parent flat_attribute_change file). The villager's
			// nyan imports it as 'edisc_flac' and references it via that
			// alias, so we match the full type.nyan FQON here.
			if (effect.extends(
				"engine.effect.discrete.flat_attribute_change.type.FlatAttributeChangeDecrease")) {
				// Skip the de2_base AoE2AttackFallback. It is the
				// conservative 1-damage default the converter writes so
				// that ANY attacker can deal at least 1 damage; for
				// the per-hit damage read it is the wrong answer. The
				// suffix and FQON check are both safe (suffix is a
				// stable class name in the de2_base nyan; FQON check
				// is the namespace the converter emits the fallback
				// under).
				bool is_fallback = (effect_fqon.find(".AoE2AttackFallback") != std::string::npos) ||
					(effect_fqon.find("flat_attribute_change.fallback.") != std::string::npos);
				if (is_fallback) {
					continue;
				}
				// Skip effects with zero change_value. The de2_base
				// converter writes these as placeholders (e.g. ram vs
				// building attacks deal 0 damage to villagers) and
				// they would silently swallow the attack if picked.
				// Filtering them keeps the result a positive damage
				// per hit, matching the Ubuntu test intent.
				if (effect.has("FlatAttributeChange.change_value")) {
					auto cv = effect.get_object("FlatAttributeChange.change_value");
					if (cv.has("AttributeAmount.amount") &&
					    cv.get_int("AttributeAmount.amount") == 0) {
						continue;
					}
				}
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

int64_t Attack::get_damage_against(const nyan::Object &resistance) const {
	std::unordered_map<nyan::fqon_t, int64_t> armor;
	auto target_view = resistance.get_view();
	for (const auto &fqon : stable_object_fqons(resistance.get_set("Resistance.resistances"))) {
		auto entry = target_view->get_object(fqon);
		if (entry.has("FlatAttributeChange.block_value")) {
			auto type = entry.get_object("FlatAttributeChange.type").get_name();
			auto value = entry.get_object("FlatAttributeChange.block_value");
			armor.emplace(type, value.get_int("AttributeAmount.amount"));
		}
	}

	const auto &attack = this->get_ability();
	auto attack_view = attack.get_view();
	int64_t total = 0;
	int64_t fallback = 0;
	const auto health = this->get_health_attribute_fqon();
	for (const auto &batch_fqon : stable_object_fqons(attack.get_set("ApplyDiscreteEffect.batches"))) {
		auto batch = attack_view->get_object(batch_fqon);
		for (const auto &fqon : stable_object_fqons(batch.get_set("EffectBatch.effects"))) {
			auto effect = attack_view->get_object(fqon);
			if (not effect.extends("engine.effect.discrete.flat_attribute_change.type.FlatAttributeChangeDecrease")
			    or not effect.has("FlatAttributeChange.change_value")) {
				continue;
			}
			auto value = effect.get_object("FlatAttributeChange.change_value");
			if (value.get_object("AttributeAmount.type").get_name() != health) {
				continue;
			}
			auto amount = value.get_int("AttributeAmount.amount");
			if (fqon.ends_with(".AoE2AttackFallback")) {
				fallback = amount;
				continue;
			}
			auto type = effect.get_object("FlatAttributeChange.type").get_name();
			auto block = armor.find(type);
			if (block != armor.end()) {
				total += std::max<int64_t>(0, amount - block->second);
			}
		}
	}
	return std::max(total, fallback);
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
	++this->order_id;
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
