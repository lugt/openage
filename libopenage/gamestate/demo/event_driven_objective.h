// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {


/**
 * Demo 14 - P4.1 event-driven Mission 1 objective.
 *
 * Pins the new behavior described in docs/mvp/reports/
 * p4.1-event-driven-objective.md.  See the .cpp file for the full
 * pin list (1-13).
 *
 * This is the GREEN gate for P4.1: the demo fails until
 * ObjectiveController::notify_entity_removed, ObjectiveRemovalHandler,
 * and the polling-removal work are all in place.
 */
void simulation_demo_14(const util::Path &path);


} // namespace openage::gamestate::tests
