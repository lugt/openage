// Copyright 2026 the openage authors. See copying.md for legal info.

#include "build.h"

#include "gamestate/component/types.h"


namespace openage::gamestate::component {

component_t Build::get_type() const {
	return component_t::BUILD;
}

} // namespace openage::gamestate::component
