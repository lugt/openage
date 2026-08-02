// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4B: PlacementValidator.
//
// Validates a requested entity placement against the map extent,
// terrain passability, the static occupancy grid, and the set of
// already-accepted placements. Production policy is FAIL LOUD:
// an invalid placement produces a PlacementFailure with the
// blocker identified and a context message; the loader does not
// silently relocate.
//
// The validator is intentionally decoupled from the loader so it
// can be exercised in isolation (demo 26) and so future debug
// modes can add a find_nearest_valid_position policy on top.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gamestate/component/api/footprint.h"
#include "gamestate/types.h"
#include "scenario/static_occupancy_grid.h"


namespace openage::gamestate::scenario {


enum class PlacementFailureReason : uint8_t {
	OK = 0,
	OUT_OF_MAP,         // footprint extends beyond the map extent
	UNWALKABLE_TERRAIN,  // footprint overlaps non-walkable terrain
	STATIC_OBSTACLE,     // footprint overlaps an already-occupied cell
	ENTITY_OVERLAP,      // footprint overlaps a previously-accepted placement
	INVALID_FOOTPRINT,   // half-extents non-finite / non-positive
};


struct PlacementFailure {
	PlacementFailureReason reason{PlacementFailureReason::OK};
	entity_id_t blocking_entity{0};
	openage::gamestate::component::FootprintBounds requested_bounds{};
	std::string context;

	bool ok() const { return reason == PlacementFailureReason::OK; }

	std::string reason_string() const {
		switch (reason) {
		case PlacementFailureReason::OK: return "OK";
		case PlacementFailureReason::OUT_OF_MAP: return "OUT_OF_MAP";
		case PlacementFailureReason::UNWALKABLE_TERRAIN: return "UNWALKABLE_TERRAIN";
		case PlacementFailureReason::STATIC_OBSTACLE: return "STATIC_OBSTACLE";
		case PlacementFailureReason::ENTITY_OVERLAP: return "ENTITY_OVERLAP";
		case PlacementFailureReason::INVALID_FOOTPRINT: return "INVALID_FOOTPRINT";
		}
		return "UNKNOWN";
	}
};


/**
 * The placement request. center is in tile units (1 tile = 1
 * cell). The footprint's center is the entity's anchor; bounds
 * are derived from (center, radius_x, radius_y).
 */
struct PlacementRequest {
	entity_id_t entity{0};
	std::string entity_fqon;
	std::string scenario_object_id;
	double center_x{0.0};
	double center_y{0.0};
	double radius_x{0.0};
	double radius_y{0.0};
	bool blocks_placement{false};
	// For tests/debug: the validator can skip the
	// ENTITY_OVERLAP / STATIC_OBSTACLE checks if the caller has
	// already registered the placement elsewhere. Default false.
	bool skip_overlap_check{false};
};


class PlacementValidator {
public:
	PlacementValidator(
		const std::shared_ptr<StaticOccupancyGrid> &occupancy,
		int map_min_x, int map_min_y, int map_max_x, int map_max_y);

	/**
	 * Validate a placement request. The validator does NOT
	 * occupy the grid on accept (the caller calls
	 * StaticOccupancyGrid::occupy after a successful validate).
	 * On reject, the placement is left untouched.
	 */
	PlacementFailure validate(const PlacementRequest &req) const;

	/**
	 * Production convenience: validate and (on success) occupy.
	 * On failure, the occupancy grid is unchanged. The caller
	 * receives a PlacementFailure whose context includes the
	 * ScenarioObjectId and entity FQON.
	 */
	PlacementFailure validate_and_occupy(const PlacementRequest &req);

	/**
	 * Stable nearest-valid search for debug fixtures ONLY. The
	 * search is deterministic (scans the bounding box of the
	 * request in a fixed order: row-major, starting from the
	 * closest cell to the requested center). Returns the
	 * accepted center on success, nullopt if no cell within
	 * max_radius_tiles passes validation.
	 *
	 * Production must NOT call this. It is gated behind a
	 * debug policy in the loader; the Mission Pack default is
	 * fail-loud.
	 */
	std::optional<std::pair<double, double>> find_nearest_valid(
		const PlacementRequest &req,
		double max_radius_tiles) const;

private:
	std::shared_ptr<StaticOccupancyGrid> occ_;
	int min_x_, min_y_, max_x_, max_y_;
};


} // namespace openage::gamestate::scenario