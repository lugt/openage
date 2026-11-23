// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>

#include <nyan/nyan.h>

namespace openage::gamestate {

/**
 * Game entity IDs.
 */
using entity_id_t = uint64_t;

/**
 * Player IDs.
 */
using player_id_t = uint64_t;

struct PlayerEconomy {
	int64_t wood = 0;
	int64_t food = 0;
	int64_t gold = 0;
	int64_t stone = 0;
	int64_t population = 0;
	int64_t population_capacity = 0;

	bool operator==(const PlayerEconomy &) const = default;
};

/**
 * Per-slot configuration passed into \p Game at construction.
 *
 * Carries the FQON of the nyan PlayerSetup (== civ) the slot will use.
 * Other slot fields (color, team, AI flag, ...) are deliberately omitted
 * here: they are useful for full RTS matchmaking but the tutorial only
 * needs the civ choice to demonstrate A4's civ-patch application path.
 * Future expansion is additive - extend the struct without breaking
 * existing call sites.
 */
struct PlayerDescriptor {
	/**
	 * Fully-qualified nyan object name of the PlayerSetup the slot
	 * initialises with, e.g. "data.civ.britons.britons".
	 *
	 * If empty, the slot is created without applying any civ patches or
	 * starting resources. (Useful for debug-only observer slots; production
	 * code should always populate this.)
	 */
	nyan::fqon_t player_setup_fqon;
};

} // namespace openage::gamestate
