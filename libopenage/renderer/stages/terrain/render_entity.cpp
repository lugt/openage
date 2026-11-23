// Copyright 2022-2024 the openage authors. See copying.md for legal info.

#include "render_entity.h"

#include <algorithm>
#include <array>
#include <mutex>


namespace openage::renderer::terrain {

RenderEntity::RenderEntity() :
	renderer::RenderEntity{},
	size{0, 0},
	tiles{},
	terrain_paths{},
	vertices{} {
}

void RenderEntity::update_tile(const util::Vector2s size,
                               const coord::tile &pos,
                               const terrain_elevation_t elevation,
                               const std::string terrain_path,
                               const time::time_t time) {
	std::unique_lock lock{this->mutex};

	if (this->vertices.empty()) {
		throw Error(MSG(err) << "Cannot update tile: Vertices have not been initialized yet.");
	}

	// find the postion of the tile in the vertex array
	auto v_width = size[0] + 1;
	auto left_corner = pos.ne + pos.se * v_width;

	// update the 4 vertices of the tile
	this->vertices[left_corner].up = elevation.to_float();                 // left corner
	this->vertices[left_corner + 1].up = elevation.to_float();             // bottom corner
	this->vertices[left_corner + v_width].up = elevation.to_float();     // top corner
	this->vertices[left_corner + v_width + 1].up = elevation.to_float(); // right corner

	// update tile
	this->tiles.at(pos.ne + pos.se * size[0]) = {elevation, terrain_path};

	// update the last update time
	this->last_update = time;

	// update the terrain paths
	this->terrain_paths.insert(terrain_path);

	this->changed = true;
}

void RenderEntity::update(const util::Vector2s size,
                          const tiles_t tiles,
                          const time::time_t time,
                          std::span<const float> corner_heights) {
	std::unique_lock lock{this->mutex};

	if (size[0] == 0 or size[1] == 0 or tiles.size() != size[0] * size[1]
	    or (not corner_heights.empty()
	        and corner_heights.size() != (size[0] + 1) * (size[1] + 1))) {
		throw Error{ERR << "Terrain data does not match chunk size " << size};
	}
	this->size = util::Vector2s{size[0] + 1, size[1] + 1};
	this->vertices.clear();
	this->vertices.reserve(this->size[0] * this->size[1]);
	for (size_t j = 0; j < this->size[1]; ++j) {
		for (size_t i = 0; i < this->size[0]; ++i) {
			float height = 0;
			if (corner_heights.empty()) {
				for (size_t y = j == 0 ? 0 : j - 1; y <= j and y < size[1]; ++y) {
					for (size_t x = i == 0 ? 0 : i - 1; x <= i and x < size[0]; ++x) {
						height = std::max(height, tiles[y * size[0] + x].first.to_float());
					}
				}
			}
			else {
				height = corner_heights[j * this->size[0] + i];
			}
			this->vertices.emplace_back(static_cast<float>(i), static_cast<float>(j), height);
		}
	}

	// update tiles
	this->tiles = tiles;

	// update the last update time
	this->last_update = time;

	// update the terrain paths
	this->terrain_paths.clear();
	for (const auto &tile : this->tiles) {
		this->terrain_paths.insert(tile.second);
	}

	this->changed = true;
}

const std::vector<coord::scene3> RenderEntity::get_vertices() {
	std::shared_lock lock{this->mutex};

	return this->vertices;
}

const RenderEntity::tiles_t RenderEntity::get_tiles() {
	std::shared_lock lock{this->mutex};

	return this->tiles;
}

const std::unordered_set<std::string> RenderEntity::get_terrain_paths() {
	std::shared_lock lock{this->mutex};

	return this->terrain_paths;
}

const util::Vector2s RenderEntity::get_size() {
	std::shared_lock lock{this->mutex};

	return this->size;
}

} // namespace openage::renderer::terrain
