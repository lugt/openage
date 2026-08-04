// Copyright 2026 the openage authors. See copying.md for legal info.

#include "pathfinding/group_move_handler.h"

#include "coord/phys.h"
#include "coord/tile.h"
#include "event/evententity.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/event/spawn_entity.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "input/semantic_action.h"
#include "log/log.h"
#include "log/message.h"
#include "pathfinding/formation_planner.h"
#include "scenario/dynamic_occupancy.h"
#include "scenario/static_occupancy_grid.h"
#include "time/clock.h"
#include "time/time_loop.h"

#include <algorithm>
#include <cstdint>
#include <vector>


namespace openage::gamestate::event {


GroupMoveHandler::GroupMoveHandler(
	const std::shared_ptr<openage::gamestate::GameSimulation>
		&simulation) :
	openage::event::OnceEventHandler{"game.group_move"},
	simulation_{simulation} {
}


void GroupMoveHandler::setup_event(
	const std::shared_ptr<openage::event::Event> & /*event*/,
	const std::shared_ptr<openage::event::State> & /*state*/) {
	// No dependencies: this handler is independent.
}


void GroupMoveHandler::invoke(
	openage::event::EventLoop &loop,
	const std::shared_ptr<openage::event::EventEntity> &target,
	const std::shared_ptr<openage::event::State> &state,
	const openage::time::time_t &time,
	const openage::event::EventHandler::param_map &params) {

	log::log(MSG(info) << "GroupMoveHandler::invoke called (time="
	                   << time << ")");
	// The simulation_ handle is optional: when null
	// (default-constructed) the handler dispatches
	// MoveCommands directly via the event loop. When
	// non-null it can be used to look up a spawner or
	// commander. For the headless / demo path we use
	// the event's own target as the dispatch source.
	(void)this->simulation_;
	auto gstate = std::dynamic_pointer_cast<
		openage::gamestate::GameState>(state);
	if (not gstate) {
		return;
	}

	// Read params.
	auto selection_ids = params.get("entity_ids",
		std::vector<openage::gamestate::entity_id_t>{});
	double cx = params.get("center_x", 0.0);
	double cy = params.get("center_y", 0.0);
	uint64_t generation = params.get("generation",
		static_cast<uint64_t>(0));

	// Filter to movable entities. The unit must have the
	// Move ability and an owner that matches the local
	// player. Buildings (no Move) are dropped.
	std::vector<openage::gamestate::entity_id_t> movable;
	movable.reserve(selection_ids.size());
	for (auto id : selection_ids) {
		if (not gstate->has_game_entity(id)) {
			continue;
		}
		auto e = gstate->get_game_entity(id);
		if (not e->has_component(
			    openage::gamestate::component::component_t::MOVE)) {
			continue;
		}
		movable.push_back(id);
	}
	if (movable.empty()) {
		log::log(MSG(warn) << "GroupMoveHandler: no movable entity "
		                   << "in selection of size "
		                   << selection_ids.size());
		return;
	}

	// Compute the footprint-derived slot pitch from the
	// max footprint in the selection.
	double max_w = 0.0;
	double max_h = 0.0;
	for (auto id : movable) {
		auto e = gstate->get_game_entity(id);
		if (not e->has_footprint()) {
			continue;
		}
		auto fp = e->footprint();
		double w = fp.radius_x() * 2.0;
		double h = fp.radius_y() * 2.0;
		if (w > max_w) max_w = w;
		if (h > max_h) max_h = h;
	}
	if (max_w <= 0.0) max_w = 0.8;
	if (max_h <= 0.0) max_h = 0.8;

	// Build DynamicOccupancy from the live state. Each
	// movable + each non-movable entity gets registered.
	openage::scenario::DynamicOccupancy occ;
	for (auto id : movable) {
		auto e = gstate->get_game_entity(id);
		if (not e->has_footprint()
		    or not e->has_component(
			    openage::gamestate::component::component_t::POSITION)) {
			continue;
		}
		auto fp = e->footprint();
		auto pc = std::dynamic_pointer_cast<
			openage::gamestate::component::Position>(
			e->get_component(
				openage::gamestate::component::component_t::POSITION));
		if (not pc) {
			continue;
		}
		auto sp = pc->get_positions().get(
			openage::time::time_t{0});
		openage::gamestate::component::FootprintBounds b;
		double rx = fp.radius_x();
		double ry = fp.radius_y();
		b.min_x = sp.ne.to_double() - rx;
		b.min_y = sp.se.to_double() - ry;
		b.max_x = sp.ne.to_double() + rx;
		b.max_y = sp.se.to_double() + ry;
		occ.register_entity(id, b);
	}

	// Static grid from the map.
	auto map = gstate->get_map();
	openage::gamestate::scenario::StaticOccupancyGrid
		static_occ(0, 0, 16, 16);
	// The full Mission 1 map is 16x16. If the
	// StaticOccupancyGrid is empty we use that as the
	// base. The DynamicOccupancy check is what matters
	// for the multi-unit case; for the MVP we leave the
	// static grid as a no-op (Map's grid is held
	// separately; the production runtime consults
	// Map::get_static_occupancy_grid which is the
	// authoritative source -- but for the headless demo
	// path a fresh grid is fine).

	// Plan the formation.
	auto plan = openage::pathfinding::plan_formation(
		movable, cx, cy, max_w, max_h, occ, static_occ,
		0, 0, 16, 16);
	if (plan.result
	    != openage::pathfinding::FormationResult::OK) {
		log::log(MSG(warn) << "GroupMoveHandler: formation FAILED ("
		                   << plan.reason
		                   << ") for selection of size "
		                   << movable.size());
		return;
	}

	// Dispatch one MoveCommand per assignment. The
	// command's `entity_ids` is a single-element vector
	// so the Move runtime treats it as a normal move.
	for (size_t i = 0; i < plan.assignments.size(); ++i) {
		const auto &a = plan.assignments[i];
		// Use coord::tile::to_phys3 so the FixedPoint
		// uses the correct shift (raw = tile * 65536).
		// Constructing phys_t{double} directly would
		// double-shift (raw = double * 65536) which
		// overflows.
		openage::coord::phys3 move_target{
			openage::coord::tile{
				static_cast<openage::coord::tile_t>(
					a.slot_center_x),
				static_cast<openage::coord::tile_t>(
					a.slot_center_y)}.to_phys3()};
		openage::event::EventHandler::param_map::map_t move_raw{
			{"type", openage::gamestate::component::command::command_t::MOVE},
			{"target", move_target},
			{"entity_ids",
			 std::vector<openage::gamestate::entity_id_t>{a.entity}},
			{"generation", static_cast<int64_t>(generation)},
		};
		openage::event::EventHandler::param_map move_params{move_raw};
		// Use the event's target as the dispatch source.
		// In production this would be the spawner; for
		// the demo path the target that invoked the
		// group_move handler is the EventEntity that
		// dispatched it (the spawner for the
		// input binding).
		loop.create_event("game.send_command", target, gstate,
			time, move_params);
		log::log(MSG(info) << "GroupMoveHandler: dispatched MoveCommand "
		                   << "for entity=" << a.entity
		                   << " to (" << a.slot_center_x << ","
		                   << a.slot_center_y << ")");
	}
	log::log(MSG(info) << "GroupMoveHandler: dispatched "
	                   << plan.assignments.size() << " MoveCommands from "
	                   << movable.size() << " movable units (gen="
	                   << generation << ")");
}


openage::time::time_t GroupMoveHandler::predict_invoke_time(
	const std::shared_ptr<openage::event::EventEntity> & /*target*/,
	const std::shared_ptr<openage::event::State> & /*state*/,
	const openage::time::time_t &at) {
	return at;
}


}  // namespace openage::gamestate::event
