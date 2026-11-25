// Copyright 2026 the openage authors. See copying.md for legal info.

#include "construction.h"

#include <algorithm>
#include <cmath>

#include "event/event_loop.h"
#include "gamestate/component/api/build.h"
#include "gamestate/component/api/live.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/system/idle.h"

namespace openage::gamestate::event {

namespace {

const nyan::fqon_t health = "de2_base.data.util.attribute.types.Health";

void apply_progress(GameEntity &entity, component::Build &build, double next_exact, const time::time_t &time) {
	const auto clamped = std::clamp(next_exact, build.exact_progress, 100.0);
	const auto new_progress = static_cast<int64_t>(std::floor(clamped + 1e-9));
	if (new_progress > build.get_progress()) {
		auto live = entity.try_get_component_as<component::Live>(component::component_t::LIVE);
		if (live) {
			const auto hp = live->get_attribute_value(time, health);
			const auto max_hp = live->get_attribute_max(health);
			if (hp and max_hp) {
				const auto previous = std::max<int64_t>(1, *max_hp * build.get_progress() / 100);
				const auto new_value = std::max<int64_t>(1, *max_hp * new_progress / 100);
				live->set_attribute(time, health, std::min(*max_hp, *hp + new_value - previous));
			}
		}
		build.set_progress(new_progress);
		if (entity.has_component(component::component_t::IDLE)) {
			system::Idle::idle(std::shared_ptr<GameEntity>(&entity, [](GameEntity *) {}), time);
		}
	}
	build.exact_progress = clamped;
}

void advance_builders(GameState &game, GameEntity &site, component::Build &build, const time::time_t &time) {
	std::erase_if(build.active_builders, [&](entity_id_t worker_id) {
		if (not game.has_game_entity(worker_id)) {
			return true;
		}
		const auto worker = game.get_game_entity(worker_id);
		const auto assigned = build.assignments.find(worker_id);
		return worker->is_dead() or assigned == build.assignments.end()
		    or worker->construction_order != assigned->second;
	});
	if (time > build.last_update and not build.active_builders.empty()
	    and build.build_seconds > 0.0 and build.exact_progress < 100.0) {
		const double dt = (time - build.last_update).to_double();
		const double aegis_mult = game.fast_actions() ? 100.0 : 1.0;
		const double multiplier = ((static_cast<double>(build.active_builders.size()) + 2.0) / 3.0) * aegis_mult;
		const double delta = 100.0 * dt * multiplier / build.build_seconds;
		apply_progress(site, build, build.exact_progress + delta, time);
	}
	build.last_update = time;
}

void schedule_next(openage::event::EventLoop &loop,
                   const std::shared_ptr<openage::event::EventEntity> &target,
                   const std::shared_ptr<openage::event::State> &state,
                   const GameState &game,
                   entity_id_t site_id,
                   component::Build &build,
                   const time::time_t &time) {
	if (build.active_builders.empty() or build.build_seconds <= 0.0 or build.get_progress() >= 100) {
		return;
	}
	int64_t next_target = 100;
	for (int64_t milestone : {25, 50, 75, 100}) {
		if (static_cast<double>(milestone) > build.exact_progress + 1e-6) {
			next_target = milestone;
			break;
		}
	}
	const double aegis_mult = game.fast_actions() ? 100.0 : 1.0;
	const double multiplier = ((static_cast<double>(build.active_builders.size()) + 2.0) / 3.0) * aegis_mult;
	const double rate = 100.0 * multiplier / build.build_seconds;
	const double delay = std::max(0.0, (static_cast<double>(next_target) - build.exact_progress) / rate);
	const auto gen = ++build.schedule_gen;
	loop.create_event("game.construction", target, state, time + time::time_t::from_double(delay), {
		{"entity_id", site_id}, {"gen", gen},
	});
}

} // namespace

ConstructionHandler::ConstructionHandler() :
	OnceEventHandler{"game.construction"} {}

void ConstructionHandler::invoke(openage::event::EventLoop &loop,
                                  const std::shared_ptr<openage::event::EventEntity> &target,
                                  const std::shared_ptr<openage::event::State> &state,
                                  const time::time_t &time,
                                  const param_map &params) {
	auto game = std::static_pointer_cast<GameState>(state);
	auto id = params.get("entity_id", entity_id_t{});
	if (not game->has_game_entity(id)) {
		return;
	}
	auto entity = game->get_game_entity(id);
	auto build = entity->try_get_component_as<component::Build>(component::component_t::BUILD);
	if (entity->is_dead() or not build) {
		return;
	}
	if (params.get("depart", false)) {
		const auto worker_id = params.get("worker", entity_id_t{});
		advance_builders(*game, *entity, *build, time);
		build->active_builders.erase(worker_id);
		build->assignments.erase(worker_id);
		schedule_next(loop, target, state, *game, id, *build, time);
		return;
	}
	if (params.check_type<entity_id_t>("worker")) {
		const auto worker_id = params.get("worker", entity_id_t{});
		if (not game->has_game_entity(worker_id)) {
			return;
		}
		const auto worker = game->get_game_entity(worker_id);
		const auto assigned = build->assignments.find(worker_id);
		if (worker->is_dead() or not worker->construct_ability
		    or worker->construction_order != params.get("order", uint64_t{})
		    or assigned == build->assignments.end() or assigned->second != worker->construction_order) {
			return;
		}
		if (params.get("arrive", false)) {
			advance_builders(*game, *entity, *build, time);
			if (build->get_progress() >= 100) {
				if (worker->has_component(component::component_t::IDLE)) {
					system::Idle::idle(worker, time);
				}
				worker->building_site = 0;
				return;
			}
			build->active_builders.insert(worker_id);
			worker->play_animation(*worker->construct_ability, time);
			schedule_next(loop, target, state, *game, id, *build, time);
			return;
		}
	}
	if (params.check_type<uint64_t>("gen")) {
		if (params.get("gen", uint64_t{}) != build->schedule_gen) {
			return;
		}
		advance_builders(*game, *entity, *build, time);
		if (build->get_progress() >= 100) {
			for (const auto worker_id : build->active_builders) {
				if (game->has_game_entity(worker_id)) {
					const auto worker = game->get_game_entity(worker_id);
					worker->building_site = 0;
					if (worker->has_component(component::component_t::IDLE)) {
						system::Idle::idle(worker, time);
					}
				}
			}
			build->active_builders.clear();
			build->assignments.clear();
			if (auto live = entity->try_get_component_as<component::Live>(component::component_t::LIVE)) {
				auto hp = live->get_attribute_value(time, health);
				auto max_hp = live->get_attribute_max(health);
				if (hp and max_hp) {
					entity->render_damage(time, *max_hp, *hp, *max_hp);
				}
			}
		}
		else {
			schedule_next(loop, target, state, *game, id, *build, time);
		}
		return;
	}
	auto progress = params.get("progress", int64_t{});
	if (progress < 0 or progress > 100 or progress <= build->get_progress()) {
		return;
	}
	apply_progress(*entity, *build, static_cast<double>(progress), time);
	if (progress == 100) {
		if (auto live = entity->try_get_component_as<component::Live>(component::component_t::LIVE)) {
			auto hp = live->get_attribute_value(time, health);
			auto max_hp = live->get_attribute_max(health);
			if (hp and max_hp) {
				entity->render_damage(time, *max_hp, *hp, *max_hp);
			}
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
