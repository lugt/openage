// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4C: WaypointReachedEvent + MovementArrivedEvent handlers.
//
// Both are OnceEventHandler loggers: they observe the dispatch
// of the corresponding event name so any system that wants to
// react to movement progress (HUD waypoint counter, audio
// footsteps, AI perception, future replay log) can register an
// observer via the EventLoop. The handlers themselves do NOT
// mutate game state; they only log the arrival so a regression
// is visible in the diagnostic stream.
//
// Event names:
//   "game.waypoint_reached"  -- fired by Move::move_default at
//                                each segment boundary (between
//                                waypoints). Carries entity_id,
//                                segment_index, segment_count,
//                                waypoint (CellPosition),
//                                segment_time (time::time_t).
//   "game.movement_arrived"   -- fired by Move::move_default when
//                                the last segment has been
//                                written. Carries entity_id,
//                                status (OK | FAILED), final
//                                waypoint (CellPosition),
//                                total_time (time::time_t).

#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "event/evententity.h"
#include "event/eventhandler.h"
#include "scenario/static_occupancy_grid.h"


namespace openage {

namespace event {
class EventLoop;
class Event;
class State;
} // namespace event

namespace gamestate::event {


class WaypointReachedHandler : public openage::event::OnceEventHandler {
public:
	WaypointReachedHandler();
	~WaypointReachedHandler() = default;

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


class MovementArrivedHandler : public openage::event::OnceEventHandler {
public:
	MovementArrivedHandler();
	~MovementArrivedHandler() = default;

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
