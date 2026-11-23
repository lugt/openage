// Copyright 2022-2024 the openage authors. See copying.md for legal info.

#include "render_entity.h"

#include <functional>
#include <mutex>

#include "renderer/definitions.h"


namespace openage::renderer::world {

RenderEntity::RenderEntity() :
	renderer::RenderEntity{},
	ref_id{0},
	position{nullptr, 0, "", nullptr, SCENE_ORIGIN},
	angle{nullptr, 0, "", nullptr, 0},
	animation_path{nullptr, 0} {
}

void RenderEntity::update(const uint32_t ref_id,
                          const curve::Continuous<coord::phys3> &position,
                          const curve::Segmented<coord::phys_angle_t> &angle,
                          const std::string animation_path,
                          const time::time_t time,
                          uint32_t owner) {
	std::unique_lock lock{this->mutex};

	this->ref_id = ref_id;
	this->owner = owner;
	const auto sync_from = std::min(time, this->last_update);
	this->position.sync(position,
	                    std::function<coord::scene3(const coord::phys3 &)>([](const coord::phys3 &pos) {
							return pos.to_scene3();
						}),
	                    sync_from);
	this->angle.sync(angle, sync_from);
	this->animation_path.set_last(time, animation_path);
	this->changed = true;
	this->last_update = time;
}

void RenderEntity::update(const uint32_t ref_id,
                          const coord::phys3 position,
                          const std::string animation_path,
                          const time::time_t time) {
	std::unique_lock lock{this->mutex};

	this->ref_id = ref_id;
	this->position.set_last(time, position.to_scene3());
	this->animation_path.set_last(time, animation_path);
	this->changed = true;
	this->last_update = time;
}

void RenderEntity::mark_removed(const time::time_t &at) {
	this->removal_time = at;
	this->removed.store(true);
}

bool RenderEntity::is_removed(const time::time_t &at) const {
	return this->removed.load() and at >= this->removal_time;
}

uint32_t RenderEntity::get_id() {
	std::shared_lock lock{this->mutex};

	return this->ref_id;
}

const curve::Continuous<coord::scene3> &RenderEntity::get_position() {
	return this->position;
}

const curve::Segmented<coord::phys_angle_t> &RenderEntity::get_angle() {
	return this->angle;
}

const curve::Discrete<std::string> &RenderEntity::get_animation_path() {
	return this->animation_path;
}

} // namespace openage::renderer::world
