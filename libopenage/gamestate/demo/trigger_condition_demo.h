// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {


/**
 * Demo 16 - P4.2 EntityRemovedCondition (TDD GREEN).
 *
 * Pins the trigger runtime's first concrete Condition:
 *
 *   1. A condition bound to an EXISTING named entity evaluates
 *      false (the entity is still in the GameState index).
 *   2. After the entity is removed from the GameState, the same
 *      condition evaluates true -- the condition is a pure
 *      (state, time) -> bool read, no event subscription needed
 *      for evaluate().
 *   3. A condition bound to an unresolved FQON (no spawned
 *      instance) is permanently false; it does not crash and does
 *      not become true after an unrelated removal.
 *   4. The condition's dependencies() expose the resolved entity
 *      DependencyKey so the registry can subscribe it to the
 *      right removal notifications; an unresolved condition has
 *      no dependencies.
 *   5. Removing a DIFFERENT entity (PlayerVillager) does not make
 *      the EnemyVillager condition true -- the condition is
 *      identity-bound, not "any entity removed".
 *
 * This is a unit test of the Condition interface; it does NOT
 * exercise the event loop or the TriggerRuntime dispatcher.
 */
void simulation_demo_16(const util::Path &path);


} // namespace openage::gamestate::tests
