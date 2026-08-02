// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.2 trigger runtime: the orchestrator + dependency registry.
//
// TriggerRuntime owns the per-load list of TriggerInstances
// and the (DependencyKey -> set of TriggerInstances) index
// that the openage event system notifies when a dependency
// changes. The runtime is the only object that the
// RemovalEventHandler and the trigger.evaluate handler talk to
// for trigger lifecycle; the per-trigger classes are private
// to the runtime.
//
// The runtime's contract with the rest of the engine:
//   * RemovalEventHandler -> notify_entity_removed(entity_id)
//     -> runtime -> schedule a trigger.evaluate event for
//     every TriggerInstance that has a condition subscribed
//     to the entity's removal.
//   * TriggerEvaluateHandler -> evaluate(trigger_id, at) ->
//     runtime -> evaluate_conditions, fire effects, update
//     state.
//   * ActivateTriggerHandler -> set_enabled(true) on the
//     target trigger.
//   * DeclareVictoryHandler -> MissionState::declare_victory
//     with the event's reason param.
//
// The runtime has no per-tick poll. It is purely event-driven,
// like the P4.1 objective controller it extends. The
// event-driven path is the canonical extension point; new
// dependency kinds register themselves with the runtime.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "event/event_loop.h"
#include "event/evententity.h"
#include "event/eventhandler.h"
#include "event/state.h"
#include "gamestate/game_state.h"
#include "scenario/mission_pack.h"
#include "scenario/trigger/mission_state.h"
#include "scenario/trigger/trigger_definition.h"
#include "scenario/trigger/trigger_instance.h"
#include "time/time.h"


namespace openage::scenario::trigger {


/**
 * A small EventEntity used as the event target for
 * trigger.evaluate, set_objective_state, activate_trigger,
 * declare_victory events emitted by the trigger runtime.
 * State (and therefore GameState) does not enable
 * shared_from_this, so the runtime owns a dedicated entity
 * for these events. The entity is allocated once at
 * TriggerRuntime construction and reused.
 */
class TriggerEventEntity : public openage::event::EventEntity {
public:
	explicit TriggerEventEntity(
		const std::shared_ptr<openage::event::EventLoop> &loop) :
		openage::event::EventEntity{loop} {}
	size_t id() const override { return 0x7e7e'7e7e'7e7e'7e7eULL; }
	std::string idstr() const override { return "TriggerRuntimeEventEntity"; }
};


/**
 * The P4.2 trigger runtime. One per load. The runtime owns:
 *   * The list of TriggerInstance (one per IR trigger)
 *   * The (DependencyKey -> set of TriggerInstance) index
 *   * The MissionState (one per load)
 *   * The openage event handlers for "set_objective_state",
 *     "activate_trigger", and "declare_victory"
 *
 * The runtime does NOT register a "trigger.evaluate" handler
 * itself; it schedules trigger.evaluate events via
 * create_event, and the openage event loop dispatches them
 * to the registered handler. The handler is a single class
 * (TriggerEvaluateHandler) that calls back into the
 * runtime; the split is for the same reason the
 * RemovalEventHandler is a separate class: the handler is a
 * unit of dispatch, the runtime is the policy.
 */
class TriggerRuntime : public std::enable_shared_from_this<TriggerRuntime> {
public:
	TriggerRuntime(
		const std::shared_ptr<openage::event::EventLoop> &loop,
		std::shared_ptr<openage::gamestate::GameState> state);
	~TriggerRuntime() = default;

	/**
	 * Register the openage event handlers (set_objective_state,
	 * activate_trigger, declare_victory, trigger.evaluate) with
	 * the event loop. Called once at the end of
	 * GameSimulation::initialize after the registry is
	 * populated.
	 */
	void register_handlers();

	/**
	 * Add a trigger from a MissionTriggerDefinition IR
	 * record. The runtime constructs the TriggerDefinition
	 * snapshot, the condition list, the effect list, and the
	 * TriggerInstance; registers the instance in the
	 * dependency index; and returns the new instance's id
	 * so the caller can record it.
	 */
	TriggerId add_trigger(const gamestate::MissionTriggerDefinition &ir);

	/**
	 * Add an objective (Mission 1 has one). The objective
	 * starts in INACTIVE state; the loader is expected to
	 * follow up with a SetObjectiveState(ACTIVE) effect
	 * (or the runtime can do that itself if a "primary
	 * objective" flag is set -- out of scope for P4.2).
	 */
	void add_objective(const std::string &objective_id);

	/**
	 * P4.1-style entry point. Called by
	 * RemovalEventHandler::add_observer via
	 * ObjectiveRemovalHandler. Forwards to the dependency
	 * registry; schedules a trigger.evaluate event for
	 * every trigger subscribed to this entity's removal.
	 *
	 * `at` is the simulation time at which the removal was
	 * observed. The GameSimulation bridge obtains it from
	 * time_loop->get_clock()->get_time(); tests pass an
	 * explicit value. The runtime does NOT consult any clock
	 * itself, so it stays a pure policy object.
	 */
	void notify_entity_removed(
		openage::gamestate::entity_id_t entity_id,
		openage::time::time_t at);

	/**
	 * Called by the trigger.evaluate handler when a
	 * trigger is selected for evaluation. The handler is
	 * stateless; the runtime is the policy.
	 */
	void evaluate_trigger(
		TriggerId trigger_id,
		openage::time::time_t at);

