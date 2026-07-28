// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Demo 10 — Mission 1 lifecycle characterization (P3.1).

#pragma once

#include "../../util/compiler.h"
// pxd: from libopenage.util.path cimport Path


namespace openage {

namespace util {
class Path;
}

namespace gamestate::tests {

void simulation_demo_10(const util::Path &path);

}  // namespace gamestate::tests
}  // namespace openage