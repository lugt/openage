// Copyright 2026 the openage authors. See copying.md for legal info.

#include "gather.h"

#include "gamestate/component/types.h"


namespace openage::gamestate::component {

component_t Gather::get_type() const {
	return component_t::GATHER;
}

} // namespace openage::gamestate::component
