// Copyright 2026 the openage authors. See copying.md for legal info.

#include "production.h"

#include "gamestate/component/types.h"


namespace openage::gamestate::component {

component_t Production::get_type() const {
	return component_t::PRODUCTION;
}

} // namespace openage::gamestate::component
