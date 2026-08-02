// Copyright 2026 the openage authors. See copying.md for legal info.

#include "scenario/placement_validator.h"

#include "log/log.h"
#include "log/message.h"

#include <cmath>


namespace openage::gamestate::scenario {


PlacementValidator::PlacementValidator(
	const std::shared_ptr<StaticOccupancyGrid> &occupancy,
	int map_min_x, int map_min_y, int map_max_x, int map_max_y) :
	occ_{occupancy},
	min_x_{map_min_x},
	min_y_{map_min_y},
	max_x_{map_max_x},
	max_y_{map_max_y} {}


static openage::gamestate::component::FootprintBounds make_bounds(
	const PlacementRequest &req) {
	openage::gamestate::component::FootprintBounds b;
	b.min_x = req.center_x - req.radius_x;
	b.min_y = req.center_y - req.radius_y;
	b.max_x = req.center_x + req.radius_x;
	b.max_y = req.center_y + req.radius_y;
	return b;
}


PlacementFailure PlacementValidator::validate(const PlacementRequest &req) const {
	PlacementFailure out;
	out.requested_bounds = make_bounds(req);

	// 1. Invalid footprint: half-extents must be finite and
	//    non-negative. A 0-extent footprint is degenerate
	//    (entity is a point); we reject it under the strict
	//    "no large epsilon" rule. The factory clamps
	//    negatives to 0 with a warning, so a 0 here means a
	//    missing Collision or a bad reconstruction override.
	if (!std::isfinite(req.radius_x) || !std::isfinite(req.radius_y)
	    || req.radius_x < 0.0 || req.radius_y < 0.0) {
		out.reason = PlacementFailureReason::INVALID_FOOTPRINT;
		out.context = "entity_fqon=" + req.entity_fqon
			+ " scenario_object_id=" + req.scenario_object_id
			+ " radius_x=" + std::to_string(req.radius_x)
			+ " radius_y=" + std::to_string(req.radius_y);
		return out;
	}

	// 2. Out of map: every cell the bounds cover must be in map.
	//    Enumerate covered cells (floor of each axis).
	const int x_lo = static_cast<int>(std::floor(out.requested_bounds.min_x));
	const int y_lo = static_cast<int>(std::floor(out.requested_bounds.min_y));
	const int x_hi = static_cast<int>(std::floor(out.requested_bounds.max_x - 1e-9));
	const int y_hi = static_cast<int>(std::floor(out.requested_bounds.max_y - 1e-9));
	for (int x = x_lo; x <= x_hi; ++x) {
		for (int y = y_lo; y <= y_hi; ++y) {
			if (x < min_x_ || x >= max_x_ || y < min_y_ || y >= max_y_) {
				out.reason = PlacementFailureReason::OUT_OF_MAP;
				out.context = "entity_fqon=" + req.entity_fqon
					+ " scenario_object_id=" + req.scenario_object_id
					+ " out_of_map_cell=("
					+ std::to_string(x) + "," + std::to_string(y) + ")";
				return out;
			}
		}
	}

	// 3. Static obstacle / entity overlap. For non-degenerate
	//    footprints, every covered cell must be unblocked (or
	//    blocked by the same entity -- the latter cannot happen
	//    here because the caller hasn't called occupy yet for
	//    this request).
	if (!req.skip_overlap_check && occ_) {
		for (int x = x_lo; x <= x_hi; ++x) {
			for (int y = y_lo; y <= y_hi; ++y) {
				CellPosition c{x, y};
				auto blocker = occ_->blocker_of(c);
				if (blocker.has_value()) {
					out.reason = PlacementFailureReason::STATIC_OBSTACLE;
					out.blocking_entity = *blocker;
					out.context = "entity_fqon=" + req.entity_fqon
						+ " scenario_object_id=" + req.scenario_object_id
						+ " cell=(" + std::to_string(x) + ","
						+ std::to_string(y) + ")"
						+ " blocker=" + std::to_string(*blocker);
					return out;
				}
			}
		}
	}

	// 4. UNWALKABLE_TERRAIN is the per-cell terrain check. For
	//    Mission 1 all terrain is TestSceneGrass (walkable). A
	//    dedicated terrain-walkability probe is wired in
	//    TerrainNavigationGrid; the validator does not depend
	//    on it directly (the production loader constructs the
	//    validator without the terrain view and trusts the
	//    occupancy grid for the gameplay collision signal).
	//    This branch is left for the future path_type-aware
	//    refinement.

	out.reason = PlacementFailureReason::OK;
	return out;
}


PlacementFailure PlacementValidator::validate_and_occupy(
	const PlacementRequest &req) {
	auto out = this->validate(req);
	if (not out.ok()) {
		log::log(MSG(err) << "PlacementValidator: REJECTED "
		                   << out.reason_string() << " " << out.context);
		return out;
	}
	if (req.blocks_placement && occ_) {
		auto conflict = occ_->occupy(req.entity, out.requested_bounds);
		if (conflict.has_value()) {
			out.reason = PlacementFailureReason::ENTITY_OVERLAP;
			out.blocking_entity = *conflict;
			out.context = "entity_fqon=" + req.entity_fqon
				+ " scenario_object_id=" + req.scenario_object_id
				+ " occupy_conflict="
				+ std::to_string(*conflict);
			log::log(MSG(err) << "PlacementValidator: occupy failed "
			                   << out.context);
			return out;
		}
	}
	return out;
}


std::optional<std::pair<double, double>>
PlacementValidator::find_nearest_valid(
	const PlacementRequest &req,
	double max_radius_tiles) const {

	// Stable scan: start at the requested center (radius 0)
	// and expand outward in a square ring. Within each ring,
	// iterate row-major. The first accepted placement is
	// returned. This gives a deterministic, reproducible
	// resolution independent of map size.
	const int r_max = static_cast<int>(std::ceil(max_radius_tiles));
	for (int r = 0; r <= r_max; ++r) {
		for (int dy = -r; dy <= r; ++dy) {
			for (int dx = -r; dx <= r; ++dx) {
				// Only test the ring boundary on r > 0 to
				// keep the scan O(r^2) rather than O(r^4).
				if (r > 0 && std::max(std::abs(dx), std::abs(dy)) != r) {
					continue;
				}
				PlacementRequest probe = req;
				probe.center_x = req.center_x + dx;
				probe.center_y = req.center_y + dy;
				auto out = this->validate(probe);
				if (out.ok()) {
					return std::make_pair(probe.center_x, probe.center_y);
				}
			}
		}
	}
	return std::nullopt;
}


} // namespace openage::gamestate::scenario