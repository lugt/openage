// Copyright 2026 the openage authors. See copying.md for legal info.

#include "death.h"

#include "event/event_loop.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/types.h"
#include "log/log.h"
#include "log/message.h"


namespace openage::gamestate::event {


DeathEventHandler::DeathEventHandler() :
	openage::event::OnceEventHandler{"game.death"} {
}


void DeathEventHandler::setup_event(
	const std::shared_ptr<openage::event::Event> & /* event */,
	const std::shared_ptr<openage::event::State> & /* state */) {
	// No dependency tracking; the idempotency gate (mark_dead) makes
	// the handler safe even if multiple death events for the same
	// target are scheduled into the queue.
}


void DeathEventHandler::invoke(
	openage::event::EventLoop &loop,
	const std::shared_ptr<openage::event::EventEntity> &target,
	const std::shared_ptr<openage::event::State> &state,
	const time::time_t &time,
	const param_map &params) {

	auto gstate = std::dynamic_pointer_cast<openage::gamestate::GameState>(state);

	auto target_id = params.get("target_id", openage::gamestate::entity_id_t{0});

	// Stale-event safety: target may have been removed between the
	// event being scheduled and this invoke. P3.4 deferred removal
	// makes this branch slightly more common (the death event can
	// arrive before the removal event has settled), so the drop is
	// still a clean no-op.
	if (not gstate->has_game_entity(target_id)) {
		log::log(MSG(info) << "DeathEventHandler: target " << target_id
		                   << " missing at t=" << time
		                   << " -- stale event, dropping (no-op)");
		return;
	}

	auto entity = gstate->get_game_entity(target_id);

	// Idempotency gate: only the FIRST death event for this entity
	// does the mark_dead + dispatch removal + stop_attackers work.
	// Subsequent events for the same target are no-ops (no log spam).
	if (not entity->mark_dead()) {
		return;
	}

	log::log(MSG(info) << "DeathEventHandler: target " << target_id
	                   << " HP transition fired at t=" << time
	                   << " -- entity marked dead (idempotent gate)");

	// P3.4: dispatch the removal as a separate event so the
	// dispatcher's call stack never mutates the GameState entity
	// container mid-iteration. The RemovalEventHandler (below in the
	// pipeline) is the sole owner of remove_game_entity. DeathHandler
	// only flips the dead flag and stops attackers.
	openage::event::EventHandler::param_map::map_t removal_params{
		{"target_id", target_id},
		{"time", time},
	};
	loop.create_event("game.removal", target, state, time, removal_params);

	// Stop any attacker that currently has this target set. Iterate
	// over all alive entities and check their Attack component; this
	// is O(N) per death but the Mission 1 entity count is tiny.
	for (const auto &kv : gstate->get_game_entities()) {
		auto other = kv.second;
		if (not other or other->get_id() == target_id) {
			continue;
		}
		if (not other->has_component(
				openage::gamestate::component::component_t::ATTACK)) {
			continue;
		}
		auto attack = std::dynamic_pointer_cast<
			openage::gamestate::component::Attack>(
			other->get_component(
				openage::gamestate::component::component_t::ATTACK));
		if (attack and attack->is_active()
		    and attack->get_target_id() == target_id) {
			attack->stop_attack();
			log::log(MSG(info) << "DeathEventHandler: attacker " << other->get_id()
			                   << " stopped (target " << target_id << " dead)");
		}
	}
}


time::time_t DeathEventHandler::predict_invoke_time(
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> & /* state */,
	const time::time_t &at) {
	return at;
}


} // namespace openage::gamestate::event