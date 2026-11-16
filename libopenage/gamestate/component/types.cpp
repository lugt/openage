// Copyright 2021-2023 the openage authors. See copying.md for legal info.

#include "types.h"

namespace openage::gamestate::component {


const char *to_string(component_t type) {
	switch (type) {
	case component_t::POSITION:
		return "Position";
	case component_t::COMMANDQUEUE:
		return "CommandQueue";
	case component_t::OWNERSHIP:
		return "Ownership";
	case component_t::ACTIVITY:
		return "Activity";
	case component_t::IDLE:
		return "Idle";
	case component_t::TURN:
		return "Turn";
	case component_t::MOVE:
		return "Move";
	case component_t::SELECTABLE:
		return "Selectable";
	case component_t::LIVE:
		return "Live";
	case component_t::ATTACK:
		return "Attack";
	case component_t::GATHER:
		return "Gather";
	case component_t::BUILD:
		return "Build";
	case component_t::TRAIN:
		return "Train";
	case component_t::PRODUCTION:
		return "Production";
	case component_t::RESOURCE_STORAGE:
		return "ResourceStorage";
	}
	return "Unknown";
}


} // namespace openage::gamestate::component
