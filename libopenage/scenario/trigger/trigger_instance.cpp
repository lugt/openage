// Copyright 2026 the openage authors. See copying.md for legal info.

#include "scenario/trigger/trigger_instance.h"

#include <stdexcept>

#include "event/event_loop.h"
#include "log/log.h"
#include "log/message.h"
#include "time/time.h"


namespace openage::scenario::trigger {


TriggerInstance::TriggerInstance(
	const std::shared_ptr<openage::event::EventLoop> &loop,
	TriggerDefinition definition,
	std::vector<std::shared_ptr<TriggerCondition>> conditions,
	std::vector<std::shared_ptr<TriggerEffect>> effects) :
	openage::event::EventEntity{loop},
	state_{definition.initially_enabled ? TriggerState::ARMED
	                                    : TriggerState::DISABLED},
	fire_count_{0},
	definition_{std::move(definition)},
	conditions_{std::move(conditions)},
	effects_{std::move(effects)} {

	// Bind the condition/ effect index / parent_trigger_id
	// fields on the effects so the dispatch order is fully
	// self-describing.
	for (size_t i = 0; i < this->effects_.size(); ++i) {
		this->effects_[i]->set_effect_index(i);
		this->effects_[i]->set_parent_trigger_id(this->definition_.id);
	}
}


void TriggerInstance::set_enabled(bool enabled) {
	// The P4.1 TriggerState condition expects the target
	// trigger to be in the ARMED state. set_enabled is
	// the only transition that puts a trigger into ARMED
	// from any other state. It is intentionally one-way
	// for non-looping triggers: a non-looping trigger
	// that has been COMPLETED cannot be re-enabled.
	if (this->state_ == TriggerState::COMPLETED and not this->definition_.looping) {
		log::log(MSG(warn) << "TriggerInstance::set_enabled: "
		                   << "trigger '" << this->definition_.trigger_id
		                   << "' is COMPLETED and non-looping; "
		                   << "ignoring set_enabled(" << enabled << ")");
		return;
	}
	if (enabled) {
		if (this->state_ != TriggerState::ARMED) {
			this->state_ = TriggerState::ARMED;
		}
	}
	else {
		this->state_ = TriggerState::DISABLED;
	}
}


void TriggerInstance::re_arm() {
	if (not this->definition_.looping) {
		log::log(MSG(warn) << "TriggerInstance::re_arm: trigger '"
		                   << this->definition_.trigger_id
		                   << "' is not looping; ignoring");
		return;
	}
	// Looping trigger: COMPLETED -> ARMED. FIRED -> ARMED.
	// (For non-looping we never get here because re_arm is
	// called only by the runtime's effect loop, which
	// checks definition.looping first.)
	this->state_ = TriggerState::ARMED;
}


void TriggerInstance::begin_delayed_fire() {
	// ARMED -> WAITING_DELAY. The runtime is the only caller;
	// a trigger that is already WAITING_DELAY / FIRED / COMPLETED
	// is left untouched (a duplicate dependency notification during
	// the delay window must not reset the wait).
	if (this->state_ != TriggerState::ARMED) {
		return;
	}
	this->state_ = TriggerState::WAITING_DELAY;
}


void TriggerInstance::cancel_delayed_fire() {
	// WAITING_DELAY -> ARMED. The delayed re-evaluation found the
	// conditions no longer hold; the trigger goes back to waiting.
	if (this->state_ != TriggerState::WAITING_DELAY) {
		return;
	}
	this->state_ = TriggerState::ARMED;
}


bool TriggerInstance::evaluate_conditions(
	const openage::gamestate::GameState &state,
	openage::time::time_t at) const {

	// DISABLED and COMPLETED triggers never evaluate. WAITING_DELAY
	// is handled explicitly by TriggerRuntime::evaluate_trigger (a
	// WAITING_DELAY trigger that is re-evaluated at its delayed fire
	// time must pass this check so the runtime can fire it), so it is
	// NOT gated here.
	if (this->state_ == TriggerState::DISABLED or
	    this->state_ == TriggerState::COMPLETED) {
		return false;
	}

	// All conditions must hold for the trigger to be
	// eligible to fire. Empty conditions list is
	// interpreted as "always true" so triggers with no
	// conditions can still be used as timer-only or
	// unconditionally-armed triggers.
	if (this->conditions_.empty()) {
		return true;
	}
	bool has_or = false;
	bool group_ok = true;
	bool group_has_cond = false;
	for (const auto &c : this->conditions_) {
		if (c->is_or_separator()) {
			has_or = true;
			if (group_has_cond and group_ok) {
				return true;
			}
			group_ok = true;
			group_has_cond = false;
			continue;
		}
		group_has_cond = true;
		if (group_ok and not c->evaluate(state, at)) {
			group_ok = false;
		}
	}
	if (has_or) {
		return group_has_cond and group_ok;
	}
	return group_ok;
}


void TriggerInstance::fire(
	const std::shared_ptr<openage::event::EventLoop> &loop,
	const std::shared_ptr<openage::event::EventEntity> &target,
	const std::shared_ptr<openage::event::State> &state,
	openage::time::time_t at) {

	if (this->state_ == TriggerState::FIRED or
	    this->state_ == TriggerState::COMPLETED) {
		// Already fired. The spec says: "same timestamp
		// duplicate dependency notification 不得重复触发".
		// The trigger runtime ensures this by checking the
		// state at evaluate time; if we get here, the
		// duplicate came through anyway. Log and return.
		log::log(MSG(warn) << "TriggerInstance::fire: trigger '"
		                   << this->definition_.trigger_id
		                   << "' already in state "
		                   << static_cast<int>(this->state_)
		                   << "; ignoring duplicate fire");
		return;
	}

	// The delay is on the definition; if it is non-zero the
	// runtime should have routed through WAITING_DELAY
	// already. We accept the call regardless (the runtime
	// is the only caller) and proceed to FIRED.
	this->state_ = TriggerState::FIRED;
	this->fire_count_ += 1;

	// Enqueue the effects in stable order. The runtime
	// has already sorted effects by (at, trigger_id,
	// effect_index, creation_seq); here we enqueue them
	// in effect_index order which is the only ordering
	// the trigger instance knows.
	for (auto &eff : this->effects_) {
		eff->enqueue(loop, target, state, at);
	}

	// Non-looping triggers transition to COMPLETED. The
	// trigger can no longer fire (the duplicate-fire check
	// above short-circuits). Looping triggers stay FIRED
	// until the runtime re_arms them.
	if (not this->definition_.looping) {
		this->state_ = TriggerState::COMPLETED;
	}
}


}  // namespace openage::scenario::trigger
