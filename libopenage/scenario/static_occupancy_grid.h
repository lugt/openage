// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4B: StaticOccupancyGrid + NavigationGrid.
//
// The StaticOccupancyGrid records, per tile cell, which static
// entity (if any) blocks the cell. It is the gameplay/navigation
// layer's source of truth for "is this tile walkable?" (combined
// with the terrain walkability from P2).
//
// Scope (P4.4B):
//   - Static (building) footprints only. Dynamic units are NOT
//     permanently written here; their own tile is passable for
//     them and dynamic unit-unit avoidance is P4.4C / A* scope.
//   - 1 cell = 1 tile (cell anchor = cell center).
//   - Out-of-map cells are blocked (returns nullopt source).
//   - occupy() is idempotent for the same (entity, bounds) and
//     REJECTS overlap with a different entity (logs + returns
//     false so the caller can surface a clear error).
//
// NavigationGrid is the P4.4C contract: P4.4C's A* consumes
// is_walkable(cell) + cost(cell). P4.4B provides a
// TerrainNavigationGrid implementation that combines the terrain
// walkability and the StaticOccupancyGrid; P4.4C plugs A* in
// on top.

#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gamestate/component/api/footprint.h"
#include "gamestate/types.h"


namespace openage::gamestate {
class Terrain;
}


namespace openage::gamestate::scenario {


/**
 * Tile coordinate. Integer tile indices (matches the dump and
 * terrain chunk layout).
 */
struct CellPosition {
	int x;
	int y;

	bool operator==(const CellPosition &o) const {
		return x == o.x && y == o.y;
	}
};


// Hash for CellPosition (used by unordered_map).
struct CellHash {
	size_t operator()(const CellPosition &c) const noexcept {
		return std::hash<int64_t>{}((int64_t(c.x) << 32) | uint32_t(c.y));
	}
};


/**
 * Records which static entity (if any) blocks each tile cell.
 *
 * Construction takes the map's tile extent (from the terrain)
 * so out-of-map queries are answered without per-call checks.
 */
class StaticOccupancyGrid {
public:
	StaticOccupancyGrid(int map_min_x, int map_min_y,
	                    int map_max_x, int map_max_y);

	/**
	 * True if the cell is blocked by a static entity. Out-of-map
	 * cells return true (treated as not walkable).
	 */
	bool is_blocked(CellPosition cell) const;

	/**
	 * The entity id that blocks the cell, or nullopt if the cell
	 * is walkable (or out-of-map).
	 */
	std::optional<entity_id_t> blocker_of(CellPosition cell) const;

	/**
	 * Mark all cells covered by the footprint as blocked by
	 * `entity`. Idempotent for the same (entity, bounds):
	 * re-occupying with the same entity is a no-op. REJECTS
	 * overlap with a different entity (logs an error and returns
	 * the blocking entity's id; the caller decides how to
	 * surface it).
	 */
	std::optional<entity_id_t> occupy(
		entity_id_t entity,
		const openage::gamestate::component::FootprintBounds &bounds);

	/**
	 * Release all cells currently blocked by `entity`. Safe to
	 * call on an entity that does not occupy any cells. Used
	 * when an entity is removed (buildings do not despawn in
	 * Mission 1, but the API supports it for future scenarios).
	 */
	void release(entity_id_t entity);

	/**
	 * Number of cells currently blocked.
	 */
	size_t blocked_cell_count() const { return this->cells_.size(); }

private:
	bool in_map(CellPosition c) const;

	int min_x_, min_y_, max_x_, max_y_;
	// Map cell -> blocker entity. Out-of-map cells are NOT
	// stored; is_blocked returns true for them.
	std::unordered_map<CellPosition, entity_id_t, CellHash> cells_;
	// Map entity -> list of cells it occupies. Used by release()
	// to clear all cells owned by an entity, and by occupy() to
	// detect re-occupancy by the same entity.
	std::unordered_map<entity_id_t, std::vector<CellPosition>> entity_cells_;
};


/**
 * P4.4C contract: combined walkability view consumed by A*.
 *
 * P4.4B provides TerrainNavigationGrid, which composes the
 * terrain walkability (P2 APITerrain::get_path_costs) and the
 * StaticOccupancyGrid into a single query surface. P4.4C's A*
 * reads is_walkable(cell) and cost(cell) through this interface;
 * the underlying implementation can be swapped (e.g. flow-field
 * vs A* vs hierarchical) without changing P4.4C or the Move
 * runtime's call site.
 */
class NavigationGrid {
public:
	virtual ~NavigationGrid() = default;

	virtual bool is_walkable(CellPosition cell) const = 0;
	virtual double cost(CellPosition cell) const = 0;
};


/**
 * Composition: terrain walkability (P2) AND static occupancy.
 * A cell is walkable iff the terrain is walkable AND the static
 * occupancy grid is not blocked.
 */
class TerrainNavigationGrid final : public NavigationGrid {
public:
	TerrainNavigationGrid(
		const std::shared_ptr<StaticOccupancyGrid> &static_occ,
		const std::shared_ptr<Terrain> &terrain);

	bool is_walkable(CellPosition cell) const override;
	double cost(CellPosition cell) const override;

private:
	std::shared_ptr<StaticOccupancyGrid> occ_;
	std::shared_ptr<Terrain> terrain_;
};


} // namespace openage::gamestate::scenario