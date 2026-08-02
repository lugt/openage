// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.2 trigger runtime: runtime-facing identifiers and the
// TriggerDefinition / TriggerInstance / TriggerState / Condition /
// Effect public types.
//
// The data-driven trigger runtime sits between the
// gamestate::MissionTriggerDefinition (Scenario IR, immutable per
// load) and the gamestate event system. The IR types are
// REUSED here -- the runtime adds:
//   * TriggerInstance: the per-Trigger runtime object
//   * TriggerState: the state machine
//   * TriggerId: a stable id derived from the trigger_id string
//   * DependencyKey: the stable key used by the registry to
//     subscribe handlers to dependency notifications
//   * ConditionKind / EffectKind: the string-literal dispatch
//     keys used by the registry to construct concrete
//     Condition / Effect instances from a MissionTrigger* IR
//     record.
//
// No AoE2 DE parser-specific field is allowed in this file. The
// IR may carry parser-origin strings (target_scenario_object_id
// is the ScenarioObjectId FQON after load), but the runtime
// only sees the stable normalized form.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "scenario/mission_pack.h"
#include "time/time.h"


namespace openage::scenario::trigger {


/**
 * Stable integer id assigned to a Trigger at scenario load. The
 * id is the 64-bit FNV-1a hash of the trigger_id string; it is
 * NEVER zero, so `0` can be used as a sentinel / null. The
 * mapping is process-wide and stable: the same trigger_id
 * always gets the same id, so dependency keys that use the
 * id are platform-independent and pointer-free.
 */
using TriggerId = uint64_t;

/**
 * Reserved sentinel for "no trigger". Use this instead of
 * literal `0` so the magic value is self-documenting.
 */
constexpr TriggerId NULL_TRIGGER_ID = 0;


/**
 * String-literal kind for a Condition. Adding a new kind is a
 * one-line addition here plus a new subclass in
 * trigger_condition.h and a single line in the registry.
 */
enum class ConditionKind : uint8_t {
	ENTITY_REMOVED = 1,
	TIMER_ELAPSED = 2,
	TRIGGER_STATE = 3,
};


/**
 * String-literal kind for an Effect. Same convention as
 * ConditionKind.
 */
enum class EffectKind : uint8_t {
	SET_OBJECTIVE_STATE = 1,
	ACTIVATE_TRIGGER = 2,
	DECLARE_VICTORY = 3,
};


/**
 * The state machine a TriggerInstance is in. State transitions:
 *
 *   DISABLED  --(set enabled)-->          ARMED
 *   ARMED     --(dependency fires,
 *                all conditions true,
 *                no delay)-->             FIRED
 *   ARMED     --(dependency fires,
 *                all conditions true,
 *                delay > 0)-->             WAITING_DELAY
 *   WAITING_DELAY --(delay elapsed,
 *                     conditions still
 *                     true)-->              FIRED
 *   FIRED     --(non-looping)-->          COMPLETED
 *   FIRED     --(looping + re-armed)-->   ARMED
 *
 * The state is a plain enum, not a bitfield. Transitions are
 * driven by the trigger evaluation event; there is no per-tick
 * poll and no hidden state.
 */
enum class TriggerState : uint8_t {
	DISABLED = 0,
	ARMED = 1,
	WAITING_DELAY = 2,
	FIRED = 3,
	COMPLETED = 4,
};


/**
 * Public alias of the Scenario IR Condition. The runtime does
 * NOT take ownership of the IR definition; it copies the
 * fields it needs into a TriggerCondition subclass. The
 * MissionTrigger* structs are documented in mission_pack.h.
 */
using IRTriggerDefinition = gamestate::MissionTriggerDefinition;
using IRTriggerCondition = gamestate::MissionTriggerCondition;
using IRTriggerEffect = gamestate::MissionTriggerEffect;


/**
 * Runtime-facing snapshot of a trigger that was loaded from
 * the IR. The MissionTriggerDefinition in the IR is immutable;
 * the runtime constructs one of these per loaded trigger and
 * uses it as the constructor argument for the
 * TriggerCondition / TriggerEffect subclasses.
 */
struct TriggerDefinition {
	TriggerId id{NULL_TRIGGER_ID};
	std::string trigger_id;            // original IR string
	bool initially_enabled{true};
	bool looping{false};
	time::time_t delay{time::time_t{0}};
	std::vector<IRTriggerCondition> conditions;
	std::vector<IRTriggerEffect> effects;
};


/**
 * The per-load trigger runtime object. One per IR trigger.
 * Lives in the GameSimulation (or the Scenario subsystem)
 * and is registered with the dependency registry at
 * construction.
 */
class TriggerInstance;
using TriggerInstanceRef = std::shared_ptr<TriggerInstance>;


/**
 * Stable id derivation. NOT a hash of a pointer; FNV-1a of the
 * trigger_id string. The same trigger_id always maps to the
 * same id, regardless of allocation order or platform.
 */
TriggerId make_trigger_id(const std::string &trigger_id);


}  // namespace openage::scenario::trigger
