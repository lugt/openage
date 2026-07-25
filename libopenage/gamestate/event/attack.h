// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <memory>
#include <string>

#include "event/evententity.h"
#include "event/eventhandler.h"


namespace openage {

namespace event {
class EventLoop;
class Event;
class State;
} // namespace event

namespace gamestate::event {

/**
 * Entry point of the event-driven AttackSystem (Phase 2).
 *
 * SendCommandHandler::ATTACK enqueues an AttackCommand into each attacker's
 * CommandQueue and then emits a "game.attack" event carrying the attacker IDs
 * and target ID. This handler fires on that event and is the only consumer of
 * the AttackCommand: it pops the command (so the activity graph's
 * BranchCommand never sees an unconsumed ATTACK and loops) and hands off to
 * the AttackSystem to set up persistent attack state.
 *
 * Why an event and not the activity graph: Attack is an ApplyDiscreteEffect
 * ability (event-based), not an Activity Task Node. Routing it through the
 * command_queue + activity would require either a Move overload (rejected) or
 * a new activity branch (converter re-run, deferred). The game.attack event
 * keeps the activity graph untouched while giving the AttackSystem a
 * well-defined entry point that is straightforward to migrate to an Activity
 * Task Node later (see AttackSystem migration note).
 *
 * Ordering guarantee: create_event() pushes this event directly into the
 * event_queue (fired in the current execute_events pass), while the
 * add_command() change reschedules process_command via the changes set (fired
 * in a later reach_time iteration). So AttackHandler always runs before
 * process_command -- the ATTACK is consumed before the activity looks.
 */
class AttackHandler : public openage::event::OnceEventHandler {
public:
	AttackHandler();
	~AttackHandler() = default;

	void setup_event(const std::shared_ptr<openage::event::Event> &event,
	                 const std::shared_ptr<openage::event::State> &state) override;

	void invoke(openage::event::EventLoop &loop,
	            const std::shared_ptr<openage::event::EventEntity> &target,
	            const std::shared_ptr<openage::event::State> &state,
	            const time::time_t &time,
	            const param_map &params) override;

	time::time_t predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> &target,
	                                 const std::shared_ptr<openage::event::State> &state,
	                                 const time::time_t &at) override;
};

} // namespace gamestate::event
} // namespace openage
