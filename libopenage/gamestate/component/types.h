// Copyright 2021-2023 the openage authors. See copying.md for legal info.

#pragma once


namespace openage::gamestate::component {

/**
 * Types of components.
 */
enum class component_t {
	// Internal
	POSITION,
	COMMANDQUEUE,
	OWNERSHIP,
	ACTIVITY,

	// API - movement / lifecycle
	IDLE,
	TURN,
	MOVE,
	SELECTABLE,
	LIVE,
	ATTACK,

	// Resource / production (tutorial-level)
	GATHER,
	BUILD,
	TRAIN,
	PRODUCTION,
	RESOURCE_STORAGE,
};

} // namespace openage::gamestate::component
