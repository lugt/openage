// Copyright 2026 the openage authors. See copying.md for legal info.

#include "scenario/trigger/trigger_effect.h"

#include <stdexcept>

#include "event/evententity.h"
#include "log/log.h"
#include "log/message.h"


namespace openage::scenario::trigger {


// ---------------------------------------------------------------------------
// SetObjectiveStateEffect
// ---------------------------------------------------------------------------


SetObjectiveStateEffect::SetObjectiveStateEffect(
	const std::string &objective_id,
	const std::string &target_state) :
	objective_id_{objective_id},
	target_state_{target_state} {}


void SetObjectiveStateEffect::enqueue(
	const std::shared_ptr<openage::event::EventLoop> &loop,
	const std::shared_ptr<openage::event::EventEntity> &target,
	const std::shared_ptr<openage::event::State> &state,
	openage::time::time_t at) const {

	openage::event::EventHandler::param_map::map_t params{
		{"objective_id", this->objective_id_},
		{"target_state", this->target_state_},
	};
	loop->create_event("set_objective_state", target, state, at, params);
}


// ---------------------------------------------------------------------------
// ActivateTriggerEffect
// ---------------------------------------------------------------------------


ActivateTriggerEffect::ActivateTriggerEffect(
	const std::string &target_trigger_id) :
	target_trigger_id_{target_trigger_id} {}


void ActivateTriggerEffect::enqueue(
	const std::shared_ptr<openage::event::EventLoop> &loop,
	const std::shared_ptr<openage::event::EventEntity> &target,
	const std::shared_ptr<openage::event::State> &state,
	openage::time::time_t at) const {

	openage::event::EventHandler::param_map::map_t params{
		{"target_trigger_id", this->target_trigger_id_},
	};
	loop->create_event("activate_trigger", target, state, at, params);
}


// ---------------------------------------------------------------------------
// DeclareVictoryEffect
// ---------------------------------------------------------------------------


DeclareVictoryEffect::DeclareVictoryEffect(
	const std::string &reason) :
	reason_{reason} {}


void DeclareVictoryEffect::enqueue(
	const std::shared_ptr<openage::event::EventLoop> &loop,
	const std::shared_ptr<openage::event::EventEntity> &target,
	const std::shared_ptr<openage::event::State> &state,
	openage::time::time_t at) const {

	openage::event::EventHandler::param_map::map_t params{
		{"reason", this->reason_},
	};
	loop->create_event("declare_victory", target, state, at, params);
}


// ---------------------------------------------------------------------------
// make_trigger_effect
// ---------------------------------------------------------------------------


std::shared_ptr<TriggerEffect> make_trigger_effect(
	const gamestate::MissionTriggerEffect &ir) {

	// The IR kind strings are the stable CamelCase tokens
	// validated by validate_mission_pack (SUPPORTED_EFFECT_KINDS
	// in mission_pack.cpp). They MUST match the validator's set.
	// The internal event names emitted by enqueue() (e.g.
	// "set_objective_state") are a SEPARATE namespace -- they are
	// the openage event-loop handler names, not data kinds, and
	// are decoupled from the IR kind.
	if (ir.kind == "SetObjective") {
		// target_scenario_object_id is the objective_id; the
		// target_state string ("ACTIVE"/"INACTIVE") rides in
		// target_area_id by IR convention (the field is
		// overloaded; the runtime treats it as a string).
		return std::make_shared<SetObjectiveStateEffect>(
			ir.target_scenario_object_id, ir.target_area_id);
	}
	if (ir.kind == "CompleteObjective") {
		// Distinct IR kind that forces the objective to COMPLETED.
		// target_scenario_object_id is the objective_id.
		return std::make_shared<SetObjectiveStateEffect>(
			ir.target_scenario_object_id, std::string{"COMPLETED"});
	}
	if (ir.kind == "ActivateTrigger") {
		return std::make_shared<ActivateTriggerEffect>(
			ir.target_trigger_id);
	}
	if (ir.kind == "DeclareVictory") {
		// The reason is in target_scenario_object_id by IR
		// convention; the runtime treats it as a string.
		return std::make_shared<DeclareVictoryEffect>(
			ir.target_scenario_object_id);
	}
	log::log(MSG(warn) << "make_trigger_effect: unknown kind '"
	                   << ir.kind << "'; returning nullptr");
	return nullptr;
}


}  // namespace openage::scenario::trigger
