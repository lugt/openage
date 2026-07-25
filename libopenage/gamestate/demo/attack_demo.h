// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage {
namespace gamestate {
namespace tests {

/**
 * Headless AttackCommand integration test (demo id 5).
 *
 * Built up increment by increment alongside the event-driven AttackSystem
 * (Phase 2, Option 1: AttackCommand/AttackSystem manage continuous attack
 * behavior; ApplyDiscreteEffect executes single-hit effects):
 *   - Increment 1 (this): unit-test the AttackCommand class (type=ATTACK,
 *     attacker/target round-trip) and smoke-test the event-driven dispatch
 *     path -- SendCommandHandler enqueues + emits "game.attack", AttackHandler
 *     pops the command before the activity graph runs (no Idle->CheckQueue
 *     loop). The smoke only asserts the dispatch path runs and the sim stays
 *     alive; range/damage/death land in later increments.
 *   - Later increments extend this demo to assert approach/damage/death/victory
 *     as those systems land.
 */
void simulation_demo_5(const util::Path &path);

}}} // openage::gamestate::tests
