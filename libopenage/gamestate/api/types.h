// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#pragma once


namespace openage::gamestate::api {

/**
 * Types of abilities for API objects.
 *
 * Tutorial-level set: covers gather/build/train/research/attack for the
 * minimal mechanics needed by the campaign tutorial. Full AoE2 ability
 * surface (Fly, Cloak, DetectCloak, Lock, LineOfSight, OverlayTerrain,
 * PassiveTransformTo variants besides Death, ...) is not loaded by
 * entity_factory yet; nyan data for them exists but is silently skipped.
 */
enum class ability_t {
	IDLE,
	LIVE,
	MOVE,
	TURN,

	// Resource / production abilities
	GATHER,
	DROP_RESOURCES,
	HARVESTABLE,
	REGENERATE_RESOURCE_SPOT,

	// Construction abilities
	CONSTRUCTABLE,
	FOUNDATION,

	// Training & research
	CREATE,
	PRODUCTION_QUEUE,
	RESEARCH,

	// Combat
	ATTACK,
	RESISTANCE,

	// Lifecycle
	PASSIVE_TRANSFORM_TO,
	DESPAWN,

	// Population / capacity bookkeeping (used by the population system; the
	// engine does not yet consume it - included so ABILITY_DEFS has a stable
	// address for future components that will).
	PROVIDE_CONTINGENT,
};

/**
 * Types of properties for API abilities.
 */
enum class ability_property_t {
	ANIMATED,
	ANIMATION_OVERRIDE,
	COMMAND_SOUND,
	EXECUTION_SOUND,
	DIPLOMATIC,
	LOCK,
};

/**
 * Types of properties for API patches.
 */
enum class patch_property_t {
	DIPLOMATIC,
};

} // namespace openage::gamestate::api
