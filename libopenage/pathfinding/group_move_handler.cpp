// Copyright 2026 the openage authors. See copying.md for legal info.

#include "pathfinding/group_move_handler.h"

#include "coord/phys.h"
#include "coord/scene.h"
#include <unordered_set>
#include "gamestate/map.h"
#include "pathfinding/navigation_service.h"
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
	const auto generation = params.get("generation", int64_t{0});
	const auto issuer = params.get("issuer", player_id_t{});
	const bool validate_owner = params.check_type<player_id_t>("issuer");

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
		auto owner = e->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		if (e->is_dead() or not e->has_component(component::component_t::MOVE)
		    or (validate_owner and (not owner or owner->get_owners().get(time) != issuer))) {
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

	// The group's current positions are traversable by its members. Reserve
	// the live footprints of other entities while assigning destinations.
	openage::scenario::DynamicOccupancy occupancy;
	const std::unordered_set<entity_id_t> group(movable.begin(), movable.end());
	for (const auto &[id, entity] : gstate->get_game_entities()) {
		if (group.contains(id) or entity->is_dead() or not entity->has_footprint()
		    or not entity->footprint().blocks_pathfinding()) {
			continue;
		}
		if (auto position = entity->try_get_component_as<component::Position>(component::component_t::POSITION)) {
			auto point = position->get_positions().get(time);
			auto footprint = entity->footprint();
			footprint.set_center(point.ne.to_double(), point.se.to_double());
			occupancy.register_entity(id, footprint.bounds());
		}
	}

	const auto map = gstate->get_map();
	const auto size = map->get_size();
	const auto navigation = map->get_navigation_service();
	const auto width = static_cast<int>(size[0]);
	const auto height = static_cast<int>(size[1]);
	scenario::StaticOccupancyGrid bounds(0, 0, width, height);
	auto plan = openage::pathfinding::plan_formation(
		movable, cx, cy, max_w, max_h, occupancy, bounds, 0, 0, width, height, {},
		navigation ? navigation->grid().get() : nullptr);
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
		const auto move_target = coord::scene3{a.slot_center_x, a.slot_center_y, 0}.to_phys3();
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
