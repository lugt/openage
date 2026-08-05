// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4B.1 §2: PlacementValidator -- pure AABB overlap, decoupled
// from the static occupancy grid.
//
// The validator's job is placement-time overlap checking, not
// runtime pathfinding walkability. It maintains its own list of
// accepted (entity, bounds, is_static) records and rejects any new
// placement whose bounds overlap a previously-accepted one
// (STATIC_OBSTACLE if the blocker is static, ENTITY_OVERLAP
// otherwise). The static occupancy grid (StaticOccupancyGrid) is
// a separate concern owned by the loader / runtime pathfinder
// (P4.4C) and is occupied separately, only for entities that are
// is_static && blocks_pathfinding. Moving units do NOT enter the
// static grid.
//
// Production policy is FAIL LOUD: an invalid placement produces a
// PlacementFailure with reason, blocking entity id, bounds, and
// context (FQON, ScenarioObjectId). The debug relocate policy
// (find_nearest_valid) is stable + bounded; production must not
// call it.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gamestate/component/api/footprint.h"
#include "gamestate/types.h"


namespace openage::gamestate::scenario {


enum class PlacementFailureReason : uint8_t {
	OK = 0,
	OUT_OF_MAP,
	UNWALKABLE_TERRAIN,
	STATIC_OBSTACLE,
	ENTITY_OVERLAP,
	INVALID_FOOTPRINT,
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


struct PlacementRequest {
	entity_id_t entity{0};
	std::string entity_fqon;
	std::string scenario_object_id;
	double center_x{0.0};
	double center_y{0.0};
	double radius_x{0.0};
	double radius_y{0.0};
	bool blocks_placement{false};
	bool is_static{false};
};


class PlacementValidator {
public:
	PlacementValidator(int map_min_x, int map_min_y,
	                  int map_max_x, int map_max_y);

	/**
	 * Validate a placement request against the map extent, the
	 * INVALID_FOOTPRINT contract, and the AABB overlap with
	 * every previously-accepted placement. On OK, the
	 * placement is recorded in the internal accepted list so
	 * subsequent placements see it. The validator does NOT
	 * touch the StaticOccupancyGrid; the loader does that
	 * separately (only for is_static && blocks_placement).
	 */
	PlacementFailure validate(const PlacementRequest &req);

	/**
	 * Drop every accepted placement owned by `entity` from the
	 * validator's accepted list. Used when an entity is
	 * removed (e.g. by the loader re-spawning after a
	 * retry). Does not touch the static grid; the grid's
	 * release() is a separate call.
	 */
	void forget(entity_id_t entity);

	std::optional<std::pair<double, double>>
	find_nearest_valid(const PlacementRequest &req,
	                   double max_radius_tiles) const;

	size_t accepted_count() const { return this->accepted_.size(); }

private:
	struct Accepted {
		entity_id_t entity;
		openage::gamestate::component::FootprintBounds bounds;
		bool is_static;
	};

	int min_x_, min_y_, max_x_, max_y_;
	std::vector<Accepted> accepted_;
};


} // namespace openage::gamestate::scenario
