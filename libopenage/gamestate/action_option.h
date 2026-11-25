// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <map>
#include <nyan/nyan.h>

namespace openage::gamestate {

enum class ActionKind { Build, Train, Research, Transform };

struct ActionOption {
	ActionKind kind;
	nyan::fqon_t id;
	nyan::fqon_t subject;
	std::map<nyan::fqon_t, int64_t> cost;
	double seconds;
};

} // namespace openage::gamestate
