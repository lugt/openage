// Copyright 2026 the openage authors. See copying.md for legal info.

#include "pathfinding/attack_approach.h"

#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/footprint.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "log/log.h"
#include "log/message.h"
#include "pathfinding/grid_astar.h"
#include "scenario/static_occupancy_grid.h"

#include <cmath>
#include <cstdint>
#include <set>
#include <utility>


namespace openage::pathfinding {


namespace {


using openage::gamestate::component::Footprint;
using openage::gamestate::component::FootprintBounds;
using openage::gamestate::component::Position;
using openage::gamestate::scenario::CellPosition;


// Footprint of an entity at its current Position.
// Returns std::nullopt if the entity has no Footprint or no
// Position component.
struct CenteredFootprint {
	int center_x{0};
	int center_y{0};
	double radius_x{0.0};
	double radius_y{0.0};
};

std::optional<CenteredFootprint> centered_footprint_of(
	const std::shared_ptr<openage::gamestate::GameState> &state,
	openage::gamestate::entity_id_t id) {
	if (not state->has_game_entity(id)) {
		return std::nullopt;
	}
	auto e = state->get_game_entity(id);
	if (not e->has_footprint()
	    or not e->has_component(
		    openage::gamestate::component::component_t::POSITION)) {
		return std::nullopt;
	}
	auto fp = e->footprint();
	auto pc = std::dynamic_pointer_cast<Position>(
		e->get_component(
			openage::gamestate::component::component_t::POSITION));
	if (not pc) {
		return std::nullopt;
	}
	auto sp = pc->get_positions().get(openage::time::time_t{0});
	CenteredFootprint cf;
	cf.center_x = static_cast<int>(std::floor(sp.ne.to_double()));
	cf.center_y = static_cast<int>(std::floor(sp.se.to_double()));
	cf.radius_x = fp.radius_x();
	cf.radius_y = fp.radius_y();
	return cf;
}


// AABB shortest distance between two rectangles (axis-aligned,
// closed-open [min, max)). The metric is Chebyshev on the
// dx/dy gaps: we want the attacker to be able to attack by
// reaching the target's footprint edge within the attack range,
// which is the same notion AttackHandler already uses (the
// runtime computes dist from the attacker's CENTER to the
// target's CENTER; we instead compute the AABB gap so a
// large-footprint attacker can stand at the edge). The
// Chebyshev distance is the correct metric for grid-aligned
// 8-direction movement.
double aabb_shortest_distance(
	const CenteredFootprint &a,
	const CenteredFootprint &b) {
	double ax_min = a.center_x - a.radius_x;
	double ax_max = a.center_x + a.radius_x;
	double ay_min = a.center_y - a.radius_y;
	double ay_max = a.center_y + a.radius_y;
	double bx_min = b.center_x - b.radius_x;
	double bx_max = b.center_x + b.radius_x;
	double by_min = b.center_y - b.radius_y;
	double by_max = b.center_y + b.radius_y;
	double dx = std::max({bx_min - ax_max, ax_min - bx_max, 0.0});
	double dy = std::max({by_min - ay_max, ay_min - by_max, 0.0});
	// Chebyshev: max(|dx|, |dy|) for grid-aligned 8-direction
	// movement (matches the Move runtime's tile-step metric).
	return std::max(dx, dy);
}


// Does the inflated attacker footprint (radius += mover half-
// extents) overlap `b`? Used to reject candidates whose
// attacker footprint would touch the target's footprint.
bool attacker_overlaps_target(
	const CenteredFootprint &attacker_at_candidate,
	const CenteredFootprint &target) {
	double ax_min = attacker_at_candidate.center_x - attacker_at_candidate.radius_x;
	double ax_max = attacker_at_candidate.center_x + attacker_at_candidate.radius_x;
	double ay_min = attacker_at_candidate.center_y - attacker_at_candidate.radius_y;
	double ay_max = attacker_at_candidate.center_y + attacker_at_candidate.radius_y;
	double bx_min = target.center_x - target.radius_x;
	double bx_max = target.center_x + target.radius_x;
	double by_min = target.center_y - target.radius_y;
	double by_max = target.center_y + target.radius_y;
	return ax_min < bx_max && bx_min < ax_max
	    && ay_min < by_max && by_min < ay_max;
}


// Enumerate candidate cells around the target's footprint.
// For a target covering tile range [tx_min, tx_max) x [ty_min,
// ty_max), the candidate ring is the cells at
// (x, ty_min-1) for x in [tx_min-1, tx_max] (south edge),
// (x, ty_max) for x in [tx_min-1, tx_max] (north edge),
// (tx_min-1, y) for y in [ty_min, ty_max] (west edge),
// (tx_max, y) for y in [ty_min-1, ty_max] (east edge).
// The corner cells are included by both adjacent edge sweeps
// but deduplicated by the std::set below.
std::vector<CellPosition> enumerate_candidates(
	const CenteredFootprint &target) {
	int tx_min = target.center_x - static_cast<int>(std::ceil(target.radius_x));
	int tx_max = target.center_x + static_cast<int>(std::ceil(target.radius_x));
	int ty_min = target.center_y - static_cast<int>(std::ceil(target.radius_y));
	int ty_max = target.center_y + static_cast<int>(std::ceil(target.radius_y));
	std::set<std::pair<int, int>> uniq;
	for (int x = tx_min - 1; x <= tx_max; ++x) {
		uniq.insert({x, ty_min - 1});
		uniq.insert({x, ty_max});
	}
	for (int y = ty_min - 1; y <= ty_max; ++y) {
		uniq.insert({tx_min - 1, y});
		uniq.insert({tx_max, y});
	}
	std::vector<CellPosition> out;
	out.reserve(uniq.size());
	for (const auto &p : uniq) {
		out.push_back({p.first, p.second});
	}
	// Sort by (cell_y, cell_x) for deterministic iteration.
	std::sort(out.begin(), out.end(),
		[](const CellPosition &a, const CellPosition &b) {
			if (a.y != b.y) return a.y < b.y;
			return a.x < b.x;
		});
	return out;
}


// Best survivor: lowest path_cost, then lowest distance, then
// lowest (y, x). Comparison is strict so the std::sort uses a
// strict weak ordering.
struct Survivor {
	CellPosition cell;
	std::vector<CellPosition> path;
	int path_cost{0};
	double distance{0.0};
};

bool survivor_less(const Survivor &a, const Survivor &b) {
	if (a.path_cost != b.path_cost) return a.path_cost < b.path_cost;
	if (a.distance != b.distance) return a.distance < b.distance;
	if (a.cell.y != b.cell.y) return a.cell.y < b.cell.y;
	return a.cell.x < b.cell.x;
}


}  // namespace


AttackApproachResult find_attack_approach(
	const std::shared_ptr<openage::gamestate::GameState> &state,
	openage::gamestate::entity_id_t attacker_id,
	openage::gamestate::entity_id_t target_id,
	const NavigationService &navigation) {
	AttackApproachResult out;
	out.status = AttackApproachStatus::APPROACH_FAILED;

	if (not state->has_game_entity(attacker_id)) {
		out.reason = "attacker_removed";
		return out;
	}
	if (not state->has_game_entity(target_id)) {
		out.reason = "target_removed";
		return out;
	}

	auto attacker_entity = state->get_game_entity(attacker_id);
	auto target_entity = state->get_game_entity(target_id);

	// Attacker must have a Move ability + a Footprint + a Position.
	if (not attacker_entity->has_component(
		    openage::gamestate::component::component_t::MOVE)) {
		out.reason = "attacker_no_move_ability";
		return out;
	}
	auto attacker_fp = centered_footprint_of(state, attacker_id);
	if (not attacker_fp.has_value()) {
		out.reason = "attacker_no_footprint";
		return out;
	}
	auto target_fp = centered_footprint_of(state, target_id);
	if (not target_fp.has_value()) {
		out.reason = "target_no_footprint";
		return out;
	}

	// Attack range from the Attack component (single source of
	// truth; the runtime uses the same value via
	// get_attack_range()).
	double range = 0.0;
	if (attacker_entity->has_component(
		    openage::gamestate::component::component_t::ATTACK)) {
		auto atk = std::dynamic_pointer_cast<
			openage::gamestate::component::Attack>(
			attacker_entity->get_component(
				openage::gamestate::component::component_t::ATTACK));
		if (atk) {
			range = atk->get_attack_range();
		}
	}
	else {
		out.reason = "attacker_no_attack_ability";
		return out;
	}

	// Mover half-extents for the NavigationService inflation.
	double mover_rx = attacker_fp->radius_x;
	double mover_ry = attacker_fp->radius_y;
	MoverFootprint mover{mover_rx, mover_ry};

	// Map extent (from the NavigationGrid's underlying terrain
	// chunks). For Mission 1 this is (0, 0, 16, 16); the
	// terrain-extent API would be a longer refactor; for the
	// MVP we extract from the candidate set itself by
	// clamping into [0, 16) x [0, 16) and letting the
	// NavigationService filter.
	constexpr int MAP_MIN_X = 0;
	constexpr int MAP_MIN_Y = 0;
	constexpr int MAP_MAX_X = 16;
	constexpr int MAP_MAX_Y = 16;

	auto candidates = enumerate_candidates(*target_fp);

	std::vector<Survivor> survivors;
	survivors.reserve(candidates.size());

	for (const auto &cand : candidates) {
		// Reject cells outside the map.
		if (cand.x < MAP_MIN_X or cand.x >= MAP_MAX_X
		    or cand.y < MAP_MIN_Y or cand.y >= MAP_MAX_Y) {
			continue;
		}
		// Build the attacker's footprint at the candidate.
		CenteredFootprint at_cand{
			cand.x, cand.y,
			attacker_fp->radius_x,
			attacker_fp->radius_y};
		// Reject if the attacker's footprint overlaps the
		// target's footprint (the attacker would be standing
		// inside the enemy).
		if (attacker_overlaps_target(at_cand, *target_fp)) {
			continue;
		}
		// Reject if the candidate is more than `range` away
		// from the target (AABB-shortest Chebyshev distance).
		double d = aabb_shortest_distance(at_cand, *target_fp);
		if (d > range) {
			continue;
		}
		// Verify the A* can reach the candidate. The mover
		// inflation has to be considered; the NavigationService
		// already inflates static obstacles by the mover's
		// half-extents.
		CellPosition start{
			attacker_fp->center_x, attacker_fp->center_y};
		auto path_result = navigation.request_path(
			start, cand, mover);
		if (path_result.result
		    != GridAStarPathfinder::Result::PATH_FOUND) {
			continue;
		}
		Survivor s;
		s.cell = cand;
		s.path = path_result.waypoints;
		s.path_cost = static_cast<int>(path_result.waypoints.size());
		s.distance = d;
		survivors.push_back(std::move(s));
	}

	if (survivors.empty()) {
		out.reason = "no_candidate";
		return out;
	}

	std::sort(survivors.begin(), survivors.end(), survivor_less);
	const auto &best = survivors.front();
	out.status = AttackApproachStatus::APPROACH_FOUND;
	out.approach_cell = best.cell;
	out.waypoints = best.path;
	out.reason = "ok";
	return out;
}


} // namespace openage::pathfinding
