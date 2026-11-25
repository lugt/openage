// Copyright 2023-2024 the openage authors. See copying.md for legal info.

#include "chunk.h"

#include <algorithm>

#include "renderer/resources/assets/asset_manager.h"
#include "renderer/resources/mesh_data.h"
#include "renderer/stages/terrain/mesh.h"
#include "renderer/stages/terrain/render_entity.h"


namespace openage::renderer::terrain {

TerrainChunk::TerrainChunk(const std::shared_ptr<renderer::resources::AssetManager> &asset_manager,
                           const util::Vector2s size,
                           const coord::scene2_delta offset) :
	size{size},
	offset{offset},
	asset_manager{asset_manager} {}

void TerrainChunk::set_render_entity(const std::shared_ptr<RenderEntity> &entity) {
	this->render_entity = entity;
}

void TerrainChunk::fetch_updates(const time::time_t & /* time */) {
	// TODO: Don't create model if render entity is not set
	if (not this->render_entity) {
		return;
	}

	// Check render entity for updates
	if (not this->render_entity->is_changed()) {
		return;
	}

	// Get the terrain data from the render entity
	auto terrain_size = this->render_entity->get_size();
	const auto paths = this->render_entity->get_terrain_paths();
	std::vector<std::string> terrain_paths{paths.begin(), paths.end()};
	std::ranges::sort(terrain_paths);
	auto tiles = this->render_entity->get_tiles();
	auto heightmap_verts = this->render_entity->get_vertices();

	// Recreate the mesh data
	// TODO: Change mesh instead of recreating it
	// TODO: Multiple meshes
	this->meshes.clear();
	for (const auto &terrain_path : terrain_paths) {
		auto new_mesh = this->create_mesh(terrain_size, tiles, heightmap_verts, terrain_path);
		new_mesh->create_model_matrix(this->offset);
		this->meshes.push_back(new_mesh);
	}

	// auto new_mesh = this->create_mesh();
	// new_mesh->create_model_matrix(this->offset);
	// this->meshes.clear();
	// this->meshes.push_back(new_mesh);

	// Indicate to the render entity that its updates have been processed.
	this->render_entity->clear_changed_flag();
}

void TerrainChunk::update_uniforms(const time::time_t &time) {
	for (auto &mesh : this->meshes) {
		mesh->update_uniforms(time);
	}
}

const std::vector<std::shared_ptr<TerrainRenderMesh>> &TerrainChunk::get_meshes() const {
	return this->meshes;
}

std::shared_ptr<TerrainRenderMesh> TerrainChunk::create_mesh(const util::Vector2s vert_size,
                                                             const RenderEntity::tiles_t &tiles,
                                                             const std::vector<coord::scene3> &heightmap_verts,
                                                             const std::string &texture_path) {
	auto v_width = vert_size[0];
	auto v_height = vert_size[1];

	std::vector<float> mesh_verts;
	std::vector<uint32_t> idxs;

	const auto halo = this->render_entity->get_terrain_halo();

	// All terrain layers use the same triangulation. Corner weights share
	// adjoining tiles while the center retains the tile's original texture.
	auto weights = [&](size_t x, size_t y) {
		float own = 0;
		float accumulated = 0;
		float count = 0;
		for (int dy = -1; dy <= 0; ++dy) {
			for (int dx = -1; dx <= 0; ++dx) {
				const auto col = static_cast<int64_t>(x) + dx;
				const auto row = static_cast<int64_t>(y) + dy;
				std::string path;
				if (not halo.empty()) {
					path = halo.at((col + 1) + (row + 1) * (v_width + 1));
				}
				else if (col >= 0 and row >= 0 and col < static_cast<int64_t>(v_width - 1)
				         and row < static_cast<int64_t>(v_height - 1)) {
					path = tiles.at(col + row * (v_width - 1)).second;
				}
				if (path.empty()) {
					continue;
				}
				own += path == texture_path ? 1.0f : 0.0f;
				accumulated += path <= texture_path ? 1.0f : 0.0f;
				++count;
			}
		}
		return std::array<float, 2>{own / count, accumulated / count};
	};
	auto vertex = [&](const coord::scene3 &v, const std::array<float, 2> &weight) {
		const auto world = v.to_world_space();
		mesh_verts.insert(mesh_verts.end(), {world[0], world[1], world[2], ((v.ne + this->offset.ne) / 10).to_float(), ((v.se + this->offset.se) / 10).to_float(), weight[0], weight[1]});
	};
	for (size_t y = 0; y < v_height - 1; ++y) {
		for (size_t x = 0; x < v_width - 1; ++x) {
			const std::array<std::array<float, 2>, 4> corners{
				weights(x, y), weights(x + 1, y), weights(x + 1, y + 1), weights(x, y + 1)};
			const auto &path = tiles.at(x + y * (v_width - 1)).second;
			if (path != texture_path and std::ranges::all_of(corners, [](auto w) { return w[0] == 0; })) {
				continue;
			}
			const std::array<size_t, 4> indices{
				x + y * v_width, x + 1 + y * v_width, x + 1 + (y + 1) * v_width, x + (y + 1) * v_width};
			const auto base = static_cast<uint32_t>(mesh_verts.size() / 7);
			for (size_t i = 0; i < 4; ++i) {
				vertex(heightmap_verts[indices[i]], corners[i]);
			}
			// Keep the center on the original diagonal so layers share its depth.
			const auto &a = heightmap_verts[indices[0]];
			const auto &b = heightmap_verts[indices[2]];
			vertex(coord::scene3{(a.ne + b.ne) / 2, (a.se + b.se) / 2, (a.up + b.up) / 2},
			       {path == texture_path ? 1.0f : 0.0f, path <= texture_path ? 1.0f : 0.0f});
			for (uint32_t i = 0; i < 4; ++i) {
				idxs.insert(idxs.end(), {base + i, base + (i + 1) % 4, base + 4});
			}
		}
	}

	resources::VertexInputInfo info{
		{resources::vertex_input_t::V3F32, resources::vertex_input_t::V2F32, resources::vertex_input_t::V2F32},
		resources::vertex_layout_t::AOS,
		resources::vertex_primitive_t::TRIANGLES,
		resources::index_t::U32};

	auto const vert_data_size_new = mesh_verts.size() * sizeof(float);
	std::vector<uint8_t> vert_data_new(vert_data_size_new);
	std::memcpy(vert_data_new.data(), mesh_verts.data(), vert_data_size_new);

	auto const idx_data_size = idxs.size() * sizeof(uint32_t);
	std::vector<uint8_t> idx_data(idx_data_size);
	std::memcpy(idx_data.data(), idxs.data(), idx_data_size);

	resources::MeshData meshdata{std::move(vert_data_new), std::move(idx_data), info};

	// Create the terrain mesh
	auto terrain_info = this->asset_manager->request_terrain(texture_path);
	auto terrain_mesh = std::make_shared<TerrainRenderMesh>(
		this->asset_manager,
		terrain_info,
		std::move(meshdata));

	return terrain_mesh;
}

util::Vector2s &TerrainChunk::get_size() {
	return this->size;
}

coord::scene2_delta &TerrainChunk::get_offset() {
	return this->offset;
}

} // namespace openage::renderer::terrain