	/**
	 * Return the TriggerInstance for a given id, or nullptr
	 * if no such trigger is registered. Used by tests and
	 * by the trigger.evaluate handler.
	 */
	std::shared_ptr<TriggerInstance> trigger(TriggerId id) const;

	/**
	 * Return the MissionState. The runtime owns it; this
	 * getter hands a reference for the trigger.evaluate
	 * handler and for tests.
	 */
	MissionState &mission_state() { return this->mission_state_; }
	const MissionState &mission_state() const { return this->mission_state_; }

	/**
	 * Number of registered triggers. Used by tests.
	 */
	size_t trigger_count() const { return this->triggers_.size(); }

private:
	/**
	 * The (DependencyKey -> set of TriggerInstance) index.
	 * The set is a list (not a true set) so iteration is
	 * stable; the registry preserves insertion order so the
	 * dispatch order across runs is identical.
	 */
	using TriggerList = std::vector<std::shared_ptr<TriggerInstance>>;
	std::unordered_map<DependencyKey, TriggerList, DependencyKeyHash> dependency_index_;

	/**
	 * All registered triggers, by id.
	 */
	std::unordered_map<TriggerId, std::shared_ptr<TriggerInstance>> triggers_;

	/**
	 * The mission state (objective lifecycle + victory
	 * state). Owned by the runtime.
	 */
	MissionState mission_state_;

	/**
	 * GameState pointer; the runtime does not own it.
	 */
	std::shared_ptr<openage::gamestate::GameState> state_;

	/**
	 * Event loop pointer; the runtime does not own it.
	 */
	std::shared_ptr<openage::event::EventLoop> loop_;

	/**
	 * The runtime-level event entity. Owned by the runtime.
	 * All trigger.evaluate, set_objective_state,
	 * activate_trigger, and declare_victory events emitted
	 * by the runtime use this entity as their target. The
	 * entity has no per-instance state; it is purely the
	 * openage event loop's "this event was triggered by the
	 * trigger runtime" marker.
	 */
	std::shared_ptr<TriggerEventEntity> event_entity_;

	/**
	 * Internal: build the (DependencyKey -> TriggerInstance)
	 * entries for one trigger's conditions.
	 */
	void index_trigger_conditions(
		const std::shared_ptr<TriggerInstance> &instance);
};


/**
 * The trigger.evaluate event handler. Subscribed to the
 * "trigger.evaluate" event name; the handler queries the
 * runtime for the target TriggerInstance, evaluates it, and
 * fires its effects on the openage event loop.
 *
 * The handler is a thin wrapper around
 * TriggerRuntime::evaluate_trigger so the runtime remains
 * the policy layer.
 */
class TriggerEvaluateHandler : public openage::event::OnceEventHandler {
public:
	explicit TriggerEvaluateHandler(
		const std::shared_ptr<TriggerRuntime> &runtime);
	~TriggerEvaluateHandler() override = default;

	void setup_event(
		const std::shared_ptr<openage::event::Event> &event,
		const std::shared_ptr<openage::event::State> &state) override;

	void invoke(
		openage::event::EventLoop &loop,
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		const openage::time::time_t &time,
		const param_map &params) override;

	openage::time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		const openage::time::time_t &at) override;

private:
	std::shared_ptr<TriggerRuntime> runtime;
};


/**
 * The set_objective_state event handler. Subscribed to
 * "set_objective_state"; the handler updates the
 * MissionState.
 */
class SetObjectiveStateHandler : public openage::event::OnceEventHandler {
public:
	explicit SetObjectiveStateHandler(
		const std::shared_ptr<TriggerRuntime> &runtime);
	~SetObjectiveStateHandler() override = default;

	void setup_event(
		const std::shared_ptr<openage::event::Event> &event,
		const std::shared_ptr<openage::event::State> &state) override;

	void invoke(
		openage::event::EventLoop &loop,
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		const openage::time::time_t &time,
		const param_map &params) override;

	openage::time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		const openage::time::time_t &at) override;

private:
	std::shared_ptr<TriggerRuntime> runtime;
};


/**
 * The activate_trigger event handler. Subscribed to
 * "activate_trigger"; the handler sets the target trigger
 * enabled (DISABLED -> ARMED).
 */
class ActivateTriggerHandler : public openage::event::OnceEventHandler {
public:
	explicit ActivateTriggerHandler(
		const std::shared_ptr<TriggerRuntime> &runtime);
	~ActivateTriggerHandler() override = default;

	void setup_event(
		const std::shared_ptr<openage::event::Event> &event,
		const std::shared_ptr<openage::event::State> &state) override;

	void invoke(
		openage::event::EventLoop &loop,
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		const openage::time::time_t &time,
		const param_map &params) override;

	openage::time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		const openage::time::time_t &at) override;

private:
	std::shared_ptr<TriggerRuntime> runtime;
};


/**
 * The declare_victory event handler. Subscribed to
 * "declare_victory"; the handler transitions the MissionState
 * to VICTORY.
 */
class DeclareVictoryHandler : public openage::event::OnceEventHandler {
public:
	explicit DeclareVictoryHandler(
		const std::shared_ptr<TriggerRuntime> &runtime);
	~DeclareVictoryHandler() override = default;

	void setup_event(
		const std::shared_ptr<openage::event::Event> &event,
		const std::shared_ptr<openage::event::State> &state) override;

	void invoke(
		openage::event::EventLoop &loop,
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		const openage::time::time_t &time,
		const param_map &params) override;

	openage::time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		const openage::time::time_t &at) override;

private:
	std::shared_ptr<TriggerRuntime> runtime;
};


}  // namespace openage::scenario::trigger
