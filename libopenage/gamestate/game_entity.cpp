// Copyright 2022-2024 the openage authors. See copying.md for legal info.

#include "game_entity.h"

#include "error/error.h"
#include "gamestate/api/ability.h"
#include "gamestate/api/animation.h"
#include "gamestate/api/types.h"
#include "gamestate/api/property.h"
#include "gamestate/component/api/idle.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/base_component.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "log/message.h"
#include "renderer/stages/world/render_entity.h"

namespace openage::gamestate {

GameEntity::GameEntity(entity_id_t id) :
	id{id},
	components{},
	render_entity{nullptr} {
}

std::shared_ptr<GameEntity> GameEntity::copy(entity_id_t id) {
	auto copy = std::shared_ptr<GameEntity>(new GameEntity(*this));
	copy->set_id(id);

	return copy;
}

entity_id_t GameEntity::get_id() const {
	return this->id;
}

void GameEntity::set_render_entity(const std::shared_ptr<renderer::world::RenderEntity> &entity) {
	// TODO: Transfer state from old render entity to new one?

	this->render_entity = entity;
}

void GameEntity::remove_from_renderer() {
	if (this->render_entity) {
		this->render_entity->mark_removed(this->animation_removal_time.value_or(time::TIME_MIN));
		this->render_entity.reset();
	}
}

void GameEntity::set_death_animation(std::string path, time::time_t duration) {
	this->death_animation = std::move(path);
	this->death_duration = duration;
}

void GameEntity::set_corpse_animation(std::string path, time::time_t duration) {
	this->corpse_animation = std::move(path);
	this->corpse_duration = duration;
}

void GameEntity::add_damage_animation(double max_health_percent, std::string path) {
	this->damage_animations.emplace(max_health_percent, std::move(path));
}

void GameEntity::render_damage(const time::time_t &time, int64_t previous_hp, int64_t current_hp, int64_t max_hp) {
	if (this->damage_animations.empty() or current_hp <= 0 or max_hp <= 0) {
		return;
	}
	auto stage = this->damage_animations.lower_bound(100.0 * current_hp / max_hp);
	auto previous_stage = this->damage_animations.lower_bound(100.0 * previous_hp / max_hp);
	if (stage == previous_stage) {
		return;
	}
	if (stage != this->damage_animations.end()) {
		this->render_update(time, stage->second);
	}
	else if (auto idle = this->try_get_component_as<component::Idle>(component::component_t::IDLE)) {
		this->play_animation(idle->get_ability(), time);
	}
}

void GameEntity::render_death(const time::time_t &time) {
	if (this->death_animation.empty()) {
		return;
	}
	const auto position = this->try_get_component_as<component::Position>(component::component_t::POSITION);
	if (position) {
		position->set_position(time, position->get_positions().get(time));
		position->set_angle(time, position->get_angles().get(time));
	}
	this->render_update(time, this->death_animation);
	auto end = time + this->death_duration;
	if (not this->corpse_animation.empty() and not this->has_component(component::component_t::BUILD)) {
		this->render_update(end, this->corpse_animation);
		end += this->corpse_duration;
	}
	this->animation_removal_time = end;
}

bool GameEntity::play_animation(const nyan::Object &ability, const time::time_t &time) {
	auto path = api::APIAnimation::get_ability_animation_path(ability);
	if (path.empty()) {
		return false;
	}
	this->render_update(time, path);
	return true;
}

void GameEntity::set_manager(const std::shared_ptr<GameEntityManager> &manager) {
	this->manager = manager;
}

const std::shared_ptr<GameEntityManager> &GameEntity::get_manager() const {
	return this->manager;
}

const std::shared_ptr<component::Component> &GameEntity::get_component(component::component_t type) {
	auto it = this->components.find(type);
	if (it == this->components.end()) {
		// Name the entity and the component. A bare
		// "unordered_map::at: key not found" gives no clue which
		// entity of a scene was missing what, and the entity id alone
		// does not say which component was being asked for.
		throw openage::error::Error{
			MSG(err) << "entity " << this->id << " has no "
			         << component::to_string(type) << " component"};
	}
	return it->second;
}

std::shared_ptr<component::Component> GameEntity::try_get_component(component::component_t type) {
	auto it = this->components.find(type);
	if (it == this->components.end()) {
		return nullptr;
	}
	return it->second;
}

void GameEntity::add_component(const std::shared_ptr<component::Component> &component) {
	this->components.insert({component->get_type(), component});
}

bool GameEntity::has_component(component::component_t type) const {
	return this->components.contains(type);
}

void GameEntity::render_update(const time::time_t &time,
                               const std::string &animation_path) {
	if (this->render_entity != nullptr) {
		const auto &pos = dynamic_pointer_cast<component::Position>(
							  this->components.at(component::component_t::POSITION))
		                      ->get_positions();
		const auto &angle = dynamic_pointer_cast<component::Position>(
								this->components.at(component::component_t::POSITION))
		                        ->get_angles();
		const auto ownership = this->try_get_component_as<component::Ownership>(
			component::component_t::OWNERSHIP);
		const auto owner = ownership ? ownership->get_owners().get(time) : 0;
		this->render_entity->update(this->id, pos, angle, animation_path, time, owner);
	}
}

void GameEntity::set_id(entity_id_t id) {
	this->id = id;
}

} // namespace openage::gamestate
