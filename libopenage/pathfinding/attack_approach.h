// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4D.1.3: AttackApproach -- the central service that finds a
// reachable approach position for an attacker to attack a target.
//
// Why this exists:
//   P4.4C's Move runtime accepts any reachable cell as a target.
//   For ATTACK, "target the enemy's center" is wrong -- the
//   attacker's footprint would overlap the target's footprint
//   (TC's 4x4 + Villager's 0.2x0.2 = collision with the enemy).
//   The correct approach is to find a cell adjacent to the
//   target's footprint where the attacker can stand AND reach
//   via the navigation service AND be inside the attack range.
//
// Algorithm:
//   1. Enumerate candidate cells around the target's footprint.
//      - All cells adjacent to the target's bounding box
//        (4 sides + 4 corners = up to 8*k cells for k-radius).
//   2. For each candidate:
//      - Reject if attacker footprint (inflated by mover
//        half-extents via NavigationService) intersects
//        target footprint or any static obstacle.
//      - Reject if outside map bounds.
//      - Compute the AABB-shortest distance from attacker
//        footprint to target footprint.
//      - Reject if distance > attack range (or the
//        AABB-shortest distance is > range).
//   3. For each survivor, run NavigationService::request_path
//      from attacker's current cell to the candidate. A
//      candidate with NO path is rejected (not just unreachable
//      in the cost sense -- the A* must return PATH_FOUND).
//   4. Sort survivors by (path cost, distance to target,
//      cell y, cell x) and pick the lowest.
//
// Output:
//   - APPROACH_FOUND: the chosen cell + the path. The Move
//     runtime uses this path as a normal MoveCommand; the
//     attack command stays ATTACK so the HUD shows ATTACK
//     during approach (no MOVE-mode override).
//   - APPROACH_FAILED: the target is unreachable, the
//     attacker has no Move ability, or every candidate cell
//     is blocked / out-of-range. The runtime emits an
//     ATTACK_FAILED event; the unit stays put.
//
// Determinism: candidates are visited in (cell_y, cell_x) order
// (sorted); the path is deterministic (P4.4C GridAStarPathfinder
// with std::map). No unordered iteration, no FQON lookups.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "gamestate/component/api/footprint.h"
#include "gamestate/types.h"
#include "pathfinding/navigation_service.h"
#include "scenario/static_occupancy_grid.h"


namespace openage::gamestate {
class GameState;
class GameEntity;
}


namespace openage::pathfinding {


/**
 * The shape of an approach result. APPROACH_FAILED is the only
 * failure mode the production runtime cares about; the demo
 * checks the specific reason via the `reason` string for
 * diagnostics.
 */
enum class AttackApproachStatus : uint8_t {
	APPROACH_FOUND = 0,
	APPROACH_FAILED,
};


/**
 * Result of a single find_attack_approach call. When status is
 * APPROACH_FOUND, `waypoints` contains the cell sequence the
 * attacker must traverse (start inclusive, target cell
 * inclusive) and `approach_cell` is the cell the attacker
 * will end up in.
 */
struct AttackApproachResult {
	AttackApproachStatus status{AttackApproachStatus::APPROACH_FAILED};
	openage::gamestate::scenario::CellPosition approach_cell{0, 0};
	std::vector<openage::gamestate::scenario::CellPosition> waypoints;

	/**
	 * Diagnostic reason. Stable across runs (no FQON).
	 * Examples: "no_candidate", "out_of_range", "no_path",
	 * "attacker_no_move_ability", "target_removed".
	 */
	std::string reason;
};


/**
 * Find a reachable approach position for `attacker` to attack
 * `target` via `navigation`.
 *
 * Pre-conditions:
 *   - attacker / target are in the state.
 *   - attacker has a Position + Footprint + Move ability.
 *   - target has a Position + Footprint.
 *   - target is hostile to attacker's owner (the runtime
 *     verifies this; the service trusts the caller's
 *     classification).
 *
 * Pure function: does not mutate state. The runtime is
 * responsible for emitting the resulting MoveCommand (or the
 * failure event).
 */
AttackApproachResult find_attack_approach(
	const std::shared_ptr<openage::gamestate::GameState> &state,
	openage::gamestate::entity_id_t attacker_id,
	openage::gamestate::entity_id_t target_id,
	const NavigationService &navigation);


} // namespace openage::pathfinding
