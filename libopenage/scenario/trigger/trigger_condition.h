// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.2 trigger runtime: the TriggerCondition interface and
// the three concrete implementations required by the Mission 1
// MVP (EntityRemoved, TimerElapsed, TriggerState).
//
// A TriggerCondition is a pure function (state, time) -> bool.
// It is constructed from a MissionTriggerCondition IR record
// (the target_scenario_object_id / target_area_id /
// target_trigger_id / target_player_id fields carry the
// stable identity the condition binds to). The condition holds
// the resolved runtime handle (entity id, area id, trigger
// id) so evaluate() does not need to re-resolve on every call.
//
// Conditions are NOT GameState subscribers. They evaluate
// only when the TriggerEvaluateHandler calls evaluate() on
// them. There is no per-tick poll and no hidden state.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gamestate/game_state.h"
#include "scenario/mission_pack.h"
#include "scenario/trigger/trigger_definition.h"
#include "time/time.h"


namespace openage::scenario::trigger {


/**
 * Stable identity for a dependency. The dependency registry
 * uses this to map dependency notifications to the affected
 * TriggerConditions / TriggerInstances.
 *
 * The registry is keyed by (DependencyKind, key). The key
 * is a stable integer (entity_id_t, trigger_id, etc.) so the
 * map is hash-stable across runs and platforms. The
 * DependencyKind tag is required because the same integer
 * could mean different things in different contexts (entity
 * 0 is a placeholder, not a stable identity).
 */
enum class DependencyKind : uint8_t {
	ENTITY = 1,
	TRIGGER = 2,
	TIMER = 3,
};


struct DependencyKey {
	DependencyKind kind;
	uint64_t key;

	bool operator==(const DependencyKey &other) const {
		return kind == other.kind && key == other.key;
	}
};


/**
 * Hash for DependencyKey so the registry can use unordered_map.
 * The hash is a simple FNV-1a-mix of (kind, key); it is
 * platform-independent because both fields are integers.
 */
struct DependencyKeyHash {
	size_t operator()(const DependencyKey &k) const noexcept;
};


/**
 * The abstract condition interface. Implementations are
 * constructed by the registry from a MissionTriggerCondition
 * IR record; they hold their resolved runtime handle and
 * answer evaluate() purely.
 */
class TriggerCondition {
public:
	virtual ~TriggerCondition() = default;

	/**
	 * Evaluate the condition at simulation time `at` against
	 * the given GameState. Pure: no side effects, no
	 * subscription, no logging.
	 */
	virtual bool evaluate(
		const openage::gamestate::GameState &state,
		openage::time::time_t at) const = 0;

	/**
	 * Return the set of dependency keys this condition is
	 * sensitive to. The registry subscribes the condition to
	 * notifications on these keys; a notification for any
	 * other key does NOT cause this condition to be
	 * re-evaluated.
	 */
	virtual std::vector<DependencyKey> dependencies() const = 0;
};


/**
 * COND_KIND_ENTITY_REMOVED. The condition is true if the
 * entity identified by target_scenario_object_id is no longer
 * in the GameState. The IR record's target_scenario_object_id
 * is the placed-instance FQON; the constructor resolves it
 * once via the GameState's ScenarioObjectId -> entity_id map
 * (or stores the unresolved FQON for a later re-resolve; the
 * registry guarantees a re-resolve before the first evaluate
 * call).
 */
class EntityRemovedCondition : public TriggerCondition {
public:
	EntityRemovedCondition(
		const openage::gamestate::GameState &state,
		const gamestate::MissionTriggerCondition &ir);

	bool evaluate(
		const openage::gamestate::GameState &state,
		openage::time::time_t at) const override;

	std::vector<DependencyKey> dependencies() const override;

	/**
	 * Target ScenarioObjectId FQON. The constructor resolves
	 * it to an entity_id_t; if the resolution fails the
	 * condition is permanently false (defensive: an
	 * unresolved FQON is a Scenario loader bug, not a runtime
	 * condition that can become true).
	 */
	const std::string &target_scenario_object_id() const {
		return this->target_scenario_object_id_;
	}

private:
	std::string target_scenario_object_id_;
	int64_t target_entity_id_{-1};   // -1 = unresolved
};


/**
 * COND_KIND_TIMER_ELAPSED. The condition is true at simulation
 * time `at` iff `at >= delay` (the delay is on the trigger
 * definition; the condition fires once that delay has
 * elapsed). Single-shot: it remains true forever after the
 * first time. Re-arm via the parent trigger's
 * re_arm() / set_enabled() cycle, which resets the trigger's
 * state.
 */
class TimerElapsedCondition : public TriggerCondition {
public:
	explicit TimerElapsedCondition(
		openage::time::time_t trigger_delay);

	bool evaluate(
		const openage::gamestate::GameState &state,
		openage::time::time_t at) const override;

	std::vector<DependencyKey> dependencies() const override;

private:
	openage::time::time_t trigger_delay;
	mutable bool fired_{false};
};


/**
 * Function the registry installs so TriggerStateCondition can ask
 * "what state is trigger X in?" without taking a hard back-reference
 * to the TriggerRuntime (which would create a header cycle). The
 * registry sets this on every TriggerStateCondition it constructs;
 * if it is unset the condition is permanently false (defensive).
 */
using TriggerStateProvider = std::function<TriggerState(TriggerId)>;


/**
 * COND_KIND_TRIGGER_STATE. The condition is true iff the
 * target trigger (by trigger_id string) is in the target
 * state. Used for triggers that depend on the activation of
 * another trigger (the ActivateTriggerEffect + TriggerState
 * condition chain).
 */
class TriggerStateCondition : public TriggerCondition {
public:
	TriggerStateCondition(
		TriggerId target_trigger_id,
		TriggerState target_state);

	bool evaluate(
		const openage::gamestate::GameState &state,
		openage::time::time_t at) const override;

	std::vector<DependencyKey> dependencies() const override;

	TriggerId target_trigger_id() const { return this->target_trigger_id_; }
	TriggerState target_state() const { return this->target_state_; }

	/**
	 * Install the registry-backed state lookup. Called by the
	 * registry after construction.
	 */
	void set_state_provider(TriggerStateProvider provider) {
		this->provider = std::move(provider);
	}

private:
	TriggerId target_trigger_id_;
	TriggerState target_state_;
	TriggerStateProvider provider;
};


/**
 * Registry helper: construct a TriggerCondition from an IR
 * record. Returns nullptr if the IR record's kind is unknown
 * to the runtime. The kind string in the IR maps to a
 * ConditionKind via a small dispatch table; the runtime does
 * NOT consult the AoE2 DE parser.
 */
std::shared_ptr<TriggerCondition> make_trigger_condition(
	const openage::gamestate::GameState &state,
	openage::time::time_t trigger_delay,
	const gamestate::MissionTriggerCondition &ir);


}  // namespace openage::scenario::trigger
