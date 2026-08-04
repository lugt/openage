// Copyright 2026 the openage authors. See copying.md for legal info.

#include "pathfinding/movement_reservation.h"

#include "gamestate/component/api/footprint.h"

#include <algorithm>


namespace openage::pathfinding {


namespace {


bool aabb_overlap(
	const openage::gamestate::component::FootprintBounds &a,
	const openage::gamestate::component::FootprintBounds &b) {
	return a.min_x < b.max_x && b.min_x < a.max_x
	    && a.min_y < b.max_y && b.min_y < a.max_y;
}


}  // namespace


bool MovementReservationManager::open(
	uint64_t generation,
	uint32_t slot_index,
	openage::gamestate::entity_id_t entity,
	const openage::gamestate::component::FootprintBounds
		&bounds) {
	// Idempotent: same entity reopening just updates the
	// bounds (and possibly the key).
	auto it = this->entity_to_key_.find(entity);
	if (it != this->entity_to_key_.end()) {
		auto &old_bounds = this->reservations_[it->second];
		old_bounds.bounds = bounds;
		it->second.generation = generation;
		it->second.slot_index = slot_index;
		return true;
	}
	ReservationKey key{generation, slot_index, entity};
	// Check overlap with HIGHER-priority reservations
	// only (any key < key has higher priority because
	// smaller wins). Iterating the std::map is in
	// ascending order, so we can stop at the first key >=
	// our key.
	for (const auto &kv : this->reservations_) {
		if (kv.first >= key) {
			break;
		}
		if (aabb_overlap(kv.second.bounds, bounds)) {
			// Higher-priority reservation blocks us.
			return false;
		}
	}
	// Also check reservations with lower priority
	// (would be blocked BY us if they had higher
	// priority, but they don't, so they don't block us).
	// The current open call only blocks if a HIGHER
	// priority reservation already exists.
	// For mutual exclusion at the same priority we
	// would also need to check; the (cmd_id, slot, entity)
	// tuple is unique so there can't be two reservations
	// at the same priority.
	this->reservations_[key] = {entity, bounds};
	this->entity_to_key_[entity] = key;
	return true;
}


void MovementReservationManager::close(
	openage::gamestate::entity_id_t entity) {
	auto it = this->entity_to_key_.find(entity);
	if (it == this->entity_to_key_.end()) {
		return;
	}
	this->reservations_.erase(it->second);
	this->entity_to_key_.erase(it);
}


void MovementReservationManager::clear_generation_leq(
	uint64_t generation) {
	// Collect keys to erase (can't erase while iterating).
	std::vector<ReservationKey> to_erase;
	for (const auto &kv : this->reservations_) {
		if (kv.first.generation <= generation) {
			to_erase.push_back(kv.first);
		}
		else {
			break;  // ascending: once we hit a bigger gen,
			        // all subsequent are bigger.
		}
	}
	for (const auto &k : to_erase) {
		auto bit = this->reservations_.find(k);
		if (bit != this->reservations_.end()) {
			this->entity_to_key_.erase(bit->second.entity);
			this->reservations_.erase(bit);
		}
	}
}


std::vector<openage::gamestate::entity_id_t>
MovementReservationManager::query(
	openage::gamestate::entity_id_t mover,
	const openage::gamestate::component::FootprintBounds
		&bounds) const {
	std::vector<openage::gamestate::entity_id_t> result;
	for (const auto &kv : this->reservations_) {
		auto other = kv.second.entity;
		if (other == mover) continue;
		if (not aabb_overlap(kv.second.bounds, bounds)) continue;
		result.push_back(other);
	}
	std::sort(result.begin(), result.end());
	return result;
}


std::optional<openage::gamestate::component::FootprintBounds>
MovementReservationManager::bounds_of(
	openage::gamestate::entity_id_t entity) const {
	auto it = this->entity_to_key_.find(entity);
	if (it == this->entity_to_key_.end()) {
		return std::nullopt;
	}
	auto rit = this->reservations_.find(it->second);
	if (rit == this->reservations_.end()) {
		return std::nullopt;
	}
	return rit->second.bounds;
}


std::vector<Reservation>
MovementReservationManager::list() const {
	std::vector<Reservation> out;
	out.reserve(this->reservations_.size());
	for (const auto &kv : this->reservations_) {
		out.push_back({kv.first, kv.second.entity, kv.second.bounds});
	}
	return out;
}


} // namespace openage::pathfinding
