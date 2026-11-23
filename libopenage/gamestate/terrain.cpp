// Copyright 2018-2024 the openage authors. See copying.md for legal info.

#include "terrain.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include "coord/tile.h"
#include <stdexcept>

#include "error/error.h"

#include "gamestate/terrain_chunk.h"
#include "renderer/render_factory.h"


namespace openage::gamestate {

Terrain::Terrain() :
	size{0, 0},
	chunks{} {
	// TODO: Get actual size of terrain.
}

Terrain::Terrain(const util::Vector2s &size,
                 std::vector<std::shared_ptr<TerrainChunk>> &&chunks) :
	size{size},
	chunks{std::move(chunks)} {
	if (this->chunks.empty()) {
		if (size[0] == 0 and size[1] == 0) {
			return;
		}
		throw error::Error{ERR << "Nonempty terrain needs chunks"};
	}
	const auto step = this->chunks.front()->get_size();
	if (step[0] == 0 or step[1] == 0 or size[0] == 0 or size[1] == 0) {
		throw error::Error{ERR << "Terrain and chunk dimensions must be positive"};
	}
	const auto grid = this->get_chunks_size();
	if (this->chunks.size() != grid[0] * grid[1]) {
		throw error::Error{ERR << "Terrain chunk count does not match grid " << grid};
	}
	for (size_t i = 0; i < this->chunks.size(); ++i) {
		const auto &chunk = this->chunks[i];
		const auto x = (i % grid[0]) * step[0];
		const auto y = (i / grid[0]) * step[1];
		const coord::tile_delta offset{static_cast<coord::tile_t>(x), static_cast<coord::tile_t>(y)};
		const util::Vector2s extent{std::min(step[0], size[0] - x), std::min(step[1], size[1] - y)};
		if (chunk->get_offset() != offset or chunk->get_size() != extent) {
			throw error::Error{ERR << "Invalid terrain chunk " << i
			                       << " (expected offset " << offset << ", size " << extent << ")"};
		}
	}
}

const util::Vector2s &Terrain::get_size() const {
	return this->size;
}

const util::Vector2s Terrain::get_chunks_size() const {
	if (this->chunks.empty()) {
		return {0, 0};
	}
	auto chunk_size = this->chunks.front()->get_size();
	return {(this->size[0] - 1) / chunk_size[0] + 1, (this->size[1] - 1) / chunk_size[1] + 1};
}

double Terrain::height_at(double x, double y) const {
    if (this->chunks.empty()) return 0;
    const auto step = this->chunks.front()->get_size();
    const int cell_x = static_cast<int>(std::floor(x)), cell_y = static_cast<int>(std::floor(y));
    auto corner = [&](int cx, int cy) {
        double height = 0;
        for (int ty = cy - 1; ty <= cy; ++ty) for (int tx = cx - 1; tx <= cx; ++tx) {
            if (tx < 0 or ty < 0 or tx >= static_cast<int>(this->size[0]) or ty >= static_cast<int>(this->size[1])) continue;
            const auto &chunk = this->get_chunk(coord::chunk{tx / static_cast<int>(step[0]), ty / static_cast<int>(step[1])});
            height = std::max(height, chunk->get_tile(coord::tile{tx % static_cast<int>(step[0]), ty % static_cast<int>(step[1])}).elevation.to_double());
        }
        return height;
    };
    const double dx = x-cell_x, dy=y-cell_y;
    return (1-dy)*((1-dx)*corner(cell_x,cell_y)+dx*corner(cell_x+1,cell_y))
        + dy*((1-dx)*corner(cell_x,cell_y+1)+dx*corner(cell_x+1,cell_y+1));
}

void Terrain::add_chunk(const std::shared_ptr<TerrainChunk> &chunk) {
	this->chunks.push_back(chunk);
}

const std::vector<std::shared_ptr<TerrainChunk>> &Terrain::get_chunks() const {
	return this->chunks;
}

const std::shared_ptr<TerrainChunk> &Terrain::get_chunk(size_t idx) const {
	return this->chunks.at(idx);
}

const std::shared_ptr<TerrainChunk> &Terrain::get_chunk(const coord::chunk &pos) const {
	const auto grid = this->get_chunks_size();
	if (pos.ne < 0 or pos.se < 0
	    or static_cast<size_t>(pos.ne) >= grid[0] or static_cast<size_t>(pos.se) >= grid[1]) {
		throw std::out_of_range{"Terrain chunk position is outside the map"};
	}
	return this->get_chunk(pos.ne + pos.se * grid[0]);
}

void Terrain::attach_renderer(const std::shared_ptr<renderer::RenderFactory> &render_factory) {
	// Shared corners use the highest adjacent tile across the whole map.
	// Adjacent chunk meshes therefore agree on the position of their edge.
	const auto stride = this->size[0] + 1;
	std::vector<float> heights(stride * (this->size[1] + 1), 0);
	for (const auto &chunk : this->chunks) {
		const auto extent = chunk->get_size();
		const auto offset = chunk->get_offset();
		for (size_t y = 0; y < extent[1]; ++y) {
			for (size_t x = 0; x < extent[0]; ++x) {
				const float elevation = chunk->get_tile(y * extent[0] + x).elevation.to_float();
				const auto index = (offset.se + y) * stride + offset.ne + x;
				for (auto corner : {index, index + 1, index + stride, index + stride + 1}) {
					heights[corner] = std::max(heights[corner], elevation);
				}
			}
		}
	}
	for (auto &chunk : this->get_chunks()) {
		const auto extent = chunk->get_size();
		const auto offset = chunk->get_offset();
		std::vector<float> corners;
		corners.reserve((extent[0] + 1) * (extent[1] + 1));
		for (size_t y = 0; y <= extent[1]; ++y) {
			const auto start = heights.begin() + (offset.se + y) * stride + offset.ne;
			corners.insert(corners.end(), start, start + extent[0] + 1);
		}
		auto render_entity = render_factory->add_terrain_render_entity(extent, offset);
		chunk->set_render_entity(render_entity);

		chunk->render_update(time::TIME_ZERO, corners);
	}
}

} // namespace openage::gamestate
