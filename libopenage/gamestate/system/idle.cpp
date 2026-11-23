// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#include "idle.h"

#include <vector>

#include "error/error.h"
#include "log/log.h"
#include "log/message.h"

#include "gamestate/component/api/idle.h"
#include "gamestate/component/api/build.h"
#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"


namespace openage::gamestate::system {

const time::time_t Idle::idle(const std::shared_ptr<gamestate::GameEntity> &entity,
                              const time::time_t &start_time) {
	if (not entity->has_component(component::component_t::IDLE)) [[unlikely]] {
		throw Error{ERR << "Entity " << entity->get_id() << " has no idle component."};
	}

	auto idle_component = std::dynamic_pointer_cast<component::Idle>(
		entity->get_component(component::component_t::IDLE));
	auto build = entity->try_get_component_as<component::Build>(component::component_t::BUILD);
	auto construction = build ? build->get_construction_animation() : std::string{};
	if (construction.empty()) {
		entity->play_animation(idle_component->get_ability(), start_time);
	}
	else {
		entity->render_update(start_time, construction);
	}

	// TODO: play sound

	return time::time_t::from_int(0);
}

} // namespace openage::gamestate::system
