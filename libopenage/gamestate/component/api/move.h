// Copyright 2021-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>

#include "gamestate/component/api_component.h"
#include "gamestate/component/types.h"


namespace openage::gamestate::component {

class Move final : public APIComponent {
public:
	using APIComponent::APIComponent;

	component_t get_type() const override;

	/** Invalidates scheduled replans when another movement or stop replaces them. */
	uint64_t generation = 0;
};

} // namespace openage::gamestate::component
