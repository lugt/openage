// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4F.1: GroupMoveHandler -- the production event handler
// for "game.group_move" events. Receives a single
// FormationMoveCommand from the input layer and dispatches
// one MoveCommand event per entity.
//
// Lifecycle:
//   1. Input binding (RMB on ground with multi-selection)
//      dispatches a "game.group_move" event with the
//      SelectionSnapshot and the requested centre.
//   2. GroupMoveHandler::invoke() reads the snapshot,
//      calls FormationPlanner, and for each surviving
//      assignment creates a "game.send_command" event
//      with command_t::MOVE and the per-entity target.
//   3. The Move runtime (system::Move::move_default)
//      processes the per-entity MoveCommands; each unit
//      moves to its slot.

#pragma once

#include <memory>
#include <string>

#include "event/event_loop.h"
#include "event/evententity.h"
#include "event/eventhandler.h"
#include "gamestate/types.h"


namespace openage::event {
class EventLoop;
class Event;
class State;
}  // namespace openage::event


namespace openage::gamestate {
class GameSimulation;
}


namespace openage::gamestate::event {


class GroupMoveHandler : public openage::event::OnceEventHandler {
public:
	explicit GroupMoveHandler(
		const std::shared_ptr<openage::gamestate::GameSimulation>
			&simulation);
	~GroupMoveHandler() = default;

	void setup_event(const std::shared_ptr<openage::event::Event> &event,
	                 const std::shared_ptr<openage::event::State> &state)
		override;

	void invoke(openage::event::EventLoop &loop,
	            const std::shared_ptr<openage::event::EventEntity> &target,
	            const std::shared_ptr<openage::event::State> &state,
	            const openage::time::time_t &time,
	            const openage::event::EventHandler::param_map &params)
		override;

	openage::time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		const openage::time::time_t &at) override;

private:
	std::shared_ptr<openage::gamestate::GameSimulation> simulation_;
};


}  // namespace openage::gamestate::event
