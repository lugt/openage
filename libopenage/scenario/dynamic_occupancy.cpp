// Copyright 2026 the openage authors. See copying.md for legal info.

#include "scenario/dynamic_occupancy.h"

#include "gamestate/component/api/footprint.h"

#include <algorithm>
#include <cmath>


namespace openage::scenario {


std::vector<DynamicOccupancy::BucketKey>
DynamicOccupancy::cells_for_bounds(
	const openage::gamestate::component::FootprintBounds &bounds) {
	const double x_lo_d = bounds.min_x;
	const double x_hi_d = bounds.max_x - 1e-9;
	const double y_lo_d = bounds.min_y;
	const double y_hi_d = bounds.max_y - 1e-9;
	const int x_lo = static_cast<int>(std::floor(x_lo_d));
	const int x_hi = static_cast<int>(std::floor(x_hi_d));
	const int y_lo = static_cast<int>(std::floor(y_lo_d));
	const int y_hi = static_cast<int>(std::floor(y_hi_d));
	std::vector<BucketKey> out;
	for (int x = x_lo; x <= x_hi; ++x) {
		for (int y = y_lo; y <= y_hi; ++y) {
			out.push_back({x, y});
		}
	}
	return out;
}


bool DynamicOccupancy::aabb_overlap(
	const openage::gamestate::component::FootprintBounds &a,
	const openage::gamestate::component::FootprintBounds &b) {
	return a.min_x < b.max_x && b.min_x < a.max_x
	    && a.min_y < b.max_y && b.min_y < a.max_y;
}


void DynamicOccupancy::register_entity(
	openage::gamestate::entity_id_t entity,
	const openage::gamestate::component::FootprintBounds &bounds) {
	auto it = this->records_.find(entity);
	if (it != this->records_.end()) {
		// Already registered; if bounds match, idempotent.
		// If different, treat as an update.
		if (it->second.min_x == bounds.min_x
		    and it->second.max_x == bounds.max_x
		    and it->second.min_y == bounds.min_y
		    and it->second.max_y == bounds.max_y) {
			return;
		}
		this->update_entity(entity, it->second, bounds);
		return;
	}
	this->records_[entity] = bounds;
	for (const auto &k : cells_for_bounds(bounds)) {
		this->buckets_[k].push_back({entity, bounds});
	}
}


void DynamicOccupancy::update_entity(
	openage::gamestate::entity_id_t entity,
	const openage::gamestate::component::FootprintBounds &old_bounds,
	const openage::gamestate::component::FootprintBounds &new_bounds) {
	(void)old_bounds;  // we re-derive cells from old_bounds
	for (const auto &k : cells_for_bounds(old_bounds)) {
		auto it = this->buckets_.find(k);
		if (it == this->buckets_.end()) continue;
		auto &vec = it->second;
		vec.erase(
			std::remove_if(vec.begin(), vec.end(),
				[entity](const BucketEntry &e) {
					return e.entity == entity;
				}),
			vec.end());
		if (vec.empty()) {
			this->buckets_.erase(it);
		}
	}
	this->records_[entity] = new_bounds;
	for (const auto &k : cells_for_bounds(new_bounds)) {
		this->buckets_[k].push_back({entity, new_bounds});
	}
}


void DynamicOccupancy::remove_entity(
	openage::gamestate::entity_id_t entity) {
	auto it = this->records_.find(entity);
	if (it == this->records_.end()) {
		return;
	}
	auto old_bounds = it->second;
	this->records_.erase(it);
	for (const auto &k : cells_for_bounds(old_bounds)) {
		auto bit = this->buckets_.find(k);
		if (bit == this->buckets_.end()) continue;
		auto &vec = bit->second;
		vec.erase(
			std::remove_if(vec.begin(), vec.end(),
				[entity](const BucketEntry &e) {
					return e.entity == entity;
				}),
			vec.end());
		if (vec.empty()) {
			this->buckets_.erase(bit);
		}
	}
}


std::vector<openage::gamestate::entity_id_t>
DynamicOccupancy::query(
	openage::gamestate::entity_id_t mover,
	const openage::gamestate::component::FootprintBounds &bounds) const {
	std::vector<openage::gamestate::entity_id_t> result;
	for (const auto &k : cells_for_bounds(bounds)) {
		auto it = this->buckets_.find(k);
		if (it == this->buckets_.end()) continue;
		for (const auto &e : it->second) {
			if (e.entity == mover) continue;
			if (not aabb_overlap(e.bounds, bounds)) continue;
			// De-dup: an entity may appear in multiple
			// buckets. The std::find below preserves
			// insertion order; we keep the result sorted
			// by entity id at the end.
			if (std::find(result.begin(), result.end(), e.entity)
			    == result.end()) {
				result.push_back(e.entity);
			}
		}
	}
	std::sort(result.begin(), result.end());
	return result;
}


bool DynamicOccupancy::can_occupy(
	openage::gamestate::entity_id_t mover,
	const openage::gamestate::component::FootprintBounds &bounds) const {
	return this->query(mover, bounds).empty();
}


std::vector<DynamicOccupancy::Record>
DynamicOccupancy::records() const {
	std::vector<Record> out;
	out.reserve(this->records_.size());
	for (const auto &kv : this->records_) {
		out.push_back({kv.first, kv.second});
	}
	return out;
}


} // namespace openage::scenario
