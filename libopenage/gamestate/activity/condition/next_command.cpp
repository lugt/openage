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
	// Phase 2 (event-driven AttackSystem): ATTACK is no longer routed through
	// the activity graph. SendCommandHandler::ATTACK enqueues the AttackCommand
	// and emits a "game.attack" event; AttackHandler pops the command before
	// process_command fires, so the activity never sees an unconsumed ATTACK.
	// If an ATTACK somehow remains here, BranchCommand's default (Idle) would
	// loop Idle -> CheckQueue -> BranchCommand forever (caught by the iteration
	// guard in Activity::advance) -- that is now a bug, not the intended path.
	return command->get_type() == component::command::command_t::MOVE;
}

} // namespace openage::gamestate::activity
