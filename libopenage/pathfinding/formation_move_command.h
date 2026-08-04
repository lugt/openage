// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4F.1: FormationMoveCommand -- the runtime-level group
// move command. The Commander receives one FormationMoveCommand
// per player RMB; the runtime expands it into N per-entity
// MovementTasks via FormationPlanner.
//
// Why a separate command type instead of N MoveCommands:
//   - The Commander API only takes ONE command per player
//     click. A multi-entity move is a single intent, not N
//     parallel intents. The FormationMoveCommand makes this
//     explicit.
//   - Generation: every FormationMoveCommand carries a
//     generation id. New commands cancel old ones; the
//     Move runtime checks the generation on every waypoint
//     and reservation update. Stale waypoints from an
//     old generation are no-ops (they don't update Position,
//     don't release reservations belonging to the new
//     command).
//   - Per-entity destination is computed at planning time,
//     not at runtime; the runtime just executes the
//     assignments.

#pragma once

#include <cstdint>
#include <vector>

#include "gamestate/component/api/footprint.h"
#include "gamestate/types.h"
#include "pathfinding/formation_planner.h"


namespace openage::pathfinding {


/**
 * A single entity's destination within a group move. The
 * `slot_index` is the 0..N-1 stable grid position the
 * FormationPlanner picked; reservation priority uses it
 * to break ties when two units want the same cell.
 */
struct FormationAssignment {
	openage::gamestate::entity_id_t entity{0};
	uint32_t slot_index{0};
	openage::gamestate::component::FootprintBounds bounds;
	double destination_x{0.0};
	double destination_y{0.0};
};


/**
 * A single group-move command. The runtime holds ONE
 * FormationMoveCommand per (generation, slot_index, entity)
 * tuple; new commands replace old ones at the runtime
 * boundary (per spec §5).
 */
struct FormationMoveCommand {
	// Unique id within the simulation. The runtime uses
	// this to distinguish overlapping commands (e.g.
	// when an old MoveTask is in flight and a new
	// group command replaces it).
	uint64_t command_id{0};
	// Generation: NEWER > OLDER. Stale generation
	// waypoints / events are no-ops.
	uint64_t generation{0};
	// Simulation time the command was issued.
	openage::time::time_t timestamp{0};
	// Group result status. Updated by the runtime as
	// each unit transitions (RUNNING -> COMPLETED /
	// FAILED / CANCELLED).
	enum class Status : uint8_t {
		RUNNING = 0,
		COMPLETED,
		PARTIAL_FAILURE,
		FAILED,
		CANCELLED,
	};
	Status status{Status::RUNNING};
	// The group centre the user requested.
	double requested_center_x{0.0};
	double requested_center_y{0.0};
	// Per-entity assignments, in entity-id ascending
	// order. The runtime iterates this list and
	// dispatches a MoveTask per assignment.
	std::vector<FormationAssignment> assignments;
};


} // namespace openage::pathfinding
