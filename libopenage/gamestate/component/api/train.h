// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "gamestate/component/api_component.h"
#include "gamestate/component/types.h"


namespace openage::gamestate::component {

/**
 * Component carrying the in-progress training command on a building.
 *
 * Lives on entities with the \p engine.ability.type.Create or
 * \p ProductionQueue ability. Holds the currently active Creatable
 * FQON and its remaining build time; on completion the TRAIN system
 * consumes the cost already paid, calls SpawnEntityHandler to create
 * the new entity, and clears the slot so the next item can queue.
 *
 * The slot count and queue semantics (full RTS has a multi-slot
 * ProductionQueue) are deliberately skipped: the tutorial uses a
 * single-slot production so this component only ever holds one item
 * at a time.
 */
class Train final : public APIComponent {
public:
	using APIComponent::APIComponent;

	component_t get_type() const override;
};

} // namespace openage::gamestate::component
