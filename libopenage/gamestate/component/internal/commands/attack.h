// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#pragma once

#include "gamestate/component/internal/commands/base_command.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/types.h"


namespace openage::gamestate::component::command {

/**
 * Command for attacking a target game entity.
 *
 * Carries the attacker's own entity ID and the target entity's ID (unlike
 * MoveCommand, which carries a position). The AttackSystem resolves the target
 * live at processing time via GameState::get_game_entity(target), so the order
 * stays valid even if the target moves after being issued.
 *
 * Dispatch path (Phase 2, event-driven AttackSystem): SendCommandHandler
 * enqueues this into the entity's CommandQueue, then emits a "game.attack"
 * event. The AttackHandler consumes (pops) the command before the activity
 * graph's process_command fires -- so the activity never sees an unconsumed
 * ATTACK (which would loop Idle -> CheckQueue -> BranchCommand). This command
 * carries no damage/cooldown logic; it is pure intent. The persistent attack
 * state (target tracking, next-hit time) lives in the AttackSystem, not here.
 */
class AttackCommand : public Command {
public:
	/**
	 * Creates a new attack command.
	 *
	 * @param attacker ID of the game entity issuing the attack.
	 * @param target ID of the game entity to attack.
	 */
	AttackCommand(entity_id_t attacker, entity_id_t target);
	virtual ~AttackCommand() = default;

	inline command_t get_type() const override {
		return command_t::ATTACK;
	}

	/**
	 * Get the attacker entity ID.
	 *
	 * @return ID of the entity issuing the attack.
	 */
	entity_id_t get_attacker() const;

	/**
	 * Get the target entity ID.
	 *
	 * @return ID of the entity to attack.
	 */
	entity_id_t get_target() const;

private:
	/**
	 * Attacker entity ID (the unit ordered to attack).
	 */
	const entity_id_t attacker;

	/**
	 * Target entity ID.
	 */
	const entity_id_t target;
};

} // namespace openage::gamestate::component::command
