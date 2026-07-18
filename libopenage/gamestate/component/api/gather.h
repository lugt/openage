// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "gamestate/component/api_component.h"
#include "gamestate/component/types.h"


namespace openage::gamestate::component {

/**
 * Component backing the \p engine.ability.type.Gather nyan ability.
 *
 * Carries the gatherer state machine (current target ResourceSpot, carried
 * cargo count, return intent). Behavioural transitions are driven from the
 * GATHER / GATHER_PICKUP / RETURN_TO_DROP / DROP_RESOURCES systems listed in
 * gamestate/system/types.h via the activity graph attached to the entity's
 * \p Activity ability.
 *
 * Tutorial simplification: gather_rate and container capacity are read from
 * the nyan ability once at construction; per-cycle bookkeeping uses the
 * carrying integer below.
 */
class Gather final : public APIComponent {
public:
	using APIComponent::APIComponent;

	component_t get_type() const override;

	/**
	 * Units of resource currently being carried by the gatherer.
	 *
	 * Set by GATHER_PICKUP and cleared by DROP_RESOURCES; the actual
	 * stockpile credit lives on Player.resources. Keeping the per-unit
	 * count here avoids a round trip to Player on every harvester tick.
	 */
	int64_t carried_amount{0};
};

} // namespace openage::gamestate::component
