// Copyright 2026 the openage authors. See copying.md for legal info.

#include "movement.h"

#include "event/event_loop.h"
#include "gamestate/game_state.h"
#include "gamestate/types.h"
#include "log/log.h"
#include "log/message.h"
#include "scenario/static_occupancy_grid.h"


namespace openage::gamestate::event {


WaypointReachedHandler::WaypointReachedHandler(Notify notify) :
	openage::event::OnceEventHandler{"game.waypoint_reached"},
	notify{std::move(notify)} {
}


void WaypointReachedHandler::setup_event(
	const std::shared_ptr<openage::event::Event> & /* event */,
	const std::shared_ptr<openage::event::State> & /* state */) {
	// No dependency tracking -- the handler is a pure observer.
}


void WaypointReachedHandler::invoke(
	openage::event::EventLoop & /* loop */,
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> &state,
	const time::time_t &time,
	const param_map &params) {

	auto entity_id = params.get("entity_id", openage::gamestate::entity_id_t{0});
	auto segment_index = params.get("segment_index", int64_t{0});
	auto segment_count = params.get("segment_count", int64_t{0});
	auto wx = params.get("waypoint_x", int64_t{0});
	auto wy = params.get("waypoint_y", int64_t{0});
	(void)state;

	if (this->notify) {
		this->notify(entity_id, time);
	}
	log::log(MSG(info) << "WaypointReachedHandler: entity=" << entity_id
	                   << " segment=" << segment_index << "/" << segment_count
	                   << " waypoint=(" << wx << "," << wy << ")"
	                   << " t=" << time);
}


time::time_t WaypointReachedHandler::predict_invoke_time(
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> & /* state */,
	const time::time_t &at) {
	return at;
}


MovementArrivedHandler::MovementArrivedHandler(Notify notify) :
	openage::event::OnceEventHandler{"game.movement_arrived"},
	notify{std::move(notify)} {
}


void MovementArrivedHandler::setup_event(
	const std::shared_ptr<openage::event::Event> & /* event */,
	const std::shared_ptr<openage::event::State> & /* state */) {
	// No dependency tracking -- the handler is a pure observer.
}


void MovementArrivedHandler::invoke(
	openage::event::EventLoop & /* loop */,
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> &state,
	const time::time_t &time,
	const param_map &params) {

	auto entity_id = params.get("entity_id", openage::gamestate::entity_id_t{0});
	auto status = params.get("status", std::string{"OK"});
	auto wx = params.get("waypoint_x", int64_t{0});
	auto wy = params.get("waypoint_y", int64_t{0});
	(void)state;

	if (this->notify) {
		this->notify(entity_id, time);
	}
	log::log(MSG(info) << "MovementArrivedHandler: entity=" << entity_id
	                   << " status=" << status
	                   << " waypoint=(" << wx << "," << wy << ")"
	                   << " t=" << time);
}


time::time_t MovementArrivedHandler::predict_invoke_time(
	const std::shared_ptr<openage::event::EventEntity> & /* target */,
	const std::shared_ptr<openage::event::State> & /* state */,
	const time::time_t &at) {
	return at;
}


} // namespace openage::gamestate::event
