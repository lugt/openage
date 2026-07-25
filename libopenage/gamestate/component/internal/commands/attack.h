// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#pragma once

#include "gamestate/component/internal/commands/base_command.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/types.h"


namespace openage::gamestate::component::command {

/**
 * Command for attacking a target game entity.
 *
 * Carries the target entity's ID (unlike MoveCommand, which carries a
 * position). The AttackSystem resolves the target live at processing time
 * via GameState::get_game_entity(target), so the order stays valid even if
 * the target moves after being issued.
 */
class AttackCommand : public Command {
public:
	/**
	 * Creates a new attack command.
	 *
	 * @param target ID of the game entity to attack.
	 */
	AttackCommand(entity_id_t target);
	virtual ~AttackCommand() = default;

	inline command_t get_type() const override {
		return command_t::ATTACK;
	}

	/**
	 * Get the target entity ID.
	 *
	 * @return ID of the entity to attack.
	 */
	entity_id_t get_target() const;

private:
	/**
	 * Target entity ID.
	 */
	const entity_id_t target;
};

} // namespace openage::gamestate::component::command
