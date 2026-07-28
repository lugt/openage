// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Demo 9 — Mission Pack native integration (P2.7).
//
// Validates that the C++ production loader wires a Mission Pack into
// the production GameState. The demo is registered as
// `simulation_demo_9` and invoked via:
//
//     bin/run test --demo gamestate.tests.simulation_demo 9
//
// See `ww_mission1_native_load.cpp` for the full assertion list.

#pragma once

#include "../../util/compiler.h"
// pxd: from libopenage.util.path cimport Path


namespace openage {

namespace util {
class Path;
}

namespace gamestate::tests {

void simulation_demo_9(const util::Path &path);

}  // namespace gamestate::tests
}  // namespace openage