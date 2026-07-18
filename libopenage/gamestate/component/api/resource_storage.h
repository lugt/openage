// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <unordered_map>

#include "gamestate/component/api_component.h"
#include "gamestate/component/types.h"

#include "nyan/nyan.h"


namespace openage::gamestate::component {

/**
 * Component backing the \p engine.ability.type.DropResources nyan
 * ability.
 *
 * Acts as the entity-side buffer for in-transit resource cargo (e.g.
 * a villager carrying wood from a tree to a Town Center, or a Trade
 * Cart on the way to an allied market). The actual settlement path is:
 *
 *   pickup   -> component.carried_amount += rate
 *   walk back
 *   drop     -> player.resources[type_fqon] += component.carried_amount,
 *               component.carried_amount = 0
 *
 * Splitting the buffer onto the entity (rather than only on Player)
 * is what lets multiple gatherers carry simultaneously without a
 * "player-wide round-trip" allocation race. Phase C1 wires the
 * actual pickup/drop transitions on top of this skeleton.
 */
class ResourceStorage final : public APIComponent {
public:
	using APIComponent::APIComponent;

	component_t get_type() const override;
};

} // namespace openage::gamestate::component
