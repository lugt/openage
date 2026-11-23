// Copyright 2026 the openage authors. See copying.md for legal info.

#include "scenario/static_occupancy_grid.h"

#include "gamestate/api/terrain.h"
#include "gamestate/component/api/footprint.h"
#include "gamestate/terrain.h"
#include "gamestate/terrain_chunk.h"
#include "log/log.h"
#include "log/message.h"
#include "pathfinding/definitions.h"

#include <algorithm>
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
	return *it->second.begin();
}


std::optional<entity_id_t> StaticOccupancyGrid::occupy(
    entity_id_t entity,
    const openage::gamestate::component::FootprintBounds &bounds, bool allow_overlap) {
    std::vector<CellPosition> covered;
    for (int x = static_cast<int>(std::ceil(bounds.min_x - 0.5));
         x <= static_cast<int>(std::floor(bounds.max_x - 0.5 - 1e-9)); ++x) {
        for (int y = static_cast<int>(std::ceil(bounds.min_y - 0.5));
             y <= static_cast<int>(std::floor(bounds.max_y - 0.5 - 1e-9)); ++y) {
            const CellPosition cell{x, y};
            if (not this->in_map(cell)) continue;
            auto existing = this->cells_.find(cell);
            if (not allow_overlap and existing != this->cells_.end()
                and (existing->second.size() > 1 or not existing->second.contains(entity))) {
                return *existing->second.begin();
            }
            covered.push_back(cell);
        }
    }
    for (const auto cell : covered) {
        if (this->cells_[cell].insert(entity).second) this->entity_cells_[entity].push_back(cell);
    }
    return std::nullopt;
}

void StaticOccupancyGrid::release(entity_id_t entity) {
    auto found = this->entity_cells_.find(entity);
    if (found == this->entity_cells_.end()) return;
    for (const auto cell : found->second) {
        auto entry = this->cells_.find(cell);
        if (entry != this->cells_.end()) {
            entry->second.erase(entity);
            if (entry->second.empty()) this->cells_.erase(entry);
        }
    }
    this->entity_cells_.erase(found);
}


// === TerrainNavigationGrid ===

TerrainNavigationGrid::TerrainNavigationGrid(
	const std::shared_ptr<StaticOccupancyGrid> &static_occ,
	const std::shared_ptr<Terrain> &terrain) :
	occ_{static_occ} {
	if (not terrain) {
		return;
	}
	this->width_ = terrain->get_size()[0];
	this->height_ = terrain->get_size()[1];
	this->land_costs_.assign(this->width_ * this->height_, path::COST_IMPASSABLE);
	std::unordered_map<nyan::fqon_t, uint8_t> terrain_costs;
	for (const auto &chunk : terrain->get_chunks()) {
		const auto extent = chunk->get_size();
		const auto offset = chunk->get_offset();
		for (size_t y = 0; y < extent[1]; ++y) {
			for (size_t x = 0; x < extent[0]; ++x) {
				const auto &tile = chunk->get_tile(y * extent[0] + x);
				auto [it, inserted] = terrain_costs.try_emplace(tile.terrain.get_name(), path::COST_MIN);
				if (inserted) {
					for (const auto &[mode, cost] : api::APITerrain::get_path_costs(tile.terrain)) {
						if (mode.ends_with(".Land")) {
							it->second = std::clamp(cost, int{path::COST_MIN}, int{path::COST_IMPASSABLE});
							break;
						}
					}
				}
				this->land_costs_[(offset.se + y) * this->width_ + offset.ne + x] = it->second;
			}
		}
	}
}


bool TerrainNavigationGrid::is_walkable(CellPosition cell) const {
	return this->cost(cell) < path::COST_IMPASSABLE;
}


double TerrainNavigationGrid::cost(CellPosition cell) const {
	if (not occ_ || occ_->is_blocked(cell)) {
		return 1.0e9;
	}
	if (this->land_costs_.empty()) {
		return path::COST_MIN;
	}
	if (cell.x < 0 or cell.y < 0
	    or static_cast<size_t>(cell.x) >= this->width_ || static_cast<size_t>(cell.y) >= this->height_) {
		return 1.0e9;
	}
	const auto terrain_cost = this->land_costs_[cell.y * this->width_ + cell.x];
	return terrain_cost == path::COST_IMPASSABLE ? 1.0e9 : terrain_cost;
}


} // namespace openage::gamestate::scenario
