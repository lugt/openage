// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {


/**
 * Demo 18 - P4.2 trigger delay (TDD GREEN).
 *
 * Pins the delay path of the TriggerRuntime state machine:
 *
 *   1. A trigger with delay > 0 whose conditions hold enters
 *      WAITING_DELAY (NOT FIRED) when the dependency notification
 *      is dispatched. The mission is not yet victorious.
 *   2. Advancing the clock to BEFORE (notify_time + delay) does
 *      NOT fire the trigger (it stays WAITING_DELAY).
 *   3. Advancing the clock to (notify_time + delay) dispatches the
 *      delayed trigger.evaluate, re-checks the conditions, and
 *      fires -> COMPLETED + VICTORY. fire_count == 1.
 *
 * The delay is relative to the notification time (notify_time +
 * delay), not an absolute clock value; the demo uses notify_time=10
 * and delay=5 so the delayed fire is due at time 15.
 */
void simulation_demo_18(const util::Path &path);


} // namespace openage::gamestate::tests
