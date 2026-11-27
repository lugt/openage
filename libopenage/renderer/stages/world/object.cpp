// Copyright 2022-2024 the openage authors. See copying.md for legal info.

#include "object.h"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <utility>

#include <eigen3/Eigen/Dense>

#include "renderer/camera/frustum_2d.h"
#include "renderer/definitions.h"
#include "renderer/resources/animation/angle_info.h"
#include "renderer/resources/animation/animation_info.h"
#include "renderer/resources/animation/frame_info.h"
#include "renderer/resources/animation/layer_info.h"
#include "renderer/resources/assets/asset_manager.h"
#include "renderer/resources/assets/texture_manager.h"
#include "renderer/resources/frame_timing.h"
#include "renderer/resources/mesh_data.h"
#include "renderer/resources/texture_info.h"
#include "renderer/resources/texture_subinfo.h"
#include "renderer/stages/world/render_entity.h"
#include "renderer/uniform_input.h"
#include "util/fixed_point.h"
#include "util/vector.h"


namespace openage::renderer::world {

WorldObject::WorldObject(const std::shared_ptr<renderer::resources::AssetManager> &asset_manager) :
	require_renderable{true},
	changed{false},
	asset_manager{asset_manager},
	render_entity{nullptr},
	ref_id{0},
	position{nullptr, 0, "", nullptr, SCENE_ORIGIN},
	angle{nullptr, 0, "", nullptr, 0},
	animation_info{nullptr, 0},
	layer_uniforms{},
	last_update{0.0} {
}

void WorldObject::set_render_entity(const std::shared_ptr<RenderEntity> &entity) {
	this->render_entity = entity;
	this->fetch_updates();
}

void WorldObject::fetch_updates(const time::time_t &time) {
	// TODO: Calling this once per frame is very expensive
	if (not this->render_entity->is_changed()) {
		if (not this->layer_uniforms.empty() and this->layer_uniforms.size() != this->get_required_layer_count(time)) {
			this->require_renderable = true;
			this->changed = true;
		}
		return;
	}

	auto lock = this->render_entity->lock_update();
	this->ref_id = this->render_entity->get_locked_id();
	this->owner = this->render_entity->get_owner();
	this->position.sync(this->render_entity->get_position());
	this->animation_info.sync(this->render_entity->get_animation_path(),
	                          std::function<std::shared_ptr<renderer::resources::Animation2dInfo>(const std::string &)>(
								  [&](const std::string &path) {
									  if (path.empty()) {
										  auto placeholder = this->asset_manager->get_placeholder_animation();
										  if (placeholder) {
											  return (*placeholder).second;
										  }
										  return std::shared_ptr<renderer::resources::Animation2dInfo>{nullptr};
									  }
									  return this->asset_manager->request_animation(path);
								  }),
	                          this->last_update);
	this->angle.sync(this->render_entity->get_angle(), this->last_update);

	this->render_entity->acknowledge_update();
	this->changed = true;
	this->last_update = time;
	if (this->layer_uniforms.size() != this->get_required_layer_count(time)) {
		this->require_renderable = true;
	}
}

void WorldObject::update_uniforms(const time::time_t &time,
                                  const gamestate::VisibilitySnapshot *visibility) {
	this->sprite_top = 0.0f;
	// TODO: Only update uniforms that changed since last update
	if (this->layer_uniforms.empty()) [[unlikely]] {
		return;
	}

	// Object world position
	auto current_pos = this->position.get(time);
	const bool selectable = this->render_entity->is_selectable();
	// Live objects use the same visibility as selection and the minimap.
	// Remains have no simulation entity, so show them only on visible ground.
	const bool hidden_by_fog = this->is_hidden_by_fog(time, visibility);
	// New renderables need complete uniforms even when initially hidden.
	// Afterwards the fog flag alone hides them until visibility returns.
	if (hidden_by_fog and this->uniforms_initialized) {
		if (not this->fog_was_hidden) {
			for (auto &uniforms : this->layer_uniforms) {
				uniforms->update(this->fog_hidden, true);
			}
			this->fog_was_hidden = true;
		}
		return;
	}

	// Direction angle the object is facing towards currently
	auto angle_degrees = this->angle.get(time).to_float();

	// Animation information
	auto [last_update, animation_info] = this->animation_info.frame(time);

	for (size_t layer_idx = 0; layer_idx < this->layer_uniforms.size(); ++layer_idx) {
		auto &layer_unifs = this->layer_uniforms.at(layer_idx);
		layer_unifs->update(this->picking_id, selectable ? this->ref_id + 1u : 0u);
		layer_unifs->update(this->fog_hidden, hidden_by_fog);
		layer_unifs->update(this->player_color, this->owner);
		layer_unifs->update(this->obj_world_position, current_pos.to_world_space());

		// Frame subtexture
		auto &layer = animation_info->get_layer(layer_idx);
		auto &angle = layer.get_direction_angle(angle_degrees);

		// Flip subtexture horizontally if angle is mirrored
		if (angle->is_mirrored()) {
			layer_unifs->update(this->flip_x, true);
		}
		else {
			layer_unifs->update(this->flip_x, false);
		}

		// Current frame index considering current time
		size_t frame_idx;
		switch (layer.get_display_mode()) {
		case renderer::resources::display_mode::ONCE:
			frame_idx = std::min(angle->get_frame_count() - 1,
			                     layer.get_frame_timing()->get_frame(std::max(time - last_update, time::time_t::zero())));
			break;
		case renderer::resources::display_mode::LOOP: {
			// ONCE and LOOP are animated based on time
			auto &timing = layer.get_frame_timing();
			frame_idx = timing->get_frame(time, last_update);
		} break;
		case renderer::resources::display_mode::OFF:
		default:
			// OFF only shows the first frame
			frame_idx = 0;
			break;
		}

		// Index of texture and subtexture where the frame's pixels are located
		auto &frame_info = angle->get_frame(frame_idx);
		auto tex_idx = frame_info->get_texture_idx();
		auto subtex_idx = frame_info->get_subtexture_idx();

		auto &tex_info = animation_info->get_texture(tex_idx);
		auto &tex_manager = this->asset_manager->get_texture_manager();
		auto &texture = tex_manager->request(tex_info->get_image_path().value());
		layer_unifs->update(this->tex, texture);

		// Subtexture coordinates.inside texture
		auto coords = tex_info->get_subtex_info(subtex_idx).get_subtex_coords();
		layer_unifs->update(this->tile_params, coords);

		// Animation scale factor
		// Scales the subtex up or down in the shader
		auto scale = animation_info->get_scalefactor();
		layer_unifs->update(this->scale, scale);

		// Subtexture size in pixels
		auto subtex_size = tex_info->get_subtex_info(subtex_idx).get_size();
		Eigen::Vector2f subtex_size_vec{
			static_cast<float>(subtex_size[0]),
			static_cast<float>(subtex_size[1])};
		layer_unifs->update(this->subtex_size, subtex_size_vec);

		// Anchor point offset (in pixels)
		// moves the subtex in the shader so that the anchor point is at the object's position
		auto anchor = tex_info->get_subtex_info(subtex_idx).get_anchor_params();
		Eigen::Vector2f anchor_offset{
			static_cast<float>(anchor[0]),
			static_cast<float>(anchor[1])};
		layer_unifs->update(this->anchor_offset, anchor_offset);
		this->sprite_top = std::max(this->sprite_top, (subtex_size_vec.y() + anchor_offset.y()) * scale * 0.5f);
	}
	this->uniforms_initialized = true;
	this->fog_was_hidden = hidden_by_fog;
}

uint32_t WorldObject::get_id() {
	return this->ref_id;
}

bool WorldObject::is_removed(const time::time_t &at) const {
	return this->render_entity->is_removed(at);
}

const renderer::resources::MeshData WorldObject::get_mesh() {
	return resources::MeshData::make_quad();
}

const Eigen::Matrix4f WorldObject::get_model_matrix() {
	return Eigen::Matrix4f::Identity();
}

bool WorldObject::requires_renderable() const {
	return this->require_renderable;
}

void WorldObject::clear_requires_renderable() {
	this->require_renderable = false;
}

size_t WorldObject::get_required_layer_count(const time::time_t &time) const {
	auto animation_info = this->animation_info.get(time);
	if (not animation_info) {
		return 0;
	}

	return animation_info->get_layer_count();
}

std::vector<size_t> WorldObject::get_layer_positions(const time::time_t &time) const {
	auto animation_info = this->animation_info.get(time);
	if (not animation_info) {
		return {};
	}

	std::vector<size_t> positions;
	for (size_t i = 0; i < animation_info->get_layer_count(); ++i) {
		auto layer = animation_info->get_layer(i);
		positions.push_back(layer.get_position());
	}

	return positions;
}

bool WorldObject::is_changed() {
	return this->changed;
}

void WorldObject::clear_changed_flag() {
	this->changed = false;
}

void WorldObject::set_uniforms(std::vector<std::shared_ptr<renderer::UniformInput>> &&uniforms) {
	this->layer_uniforms = std::move(uniforms);
	this->uniforms_initialized = false;
	this->fog_was_hidden = false;
}

void WorldObject::clear_uniforms() {
	this->layer_uniforms.clear();
	this->uniforms_initialized = false;
	this->fog_was_hidden = false;
	this->require_renderable = true;
	this->sprite_top = 0.0f;
}

bool WorldObject::is_hidden_by_fog(const time::time_t &time, const gamestate::VisibilitySnapshot *visibility) const {
	if (not visibility) {
		return false;
	}
	if (this->render_entity->is_selectable()) {
		return not visibility->contains(this->ref_id);
	}
	const auto point = this->position.get(time);
	return visibility->at(point.ne.to_int(), point.se.to_int()) != gamestate::TileVisibility::Visible;
}

bool WorldObject::is_visible(const camera::Frustum2d &frustum,
                             const time::time_t &time) {
	static const Eigen::Matrix4f model_matrix = this->get_model_matrix();
	Eigen::Vector3f current_pos = this->position.get(time).to_world_space();
	auto animation_info = this->animation_info.get(time);
	return frustum.in_frustum(current_pos,
	                          model_matrix,
	                          animation_info->get_scalefactor(),
	                          animation_info->get_max_bounds());
}

} // namespace openage::renderer::world
