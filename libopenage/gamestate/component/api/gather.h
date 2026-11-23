// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <unordered_map>

#include "gamestate/component/api_component.h"
#include "gamestate/component/types.h"


namespace openage::gamestate::component {

/**
 * Gather abilities keyed by the resource spot they target. A unit may have
 * several abilities with different collection speeds and carrying capacities.
 */
class Gather final : public APIComponent {
public:
	using APIComponent::APIComponent;

	component_t get_type() const override;
	void add_ability(const nyan::Object &ability);
	const nyan::Object *find_ability(const nyan::fqon_t &spot) const;

	uint64_t order{0};
	int64_t carried_amount{0};
	nyan::fqon_t carried_resource;
	nyan::fqon_t carried_container;

private:
	std::unordered_map<nyan::fqon_t, nyan::Object> abilities;
};

} // namespace openage::gamestate::component
