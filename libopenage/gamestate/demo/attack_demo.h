// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage {
namespace gamestate {
namespace tests {

/**
 * Headless AttackCommand integration test (demo id 5).
 *
 * Built up phase by phase alongside the AttackCommand vertical slice:
 *   - Phase 1 (this): unit-test the AttackCommand class (type=ATTACK,
 *     target round-trip) and smoke-test that SendCommandHandler's ATTACK
 *     branch enqueues without crashing. AttackSystem is not wired yet, so
 *     the activity graph's Move node pops and drops the command; the smoke
 *     only asserts the dispatch path runs and the sim stays alive.
 *   - Later phases extend this demo to assert approach/damage/death/victory
 *     as those systems land.
 */
void simulation_demo_5(const util::Path &path);

}}} // openage::gamestate::tests
