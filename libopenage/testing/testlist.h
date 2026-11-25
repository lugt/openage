// Copyright 2014-2024 the openage authors. See copying.md for legal info.

#pragma once


// pxd: from libcpp.string cimport string
#include <string>

// pxd: from libcpp.vector cimport vector
#include <vector>

#include "testing.h"


namespace openage {
namespace testing {


/**
 * Invokes the test or demo with the given name.
 *
 * pxd: void run_method(string name) except +
 */
OAAPI void run_method(const std::string &name);


/**
 * Run the asset-free snapshot scenario and its save/restore counterpart.
 *
 * pxd: vector[string] run_snapshot_scenario(const string &scenario) except +
 */
OAAPI std::vector<std::string> run_snapshot_scenario(const std::string &scenario);


} // namespace testing
} // namespace openage
