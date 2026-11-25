// Copyright 2026+ the openage authors. See copying.md for legal info.

#pragma once

#include <string>
#include <vector>

namespace openage::util {
class Path;
}

namespace openage::gamestate::tests {

std::string run_wallace_golden(const util::Path &path, const std::string &scenario_json, std::vector<double> &timings);

} // namespace openage::gamestate::tests
