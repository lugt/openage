// Copyright 2026 the openage authors. See copying.md for legal info.

#include "resource_storage.h"

#include "gamestate/component/types.h"


namespace openage::gamestate::component {

component_t ResourceStorage::get_type() const {
	return component_t::RESOURCE_STORAGE;
}

} // namespace openage::gamestate::component
