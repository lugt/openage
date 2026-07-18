// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "gamestate/component/api_component.h"
#include "gamestate/component/types.h"


namespace openage::gamestate::component {

/**
 * Component backing the \p engine.ability.type.Create and
 * \p engine.ability.type.ProductionQueue nyan abilities (defining what
 * can be produced and how), distinct from \p Train which carries the
 * currently-active training slot (runtime progress).
 *
 * Initially created in the same code path as Train; both share the
 * ability object but Production owns the read-only "what can this
 * building queue" data (CreatableGameEntity set, queue size, build
 * modes) while Train owns the per-tick progress curve.
 */
class Production final : public APIComponent {
public:
	using APIComponent::APIComponent;

	component_t get_type() const override;
};

} // namespace openage::gamestate::component
