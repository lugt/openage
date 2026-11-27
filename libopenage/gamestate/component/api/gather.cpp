// Copyright 2026 the openage authors. See copying.md for legal info.

#include "gather.h"

#include "error/error.h"
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
	if (found != this->abilities.end()) {
		return &found->second;
	}
	const auto view = this->get_ability().get_view();
	const auto definition = view->get_object(spot);
	const nyan::Object *result = nullptr;
	nyan::fqon_t selected;
	for (const auto &[target, ability] : this->abilities) {
		if (not definition.extends(target)) {
			continue;
		}
		if (result and view->get_object(selected).extends(target)) {
			continue;
		}
		if (result and not view->get_object(target).extends(selected)) {
			throw Error{MSG(err) << "Ambiguous gather targets for resource spot " << spot};
		}
		selected = target;
		result = &ability;
	}
	return result;
}

} // namespace openage::gamestate::component
