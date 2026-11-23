// Copyright 2026 the openage authors. See copying.md for legal info.

#include "scenario/trigger/trigger_runtime.h"

#include <stdexcept>

#include "event/event.h"
#include "event/evententity.h"
#include "event/eventhandler.h"
#include "event/event_loop.h"
#include "gamestate/game_state.h"
#include "log/log.h"
#include "log/message.h"
#include "scenario/mission_pack.h"
#include "scenario/trigger/trigger_condition.h"
#include "scenario/trigger/original_scenario.h"
#include "scenario/trigger/trigger_effect.h"
#include "time/time.h"


namespace openage::scenario::trigger {


// ---------------------------------------------------------------------------
// TriggerRuntime
// ---------------------------------------------------------------------------


TriggerRuntime::TriggerRuntime(
	const std::shared_ptr<openage::event::EventLoop> &loop,
	std::shared_ptr<openage::gamestate::GameState> state) :
	state_{std::move(state)},
	loop_{loop},
	event_entity_{std::make_shared<TriggerEventEntity>(loop)} {}


void TriggerRuntime::register_handlers() {
	if (not this->loop_) {
		return;
	}
	register_scenario_effect_handlers(this->loop_);
	this->loop_->add_event_handler(
		std::make_shared<TriggerEvaluateHandler>(
			this->shared_from_this()));
	this->loop_->add_event_handler(
		std::make_shared<SetObjectiveStateHandler>(
			this->shared_from_this()));
	this->loop_->add_event_handler(
		std::make_shared<ActivateTriggerHandler>(
			this->shared_from_this()));
	this->loop_->add_event_handler(
		std::make_shared<DeclareVictoryHandler>(
			this->shared_from_this()));
}


TriggerId TriggerRuntime::add_trigger(
	const gamestate::MissionTriggerDefinition &ir) {

	// Build the runtime-facing definition snapshot.
	TriggerDefinition def;
	def.id = make_trigger_id(ir.trigger_id);
	def.trigger_id = ir.trigger_id;
	def.initially_enabled = ir.initially_enabled;
	def.looping = ir.looping;
	def.delay = openage::time::time_t::from_double(ir.delay);
	def.conditions = ir.conditions;
	def.effects = ir.effects;

	// Construct the condition list. Unknown kinds log a
	// warning and are skipped (the IR's error path is the
	// writer, not the runtime).
	std::vector<std::shared_ptr<TriggerCondition>> conditions;
	for (auto &cir : def.conditions) {
		auto area = this->areas_.find(cir.target_area_id);
		if (cir.kind == "AoE2Condition" and not this->original_) throw std::invalid_argument{"Original condition requires scenario context"};
        auto c = cir.kind == "AoE2Condition" ? this->original_->condition(cir) :
            make_trigger_condition(*this->state_, def.delay, cir, area == this->areas_.end() ? nullptr : &area->second);
		if (c) {
			conditions.push_back(c);
		}
	}

	// Construct the effect list. Same skip-on-unknown-kind.
	std::vector<std::shared_ptr<TriggerEffect>> effects;
	for (auto &eir : def.effects) {
		if (eir.kind == "AoE2Effect" and not this->original_) throw std::invalid_argument{"Original effect requires scenario context"};
        auto e = eir.kind == "AoE2Effect" ? this->original_->effect(eir) : make_trigger_effect(eir);
		if (e) {
			e->set_effect_index(effects.size());
			e->set_parent_trigger_id(def.id);
			effects.push_back(e);
		}
		// Auto-register any objective referenced by a
		// SetObjective / CompleteObjective effect so the
		// SetObjectiveStateHandler's set_objective_state() call
		// lands on a known objective (otherwise it returns false
		// and the state stays unset). This keeps the runtime
		// self-contained: the trigger data is the single source
		// of truth for which objectives exist.
		if (eir.kind == "SetObjective" or eir.kind == "CompleteObjective") {
			this->mission_state_.init_objective(eir.target_scenario_object_id);
		}
	}

	// Build the instance. The instance's constructor wires
	// the effect index / parent_trigger_id (idempotent with
	// what we just set, kept here for clarity).
	auto instance = std::make_shared<TriggerInstance>(
		this->loop_, def, conditions, effects);

	// Index the trigger's conditions so the dependency
	// registry can find it on a notification.
	this->index_trigger_conditions(instance);

	this->triggers_[def.id] = instance;
	return def.id;
}


void TriggerRuntime::add_objective(const std::string &objective_id) {
	this->mission_state_.init_objective(objective_id);
}


void TriggerRuntime::index_trigger_conditions(
	const std::shared_ptr<TriggerInstance> &instance) {

	for (auto &c : instance->conditions()) {
		for (auto &key : c->dependencies()) {
			this->dependency_index_[key].push_back(instance);
		}
	}
}


void TriggerRuntime::notify_entity_removed(gamestate::entity_id_t entity_id, time::time_t at) {
	this->notify_entity_changed(entity_id, at);
}

void TriggerRuntime::load_original(const util::json::Value &config, const std::shared_ptr<gamestate::EntityFactory> &factory) {
    this->original_ = std::make_shared<OriginalScenario>(*this, this->state_, factory, config);
}

void TriggerRuntime::schedule_evaluation(TriggerId id, time::time_t at) {
    this->loop_->create_event("trigger.evaluate", this->event_entity_, this->state_, at, {{"trigger_id", static_cast<int64_t>(id)}});
}

void TriggerRuntime::notify_entity_changed(gamestate::entity_id_t entity_id, time::time_t at) {
    if (this->original_) this->original_->changed(at);

	if (not this->loop_ or not this->state_) {
		return;
	}

	DependencyKey key{DependencyKind::ENTITY,
	                  static_cast<uint64_t>(entity_id)};
	auto it = this->dependency_index_.find(key);
	if (it == this->dependency_index_.end()) {
		// No trigger is subscribed to this entity's
		// removal. Silent no-op -- the dependency
		// registry is keyed on the actual
		// (kind, key) pair, and a notification for
		// a key with no subscribers is the normal
		// case.
		return;
	}

	// Schedule a trigger.evaluate event for every
	// subscribed trigger. The events are scheduled
	// with the reference time `at` (the simulation
	// time at which the removal happened); the
	// dispatch order is determined by the openage
	// event loop's strict-weak-ordering Event
	// comparator, which the P4.1.1 commit established.
	for (auto &instance : it->second) {
		openage::event::EventHandler::param_map::map_t params{
			{"trigger_id",
			 static_cast<int64_t>(instance->trigger_id())},
		};
		this->loop_->create_event(
			"trigger.evaluate", this->event_entity_,
			this->state_, at, params);
	}
}


void TriggerRuntime::evaluate_trigger(
	TriggerId trigger_id,
	openage::time::time_t at) {

	auto it = this->triggers_.find(trigger_id);
	if (it == this->triggers_.end()) {
		log::log(MSG(warn) << "TriggerRuntime::evaluate_trigger: "
		                   << "unknown trigger_id " << trigger_id);
		return;
	}
	auto &instance = it->second;

	// WAITING_DELAY: this evaluation is the delayed fire that the
	// runtime scheduled at (notify_time + delay) when the trigger
	// first armed its delay. Re-check the conditions; fire if they
	// still hold, otherwise cancel the wait back to ARMED.
	if (instance->state() == TriggerState::WAITING_DELAY) {
		if (instance->evaluate_conditions(*this->state_, at)) {
			instance->fire(this->loop_, this->event_entity_, this->state_, at);
    if (this->original_) this->original_->complete_objective(trigger_id);
		}
		else {
			instance->cancel_delayed_fire();
		}
		return;
	}

	if (not instance->evaluate_conditions(*this->state_, at)) {
		// Conditions did not all hold; the trigger stays
		// in its current state. No fire, no effects.
		return;
	}

	// Conditions hold. If the delay is non-zero, defer: transition
	// ARMED -> WAITING_DELAY and schedule a trigger.evaluate at
	// (at + delay) so the conditions are re-checked and the trigger
	// fires once the delay has elapsed. Mission 1's defeat_enemy
	// trigger has delay=0, so the common path is the direct fire
	// below.
	if (instance->definition().delay > openage::time::time_t{0}) {
		instance->begin_delayed_fire();
		openage::time::time_t at_delay = at + instance->definition().delay;
		openage::event::EventHandler::param_map::map_t params{
			{"trigger_id",
			 static_cast<int64_t>(instance->trigger_id())},
		};
		this->loop_->create_event(
			"trigger.evaluate", this->event_entity_,
			this->state_, at_delay, params);
		return;
	}

	// Direct fire (delay == 0). Effects are enqueued in effect_index
	// order; the openage event loop dispatches them in
	// strict-weak-ordering by (time, handler_id,
	// entity_fqon, creation_seq), which is the stable
	// order across runs.
	instance->fire(this->loop_, this->event_entity_, this->state_, at);
    if (this->original_) this->original_->complete_objective(trigger_id);
}


std::shared_ptr<TriggerInstance> TriggerRuntime::trigger(
	TriggerId id) const {

	auto it = this->triggers_.find(id);
	if (it == this->triggers_.end()) {
		return nullptr;
	}
	return it->second;
}


// ---------------------------------------------------------------------------
// TriggerEvaluateHandler
// ---------------------------------------------------------------------------


TriggerEvaluateHandler::TriggerEvaluateHandler(
	const std::shared_ptr<TriggerRuntime> &runtime) :
	openage::event::OnceEventHandler{"trigger.evaluate"},
	runtime{runtime} {}


void TriggerEvaluateHandler::setup_event(
	const std::shared_ptr<openage::event::Event> & /* event */,
	const std::shared_ptr<openage::event::State> & /* state */) {
	// No dependencies. The trigger is selected by trigger_id
	// in the params; the handler dispatches synchronously
	// when the event is reached.
}


void TriggerEvaluateHandler::invoke(
	openage::event::EventLoop & /* loop */,
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> &state,
	const openage::time::time_t &time,
	const param_map &params) {

	(void)state;
	if (not this->runtime) {
		return;
	}
	auto tid = static_cast<TriggerId>(
		params.get("trigger_id", static_cast<int64_t>(NULL_TRIGGER_ID)));
	if (tid == NULL_TRIGGER_ID) {
		return;
	}
	this->runtime->evaluate_trigger(tid, time);
}


openage::time::time_t TriggerEvaluateHandler::predict_invoke_time(
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> & /* state */,
	const openage::time::time_t &at) {
	return at;
}


// ---------------------------------------------------------------------------
// SetObjectiveStateHandler
// ---------------------------------------------------------------------------


SetObjectiveStateHandler::SetObjectiveStateHandler(
	const std::shared_ptr<TriggerRuntime> &runtime) :
	openage::event::OnceEventHandler{"set_objective_state"},
	runtime{runtime} {}


void SetObjectiveStateHandler::setup_event(
	const std::shared_ptr<openage::event::Event> & /* event */,
	const std::shared_ptr<openage::event::State> & /* state */) {}


void SetObjectiveStateHandler::invoke(
	openage::event::EventLoop & /* loop */,
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> &state,
	const openage::time::time_t & /* time */,
	const param_map &params) {

	(void)state;
	if (not this->runtime) {
		return;
	}
	auto objective_id = params.get<std::string>("objective_id", std::string{});
	auto target_state = params.get<std::string>("target_state", std::string{});
	if (objective_id.empty() or target_state.empty()) {
		// Contract: required-lookup failure must NOT be
		// silent. Log + record the diagnostic so the
		// operator can find the offending trigger.
		log::log(MSG(err) << "SetObjectiveStateHandler: empty "
		                   << "objective_id or target_state; "
		                   << "ignoring event");
		return;
	}
	ObjectiveState new_state = ObjectiveState::INACTIVE;
	if (target_state == "INACTIVE") {
		new_state = ObjectiveState::INACTIVE;
	}
	else if (target_state == "ACTIVE") {
		new_state = ObjectiveState::ACTIVE;
	}
	else if (target_state == "COMPLETED") {
		new_state = ObjectiveState::COMPLETED;
	}
	else {
		log::log(MSG(err) << "SetObjectiveStateHandler: unknown "
		                   << "target_state '" << target_state
		                   << "'; ignoring event");
		return;
	}
	this->runtime->mission_state().set_objective_state(objective_id, new_state);
}


openage::time::time_t SetObjectiveStateHandler::predict_invoke_time(
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> & /* state */,
	const openage::time::time_t &at) {
	return at;
}


// ---------------------------------------------------------------------------
// ActivateTriggerHandler
// ---------------------------------------------------------------------------


ActivateTriggerHandler::ActivateTriggerHandler(
	const std::shared_ptr<TriggerRuntime> &runtime) :
	openage::event::OnceEventHandler{"activate_trigger"},
	runtime{runtime} {}


void ActivateTriggerHandler::setup_event(
	const std::shared_ptr<openage::event::Event> & /* event */,
	const std::shared_ptr<openage::event::State> & /* state */) {}


void ActivateTriggerHandler::invoke(
	openage::event::EventLoop & /* loop */,
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> &state,
	const openage::time::time_t & /* time */,
	const param_map &params) {

	(void)state;
	if (not this->runtime) {
		return;
	}
	auto target_trigger_id_str = params.get<std::string>(
		"target_trigger_id", std::string{});
	if (target_trigger_id_str.empty()) {
		log::log(MSG(err) << "ActivateTriggerHandler: empty "
		                   << "target_trigger_id; ignoring event");
		return;
	}
	TriggerId tid = make_trigger_id(target_trigger_id_str);
	auto t = this->runtime->trigger(tid);
	if (not t) {
		log::log(MSG(err) << "ActivateTriggerHandler: unknown "
		                   << "trigger_id '" << target_trigger_id_str
		                   << "'; ignoring event");
		return;
	}
	t->set_enabled(true);
}


openage::time::time_t ActivateTriggerHandler::predict_invoke_time(
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> & /* state */,
	const openage::time::time_t &at) {
	return at;
}


// ---------------------------------------------------------------------------
// DeclareVictoryHandler
// ---------------------------------------------------------------------------


DeclareVictoryHandler::DeclareVictoryHandler(
	const std::shared_ptr<TriggerRuntime> &runtime) :
	openage::event::OnceEventHandler{"declare_victory"},
	runtime{runtime} {}


void DeclareVictoryHandler::setup_event(
	const std::shared_ptr<openage::event::Event> & /* event */,
	const std::shared_ptr<openage::event::State> & /* state */) {}


void DeclareVictoryHandler::invoke(
	openage::event::EventLoop & /* loop */,
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> &state,
	const openage::time::time_t & /* time */,
	const param_map &params) {

	(void)state;
	if (not this->runtime) {
		return;
	}
	auto reason = params.get<std::string>("reason", std::string{});
	this->runtime->mission_state().declare_victory(reason);
}


openage::time::time_t DeclareVictoryHandler::predict_invoke_time(
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> & /* state */,
	const openage::time::time_t &at) {
	return at;
}


}  // namespace openage::scenario::trigger
