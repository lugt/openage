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
#include <utility>
#include <vector>

#include "event/event_loop.h"
#include "event/evententity.h"
#include "event/eventhandler.h"
#include "log/log.h"
#include "log/message.h"

#include "coord/phys.h"
#include "coord/tile.h"
#include "curve/continuous.h"
#include "curve/segmented.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/system/idle.h"
#include "gamestate/component/internal/command_queue.h"
#include "gamestate/component/internal/commands/move.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/manager.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "gamestate/terrain.h"
#include "pathfinding/navigation_service.h"
#include "pathfinding/path.h"
#include "pathfinding/pathfinder.h"
#include "scenario/static_occupancy_grid.h"
#include "scenario/swept_aabb.h"
#include "util/fixed_point.h"

import nyan;


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

	log::log(DBG << "Move::find_path: grid_id=" << grid_id
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

	auto attack = entity->try_get_component_as<component::Attack>(component::component_t::ATTACK);
	if (attack and attack->is_active()) {
		if (attack->is_approaching()) {
			Move::halt(entity, state, start_time);
		}
		attack->stop_attack();
	}
	return Move::move_default(entity, state, command->get_target(), start_time, true);
}


const time::time_t Move::move_default(const std::shared_ptr<gamestate::GameEntity> &entity,
                                      const std::shared_ptr<openage::gamestate::GameState> &state,
                                      const coord::phys3 &destination,
	                                      const time::time_t &start_time,
	                                      bool retry_on_block,
	                                      uint32_t attempt) {
	if (not entity->has_component(component::component_t::MOVE)) [[unlikely]] {
		log::log(WARN << "Entity " << entity->get_id() << " has no move component.");
		return time::time_t::from_int(0);
	}

	auto move_component = std::dynamic_pointer_cast<component::Move>(
		entity->get_component(component::component_t::MOVE));
	++move_component->generation;
	auto move_ability = move_component->get_ability();
	auto move_path_grid = move_ability.get<nyan::ObjectValue>("Move.path_type");
	auto pos_component = entity->try_get_component_as<component::Position>(component::component_t::POSITION);
	const auto current_pos = pos_component->get_positions().get(start_time);

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
		log::log(DBG << "Move::move_default: entity=" << entity->get_id()
		             << " nav_service=yes start=(" << start_cell.x << "," << start_cell.y << ")"
		             << " dest=(" << target_cell.x << "," << target_cell.y << ")"
		             << " mover_rx=" << mover_rx << " mover_ry=" << mover_ry);
		const auto occupancy = map->get_dynamic_occupancy();
		const component::FootprintBounds target_bounds{
			target_cell.x + 0.5 - mover_rx, target_cell.y + 0.5 - mover_ry, target_cell.x + 0.5 + mover_rx, target_cell.y + 0.5 + mover_ry};
		pathfinding::NavigationService::Path path_result;
		// Searching the whole map cannot resolve an occupied destination.
		if (not occupancy or occupancy->can_occupy(entity->get_id(), target_bounds)) {
			path_result = nav_service->request_path(start_cell, target_cell, mover, [&](auto from, auto to) {
				if (not occupancy) {
					return true;
				}
				const auto a = from == start_cell ? current_pos : coord::tile{from.x, from.y}.to_phys3_center();
				const auto b = coord::tile{to.x, to.y}.to_phys3_center();
				const component::FootprintBounds begin{a.ne.to_double() - mover_rx, a.se.to_double() - mover_ry, a.ne.to_double() + mover_rx, a.se.to_double() + mover_ry};
				const component::FootprintBounds end{b.ne.to_double() - mover_rx, b.se.to_double() - mover_ry, b.ne.to_double() + mover_rx, b.se.to_double() + mover_ry};
				return occupancy->can_occupy(entity->get_id(), ::openage::scenario::swept_aabb(begin, end));
			});
		}
		if (path_result.result != pathfinding::GridAStarPathfinder::Result::PATH_FOUND) {
			// A temporary occupant must not make a reachable destination disappear.
			// The movement clamp preserves clearance while the order waits to replan.
			path_result = nav_service->request_path(start_cell, target_cell, mover);
		}
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
			if (loop and entity->get_manager()) {
				openage::event::EventHandler::param_map::map_t arrived_params{
					{"entity_id", entity->get_id()},
					{"status", std::string{status}},
					{"waypoint_x", static_cast<int64_t>(target_cell.x)},
					{"waypoint_y", static_cast<int64_t>(target_cell.y)},
				};
				auto target = entity->get_manager();
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
		log::log(DBG << "Move::move_default: A* waypoints=" << waypoints.size()
		                   << " segments=" << (waypoints.size() > 0 ? waypoints.size() - 1 : 0));
	}
	else {
		// Legacy flow-field pathfinder path. Kept for tests that
		// build a Map without a NavigationService (no mission pack
		// loader, only synthetic terrain).
		auto pathfinder = map->get_pathfinder();
		log::log(DBG << "Move::move_default: entity=" << entity->get_id()
		             << " path_type=" << move_path_grid->get_name()
		             << " current=(" << current_pos.ne << "," << current_pos.se << ")"
		             << " dest=(" << destination.ne << "," << destination.se << ")");
		auto grid_id = map->get_grid_id(move_path_grid->get_name());
		waypoints = find_path(pathfinder, grid_id, current_pos, destination, start_time);
		log::log(DBG << "Move::move_default: grid_id=" << grid_id
		                   << " waypoints=" << waypoints.size() << " total_time_so_far=0");
	}

	const auto planned_goal = waypoints.size() > 1 ? waypoints.back() : current_pos;
	const auto duration = Move::move_along_path(entity, state, std::move(waypoints), start_time);
	const auto reached = pos_component->get_positions().get(start_time + duration);
	const bool obstructed = reached.ne != planned_goal.ne or reached.se != planned_goal.se;
	// Bound persistent obstructions so an unreachable order cannot queue forever.
	if (retry_on_block and obstructed and attempt < 120
	    and state->get_event_loop() and entity->get_manager()) {
		state->get_event_loop()->create_event("game.movement_arrived", entity->get_manager(), state, start_time + duration + time::time_t::from_double(0.25), {
																																								  {"entity_id", entity->get_id()},
																																								  {"retry_target", destination},
																																								  {"generation", move_component->generation},
																																								  {"attempt", attempt + 1},
																																							  });
	}
	else if (retry_on_block and obstructed and attempt == 120) {
		log::log(WARN << "Movement replan limit reached for entity " << entity->get_id());
	}
	return duration;
}


