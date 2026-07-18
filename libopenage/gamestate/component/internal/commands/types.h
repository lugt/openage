// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#pragma once


namespace openage::gamestate::component::command {

/**
 * Command types.
 */
enum class command_t {
	NONE,

	CUSTOM,
	IDLE,
	MOVE,

	// Tutorial-level expansion: gather/build/train/attack commands. Full RTS
	// (idle-as-stop, patrol, garrison, queue, etc.) is out of scope for the
	// tutorial level; see doc/reverse_engineering for the complete set.
	GATHER,
	BUILD,
	TRAIN,
	ATTACK,
};

} // namespace openage::gamestate::component::command
