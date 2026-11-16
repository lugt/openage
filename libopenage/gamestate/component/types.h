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

/**
 * Human-readable name of a component type.
 *
 * Used in diagnostics: a missing component can then be named in an
 * error message instead of being reported as a bare enum value.
 *
 * @param type Component type.
 * @return Static string with the component's name.
 */
const char *to_string(component_t type);

} // namespace openage::gamestate::component
