// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.2 trigger runtime: the TriggerInstance class.
//
// TriggerInstance is the per-load runtime object for a single
// IR trigger. It owns its conditions and effects, holds the
// state machine, and is the unit of evaluation when a
// dependency notification arrives.
//
// IMPORTANT: this class is NOT a polling controller. It never
// reads the GameState on its own. It evaluates ONLY when the
// TriggerEvaluateHandler fires for it (via a
// TriggerEvaluateEvent scheduled by a dependency
// notification). The P4.1 event-driven path that the
// WilliamWallaceMission01ObjectiveController established is
// the pattern this class extends -- it does NOT introduce a
// per-tick poll.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "event/evententity.h"
#include "event/state.h"
#include "scenario/mission_pack.h"
#include "scenario/trigger/trigger_condition.h"
#include "scenario/trigger/trigger_definition.h"
#include "scenario/trigger/trigger_effect.h"
#include "time/time.h"


namespace openage::scenario::trigger {


/**
 * The runtime instance of a single trigger. Owns its conditions
 * and effects; holds the state machine and the fire counter.
 *
 * The instance inherits EventEntity so it can be the
 * dependency target of the openage event system. It is NOT a
 * GameState subscriber: it only responds to explicit
 * trigger.evaluate events that the
 * TriggerEvaluateHandler dispatches in response to dependency
 * notifications.
 */
class TriggerInstance
	: public openage::event::EventEntity
	, public std::enable_shared_from_this<TriggerInstance> {
public:
	/**
	 * Build a TriggerInstance from a TriggerDefinition snapshot
	 * plus the concrete Condition and Effect lists constructed
	 * by the registry. The constructor wires the conditions
	 * to the dependency registry; the caller is responsible
	 * for adding the instance to the registry's
	 * affected-trigger list via the supplied callbacks.
	 *
	 * @param loop         Event loop this instance lives in.
	 * @param definition   Runtime-facing definition snapshot.
	 * @param conditions   Concrete Condition instances; the
	 *                     runtime takes shared ownership.
	 * @param effects      Concrete Effect instances; the
	 *                     runtime takes shared ownership.
	 */
	TriggerInstance(
		const std::shared_ptr<openage::event::EventLoop> &loop,
		TriggerDefinition definition,
		std::vector<std::shared_ptr<TriggerCondition>> conditions,
		std::vector<std::shared_ptr<TriggerEffect>> effects);

	~TriggerInstance() override = default;

	// EventEntity
	size_t id() const override {
		return static_cast<size_t>(this->definition_.id);
	}
	std::string idstr() const override {
		return "TriggerInstance(" + this->definition_.trigger_id + ")";
	}

	TriggerId trigger_id() const { return this->definition_.id; }
	const std::string &trigger_name() const { return this->definition_.trigger_id; }
	TriggerState state() const { return this->state_; }
	uint32_t fire_count() const { return this->fire_count_; }
	const TriggerDefinition &definition() const { return this->definition_; }
	const std::vector<std::shared_ptr<TriggerCondition>> &conditions() const { return this->conditions_; }
	const std::vector<std::shared_ptr<TriggerEffect>> &effects() const { return this->effects_; }

	/**
	 * Enable or disable the trigger. A disabled trigger does
	 * not evaluate; a re-enable transitions DISABLED -> ARMED
	 * (only if initially_enabled was true; a
	 * initially_enabled=false trigger stays DISABLED until
	 * set_enabled(true) is called).
	 */
	void set_enabled(bool enabled);

	/**
	 * Re-arm a looping trigger. Only valid in FIRED or
	 * COMPLETED. Non-looping triggers reject the call.
	 */
	void re_arm();

	/**
	 * Transition ARMED -> WAITING_DELAY. Called by the runtime
	 * when a trigger's conditions hold but its delay has not yet
	 * elapsed. The runtime schedules the delayed trigger.evaluate
	 * at (notification_time + delay); when that fires, the runtime
	 * re-checks conditions and either fires (conditions still true)
	 * or calls cancel_delayed_fire() (conditions no longer true).
	 */
	void begin_delayed_fire();

	/**
	 * Transition WAITING_DELAY -> ARMED. Called by the runtime
	 * when a delayed re-evaluation finds the conditions no longer
	 * hold; the trigger goes back to waiting for the next
	 * dependency notification.
	 */
	void cancel_delayed_fire();

	/**
	 * Called by the trigger.evaluate handler to evaluate
	 * the conditions. Returns true iff all conditions
	 * currently hold AND the trigger is in a state that can
	 * fire (ARMED, or WAITING_DELAY with elapsed delay).
	 *
	 * The state machine transition is NOT performed here; the
	 * caller (TriggerEvaluateHandler) is responsible for
	 * calling fire() when this returns true and the delay
	 * (if any) is satisfied.
	 */
	bool evaluate_conditions(
		const openage::gamestate::GameState &state,
		openage::time::time_t at) const;

	/**
	 * Fire the trigger: transition to FIRED, enqueue the
	 * effect events, bump the fire_count. After this returns,
	 * the trigger is in FIRED (or COMPLETED for non-looping).
	 *
	 * @param loop   The event loop used to enqueue effect
	 *               events.
	 * @param target The runtime-level EventEntity used as the
	 *               event target for every effect event.
	 * @param state  The GameState shared_ptr passed through to
	 *               create_event (effects never dereference it).
	 * @param at     The simulation time at which the trigger
	 *               fires (used as the effect event's
	 *               reference time).
	 */
	void fire(
		const std::shared_ptr<openage::event::EventLoop> &loop,
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		openage::time::time_t at);

private:
	/**
	 * State machine. Owned exclusively by this class.
	 * Transitions are private; external code uses
	 * set_enabled / re_arm / fire.
	 */
	TriggerState state_;
	uint32_t fire_count_{0};
	TriggerDefinition definition_;
	std::vector<std::shared_ptr<TriggerCondition>> conditions_;
	std::vector<std::shared_ptr<TriggerEffect>> effects_;
};


}  // namespace openage::scenario::trigger
