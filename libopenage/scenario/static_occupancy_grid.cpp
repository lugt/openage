// Copyright 2026 the openage authors. See copying.md for legal info.

#include "scenario/static_occupancy_grid.h"

#include "gamestate/component/api/footprint.h"
#include "gamestate/terrain.h"
#include "gamestate/terrain_chunk.h"
#include "log/log.h"
#include "log/message.h"

#include <cmath>


namespace openage::gamestate::scenario {


StaticOccupancyGrid::StaticOccupancyGrid(
	int map_min_x, int map_min_y, int map_max_x, int map_max_y) :
	min_x_{map_min_x},
	min_y_{map_min_y},
	max_x_{map_max_x},
	max_y_{map_max_y} {}


bool StaticOccupancyGrid::in_map(CellPosition c) const {
	return c.x >= min_x_ && c.x < max_x_
	    && c.y >= min_y_ && c.y < max_y_;
}


bool StaticOccupancyGrid::is_blocked(CellPosition cell) const {
	if (not this->in_map(cell)) {
		// Out-of-map = not walkable. P4.4C's A* will treat
		// is_walkable() == false as an impassable boundary.
		return true;
	}
	return this->cells_.find(cell) != this->cells_.end();
}


std::optional<entity_id_t> StaticOccupancyGrid::blocker_of(
	CellPosition cell) const {
	if (not this->in_map(cell)) {
		return std::nullopt;
	}
	auto it = this->cells_.find(cell);
	if (it == this->cells_.end()) {
		return std::nullopt;
	}
	return it->second;
}


std::optional<entity_id_t> StaticOccupancyGrid::occupy(
	entity_id_t entity,
	const openage::gamestate::component::FootprintBounds &bounds) {

	// Cell-center containment rule (per spec §5: "cell anchor =
	// cell center; a cell is covered if its center lies inside
	// the footprint bounds"). Iterate every cell whose center
	// could plausibly be inside [min, max): the x range is
	// (min_x - 0.5, max_x - 0.5) and similarly for y. For
	// sub-tile extents (e.g. Villager rx=0.2) this correctly
	// covers ZERO cells (the unit is smaller than half a cell),
	// which is the right semantics for a static occupancy grid
	// (units are handled by the pathfinder's dynamic avoidance,
	// P4.4C).
	std::optional<entity_id_t> first_conflict;
	std::vector<CellPosition> covered;

	// The x cells with centers inside the bounds are integers
	// x where (x+0.5) in [min_x, max_x), i.e. x in
	// [min_x - 0.5, max_x - 0.5). The smallest x is
	// ceil(min_x - 0.5 + eps) and the largest is
	// floor(max_x - 0.5 - eps). For non-negative maps the
	// map extent gates the scan.
	const double x_lo_d = bounds.min_x - 0.5;
	const double x_hi_d = bounds.max_x - 0.5;
	const double y_lo_d = bounds.min_y - 0.5;
	const double y_hi_d = bounds.max_y - 0.5;
	const int x_lo = static_cast<int>(std::ceil(x_lo_d));
	const int x_hi = static_cast<int>(std::floor(x_hi_d));
	const int y_lo = static_cast<int>(std::ceil(y_lo_d));
	const int y_hi = static_cast<int>(std::floor(y_hi_d));

	for (int x = x_lo; x <= x_hi; ++x) {
		for (int y = y_lo; y <= y_hi; ++y) {
			CellPosition c{x, y};
			if (not this->in_map(c)) {
				// out-of-map cells are silently skipped
				// here; PlacementValidator should have
				// rejected this placement, but if it
				// slipped through (debug policy), we do
				// not register out-of-map cells and
				// return a conflict with nullopt.
				if (not first_conflict.has_value()) {
					first_conflict = std::nullopt;
				}
				continue;
			}
			auto existing = this->cells_.find(c);
			if (existing == this->cells_.end()) {
				covered.push_back(c);
			}
			else if (existing->second != entity) {
				// Conflict with a different entity. The
				// PlacementValidator should have caught
				// this; log and return the conflict so
				// the loader can surface it.
				log::log(MSG(err) << "StaticOccupancyGrid::occupy: "
				                   << "entity " << entity
				                   << " conflicts with entity "
				                   << existing->second
				                   << " at cell ("
				                   << c.x << "," << c.y << ")");
				if (not first_conflict.has_value()) {
					first_conflict = existing->second;
				}
			}
			// else: same entity re-occupying its own cell; OK.
		}
	}

	if (first_conflict.has_value()) {
		// Do not register any of `covered`; the placement is
		// rejected. The loader decides how to surface (the
		// validator should have caught this already).
		return first_conflict;
	}

	for (auto c : covered) {
		this->cells_[c] = entity;
	}
	// Append to the entity's cell list (idempotent: re-occupying
	// adds duplicates, but release() iterates and dedups by
	// erasing cells; duplicates are harmless).
	auto &elist = this->entity_cells_[entity];
	for (auto c : covered) {
		elist.push_back(c);
	}
	return std::nullopt;
}


void StaticOccupancyGrid::release(entity_id_t entity) {
	auto it = this->entity_cells_.find(entity);
	if (it == this->entity_cells_.end()) {
		return;
	}
	for (auto c : it->second) {
		auto cit = this->cells_.find(c);
		if (cit != this->cells_.end() && cit->second == entity) {
			this->cells_.erase(cit);
		}
	}
	this->entity_cells_.erase(it);
}


// === TerrainNavigationGrid ===

TerrainNavigationGrid::TerrainNavigationGrid(
	const std::shared_ptr<StaticOccupancyGrid> &static_occ,
	const std::shared_ptr<Terrain> &terrain) :
	occ_{static_occ},
	terrain_{terrain} {}


bool TerrainNavigationGrid::is_walkable(CellPosition cell) const {
	if (not occ_ || occ_->is_blocked(cell)) {
		return false;
	}
	if (not terrain_) {
		// No terrain means treat as walkable; the
		// occupancy grid already gated this cell.
		return true;
	}
	// Terrain walkability probe: the cell is walkable iff a
	// terrain chunk contains the tile. P4.4B uses the simple
	// "tile exists" predicate; APITerrain::get_path_costs
	// is the per-path-type refinement (out of scope until a
	// path type is bound to a query). For Mission 1 all
	// tiles are TestSceneGrass with cost 1; we do not need
	// the cost refinement here.
	for (auto &chunk : terrain_->get_chunks()) {
		const auto &off = chunk->get_offset();
		const auto &sz = chunk->get_size();
		if (cell.x >= off.ne && cell.x < off.ne + static_cast<int>(sz[0])
		    && cell.y >= off.se && cell.y < off.se + static_cast<int>(sz[1])) {
			return true;
		}
	}
	return false;
}


double TerrainNavigationGrid::cost(CellPosition cell) const {
	if (not occ_ || occ_->is_blocked(cell)) {
		// P4.4C: A* will treat cost >= some impassable
		// threshold as blocked. Return a large finite value
		// (NOT infinity, so the heuristic still has a
		// finite value) -- the A* will route around.
		return 1.0e9;
	}
	// Terrain default cost: 1 per tile. P4.4C can read
	// APITerrain::get_path_costs for the actual cost.
	return 1.0;
}


} // namespace openage::gamestate::scenario