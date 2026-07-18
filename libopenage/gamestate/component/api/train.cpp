// Copyright 2026 the openage authors. See copying.md for legal info.

#include "train.h"

#include "gamestate/component/types.h"


namespace openage::gamestate::component {

component_t Train::get_type() const {
	return component_t::TRAIN;
}

} // namespace openage::gamestate::component
