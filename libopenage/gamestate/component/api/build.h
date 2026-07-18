// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "gamestate/component/api_component.h"
#include "gamestate/component/types.h"


namespace openage::gamestate::component {

/**
 * Component backing the \p engine.ability.type.Constructable and
 * \p engine.ability.type.Foundation nyan abilities.
 *
 * A building slot gains both:
 *   - Foundation at placement time (it is a phantom tile)
 *   - Constructable once construction completes and the entity can take
 *     its real place in the world
 *
 * Sharing a single component class keeps the construction pipeline
 * (villager targets foundation -> progress advances -> foundation
 * transforms into the Constructable building) symmetrical: the same
 * component is on the entity the whole time, only its progress curve
 * and "is constructed" flag change.
 *
 * Progress is stored as a 0..1 float curve so the activity graph can
 * advance it via curve::Continuous<float> (same primitive training and
 * research reuse in Phase C/D).
 */
class Build final : public APIComponent {
public:
	using APIComponent::APIComponent;

	component_t get_type() const override;
};

} // namespace openage::gamestate::component
