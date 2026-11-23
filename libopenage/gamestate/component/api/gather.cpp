// Copyright 2026 the openage authors. See copying.md for legal info.

#include "gather.h"

#include "gamestate/component/types.h"


namespace openage::gamestate::component {

component_t Gather::get_type() const {
	return component_t::GATHER;
}

void Gather::add_ability(const nyan::Object &ability) {
	for (const auto &value : ability.get_set("Gather.targets")) {
		auto ref = std::dynamic_pointer_cast<nyan::ObjectValue>(value.get_ptr());
		this->abilities.emplace(ref->get_name(), ability);
	}
}

const nyan::Object *Gather::find_ability(const nyan::fqon_t &spot) const {
	auto found = this->abilities.find(spot);
	return found == this->abilities.end() ? nullptr : &found->second;
}

} // namespace openage::gamestate::component
