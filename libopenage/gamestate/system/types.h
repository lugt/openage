// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#pragma once


namespace openage::gamestate::system {

/**
 * Associates a system function with an identifier.
 */
enum class system_id_t {
	NONE,

	IDLE,

	MOVE_COMMAND,
	MOVE_DEFAULT,

	ACTIVITY_ADVANCE,

	// Resource gathering chain (villager -> spot -> TC drop)
	GATHER,
	GATHER_PICKUP,
	RETURN_TO_DROP,
	DROP_RESOURCES,

	// Construction (villager -> foundation -> progress)
	BUILD_PROGRESS,

	// Training (production queue ticking down + spawn on completion)
	TRAIN,

	// Research (building -> progress -> patch commit on completion)
	RESEARCH,
};

} // namespace openage::gamestate::system
