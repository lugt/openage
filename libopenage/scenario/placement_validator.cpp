// Copyright 2026 the openage authors. See copying.md for legal info.

#include "scenario/placement_validator.h"

#include "log/log.h"
#include "log/message.h"

#include <algorithm>
#include <cmath>


namespace openage::gamestate::scenario {


PlacementValidator::PlacementValidator(
	int map_min_x, int map_min_y, int map_max_x, int map_max_y) :
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


PlacementFailure PlacementValidator::validate(const PlacementRequest &req) {
	PlacementFailure out;
	out.requested_bounds = make_bounds(req);

	if (!std::isfinite(req.radius_x) || !std::isfinite(req.radius_y)
	    || req.radius_x < 0.0 || req.radius_y < 0.0) {
		out.reason = PlacementFailureReason::INVALID_FOOTPRINT;
		out.context = "entity_fqon=" + req.entity_fqon
			+ " scenario_object_id=" + req.scenario_object_id
			+ " radius_x=" + std::to_string(req.radius_x)
			+ " radius_y=" + std::to_string(req.radius_y);
		return out;
	}

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

	for (const auto &acc : this->accepted_) {
		if (acc.bounds.overlaps(out.requested_bounds)) {
			out.blocking_entity = acc.entity;
			out.context = "entity_fqon=" + req.entity_fqon
				+ " scenario_object_id=" + req.scenario_object_id
				+ " blocker=" + std::to_string(acc.entity)
				+ " blocker_is_static=" + std::to_string(acc.is_static);
			out.reason = acc.is_static
				? PlacementFailureReason::STATIC_OBSTACLE
				: PlacementFailureReason::ENTITY_OVERLAP;
			return out;
		}
	}

	out.reason = PlacementFailureReason::OK;
	this->accepted_.push_back({req.entity, out.requested_bounds, req.is_static});
	return out;
}


void PlacementValidator::forget(entity_id_t entity) {
	this->accepted_.erase(
		std::remove_if(this->accepted_.begin(), this->accepted_.end(),
			[entity](const Accepted &a) { return a.entity == entity; }),
		this->accepted_.end());
}


std::optional<std::pair<double, double>>
PlacementValidator::find_nearest_valid(
	const PlacementRequest &req, double max_radius_tiles) const {

	const int r_max = static_cast<int>(std::ceil(max_radius_tiles));
	for (int r = 0; r <= r_max; ++r) {
		for (int dy = -r; dy <= r; ++dy) {
			for (int dx = -r; dx <= r; ++dx) {
				if (r > 0 && std::max(std::abs(dx), std::abs(dy)) != r) {
					continue;
				}
				PlacementRequest probe = req;
				probe.center_x = req.center_x + dx;
				probe.center_y = req.center_y + dy;
				if (!std::isfinite(probe.radius_x) || !std::isfinite(probe.radius_y)) {
					continue;
				}
				openage::gamestate::component::FootprintBounds pb{
					probe.center_x - probe.radius_x,
					probe.center_y - probe.radius_y,
					probe.center_x + probe.radius_x,
					probe.center_y + probe.radius_y};
				bool oom = false;
				for (int x = static_cast<int>(std::floor(pb.min_x));
				     x <= static_cast<int>(std::floor(pb.max_x - 1e-9)) && !oom; ++x) {
					for (int y = static_cast<int>(std::floor(pb.min_y));
					     y <= static_cast<int>(std::floor(pb.max_y - 1e-9)) && !oom; ++y) {
						if (x < min_x_ || x >= max_x_ || y < min_y_ || y >= max_y_) {
							oom = true;
						}
					}
				}
				if (oom) continue;
				bool overlap = false;
				for (const auto &acc : this->accepted_) {
					if (acc.bounds.overlaps(pb)) {
						overlap = true;
						break;
					}
				}
				if (!overlap) {
					return std::make_pair(probe.center_x, probe.center_y);
				}
			}
		}
	}
	return std::nullopt;
}


} // namespace openage::gamestate::scenario