const time::time_t Move::move_along_path(const std::shared_ptr<GameEntity> &entity,
                                         const std::shared_ptr<GameState> &state,
                                         std::vector<coord::phys3> waypoints,
                                         const time::time_t &start_time) {
	auto move = entity->try_get_component_as<component::Move>(component::component_t::MOVE);
	auto pos_component = entity->try_get_component_as<component::Position>(component::component_t::POSITION);
	if (not move or not pos_component or waypoints.empty()) {
		return time::time_t::zero();
	}
	++move->generation;
	auto move_ability = move->get_ability();
	auto move_speed = move_ability.get<nyan::Float>("Move.speed");
	auto &positions = pos_component->get_positions();
	auto current_pos = positions.get(start_time);
	auto map = state->get_map();
    if (map) for (auto &point : waypoints) point.up = coord::phys_t::from_double(map->get_terrain()->height_at(point.ne.to_double(), point.se.to_double()));
	auto dyn_occ = map ? map->get_dynamic_occupancy() : nullptr;
	const auto footprint = entity->has_footprint() ? entity->footprint() : component::Footprint{};
	auto occ_bounds_at = [&](const coord::phys3 &p) {
		const double x = p.ne.to_double();
		const double y = p.se.to_double();
		return component::FootprintBounds{x - footprint.radius_x(), y - footprint.radius_y(),
		                                  x + footprint.radius_x(), y + footprint.radius_y()};
	};
	if (dyn_occ) {
		dyn_occ->register_entity(entity->get_id(), occ_bounds_at(current_pos));
	}

	// The path starts in the current cell, which may differ from its center.
	if (not waypoints.empty()) {
		waypoints.front() = current_pos;
	}
	double total_time = 0;
	pos_component->set_position(start_time, current_pos);
	log::log(DBG << "Move::move_default: move_speed.get()=" << move_speed.get()
	                   << " move_speed->is_infinite_positive()=" << move_speed->is_infinite_positive());
	auto event_loop = state->get_event_loop();
	const size_t segment_count = waypoints.size() > 0 ? waypoints.size() - 1 : 0;
	// P4.4F: `blocked` is set when a segment is clamped by a
	// dynamic blocker; the loop then breaks and a BLOCKED
	// MovementArrivedEvent is dispatched after it. `blocked_at`
	// remembers the clamped waypoint for that event.
	bool blocked = false;
	coord::phys3 blocked_at{0, 0, 0};
	for (size_t i = 1; i < waypoints.size(); ++i) {
		auto prev_waypoint = waypoints[i - 1];
		auto cur_waypoint = waypoints[i];

		auto path_vector = cur_waypoint - prev_waypoint;
		if (cur_waypoint == prev_waypoint) {
			continue;
		}
		auto path_angle = path_vector.to_angle();

		// P4.4F: pre-check the swept AABB against
		// DynamicOccupancy. If a NON-self entity's bounds
		// overlap the swept volume, the segment is blocked.
		// Clamp the committed waypoint to a collision-free
		// sub-target via bisection on can_occupy, then break
		// the segment loop after emitting events so the unit
		// moves to the safe point and halts.
		coord::phys3 committed_waypoint = cur_waypoint;
		if (dyn_occ) {
			openage::gamestate::component::FootprintBounds swept
				= ::openage::scenario::swept_aabb(
					occ_bounds_at(prev_waypoint),
					occ_bounds_at(cur_waypoint));
			auto blockers = dyn_occ->query(
				entity->get_id(), swept);
			if (not blockers.empty()) {
				blocked = true;
				// Bisect along prev->cur for the largest t in
				// [0,1] whose footprint does not overlap any
				// other registered entity. can_occupy excludes
				// self. normalize(length*t) is unit-safe (no
				// manual 65536 math) -- see coord/phys.h:57-58.
				double t_safe = 0.0;
				double seg_len = path_vector.length();
				if (seg_len > 0.0) {
					double lo = 0.0, hi = 1.0;
					for (int step = 0; step < 8; ++step) {
						double mid = (lo + hi) * 0.5;
						auto test_point = prev_waypoint
						                  + path_vector.normalize(seg_len * mid);
						if (dyn_occ->can_occupy(entity->get_id(), ::openage::scenario::swept_aabb(occ_bounds_at(prev_waypoint), occ_bounds_at(test_point)))) {
							t_safe = mid;
							lo = mid;
						}
						else {
							hi = mid;
						}
					}
				}
				if (t_safe <= 0.0) {
					// Already touching the blocker: unit stays
					// put (zero-length committed segment).
					committed_waypoint = prev_waypoint;
				}
				else {
					committed_waypoint = prev_waypoint
						+ path_vector.normalize(seg_len * t_safe);
				}
				log::log(MSG(warn) << "Move::move_default: segment blocked by "
				             << blockers.size()
				             << " dynamic entities, clamped to t_safe=" << t_safe
				             << " entity=" << entity->get_id());
			}
		}

		if (committed_waypoint != prev_waypoint) {
			pos_component->set_angle(start_time + total_time, path_angle);
		}

		// movement -- use the (possibly clamped) committed waypoint.
		double move_time = 0;
		if (not move_speed->is_infinite_positive()) {
			auto distance = (committed_waypoint - prev_waypoint).length();
			move_time = distance / move_speed->get();
		}
		total_time += move_time;

		pos_component->set_position(start_time + total_time, committed_waypoint);

		// P4.4F: update DynamicOccupancy after each segment
		// commit so subsequent movers see the latest footprint
		// position. On a BLOCKED segment the committed waypoint
		// is the clamped sub-target, so occupancy tracks where
		// the unit actually stops (not the through-blocker
		// endpoint).
		//
		// MVP limitation (occupancy/curve sync): for UNBLOCKED
		// multi-segment moves, occupancy advances through every
		// segment endpoint AT SETUP TIME, while the visible
		// position curve interpolates over real time. So
		// occupancy LEADS the interpolated position for all but
		// the last segment. True curve-tracking would require a
		// time-based game.waypoint_reached observer that advances
		// occupancy per-segment as the curve reaches each keyframe
		// -- that is deferred (new event handler wiring is out of
		// MVP scope). The clamp logic here only fixes the blocked
		// case; unblocked moves keep the existing setup-time
		// advancement.
		if (dyn_occ) {
			openage::gamestate::component::FootprintBounds prev_b
				= occ_bounds_at(prev_waypoint);
			openage::gamestate::component::FootprintBounds cur_b
				= occ_bounds_at(committed_waypoint);
			// DynamicOccupancy::update_entity does an old->new
			// bucket diff; we pass the committed bounds as `new`.
			dyn_occ->update_entity(entity->get_id(),
				prev_b, cur_b);
		}

		// P4.4C: dispatch a WaypointReachedEvent at the end of
		// every segment. The event is created at the segment's
		// end time so the diagnostic stream records the arrival
		// in the same order as the Position Curve's keyframes.
		// On a blocked segment this reports the committed
		// (clamped) waypoint, matching the keyframe the curve
		// actually reaches.
		if (event_loop and entity->get_manager()) {
			auto wp_tile = committed_waypoint.to_tile();
			openage::event::EventHandler::param_map::map_t wp_params{
				{"entity_id", entity->get_id()},
				{"segment_index", static_cast<int64_t>(i)},
				{"segment_count", static_cast<int64_t>(segment_count)},
				{"waypoint_x", static_cast<int64_t>(wp_tile.ne)},
				{"waypoint_y", static_cast<int64_t>(wp_tile.se)},
			};
			auto target = entity->get_manager();
			event_loop->create_event("game.waypoint_reached", target,
				state, start_time + total_time, wp_params);
		}

		// P4.4F: a blocked segment is the last committed
		// segment -- stop after the safe point so the unit
		// halts at the blocker edge instead of pathing
		// through it. The BLOCKED arrived event is dispatched
		// after the loop.
		if (blocked) {
			blocked_at = committed_waypoint;
			break;
		}
	}

	// P4.4C/F: dispatch MovementArrivedEvent at the end of the
	// move. status=BLOCKED when a segment was clamped by a
	// dynamic blocker and the loop broke early; status=OK when
	// the unit reached its final waypoint. Zero-waypoint paths
	// were already filtered above as failures.
	if (event_loop and entity->get_manager() and blocked) {
		auto blocked_tile = blocked_at.to_tile();
		openage::event::EventHandler::param_map::map_t arrived_params{
			{"entity_id", entity->get_id()},
			{"status", std::string{"BLOCKED"}},
			{"waypoint_x", static_cast<int64_t>(blocked_tile.ne)},
			{"waypoint_y", static_cast<int64_t>(blocked_tile.se)},
		};
		auto target = entity->get_manager();
		event_loop->create_event("game.movement_arrived", target,
			state, start_time + total_time, arrived_params);
	}
	else if (event_loop and entity->get_manager() and not waypoints.empty()) {
		auto final_tile = waypoints.back().to_tile();
		openage::event::EventHandler::param_map::map_t arrived_params{
			{"entity_id", entity->get_id()},
			{"status", std::string{"OK"}},
			{"waypoint_x", static_cast<int64_t>(final_tile.ne)},
			{"waypoint_y", static_cast<int64_t>(final_tile.se)},
		};
		auto target = entity->get_manager();
		event_loop->create_event("game.movement_arrived", target,
			state, start_time + total_time, arrived_params);
	}

	// Verify the curve now has a future keyframe (Phase 0.5B bug #13 follow-up:
	// Continuous::set_last erases all elements that follow at a later time, so
	// each new move_default wipes future keyframes. Log where the dest keyframe
	// landed and what a mid-point query returns, so a stuck villager is
	// attributable to either the math here or the curve semantics).
	log::log(DBG << "Move::move_default: total_time=" << total_time
	                   << " end keyframe at t+" << total_time << " (=start_time + total_time)");
	if (total_time > 0) {
		auto query_t = start_time + time::time_t::from_double(total_time * 0.5);
		auto mid_pos = positions.get(query_t);
		log::log(DBG << "Move::move_default: mid-t pos=(" << mid_pos.ne << "," << mid_pos.se
		                   << ") (expect ~partway between current and dest)");
	}

	if (total_time > 0) {
		entity->play_animation(move_ability, start_time);
	}
	if (entity->has_component(component::component_t::IDLE)) {
		Idle::idle(entity, start_time + total_time);
	}

	return total_time;
}

void Move::halt(const std::shared_ptr<GameEntity> &entity,
                const std::shared_ptr<GameState> &state,
                const time::time_t &time) {
	if (auto move = entity->try_get_component_as<component::Move>(component::component_t::MOVE)) {
		++move->generation;
	}
	if (auto position = entity->try_get_component_as<component::Position>(component::component_t::POSITION)) {
		const auto point = position->get_positions().get(time);
		position->set_position(time, point);
		position->set_angle(time, position->get_angles().get(time));
		if (auto map = state->get_map(); map and entity->has_component(component::component_t::MOVE)) {
			register_entity_in_dynamic_occupancy(*map, *entity, point.ne.to_double(), point.se.to_double());
		}
	}
}

} // namespace openage::gamestate::system
