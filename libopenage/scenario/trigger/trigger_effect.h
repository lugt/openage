// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.2 trigger runtime: the TriggerEffect interface and
// the three concrete implementations required by the Mission 1
// MVP (SetObjectiveState, ActivateTrigger, DeclareVictory).
//
// A TriggerEffect enqueues a *single* discrete event on the
// openage event loop. It is the effect's responsibility to
// construct the right event with the right parameters and
// reference time. Effects never mutate GameState directly;
// they go through the openage event system so that
// downstream subscribers (HUD, audio, etc., when added later)
// see the same events the rest of the engine sees.
//
// The runtime dispatches effects in a stable order:
//
//   1. simulation_time  (the trigger's fire time)
//   2. trigger_id       (FNV-1a of trigger_id string)
//   3. effect_index     (the position in the trigger's
//                        effect list; 0, 1, 2, ...)
//   4. event creation sequence  (process-wide monotonic,
//                                used as the final tie-breaker)
//
// The effect is NOT responsible for ordering itself; the
// trigger instance dispatches its effects in this order.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "event/event_loop.h"
#include "event/evententity.h"
#include "event/eventhandler.h"
#include "event/state.h"
#include "gamestate/game_state.h"
#include "scenario/mission_pack.h"
#include "scenario/trigger/trigger_definition.h"
#include "time/time.h"


namespace openage::scenario::trigger {


/**
 * The abstract effect interface. Each implementation enqueues
 * exactly ONE event on the supplied event loop. The effect
 * does not mutate GameState directly; the event handler does.
 */
class TriggerEffect {
public:
	virtual ~TriggerEffect() = default;

	/**
	 * Enqueue the effect's event on the loop. The effect
	 * name (e.g. "SetObjectiveState") is used as the event
	 * name; the effect's parameters (objective_id,
	 * target_state, target_trigger_id, etc.) are carried in
	 * the param_map.
	 *
	 * The runtime supplies the `target` event entity and the
	 * `state` shared_ptr; the effect does NOT create either.
	 * `state` is passed through to create_event unchanged
	 * (create_event takes a shared_ptr<State>, and GameState
	 * IS-A State, so the runtime's shared_ptr<GameState>
	 * converts implicitly).
	 */
	virtual void enqueue(
		const std::shared_ptr<openage::event::EventLoop> &loop,
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		openage::time::time_t at) const = 0;

	/**
	 * The event name this effect emits (used by the
	 * TriggerEvaluateHandler to register the right handler
	 * and by the registry to identify the effect).
	 */
	virtual std::string event_name() const = 0;

	/**
	 * Index of this effect within the parent trigger's
	 * effect list. Set by the registry at construction.
	 * Used as part of the stable dispatch order key.
	 */
	size_t effect_index() const { return this->effect_index_; }
	void set_effect_index(size_t i) { this->effect_index_ = i; }
	TriggerId parent_trigger_id() const { return this->parent_trigger_id_; }
	void set_parent_trigger_id(TriggerId id) { this->parent_trigger_id_ = id; }

private:
	size_t effect_index_{0};
	TriggerId parent_trigger_id_{NULL_TRIGGER_ID};
};


/**
 * EFFECT_KIND_SET_OBJECTIVE_STATE. Emits "set_objective_state"
 * with params {objective_id, target_state}. The handler
 * (SetObjectiveStateHandler) updates the MissionState.
 *
 * Stable effect order key:
 *   (at, parent_trigger_id, effect_index, creation_seq)
 */
class SetObjectiveStateEffect : public TriggerEffect {
public:
	SetObjectiveStateEffect(
		const std::string &objective_id,
		const std::string &target_state);

	void enqueue(
		const std::shared_ptr<openage::event::EventLoop> &loop,
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		openage::time::time_t at) const override;

	std::string event_name() const override { return "set_objective_state"; }

	const std::string &objective_id() const { return this->objective_id_; }
	const std::string &target_state() const { return this->target_state_; }

private:
	std::string objective_id_;
	std::string target_state_;
};


/**
 * EFFECT_KIND_ACTIVATE_TRIGGER. Emits "activate_trigger" with
 * params {target_trigger_id}. The handler
 * (ActivateTriggerHandler) sets the target trigger to ARMED.
 *
 * Activation is a one-way transition: DISABLED -> ARMED (or
 * COMPLETED -> ARMED for looping triggers via the runtime's
 * re_arm path; ActivateTriggerEffect only enables, it does
 * not reset state). The handler is responsible for the
 * actual transition.
 */
class ActivateTriggerEffect : public TriggerEffect {
public:
	explicit ActivateTriggerEffect(const std::string &target_trigger_id);

	void enqueue(
		const std::shared_ptr<openage::event::EventLoop> &loop,
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		openage::time::time_t at) const override;

	std::string event_name() const override { return "activate_trigger"; }

	const std::string &target_trigger_id() const { return this->target_trigger_id_; }

private:
	std::string target_trigger_id_;
};


/**
 * EFFECT_KIND_DECLARE_VICTORY. Emits "declare_victory" with
 * params {reason}. The handler (DeclareVictoryHandler)
 * transitions the MissionState to VICTORY. The reason is
 * an optional string (e.g. "all_enemies_removed",
 * "objective_completed") that the handler may log.
 */
class DeclareVictoryEffect : public TriggerEffect {
public:
	explicit DeclareVictoryEffect(const std::string &reason);

	void enqueue(
		const std::shared_ptr<openage::event::EventLoop> &loop,
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		openage::time::time_t at) const override;

	std::string event_name() const override { return "declare_victory"; }

	const std::string &reason() const { return this->reason_; }

private:
	std::string reason_;
};


/**
 * Registry helper: construct a TriggerEffect from an IR
 * record. Returns nullptr if the IR record's kind is unknown
 * to the runtime. The kind string in the IR maps to an
 * EffectKind via a small dispatch table; the runtime does
 * NOT consult the AoE2 DE parser.
 */
void register_scenario_effect_handlers(const std::shared_ptr<openage::event::EventLoop> &loop);

std::shared_ptr<TriggerEffect> make_trigger_effect(
	const gamestate::MissionTriggerEffect &ir);


}  // namespace openage::scenario::trigger
