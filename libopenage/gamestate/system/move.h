// Copyright 2023-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <memory>
#include <vector>

#include "coord/phys.h"
#include "time/time.h"


namespace openage::gamestate {
class GameEntity;
class GameState;

namespace system {

class Move {
public:
	/**
	 * Move a game entity to a destination from a move command.
	 *
	 * @param entity Game entity.
	 * @param state Game state.
	 * @param start_time Start time of change.
	 *
	 * @return Runtime of the change in simulation time.
	 */
	static const time::time_t move_command(const std::shared_ptr<gamestate::GameEntity> &entity,
	                                       const std::shared_ptr<openage::gamestate::GameState> &state,
	                                       const time::time_t &start_time);

	/**
	 * Move a game entity to a destination.
	 *
	 * @param entity Game entity.
	 * @param state Game state.
	 * @param destination Destination coordinates.
	 * @param start_time Start time of change.
	 *
	 * @return Runtime of the change in simulation time.
	 */
	static const time::time_t move_default(const std::shared_ptr<gamestate::GameEntity> &entity,
	                                       const std::shared_ptr<openage::gamestate::GameState> &state,
	                                       const coord::phys3 &destination,
	                                       const time::time_t &start_time);

	/**
	 * Follow a planned route through the same collision and animation logic
	 * as a normal move. The first waypoint is replaced by the current position.
	 */
	static const time::time_t move_along_path(const std::shared_ptr<GameEntity> &entity,
	                                          const std::shared_ptr<GameState> &state,
	                                          std::vector<coord::phys3> waypoints,
	                                          const time::time_t &start_time);

	/** Cancel planned movement and release its destination in dynamic occupancy. */
	static void halt(const std::shared_ptr<GameEntity> &entity,
	                 const std::shared_ptr<GameState> &state,
	                 const time::time_t &time);
};

} // namespace system
} // namespace openage::gamestate
