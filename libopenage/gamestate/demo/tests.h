// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#pragma once

#include <string>
#include <vector>
#include "../../util/compiler.h"
// pxd: from libcpp.string cimport string
// pxd: from libcpp.vector cimport vector
// pxd: from libopenage.util.path cimport Path


namespace openage {
namespace util {
class Path;
} // namespace util

namespace gamestate::tests {

// pxd: void simulation_demo(int demo_id, Path path) except +
OAAPI void simulation_demo(int demo_id, const util::Path &path);

// pxd: string run_wallace_golden(Path path, string scenario_json, vector[double]& timings) except +
OAAPI std::string run_wallace_golden(const util::Path &path, const std::string &scenario_json, std::vector<double> &timings);

} // namespace gamestate::tests
} // namespace openage
