// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4F.16-22: FormationPlanner -- generates per-unit destination
// positions for a group move command. The Move runtime stays
// per-entity; this planner just decides where each entity goes.
//
// Inputs:
//   - selection: the entities to place (a SelectionSnapshot).
//   - destination: the requested group center (in tile units).
//   - max_footprint: the AABB extent of the largest entity in
//     the selection (used as the slot pitch baseline; the
//     caller computes it from the live entity footprints).
//   - dynamic: DynamicOccupancy for the live simulation (used
//     to reject slots that overlap another unit's footprint).
//   - static_occ: the StaticOccupancyGrid (rejected slots).
//   - reservation: a list of reserved FootprintBounds
//     (rejected slots).
//
// Output:
//   - assignments: a vector of (entity_id, FootprintBounds)
//     in stable order; entity index i gets slot i.
//   - result: status (OK / NO_SPACE) for the caller.
//
// Algorithm (MVP):
//   1. Compute slot count = N (selection size).
//   2. Compute grid dimensions (rows, cols) using
//      cols = ceil(sqrt(N)); rows = ceil(N / cols).
//   3. Compute slot pitch (dx, dy) from max_footprint +
//      a fixed 0.5-tile separation margin.
//   4. Generate candidate slot centres on a grid centred
//      on `destination`.
//   5. For each slot, validate:
//        a. In map bounds.
//        b. No static obstacle (terrain + static grid).
//        c. No dynamic occupancy overlap (excluding self).
//        d. No reservation overlap.
//   6. Assign slots to entities in stable order: iterate
//      the (rows, cols) grid in (y, x) lexicographic order
//      and pull the first M valid slots. M == N.
//   7. If < N valid slots, retry with a 1-tile translation
//      in 8 cardinal directions; if still < N, return
//      NO_SPACE.
//   8. If still NO_SPACE, return the partial best
//      (maximal set) -- the caller is responsible for
//      failing the command cleanly.
//
// Determinism guarantees:
//   - No unordered iteration anywhere in the planner.
//   - The retry translation order is (N, S, E, W, NE, NW,
//     SE, SW) -- fixed.
//   - The (entity -> slot) assignment is the grid order
//     paired with the (selection) order; both are sorted.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "gamestate/component/api/footprint.h"
#include "gamestate/types.h"
#include "scenario/dynamic_occupancy.h"
#include "scenario/static_occupancy_grid.h"


namespace openage::pathfinding {


enum class FormationResult : uint8_t {
	OK = 0,
	NO_SPACE,
};


/**
 * One entity's destination. `bounds` is the entity's full
 * Footprint at the chosen slot (the entity will stand with
 * its center at slot.center).
 */
struct FormationAssignment {
	openage::gamestate::entity_id_t entity;
	openage::gamestate::component::FootprintBounds bounds;
	double slot_center_x;
	double slot_center_y;
};


/**
 * Result of a single plan call. `assignments.size() ==
 * selection.size()` on OK; < on NO_SPACE (partial best).
 */
struct FormationPlan {
	FormationResult result{FormationResult::OK};
	std::vector<FormationAssignment> assignments;
	std::string reason;
};


/**
 * Plan the formation. The caller is responsible for
 * computing max_footprint (the AABB extent of the largest
 * unit in the selection -- e.g. (rx*2, ry*2)) and providing
 * the dynamic occupancy table.
 */
FormationPlan plan_formation(
	const std::vector<openage::gamestate::entity_id_t> &selection,
	double destination_x,
	double destination_y,
	double max_footprint_width,
	double max_footprint_height,
	const openage::scenario::DynamicOccupancy &dynamic,
	const openage::gamestate::scenario::StaticOccupancyGrid
		&static_occ,
	int map_min_x,
	int map_min_y,
	int map_max_x,
	int map_max_y,
	const std::vector<openage::gamestate::component::FootprintBounds>
		&reservations =
		{});


} // namespace openage::pathfinding
