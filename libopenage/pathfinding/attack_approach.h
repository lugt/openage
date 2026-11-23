// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "coord/phys.h"
#include "gamestate/types.h"
#include "pathfinding/navigation_service.h"
#include "time/time.h"

namespace openage::gamestate {
class GameState;
class GameEntity;
}

namespace openage::pathfinding {

enum class AttackApproachStatus : uint8_t {
	APPROACH_FOUND = 0,
	APPROACH_FAILED,
};

struct AttackApproachResult {
	AttackApproachStatus status{AttackApproachStatus::APPROACH_FAILED};
	gamestate::scenario::CellPosition approach_cell{0, 0};
	std::vector<gamestate::scenario::CellPosition> waypoints;
	std::string reason;
};

/** Distance used by both the approach planner and the attack event. */
double attack_distance(const gamestate::GameEntity &attacker,
                       const coord::phys3 &attacker_pos,
                       const gamestate::GameEntity &target,
                       const coord::phys3 &target_pos);

/**
 * Find a reachable, unoccupied attack position using current positions
 * and the destinations already claimed by other movers. Waypoints
 * include the start and destination cells.
 */
AttackApproachResult find_attack_approach(
	const std::shared_ptr<gamestate::GameState> &state,
	gamestate::entity_id_t attacker_id,
	gamestate::entity_id_t target_id,
	const NavigationService &navigation,
	const time::time_t &time = time::time_t::zero());

} // namespace openage::pathfinding
