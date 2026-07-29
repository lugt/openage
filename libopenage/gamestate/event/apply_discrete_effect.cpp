// Copyright 2026 the openage authors. See copying.md for legal info.

#include "apply_discrete_effect.h"

#include <nyan/nyan.h>

#include "event/event_loop.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/types.h"
#include "log/log.h"
#include "log/message.h"


namespace openage::gamestate::event {


ApplyDiscreteEffectHandler::ApplyDiscreteEffectHandler() :
	openage::event::OnceEventHandler{"game.apply_discrete_effect"} {
}


void ApplyDiscreteEffectHandler::setup_event(
	const std::shared_ptr<openage::event::Event> & /* event */,
	const std::shared_ptr<openage::event::State> & /* state */) {
	// TODO: add dependency tracking on the target entity so a remove /
	// despawn change re-evaluates this event's necessity. For now the
	// "target gone" check in invoke() handles the safety path.
}


void ApplyDiscreteEffectHandler::invoke(
	openage::event::EventLoop & /* loop */,
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> &state,
	const time::time_t &time,
	const param_map &params) {

	auto gstate = std::dynamic_pointer_cast<openage::gamestate::GameState>(state);

	auto target_id = params.get("target_id", openage::gamestate::entity_id_t{0});
	auto amount = params.get("amount", static_cast<int64_t>(0));
	auto attribute_fqon = params.get(
		"attribute_fqon", nyan::fqon_t{});

	// ---- Stale-event safety: target may have been removed between
	// the event being scheduled and this invoke. Drop silently with
	// a log line so the dispatcher can iterate the rest of the batch.
	if (not gstate->has_game_entity(target_id)) {
		log::log(MSG(info) << "ApplyDiscreteEffectHandler: target " << target_id
		                   << " missing at t=" << time
		                   << " -- stale event, dropping (no-op)");
		return;
	}

	auto entity = gstate->get_game_entity(target_id);
	auto live = std::dynamic_pointer_cast<openage::gamestate::component::Live>(
		entity->get_component(openage::gamestate::component::component_t::LIVE));
	if (not live) {
		log::log(MSG(warn) << "ApplyDiscreteEffectHandler: target " << target_id
		                   << " has no Live component at t=" << time
		                   << " -- dropping (no-op)");
		return;
	}

	if (attribute_fqon.empty()) {
		log::log(MSG(warn) << "ApplyDiscreteEffectHandler: target " << target_id
		                   << " empty attribute_fqon at t=" << time
		                   << " -- dropping (no-op)");
		return;
	}

	auto current = live->get_attribute_value(time, attribute_fqon);
	if (not current.has_value()) {
		log::log(MSG(warn) << "ApplyDiscreteEffectHandler: target " << target_id
		                   << " missing attribute '" << attribute_fqon
		                   << "' at t=" << time
		                   << " -- dropping (no-op)");
		return;
	}

	// amount is a magnitude; subtract from current. This is the
	// canonical "damage" application path: positive amount => HP
	// decreases. Healing paths (negative amount) would be added later
	// by P5+ when scripted effects land.
	int64_t new_value = current.value() - amount;
	live->set_attribute(time, attribute_fqon, new_value);

	log::log(MSG(info) << "ApplyDiscreteEffectHandler: applied amount=" << amount
	                   << " target=" << target_id
	                   << " attribute=" << attribute_fqon
	                   << " at t=" << time
	                   << " value " << current.value() << " -> " << new_value);

	// P3.2 temporary: the OLD inline death path that lived in
	// AttackHandler pre-refactor. P3.3 replaces this with a generic
	// DeathEvent dispatched off the Live::on_attribute_change callback;
	// P3.4 then defers the actual remove_game_entity to a separate
	// RemovalEvent so the AttackHandler / EffectHandler call stacks
	// never mutate the GameState entity container mid-iteration.
	//
	// The semantics preserved across the move:
	//   * remove_game_entity only when the post-effect HP <= 0
	//   * removal is a no-op (with WARN) if the target already gone
	//   * the effect handler does NOT touch attacker state -- the
	//     attacker's Attack component re-evaluates range/target on its
	//     next game.attack dispatch via the existing
	//     has_game_entity() check and stops cleanly.
	if (new_value <= 0) {
		gstate->remove_game_entity(target_id);
		log::log(MSG(info) << "ApplyDiscreteEffectHandler: target " << target_id
		                   << " HP=" << new_value
		                   << " <= 0; inline remove (P3.2 temporary, P3.3 replaces)");
	}
}


time::time_t ApplyDiscreteEffectHandler::predict_invoke_time(
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> & /* state */,
	const time::time_t &at) {
	// ONCE handlers fire at the scheduled time.
	return at;
}


} // namespace openage::gamestate::event