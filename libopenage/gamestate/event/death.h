// Copyright 2026 the openage authors. See copying.md for legal info.
//
// DeathEventHandler -- the dedicated handler for the generic
// game.death event (P3.3).
//
// Contract:
//   Event name: "game.death"
//   Trigger type: ONCE
//   Params (param_map::map_t):
//     - "target_id" : entity_id_t -- the entity that has dropped
//                     to HP <= 0
//     - "time"      : time::time_t -- the simulation time the
//                     threshold transition was observed
//
// Behaviour:
//   * Idempotency gate via GameEntity::mark_dead(): the death
//     transition fires exactly once per entity, even if multiple
//     damage effects land at the same simulation timestamp.
//   * Stale-event safety: a death event for an entity already
//     removed from GameState is a clean no-op (the dispatch is
//     logged once, then dropped).
//   * Cancels the attacker's Attack state: any entity whose
//     Attack component has this entity as target stops its
//     attack cycle. This is the "dead target stops attacker"
//     invariant required by P3.3.
//   * Does NOT remove the entity from GameState (P3.4 defers
//     that to a RemovalEvent).
//
// What the handler does NOT do (deliberately scoped out of P3.3):
//   * Deferred removal from GameState -- P3.4.
//   * Objective / mission completion notification -- P4.

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


class DeathEventHandler : public openage::event::OnceEventHandler {
public:
	DeathEventHandler();
	~DeathEventHandler() = default;

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