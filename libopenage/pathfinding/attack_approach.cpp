// Copyright 2026 the openage authors. See copying.md for legal info.

#include "pathfinding/attack_approach.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <tuple>
#include <utility>

#include "coord/tile.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "scenario/swept_aabb.h"


namespace openage::pathfinding {

namespace {

using gamestate::component::Footprint;
using gamestate::component::Position;
using gamestate::component::component_t;
using gamestate::scenario::CellPosition;

Footprint footprint_at(const gamestate::GameEntity &entity, const coord::phys3 &position) {
	auto footprint = entity.has_footprint() ? entity.footprint() : Footprint{};
	footprint.set_center(position.ne.to_double(), position.se.to_double());
	return footprint;
}

coord::phys3 cell_center(CellPosition cell) {
	return coord::tile{cell.x, cell.y}.to_phys3_center();
}

} // namespace


double attack_distance(const gamestate::GameEntity &attacker,
                       const coord::phys3 &attacker_pos,
                       const gamestate::GameEntity &target,
                       const coord::phys3 &target_pos) {
	// Keep unit combat unchanged. For buildings, measure from the edge
	// of the attacker's footprint to the edge of the building.
	if (not target.has_footprint() or target.has_component(component_t::MOVE)) {
		return std::hypot((attacker_pos.ne - target_pos.ne).to_double(),
		                  (attacker_pos.se - target_pos.se).to_double());
	}
	const auto a = footprint_at(attacker, attacker_pos).bounds();
	const auto b = footprint_at(target, target_pos).bounds();
	const auto dx = std::max({b.min_x - a.max_x, a.min_x - b.max_x, 0.0});
	const auto dy = std::max({b.min_y - a.max_y, a.min_y - b.max_y, 0.0});
	return std::hypot(dx, dy);
}


AttackApproachResult find_attack_approach(
	const std::shared_ptr<gamestate::GameState> &state,
	gamestate::entity_id_t attacker_id,
	gamestate::entity_id_t target_id,
	const NavigationService &navigation,
	const time::time_t &time) {
	AttackApproachResult out;
	if (not state->has_game_entity(attacker_id)) {
		out.reason = "attacker_removed";
		return out;
	}
	if (not state->has_game_entity(target_id)) {
		out.reason = "target_removed";
		return out;
	}

	const auto attacker = state->get_game_entity(attacker_id);
	const auto target = state->get_game_entity(target_id);
	if (not attacker->has_component(component_t::MOVE)) {
		out.reason = "attacker_no_move_ability";
		return out;
	}
	const auto attack = attacker->try_get_component_as<gamestate::component::Attack>(component_t::ATTACK);
	const auto attacker_position = attacker->try_get_component_as<Position>(component_t::POSITION);
	const auto target_position = target->try_get_component_as<Position>(component_t::POSITION);
	if (not attack or not attacker_position or not target_position) {
		out.reason = "missing_attack_or_position";
		return out;
	}

	const auto attacker_pos = attacker_position->get_positions().get(time);
	const auto target_pos = target_position->get_positions().get(time);
	const auto attacker_fp = footprint_at(*attacker, attacker_pos);
	const auto target_fp = footprint_at(*target, target_pos);
	const auto target_bounds = target_fp.bounds();
	const auto rx = attacker_fp.radius_x();
	const auto ry = attacker_fp.radius_y();
	const auto range = attack->get_attack_range();
	const auto size = state->get_map()->get_size();
	const auto start_tile = attacker_pos.to_tile();
	const CellPosition start{static_cast<int>(start_tile.ne), static_cast<int>(start_tile.se)};
	const auto occupancy = state->get_map()->get_dynamic_occupancy();
	auto can_traverse = [&](const coord::phys3 &from, const coord::phys3 &to) {
		const auto swept = ::openage::scenario::swept_aabb(
			footprint_at(*attacker, from).bounds(), footprint_at(*attacker, to).bounds());
		return not swept.overlaps(target_bounds)
		    and (not occupancy or occupancy->can_occupy(attacker_id, swept));
	};
	auto can_step = [&](CellPosition from, CellPosition to) {
		return can_traverse(from == start ? attacker_pos : cell_center(from), cell_center(to));
	};

	struct Candidate {
		CellPosition cell;
		int min_steps;
		double distance;
	};
	std::vector<Candidate> candidates;
	const int min_x = std::max(0, static_cast<int>(std::floor(target_bounds.min_x - rx - range)));
	const int min_y = std::max(0, static_cast<int>(std::floor(target_bounds.min_y - ry - range)));
	const int max_x = std::min(static_cast<int>(size[0]) - 1,
	                           static_cast<int>(std::floor(target_bounds.max_x + rx + range)));
	const int max_y = std::min(static_cast<int>(size[1]) - 1,
	                           static_cast<int>(std::floor(target_bounds.max_y + ry + range)));
	for (int y = min_y; y <= max_y; ++y) {
		for (int x = min_x; x <= max_x; ++x) {
			const CellPosition cell{x, y};
			const auto destination = cell_center(cell);
			const auto distance = attack_distance(*attacker, destination, *target, target_pos);
			if (distance <= range and navigation.grid()->is_walkable(cell)
			    and can_traverse(cell == start ? attacker_pos : destination, destination)) {
				candidates.push_back({cell, std::max(std::abs(x - start.x), std::abs(y - start.y)), distance});
			}
		}
	}
	std::sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) {
		return std::tie(a.min_steps, a.distance, a.cell.y, a.cell.x)
		     < std::tie(b.min_steps, b.distance, b.cell.y, b.cell.x);
	});
	std::optional<std::tuple<size_t, double, int, int>> best;
	for (const auto &candidate : candidates) {
		if (best and static_cast<size_t>(candidate.min_steps) >= std::get<0>(*best)) {
			break; // An A* path includes the start cell as well.
		}
		auto path = navigation.request_path(start, candidate.cell, {rx, ry}, can_step);
		if (path.result != GridAStarPathfinder::Result::PATH_FOUND) {
			continue;
		}
		const auto score = std::tuple{path.waypoints.size(), candidate.distance, candidate.cell.y, candidate.cell.x};
		if (not best or score < *best) {
			best = score;
			out.status = AttackApproachStatus::APPROACH_FOUND;
			out.approach_cell = candidate.cell;
			out.waypoints = std::move(path.waypoints);
			out.reason = "ok";
		}
	}
	if (out.status == AttackApproachStatus::APPROACH_FAILED) {
		out.reason = "no_candidate";
	}
	return out;
}

} // namespace openage::pathfinding
