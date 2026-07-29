// Copyright 2026 the openage authors. See copying.md for legal info.
//
// ApplyDiscreteEffectHandler -- the dedicated handler for the
// engine's discrete-effect application path (P3.2).
//
// Contract:
//   Event name: "game.apply_discrete_effect"
//   Trigger type: ONCE (each event fires exactly once at its time)
//   Params (param_map::map_t):
//     - "target_id"      : entity_id_t  -- the entity receiving the effect
//     - "amount"         : int64_t      -- the effect magnitude (signed;
//                                          for damage this is a positive
//                                          value that the handler
//                                          subtracts from the attribute;
//                                          for healing the caller would
//                                          pass a negative amount)
//     - "attribute_fqon" : nyan::fqon_t (string) -- the attribute to
//                                          mutate (e.g. Health FQON)
//
// Behaviour:
//   * If the target is no longer in GameState, the handler logs and
//     returns (safe no-op for stale events -- the P3.4 path).
//   * If the target has no Live component or the attribute is not
//     registered, the handler logs and returns (no throw).
//   * Otherwise it reads the current value via
//     Live::get_attribute_value(time, attribute_fqon), subtracts
//     `amount`, and writes the new value via
//     Live::set_attribute(time, attribute_fqon, new_value).
//
// What the handler does NOT do (deliberately scoped out of P3.2):
//   * Death / removal -- that is P3.3 and P3.4.
//   * Resistance lookup / armour-aware batch selection.
//   * Owner-based permission checks.
//
// P3.2 keeps the death path inline in AttackHandler (the
// gstate->remove_game_entity call is removed in P3.4).

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


class ApplyDiscreteEffectHandler : public openage::event::OnceEventHandler {
public:
	ApplyDiscreteEffectHandler();
	~ApplyDiscreteEffectHandler() = default;

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