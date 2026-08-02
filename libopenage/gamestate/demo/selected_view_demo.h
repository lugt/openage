// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {


/**
 * Demo 22 - P4.4A SelectedEntityView + MissionView contract test.
 *
 * Boots Mission 1, builds SelectedEntityView for the PlayerVillager
 * and MissionView for the runtime's MissionState, then asserts the
 * ViewModel contract. The contract is the minimum the HUD depends
 * on; every field is read from the authoritative simulation
 * component (Live Health curve, component::Attack, Ownership,
 * Position, MissionState). If any field is hard-coded or demo
 * data, this test will fail.
 */
void simulation_demo_22(const util::Path &path);


} // namespace openage::gamestate::tests