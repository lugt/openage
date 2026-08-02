// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {


/**
 * Demo 17 - P4.2 trigger evaluation, single-shot (TDD GREEN).
 *
 * Pins the full event-driven dispatch path of the TriggerRuntime:
 *
 *   1. A freshly added trigger (initially_enabled, delay=0) starts in
 *      ARMED; the mission is not yet victorious.
 *   2. Removal of a NON-target entity + notify + reach_time does NOT
 *      fire the trigger (it stays ARMED; no false victory).
 *   3. Removal of the target entity + notify_entity_removed +
 *      reach_time dispatches trigger.evaluate -> conditions hold ->
 *      fire -> DeclareVictory effect -> MissionState VICTORY. The
 *      trigger transitions ARMED -> FIRED -> COMPLETED.
 *   4. A duplicate notify + reach_time does NOT re-fire (fire_count
 *      stays 1; the COMPLETED gate holds).
 *
 * The demo drives the event loop manually with reach_time() (the
 * GameSimulation::run() loop is never entered), proving the
 * trigger.evaluate event and its DeclareVictory effect are both
 * dispatched within a single reach_time window.
 */
void simulation_demo_17(const util::Path &path);


} // namespace openage::gamestate::tests
