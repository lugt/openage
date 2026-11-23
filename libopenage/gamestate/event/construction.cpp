// Copyright 2026 the openage authors. See copying.md for legal info.

#include "construction.h"

#include <algorithm>

#include "gamestate/component/api/build.h"
#include "gamestate/component/api/live.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/system/idle.h"

namespace openage::gamestate::event {

ConstructionHandler::ConstructionHandler() :
	OnceEventHandler{"game.construction"} {}

void ConstructionHandler::invoke(openage::event::EventLoop &,
                                  const std::shared_ptr<openage::event::EventEntity> &,
                                  const std::shared_ptr<openage::event::State> &state,
                                  const time::time_t &time,
                                  const param_map &params) {
	auto game = std::static_pointer_cast<GameState>(state);
	auto id = params.get("entity_id", entity_id_t{});
	auto progress = params.get("progress", int64_t{});
	if (progress < 0 or progress > 100 or not game->has_game_entity(id)) {
		return;
	}
	auto entity = game->get_game_entity(id);
	auto build = entity->try_get_component_as<component::Build>(component::component_t::BUILD);
	if (entity->is_dead() or not build) {
		return;
	}
	if (params.check_type<entity_id_t>("worker")) {
		const auto worker_id = params.get("worker", entity_id_t{});
		if (not game->has_game_entity(worker_id)) {
			return;
		}
		const auto worker = game->get_game_entity(worker_id);
		if (worker->is_dead() or not worker->construct_ability
		    or worker->construction_order != params.get("order", uint64_t{})
		    or build->assignment != std::pair{worker_id, worker->construction_order}) {
			return;
		}
		if (progress == 0) {
			worker->play_animation(*worker->construct_ability, time);
			return;
		}
		if (progress == 100 and worker->has_component(component::component_t::IDLE)) {
			system::Idle::idle(worker, time);
			build->assignment.reset();
		}
	}
	if (progress <= build->get_progress()) {
		return;
	}
	auto live = entity->try_get_component_as<component::Live>(component::component_t::LIVE);
	const nyan::fqon_t health = "de2_base.data.util.attribute.types.Health";
	if (live) {
		const auto hp = live->get_attribute_value(time, health);
		const auto max_hp = live->get_attribute_max(health);
		if (hp and max_hp) {
			const auto previous = std::max<int64_t>(1, *max_hp * build->get_progress() / 100);
			const auto new_value = std::max<int64_t>(1, *max_hp * progress / 100);
			live->set_attribute(time, health, std::min(*max_hp, *hp + new_value - previous));
		}
	}
	build->set_progress(progress);
	if (entity->has_component(component::component_t::IDLE)) {
		system::Idle::idle(entity, time);
	}
	if (progress == 100 and live) {
		auto hp = live->get_attribute_value(time, health);
		auto max_hp = live->get_attribute_max(health);
		if (hp and max_hp) {
			entity->render_damage(time, *max_hp, *hp, *max_hp);
		}
	}
}

void ConstructionHandler::setup_event(const std::shared_ptr<openage::event::Event> &,
                                       const std::shared_ptr<openage::event::State> &) {}

time::time_t ConstructionHandler::predict_invoke_time(
	const std::shared_ptr<openage::event::EventEntity> &,
	const std::shared_ptr<openage::event::State> &,
	const time::time_t &at) {
	return at;
}

} // namespace openage::gamestate::event
