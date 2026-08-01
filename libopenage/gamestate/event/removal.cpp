// Copyright 2026 the openage authors. See copying.md for legal info.

#include "removal.h"

#include "event/event_loop.h"
#include "gamestate/game_state.h"
#include "gamestate/types.h"
#include "log/log.h"
#include "log/message.h"


namespace openage::gamestate::event {


RemovalEventHandler::RemovalEventHandler() :
	openage::event::OnceEventHandler{"game.removal"} {
}


void RemovalEventHandler::add_observer(observer_t observer) {
	this->observers.push_back(std::move(observer));
}


void RemovalEventHandler::setup_event(
	const std::shared_ptr<openage::event::Event> & /* event */,
	const std::shared_ptr<openage::event::State> & /* state */) {
	// No dependency tracking; the has_game_entity() check below makes
	// the handler safe even if removal events arrive for entities that
	// were already removed (duplicate dispatch, stale event, etc.).
}


void RemovalEventHandler::invoke(
	openage::event::EventLoop & /* loop */,
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> &state,
	const time::time_t &time,
	const param_map &params) {

	auto gstate = std::dynamic_pointer_cast<openage::gamestate::GameState>(state);

	auto target_id = params.get("target_id", openage::gamestate::entity_id_t{0});

	// Stale / duplicate safety: GameState::remove_game_entity already
	// logs a warning on missing-id and returns, but we make the
	// idempotency explicit so we can branch the log level (info for
	// the first removal, no log spam for duplicates).
	if (not gstate->has_game_entity(target_id)) {
		// Silently drop duplicates; the warn-once inside
		// remove_game_entity is suppressed here for clean log output.
		return;
	}

	gstate->remove_game_entity(target_id);

	// P4.1: notify registered observers AFTER the GameState mutation
	// succeeds. Observers include the mission-1 objective controller
	// (event-driven ACTIVE -> COMPLETED transition).
	for (auto &observer : this->observers) {
		observer(target_id);
	}

	log::log(MSG(info) << "RemovalEventHandler: target " << target_id
	                   << " removed at t=" << time
	                   << " (deferred from DeathEvent)");
}


time::time_t RemovalEventHandler::predict_invoke_time(
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> & /* state */,
	const time::time_t &at) {
	return at;
}


} // namespace openage::gamestate::event