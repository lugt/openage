// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4F.1.4: MovementReservationManager -- the deterministic
// reservation system for multi-unit movement.
//
// Why this exists:
//   When two Villagers are issued move commands at the
//   same time, both might think they can occupy the same
//   tile at the same time. Without reservations, the A*
//   would happily produce paths that overlap.
//
// Priority (per spec):
//   1. command generation (newer > older)
//   2. formation slot index (lower > higher)
//   3. entity id (lower > higher)
//
// All iteration is sorted; there is no unordered_iteration
// anywhere. Two entities with the same priority do not
// happen because (cmd_id, slot_index, entity_id) is
// strictly comparable.
//
// API:
//   * open(generation, slot, entity, bounds) -- issue a
//     reservation. Returns true if accepted; false if a
//     higher-priority reservation already occupies the
//     area (in which case the caller should wait or
//     replan).
//   * close(entity) -- release an entity's reservation.
//   * clear_generation(generation) -- drop all reservations
//     with the given generation id (used when a new command
//     cancels the old group intent).
//   * query(bounds) -- list of reservation ids (entity)
//     that overlap the bounds, sorted ascending by entity id.

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <vector>

#include "gamestate/component/api/footprint.h"
#include "gamestate/types.h"


namespace openage::pathfinding {


struct ReservationKey {
	// command generation (newer > older; smaller value wins
	// on collision)
	uint64_t generation{0};
	// formation slot index (smaller wins; 0 if not a
	// formation move)
	uint32_t slot_index{0};
	// entity id (smaller wins)
	openage::gamestate::entity_id_t entity{0};

	bool operator<(const ReservationKey &o) const {
		if (generation != o.generation) return generation < o.generation;
		if (slot_index != o.slot_index) return slot_index < o.slot_index;
		return entity < o.entity;
	}
	bool operator==(const ReservationKey &o) const {
		return generation == o.generation
		    && slot_index == o.slot_index
		    && entity == o.entity;
	}
	bool operator>=(const ReservationKey &o) const {
		return *this > o || *this == o;
	}
	bool operator>(const ReservationKey &o) const {
		return o < *this;
	}
};


struct Reservation {
	ReservationKey key;
	openage::gamestate::entity_id_t entity;
	openage::gamestate::component::FootprintBounds bounds;
};


class MovementReservationManager {
public:
	MovementReservationManager() = default;

	/**
	 * Open a new reservation. If a higher-priority
	 * reservation overlaps the same area, the call is
	 * rejected and the existing reservation(s) are left
	 * untouched.
	 */
	bool open(
		uint64_t generation,
		uint32_t slot_index,
		openage::gamestate::entity_id_t entity,
		const openage::gamestate::component::FootprintBounds
			&bounds);

	/**
	 * Release an entity's reservation. The generation
	 * and slot_index are not needed: only one reservation
	 * per entity is allowed.
	 */
	void close(openage::gamestate::entity_id_t entity);

	/**
	 * Drop every reservation whose generation is <= the
	 * given generation. Used when a new command cancels
	 * the old group intent: the new command bumps the
	 * generation, the old reservations are now stale and
	 * get cleaned up.
	 */
	void clear_generation_leq(uint64_t generation);

	/**
	 * List of entity ids whose reservation bounds
	 * overlap the query bounds. The mover's own
	 * reservation is excluded. The list is sorted
	 * ascending by entity id for deterministic ordering.
	 */
	std::vector<openage::gamestate::entity_id_t> query(
		openage::gamestate::entity_id_t mover,
		const openage::gamestate::component::FootprintBounds
			&bounds) const;

	/**
	 * True iff an entity has an open reservation.
	 */
	bool has(openage::gamestate::entity_id_t entity) const {
		return this->entity_to_key_.find(entity)
		    != this->entity_to_key_.end();
	}

	/**
	 * Return the bounds of an entity's current
	 * reservation, or nullopt if none.
	 */
	std::optional<openage::gamestate::component::FootprintBounds>
	bounds_of(openage::gamestate::entity_id_t entity) const;

	/**
	 * Current reservation count.
	 */
	size_t size() const { return this->reservations_.size(); }

	/**
	 * Bump the global generation counter and return the
	 * new value. The caller passes this value to all
	 * reservation open() calls in the new command; the
	 * old command's reservations are still in the manager
	 * but clear_generation_leq() will purge them on
	 * release.
	 */
	uint64_t next_generation() {
		return this->next_generation_++;
	}

	/**
	 * Diagnostic: list all reservations in (generation,
	 * slot, entity) priority order.
	 */
	std::vector<Reservation> list() const;

private:
	// Reservation set ordered by ReservationKey. Value is
	// (entity, bounds) so we can look up the entity by key
	// without a second map.
	struct ReservationValue {
		openage::gamestate::entity_id_t entity;
		openage::gamestate::component::FootprintBounds bounds;
	};
	std::map<ReservationKey, ReservationValue> reservations_;
	// entity -> key (for close / has / bounds_of).
	std::map<openage::gamestate::entity_id_t, ReservationKey>
		entity_to_key_;
	uint64_t next_generation_{1};
};


} // namespace openage::pathfinding
