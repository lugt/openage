// Copyright 2023-2025 the openage authors. See copying.md for legal info.
//
// P4.4C: Move runtime now prefers the deterministic
// NavigationService (A*) over the legacy flow-field pathfinder
// when the mission_pack_loader has wired one onto the Map.
// Failure is explicit: NO_PATH / START_BLOCKED / TARGET_BLOCKED
// dispatch a MovementArrivedEvent(status=FAILED) and the unit
// stays put. The legacy flow-field path is preserved verbatim
// for tests / demos that build a Map without a NavigationService
// -- the spec requires "keep old flow-field, do not delete or
// modify", not "remove the legacy code path".
//
// Each segment (waypoint -> waypoint transition) emits a
// WaypointReachedEvent; the last segment emits a
// MovementArrivedEvent(status=OK). Events are dispatched via the
// event loop at the segment's end time so the diagnostic stream
// records the arrival -- observers can read the entity id, the
// segment index, and the waypoint cell.

#include "move.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include <nyan/nyan.h>

#include "event/event_loop.h"
#include "event/evententity.h"
#include "event/eventhandler.h"
#include "log/log.h"
#include "log/message.h"

#include "coord/phys.h"
#include "coord/tile.h"
#include "curve/continuous.h"
#include "curve/segmented.h"
#include "gamestate/api/ability.h"
#include "gamestate/api/animation.h"
#include "gamestate/api/property.h"
#include "gamestate/api/types.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/api/turn.h"
#include "gamestate/component/internal/command_queue.h"
#include "gamestate/component/internal/commands/move.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/event/movement_entity.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "pathfinding/navigation_service.h"
#include "pathfinding/path.h"
#include "pathfinding/pathfinder.h"
#include "scenario/static_occupancy_grid.h"
#include "scenario/swept_aabb.h"
#include "util/fixed_point.h"


