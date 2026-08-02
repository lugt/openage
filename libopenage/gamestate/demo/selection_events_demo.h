// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {


/**
 * Demo 23 - P4.4A SelectionModel event wiring.
 *
 * Asserts the four ViewModel events fire from the real simulation
 * sources:
 *   - SelectionChanged on set_selected, and on removal of the
 *     selected entity (safe clear).
 *   - HealthChanged when the selected entity's Health attribute
 *     is written.
 *   - EntityRemoved for every removal.
 *   - MissionStateChanged when the runtime declares victory.
 */
void simulation_demo_23(const util::Path &path);


} // namespace openage::gamestate::tests