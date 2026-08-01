// Copyright 2026 the openage authors. See copying.md for legal info.
//
// RemovalEventHandler -- the dedicated handler for the deferred
// removal of dead entities (P3.4).
//
// Contract:
//   Event name: "game.removal"
//   Trigger type: ONCE
//   Params (param_map::map_t):
//     - "target_id" : entity_id_t -- the entity to remove from
//                     GameState
//     - "time"      : time::time_t -- the simulation time the death
//                     transition was observed (informational)
//
// Behaviour:
//   * Stale-event safety: removal of an already-removed entity is a
//     clean no-op (logged once, then dropped on duplicates).
//   * Idempotency: duplicate removal events at the same or different
//     times are no-ops.
//   * The actual remove_game_entity call is the ONLY entity-storage
//     mutation this handler performs; no DeathHandler, EffectHandler
//     or AttackHandler mutates the GameState entity container.
//
// What the handler does NOT do (out of scope for P3.4):
//   * Notify dependent systems (selection, collision, pathfinding,
//     render). The MVP Mission 1 doesn't wire any of these; the
//     P0-F-4 inventory in p3.4-report records them as future work.
//   * Reclaim memory. The shared_ptr to the GameEntity is dropped
//     when the GameState map erases the entry.

#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "event/evententity.h"
#include "event/eventhandler.h"
#include "gamestate/types.h"


namespace openage {

namespace event {
class EventLoop;
class Event;
class State;
} // namespace event

namespace gamestate::event {


class RemovalEventHandler : public openage::event::OnceEventHandler {
public:
	/**
	 * Callback signature for game.removal observers. Receives the
	 * removed entity id; called AFTER the GameState mutation
	 * succeeds, BEFORE the handler logs. Observers MUST be
	 * idempotent (the handler is called at most once per unique
	 * removal event, but a re-dispatch of a stale event can call
	 * the observer with a target id that no longer matches any
	 * active state).
	 */
	using observer_t = std::function<void(openage::gamestate::entity_id_t)>;

	RemovalEventHandler();
	~RemovalEventHandler() = default;

	/**
	 * Register a notification callback (P4.1). Called after every
	 * successful (non-stale) game.removal. The observer list grows
	 * monotonically; the handler does not own the observers, so
	 * callers must keep their observer alive for at least as long
	 * as the handler is registered with the event loop.
	 *
	 * The legacy behaviour of game.removal (GameState mutation) is
	 * unchanged; observers are the SOLE supported extension point
	 * for new consumers of game.removal.
	 */
	void add_observer(observer_t observer);

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

private:
	/**
	 * Registered observers (P4.1). Invoked in registration order
	 * after the GameState mutation succeeds. Stale / duplicate
	 * removals do NOT call observers (the observer is told about
	 * a successful mutation only).
	 */
	std::vector<observer_t> observers;
};


} // namespace gamestate::event
} // namespace openage