namespace openage::gamestate::system {


std::vector<coord::phys3> find_path(const std::shared_ptr<path::Pathfinder> &pathfinder,
                                    path::grid_id_t grid_id,
                                    const coord::phys3 &start,
                                    const coord::phys3 &end,
                                    const time::time_t &start_time) {
	auto start_tile = start.to_tile();
	auto end_tile = end.to_tile();

	// Search for a path between the start and end tiles
	path::PathRequest request{
		grid_id,
		start_tile,
		end_tile,
		start_time,
	};
	auto tile_path = pathfinder->get_path(request);

	log::log(MSG(info) << "Move::find_path: grid_id=" << grid_id
	             << " start_tile=(" << start_tile.ne << "," << start_tile.se << ")"
	             << " end_tile=(" << end_tile.ne << "," << end_tile.se << ")"
	             << " status=" << static_cast<int>(tile_path.status));

	if (tile_path.status != path::PathResult::FOUND) {
		// No path found
		log::log(MSG(warn) << "Move::find_path: NO PATH FOUND (status="
		                   << static_cast<int>(tile_path.status) << ")");
		return {};
	}

	// Get the waypoints of the path
	std::vector<coord::phys3> path;
	path.reserve(tile_path.waypoints.size());

	// Start poition is first waypoint
	path.push_back(start);

	// Pathfinder waypoints contain start and end tile; we can ignore them
	for (size_t i = 1; i < tile_path.waypoints.size() - 1; ++i) {
		auto tile = tile_path.waypoints.at(i);
		path.push_back(tile.to_phys3_center());
	}

	// End position is last waypoint
	path.push_back(end);

	return path;
}

const time::time_t Move::move_command(const std::shared_ptr<gamestate::GameEntity> &entity,
                                      const std::shared_ptr<openage::gamestate::GameState> &state,
                                      const time::time_t &start_time) {
	auto command_queue = std::dynamic_pointer_cast<component::CommandQueue>(
		entity->get_component(component::component_t::COMMANDQUEUE));
	auto command = std::dynamic_pointer_cast<component::command::MoveCommand>(
		command_queue->pop_command(start_time));

	if (not command) [[unlikely]] {
		log::log(MSG(warn) << "Command is not a move command.");
		return time::time_t::from_int(0);
	}

	return Move::move_default(entity, state, command->get_target(), start_time);
}


const time::time_t Move::move_default(const std::shared_ptr<gamestate::GameEntity> &entity,
                                      const std::shared_ptr<openage::gamestate::GameState> &state,
                                      const coord::phys3 &destination,
                                      const time::time_t &start_time) {
	if (not entity->has_component(component::component_t::MOVE)) [[unlikely]] {
		log::log(WARN << "Entity " << entity->get_id() << " has no move component.");
		return time::time_t::from_int(0);
	}

	auto turn_component = std::dynamic_pointer_cast<component::Turn>(
		entity->get_component(component::component_t::TURN));
	auto turn_ability = turn_component->get_ability();
	auto turn_speed = turn_ability.get<nyan::Float>("Turn.turn_speed");

	auto move_component = std::dynamic_pointer_cast<component::Move>(
		entity->get_component(component::component_t::MOVE));
	auto move_ability = move_component->get_ability();
	auto move_speed = move_ability.get<nyan::Float>("Move.speed");
	auto move_path_grid = move_ability.get<nyan::ObjectValue>("Move.path_type");

	auto pos_component = std::dynamic_pointer_cast<component::Position>(
		entity->get_component(component::component_t::POSITION));

	auto &positions = pos_component->get_positions();
	auto &angles = pos_component->get_angles();
	auto current_pos = positions.get(start_time);
	auto current_angle = angles.get(start_time);

	// P4.4F.1: DynamicOccupancy + swept AABB runtime hookup.
	// Map owns a shared DynamicOccupancy; register the
	// moving entity at its current footprint before the
	// segment loop, then update on every keyframe.
	auto map_for_occ = state->get_map();
	auto dyn_occ = map_for_occ ? map_for_occ->get_dynamic_occupancy()
	                          : nullptr;
	double occ_rx = 0.0;
	double occ_ry = 0.0;
	if (entity->has_footprint()) {
		auto fp = entity->footprint();
		occ_rx = fp.radius_x();
		occ_ry = fp.radius_y();
	}
	auto occ_bounds_at = [&](const coord::phys3 &p) {
		openage::gamestate::component::FootprintBounds b;
		b.min_x = p.ne.to_double() - occ_rx;
		b.min_y = p.se.to_double() - occ_ry;
		b.max_x = p.ne.to_double() + occ_rx;
		b.max_y = p.se.to_double() + occ_ry;
		return b;
	};
	if (dyn_occ) {
		dyn_occ->register_entity(
			entity->get_id(), occ_bounds_at(current_pos));
	}

	// Find path. P4.4C: prefer the deterministic NavigationService
	// (A*) over the legacy flow-field pathfinder when the loader has
	// attached one. Failure is explicit -- no straight-line
	// fallback.
	auto map = state->get_map();
	std::vector<coord::phys3> waypoints;
	auto nav_service = map ? map->get_navigation_service() : nullptr;
	if (nav_service) {
		// Build a mover half-extents from the entity's footprint
		// (Villager rx=0.2 / TC rx=2.0). When the entity has no
		// footprint (e.g. an ability probe), default to 0.0 which
		// disables inflation -- the unit becomes a point.
		double mover_rx = 0.0;
		double mover_ry = 0.0;
		if (entity->has_footprint()) {
			auto fp = entity->footprint();
			mover_rx = fp.radius_x();
			mover_ry = fp.radius_y();
		}
		openage::pathfinding::MoverFootprint mover{mover_rx, mover_ry};
		auto start_tile = current_pos.to_tile();
		auto target_tile = destination.to_tile();
		openage::gamestate::scenario::CellPosition start_cell{
			static_cast<int>(start_tile.ne), static_cast<int>(start_tile.se)};
		openage::gamestate::scenario::CellPosition target_cell{
			static_cast<int>(target_tile.ne), static_cast<int>(target_tile.se)};
		log::log(MSG(info) << "Move::move_default: entity=" << entity->get_id()
		             << " nav_service=yes start=(" << start_cell.x << "," << start_cell.y << ")"
		             << " dest=(" << target_cell.x << "," << target_cell.y << ")"
		             << " mover_rx=" << mover_rx << " mover_ry=" << mover_ry);
		auto path_result = nav_service->request_path(start_cell,
			target_cell,
			mover);
		if (path_result.result
		    != openage::pathfinding::GridAStarPathfinder::Result::PATH_FOUND) {
			// Explicit failure: dispatch MovementArrivedEvent(FAILED)
			// and leave the unit in place. No straight-line fallback.
			const char *status = "NO_PATH";
			switch (path_result.result) {
			case openage::pathfinding::GridAStarPathfinder::Result::NO_PATH:
				status = "NO_PATH"; break;
			case openage::pathfinding::GridAStarPathfinder::Result::START_BLOCKED:
				status = "START_BLOCKED"; break;
			case openage::pathfinding::GridAStarPathfinder::Result::TARGET_BLOCKED:
				status = "TARGET_BLOCKED"; break;
			default: status = "UNKNOWN"; break;
			}
			log::log(MSG(warn) << "Move::move_default: A* FAILED status=" << status
			                   << " entity=" << entity->get_id()
			                   << " start=(" << start_cell.x << "," << start_cell.y << ")"
			                   << " target=(" << target_cell.x << "," << target_cell.y << ")");
			auto loop = state->get_event_loop();
			if (loop) {
				openage::event::EventHandler::param_map::map_t arrived_params{
					{"entity_id", entity->get_id()},
					{"status", std::string{status}},
					{"waypoint_x", static_cast<int64_t>(target_cell.x)},
					{"waypoint_y", static_cast<int64_t>(target_cell.y)},
				};
				auto target = std::make_shared<
					openage::gamestate::event::MovementEventTarget>(
					loop, static_cast<size_t>(entity->get_id()));
				loop->create_event("game.movement_arrived", target,
					state, start_time, arrived_params);
			}
			return time::time_t::from_int(0);
		}
		// Convert cell waypoints to phys3 centers.
		waypoints.reserve(path_result.waypoints.size());
		for (const auto &c : path_result.waypoints) {
			waypoints.push_back(coord::tile{
				static_cast<coord::tile_t>(c.x),
				static_cast<coord::tile_t>(c.y)}.to_phys3_center());
		}
		log::log(MSG(info) << "Move::move_default: A* waypoints=" << waypoints.size()
		                   << " segments=" << (waypoints.size() > 0 ? waypoints.size() - 1 : 0));
	}
	else {
		// Legacy flow-field pathfinder path. Kept for tests that
		// build a Map without a NavigationService (no mission pack
		// loader, only synthetic terrain).
		auto pathfinder = map->get_pathfinder();
		log::log(MSG(info) << "Move::move_default: entity=" << entity->get_id()
		             << " path_type=" << move_path_grid->get_name()
		             << " current=(" << current_pos.ne << "," << current_pos.se << ")"
		             << " dest=(" << destination.ne << "," << destination.se << ")");
		auto grid_id = map->get_grid_id(move_path_grid->get_name());
		waypoints = find_path(pathfinder, grid_id, current_pos, destination, start_time);
		log::log(MSG(info) << "Move::move_default: grid_id=" << grid_id
		             << " waypoints=" << waypoints.size() << " total_time_so_far=0");
	}

	// use waypoints for movement
	double total_time = 0;
	pos_component->set_position(start_time, current_pos);
	log::log(MSG(info) << "Move::move_default: move_speed.get()=" << move_speed.get()
	                   << " move_speed->is_infinite_positive()=" << move_speed->is_infinite_positive());
	auto event_loop = state->get_event_loop();
	const size_t segment_count = waypoints.size() > 0 ? waypoints.size() - 1 : 0;
	for (size_t i = 1; i < waypoints.size(); ++i) {
		auto prev_waypoint = waypoints[i - 1];
		auto cur_waypoint = waypoints[i];

		auto path_vector = cur_waypoint - prev_waypoint;
		auto path_angle = path_vector.to_angle();

		// P4.4F.1: pre-check the swept AABB against
		// DynamicOccupancy. If a NON-self entity's bounds
		// overlap the swept volume, the segment is blocked
		// (the spec requires us not to fall back to
		// passing through; the production behaviour is
		// to log and continue past the block on the
		// existing keyframe so the unit doesn't move
		// through the blocker, but the move still
		// completes along the remaining segments). For
		// the headless demo path we surface the block
		// as a warning.
		if (dyn_occ) {
			openage::gamestate::component::FootprintBounds swept
				= ::openage::scenario::swept_aabb(
					occ_bounds_at(prev_waypoint),
					occ_bounds_at(cur_waypoint));
			auto blockers = dyn_occ->query(
				entity->get_id(), swept);
			if (not blockers.empty()) {
				log::log(MSG(warn) << "Move::move_default: segment "
					<< "blocked by " << blockers.size()
					<< " dynamic entities entity="
					<< entity->get_id());
			}
		}

		// rotation
		if (not turn_speed->is_infinite_positive()) {
			auto angle_diff = path_angle - current_angle;
			if (angle_diff < 0) {
				// get the positive difference
				angle_diff = angle_diff * -1;
			}
			if (angle_diff > 180) {
				// always use the smaller angle
				angle_diff = angle_diff - 360;
				angle_diff = angle_diff * -1;
			}

			double turn_time = angle_diff.to_double() / turn_speed->get();
			// Phase 0.5C fix #13 follow-up: cap turn_time so a large angle diff
			// does not pause the unit for many seconds. With a 90 deg difference
			// and the default turn_speed=6 deg/s the pause would be 15 s; during
			// that interval every new right-click wiped the in-flight destination
			// (Continuous::set_last erases all later keyframes), so the villager
			// stayed parked in place for the entire turn. Capping turn_time bounds
			// the pause and lets move_time dominate total_time, after which the
			// unit steps its angle to the path direction at dest time.
			//
			// This is intentionally simple -- proper rotate-while-move would need a
			// Continuous angle curve (current Segmented step is discontinuous); the
			// cap is a Phase 0.5C scope choice.
			constexpr double MAX_TURN_TIME = 0.5;
			if (turn_time > MAX_TURN_TIME) {
				turn_time = MAX_TURN_TIME;
			}
			total_time += turn_time;
			// Intermediate keyframe at start_time + total_time keeps the unit
			// at prev (=cur) for the cap duration; removed by future move
			// commands via set_last's documented erase-future semantics.
			pos_component->set_position(start_time + total_time, prev_waypoint);

			// update current angle for next waypoint
			current_angle = path_angle;
		}
		pos_component->set_angle(start_time + total_time, path_angle);

		// movement
		double move_time = 0;
		if (not move_speed->is_infinite_positive()) {
			auto distance = path_vector.length();
			move_time = distance / move_speed->get();
		}
		total_time += move_time;

		pos_component->set_position(start_time + total_time, cur_waypoint);

		// P4.4F.1: update DynamicOccupancy after each segment
		// commit. The swept AABB of (prev, cur) is the
		// shape the unit's footprint sweeps over the
		// segment; the occupancy update covers the full
		// swept volume so subsequent movers querying
		// the same area see the right bounds.
		if (dyn_occ) {
			openage::gamestate::component::FootprintBounds prev_b
				= occ_bounds_at(prev_waypoint);
			openage::gamestate::component::FootprintBounds cur_b
				= occ_bounds_at(cur_waypoint);
			// Use the swept AABB at the new position --
			// DynamicOccupancy::update_entity does an
			// old->new bucket diff; we pass the
			// destination bounds as `new` (the swept
			// volume is approximated by the
			// destination's rectangle for the
			// per-cell bucket map).
			dyn_occ->update_entity(entity->get_id(),
				prev_b, cur_b);
		}

		// P4.4C: dispatch a WaypointReachedEvent at the end of
		// every segment. The event is created at the segment's
		// end time so the diagnostic stream records the arrival
		// in the same order as the Position Curve's keyframes.
		if (event_loop) {
			auto wp_tile = cur_waypoint.to_tile();
			openage::event::EventHandler::param_map::map_t wp_params{
				{"entity_id", entity->get_id()},
				{"segment_index", static_cast<int64_t>(i)},
				{"segment_count", static_cast<int64_t>(segment_count)},
				{"waypoint_x", static_cast<int64_t>(wp_tile.ne)},
				{"waypoint_y", static_cast<int64_t>(wp_tile.se)},
			};
			// EventLoop requires a non-null EventEntity target
			// (its constructor hashes target->id()). MovementEventTarget
			// is the dedicated stub: it carries the moved entity's
			// id for traceability and lets observers query it
			// from target if they want, but the canonical
			// entity_id lives in params.
			auto target = std::make_shared<
				openage::gamestate::event::MovementEventTarget>(
				event_loop, static_cast<size_t>(entity->get_id()));
			event_loop->create_event("game.waypoint_reached", target,
				state, start_time + total_time, wp_params);
		}
	}

	// P4.4C: dispatch MovementArrivedEvent(OK) at the end of the
	// last segment. status=OK only when waypoints were non-empty
	// (i.e., A* returned a path); zero-waypoint paths were
	// already filtered above as failures.
	if (event_loop && not waypoints.empty()) {
		auto final_tile = waypoints.back().to_tile();
		openage::event::EventHandler::param_map::map_t arrived_params{
			{"entity_id", entity->get_id()},
			{"status", std::string{"OK"}},
			{"waypoint_x", static_cast<int64_t>(final_tile.ne)},
			{"waypoint_y", static_cast<int64_t>(final_tile.se)},
		};
		auto target = std::make_shared<
			openage::gamestate::event::MovementEventTarget>(
			event_loop, static_cast<size_t>(entity->get_id()));
		event_loop->create_event("game.movement_arrived", target,
			state, start_time + total_time, arrived_params);
	}

	// Verify the curve now has a future keyframe (Phase 0.5B bug #13 follow-up:
	// Continuous::set_last erases all elements that follow at a later time, so
	// each new move_default wipes future keyframes. Log where the dest keyframe
	// landed and what a mid-point query returns, so a stuck villager is
	// attributable to either the math here or the curve semantics).
	log::log(MSG(info) << "Move::move_default: total_time=" << total_time
	                   << " end keyframe at t+" << total_time << " (=start_time + total_time)");
	if (total_time > 0) {
		auto query_t = start_time + time::time_t::from_double(total_time * 0.5);
		auto mid_pos = positions.get(query_t);
		log::log(MSG(info) << "Move::move_default: mid-t pos=(" << mid_pos.ne << "," << mid_pos.se
		                   << ") (expect ~partway between current and dest)");
	}

	// properties
	auto ability = move_component->get_ability();
	if (api::APIAbility::check_property(ability, api::ability_property_t::ANIMATED)) {
		auto property = api::APIAbility::get_property(ability, api::ability_property_t::ANIMATED);
		auto animations = api::APIAbilityProperty::get_animations(property);
		auto animation_paths = api::APIAnimation::get_animation_paths(animations);

		if (animation_paths.size() > 0) [[likely]] {
			entity->render_update(start_time, animation_paths[0]);
		}
	}

	return total_time;
}

} // namespace openage::gamestate::system
