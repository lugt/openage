// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4F.10: DynamicOccupancy -- the dynamic counterpart to
// StaticOccupancyGrid. Tracks the per-tile bounds of every
// dynamic entity (Villager, unit, any moving thing) so the
// Move runtime can reject segment AABBs that would overlap
// another unit's footprint.
//
// Why this exists:
//   StaticOccupancyGrid holds BUILDINGS (TownCenter etc).
//   Per P4.4B spec, moving units MUST NOT be written into the
//   static grid -- otherwise a unit's start cell would be
//   blocked by itself, multi-unit paths would deadlock on
//   each other, and a unit could not cross its own
//   placement cell.
//   DynamicOccupancy is the per-tile occupancy table for
//   MOVING entities. Each entity occupies a set of
//   cells-per-tile-buckets; the runtime queries
//   can_occupy(mover, bounds) before committing each segment.
//
//   The implementation here is a per-cell bucket map (cell
//   center -> list of (entity, AABB) records). query(bounds)
//   returns the list of entities whose AABB overlaps the
//   query AABB, except `mover` (self-exclusion). All
//   iteration is in a stable sorted order (entity id) so
//   tie-break is deterministic.
//
// Scope (P4.4F MVP):
//   - In-memory only, no persistent storage.
//   - Single map per process (one DynamicOccupancy instance
//     is owned by the simulation; tests construct their own).
//   - No reservation system (the reservation system is a
//     separate subsystem; DynamicOccupancy only tracks the
//     geometric claim).
//   - All operations are O(N) over current entities +
//     O(K) per cell bucket where K is the bucket size;
//     production-scale would use a spatial hash but the
//     MVP is sized for tens of units per scenario.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

#include "gamestate/component/api/footprint.h"
#include "gamestate/types.h"
#include "scenario/static_occupancy_grid.h"


namespace openage::gamestate {
class GameEntity;
}


namespace openage::scenario {


/**
 * Per-cell bucket of dynamic entity records. We bucket by
 * cell center (cell.x, cell.y) where cell.x is floor of
 * AABB.min_x and cell.y is floor of AABB.min_y. An entity
 * whose AABB spans multiple cells is recorded in every
 * bucket it overlaps.
 */
class DynamicOccupancy {
public:
	DynamicOccupancy() = default;

	/**
	 * Register a new entity's bounds. Idempotent for the
	 * same (entity, bounds) pair; calling it twice with
	 * different bounds is treated as an update (old bucket
	 * entries are removed, new ones added).
	 */
	void register_entity(
		openage::gamestate::entity_id_t entity,
		const openage::gamestate::component::FootprintBounds &bounds);

	/**
	 * Update an entity's bounds atomically: remove the
	 * old buckets, add the new ones. Used by the Move
	 * runtime when a segment commits.
	 */
	void update_entity(
		openage::gamestate::entity_id_t entity,
		const openage::gamestate::component::FootprintBounds &old_bounds,
		const openage::gamestate::component::FootprintBounds &new_bounds);

	/**
	 * Drop an entity's bounds. Safe to call on a removed
	 * or never-registered entity (no-op).
	 */
	void remove_entity(
		openage::gamestate::entity_id_t entity);

	/**
	 * True iff `mover` can occupy `bounds` without
	 * overlapping any other entity's AABB. The mover's own
	 * current bounds (if any) are excluded from the
	 * check. Overlap is half-open AABB vs AABB (matches
	 * P4.4B's half-open footprint semantics).
	 */
	bool can_occupy(
		openage::gamestate::entity_id_t mover,
		const openage::gamestate::component::FootprintBounds &bounds) const;

	/**
	 * Return the list of entity ids whose registered bounds
	 * overlap `bounds`. The mover is excluded. The
	 * returned list is sorted ascending by entity id so
	 * the caller's tie-break is deterministic.
	 */
	std::vector<openage::gamestate::entity_id_t> query(
		openage::gamestate::entity_id_t mover,
		const openage::gamestate::component::FootprintBounds &bounds) const;

	/**
	 * True iff `bounds` overlaps the AABB of any other
	 * entity (excluding `mover`). Same as
	 * !query(mover, bounds).empty().
	 */
	bool has_blocker(
		openage::gamestate::entity_id_t mover,
		const openage::gamestate::component::FootprintBounds &bounds) const {
		return not this->query(mover, bounds).empty();
	}

	/**
	 * Number of currently registered entities.
	 */
	size_t size() const { return this->records_.size(); }

	/**
	 * Diagnostic: enumerate every (entity, bounds) record
	 * in stable (entity id) order.
	 */
	struct Record {
		openage::gamestate::entity_id_t entity;
		openage::gamestate::component::FootprintBounds bounds;
	};
	std::vector<Record> records() const;

private:
	// Cell -> list of (entity, bounds). The cell index is
	// (int, int) where int = floor of the AABB.min corner.
	struct BucketKey {
		int x;
		int y;
		bool operator==(const BucketKey &o) const {
			return x == o.x && y == o.y;
		}
	};
	struct BucketKeyHash {
		size_t operator()(const BucketKey &k) const noexcept {
			return std::hash<int64_t>{}(
				(static_cast<int64_t>(k.x) << 32)
				| uint32_t(k.y));
		}
	};

	struct BucketEntry {
		openage::gamestate::entity_id_t entity;
		openage::gamestate::component::FootprintBounds bounds;
	};

	// Per-entity record (entity -> bounds). Used to
	// efficiently update an entity's bounds.
	std::map<openage::gamestate::entity_id_t,
	         openage::gamestate::component::FootprintBounds> records_;

	// Cell bucket -> entries. Sorted iteration on the
	// std::map is by key, but iteration within a bucket
	// is insertion order; query() sorts the result by
	// entity id for determinism.
	std::unordered_map<BucketKey, std::vector<BucketEntry>, BucketKeyHash>
		buckets_;

	// Static helpers.
	static std::vector<BucketKey> cells_for_bounds(
		const openage::gamestate::component::FootprintBounds &bounds);
	static bool aabb_overlap(
		const openage::gamestate::component::FootprintBounds &a,
		const openage::gamestate::component::FootprintBounds &b);
};


} // namespace openage::scenario
