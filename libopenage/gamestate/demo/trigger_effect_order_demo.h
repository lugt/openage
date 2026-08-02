// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {


/**
 * Demo 19 - P4.2 effect ordering (TDD GREEN).
 *
 * Pins the multi-effect dispatch of the TriggerRuntime:
 *
 *   1. A trigger with THREE effects -- SetObjective(ACTIVE),
 *      CompleteObjective, DeclareVictory -- fires all
 *      three when its condition holds.
 *   2. The two objective effects are dispatched in stable
 *      effect_index order: SetObjective(ACTIVE) (index 0) then
 *      CompleteObjective (index 1), so the objective's FINAL
 *      state is COMPLETED. If the order were reversed the final
 *      state would be ACTIVE, so this pin is an ordering oracle,
 *      not just a "both ran" check.
 *   3. DeclareVictory (index 2) fires, so the mission is victorious.
 *   4. fire_count == 1 (a single trigger fire dispatched all three
 *      effects; no duplicate fire).
 *
 * The stable order is provided by the openage event loop's
 * Event::operator< (time, handler_id, entity_fqon, creation_seq --
 * established in P4.1.1) combined with the runtime enqueueing effects
 * in effect_index order, which assigns a monotonic creation_seq.
 */
void simulation_demo_19(const util::Path &path);


} // namespace openage::gamestate::tests
