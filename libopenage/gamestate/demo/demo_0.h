// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {

/**
 * Show the initialization of a game instance.
 */
void simulation_demo_0(const util::Path &path);

/**
 * William Wallace Mission 1 -- Phase H0 minimal headless regression.
 *
 * Loads the Mission 1 scenario via the standard runtime initialization
 * path, resolves the three PlacedEntities by their stable instance
 * FQONs, dispatches a single ATTACK against EnemyVillager through
 * the existing send_command event channel, advances the simulation
 * tick-by-tick (NO fixed tick, NO sleep), and asserts both the
 * entity-removal and the objective-finished stop conditions before
 * the deadline. A stable, pointer-free snapshot is emitted on any
 * assertion failure.
 *
 * Headless: this demo never touches Presenter, Qt, or any OpenGL
 * context, so it is safe to run on a CI box with no DISPLAY/GPU.
 */
void simulation_demo_7(const util::Path &path);

} // namespace openage::gamestate::tests
