// Copyright 2026 the openage authors. See copying.md for legal info.

#include "scenario/trigger/trigger_condition.h"

#include <stdexcept>

#include "log/log.h"
#include "log/message.h"


namespace openage::scenario::trigger {


size_t DependencyKeyHash::operator()(const DependencyKey &k) const noexcept {
	// FNV-1a over (kind byte, key 8 bytes little-endian) -- we
	// mix the kind into the low byte so the same key value
	// under different kinds hashes to different buckets.
	constexpr uint64_t FNV_OFFSET = 0xcbf29ce484222325ULL;
	constexpr uint64_t FNV_PRIME = 0x100000001b3ULL;
	uint64_t h = FNV_OFFSET;
	h ^= static_cast<uint64_t>(k.kind);
	h *= FNV_PRIME;
	for (int i = 0; i < 8; ++i) {
		uint8_t byte = static_cast<uint8_t>((k.key >> (i * 8)) & 0xff);
		h ^= byte;
		h *= FNV_PRIME;
	}
	return static_cast<size_t>(h);
}


// ---------------------------------------------------------------------------
// EntityRemovedCondition
// ---------------------------------------------------------------------------


EntityRemovedCondition::EntityRemovedCondition(
	const openage::gamestate::GameState &state,
	const gamestate::MissionTriggerCondition &ir) :
	target_scenario_object_id_{ir.target_scenario_object_id} {

	// Resolve the placed-instance FQON to a runtime entity id
	// at construction. The GameState's named-entity map is
	// populated by the Mission Pack loader before the
	// condition is built; if the lookup fails the condition
	// is permanently false (the FQON does not exist in this
	// mission).
	if (this->target_scenario_object_id_.empty()) {
		this->target_entity_id_ = -1;
		return;
	}
	auto opt = state.get_entity_id_by_instance(this->target_scenario_object_id_);
	if (opt.has_value()) {
		this->target_entity_id_ = opt.value();
	}
	else {
		this->target_entity_id_ = -1;
		log::log(MSG(warn) << "EntityRemovedCondition: no entity "
		                   << "registered for ScenarioObjectId "
		                   << this->target_scenario_object_id_
		                   << "; condition is permanently false");
	}
}


bool EntityRemovedCondition::evaluate(
	const openage::gamestate::GameState &state,
	openage::time::time_t /* at */) const {

	if (this->target_entity_id_ < 0) {
		return false;
	}
	// The condition is true iff the entity is no longer in
	// the GameState. GameState::has_game_entity is the
	// canonical predicate; it is O(1) on the index.
	return not state.has_game_entity(
		static_cast<openage::gamestate::entity_id_t>(this->target_entity_id_));
}


std::vector<DependencyKey> EntityRemovedCondition::dependencies() const {
	if (this->target_entity_id_ < 0) {
		return {};
	}
	return {DependencyKey{
		DependencyKind::ENTITY,
		static_cast<uint64_t>(this->target_entity_id_)}};
}


// ---------------------------------------------------------------------------
// TimerElapsedCondition
// ---------------------------------------------------------------------------


TimerElapsedCondition::TimerElapsedCondition(
	openage::time::time_t trigger_delay) :
	trigger_delay{trigger_delay},
	fired_{false} {}


bool TimerElapsedCondition::evaluate(
	const openage::gamestate::GameState & /* state */,
	openage::time::time_t at) const {

	// Single-shot: once the delay has elapsed the condition
	// stays true. Re-arm via the parent trigger's lifecycle
	// (set_enabled / re_arm on the TriggerInstance).
	if (this->trigger_delay <= openage::time::time_t{0}) {
		// Delay is zero or negative -- the condition is true
		// from the first evaluation. We do not flip fired_
		// here because the trigger still drives re-arm.
		return true;
	}
	if (at >= this->trigger_delay) {
		this->fired_ = true;
		return true;
	}
	return false;
}


std::vector<DependencyKey> TimerElapsedCondition::dependencies() const {
	// The condition depends on the simulation clock. The
	// dependency registry treats TIMER as a special key that
	// fires on every reach_time; see the TriggerEvaluateHandler.
	return {DependencyKey{DependencyKind::TIMER, 0}};
}


// ---------------------------------------------------------------------------
// TriggerStateCondition
// ---------------------------------------------------------------------------


TriggerStateCondition::TriggerStateCondition(
	TriggerId target_trigger_id,
	TriggerState target_state) :
	target_trigger_id_{target_trigger_id},
	target_state_{target_state} {}


bool TriggerStateCondition::evaluate(
	const openage::gamestate::GameState & /* state */,
	openage::time::time_t /* at */) const {

	// The runtime holds the canonical state in the registry;
	// the condition's evaluate asks the registry. We avoid a
	// hard dependency here by returning a pessimistic false
	// when the registry is not set -- the registry must be
	// supplied via set_state_provider() for this condition
	// to be useful.
	if (not this->provider) {
		return false;
	}
	return this->provider(this->target_trigger_id_) == this->target_state_;
}


std::vector<DependencyKey> TriggerStateCondition::dependencies() const {
	return {DependencyKey{DependencyKind::TRIGGER,
	                     static_cast<uint64_t>(this->target_trigger_id_)}};
}


// ---------------------------------------------------------------------------
// make_trigger_condition
// ---------------------------------------------------------------------------


std::shared_ptr<TriggerCondition> make_trigger_condition(
	const openage::gamestate::GameState &state,
	openage::time::time_t trigger_delay,
	const gamestate::MissionTriggerCondition &ir) {

	// The kind string in the IR is the stable CamelCase token
	// emitted by the scenario writer and validated by
	// validate_mission_pack (SUPPORTED_CONDITION_KINDS in
	// mission_pack.cpp). The runtime does not consult any
	// DE2-specific parser; the kinds here MUST match the
	// validator's set so a trigger that passes data validation
	// is also constructable by the runtime.
	if (ir.kind == "EntityDestroyed") {
		return std::make_shared<EntityRemovedCondition>(state, ir);
	}
	if (ir.kind == "TimerElapsed") {
		return std::make_shared<TimerElapsedCondition>(trigger_delay);
	}
	if (ir.kind == "TriggerFired") {
		// The IR record carries the target_trigger_id as a
		// string; we resolve it to a TriggerId here so the
		// condition can be evaluated without re-resolution.
		TriggerId tid = make_trigger_id(ir.target_trigger_id);
		// Default target state: ARMED. The IR does not
		// currently carry a state field, so the condition
		// is implicitly "the target trigger is ARMED". A
		// follow-up spike can extend the IR with a state
		// field without touching this constructor.
		return std::make_shared<TriggerStateCondition>(
			tid, TriggerState::ARMED);
	}
	log::log(MSG(warn) << "make_trigger_condition: unknown kind '"
	                   << ir.kind << "'; returning nullptr");
	return nullptr;
}


}  // namespace openage::scenario::trigger
