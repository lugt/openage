// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#include "next_command.h"

#include "gamestate/component/internal/command_queue.h"
#include "gamestate/game_entity.h"


namespace openage::gamestate::activity {

bool next_command_idle(const time::time_t &time,
                       const std::shared_ptr<gamestate::GameEntity> &entity) {
	auto command_queue = std::dynamic_pointer_cast<component::CommandQueue>(
		entity->get_component(component::component_t::COMMANDQUEUE));

	if (command_queue->get_queue().empty(time)) {
		return false;
	}

	auto command = command_queue->get_queue().front(time);
	return command->get_type() == component::command::command_t::MOVE;
}

bool next_command_move(const time::time_t &time,
                       const std::shared_ptr<gamestate::GameEntity> &entity) {
	auto command_queue = std::dynamic_pointer_cast<component::CommandQueue>(
		entity->get_component(component::component_t::COMMANDQUEUE));

	if (command_queue->get_queue().empty(time)) {
		return false;
	}

	auto command = command_queue->get_queue().front(time);
	// Phase 1 (AttackCommand vertical slice): route both MOVE and ATTACK to the
	// Move node. MOVE is processed by Move::move_command; ATTACK is popped and
	// dropped there (the dynamic_pointer_cast<MoveCommand> fails -> warn log ->
	// return 0). AttackSystem is wired into the activity graph in Phase 2, at
	// which point ATTACK gets its own branch and this falls back to MOVE-only.
	//
	// Without accepting ATTACK here, BranchCommand's default branch (Idle) is
	// taken, which never pops the command -> the activity loops
	// Idle -> CheckQueue -> BranchCommand -> Idle forever (caught by the
	// iteration guard in Activity::advance).
	auto type = command->get_type();
	return type == component::command::command_t::MOVE
	       or type == component::command::command_t::ATTACK;
}

} // namespace openage::gamestate::activity
