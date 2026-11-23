// Copyright 2026 the openage authors. See copying.md for legal info.

#include "gather.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "event/event_loop.h"
#include "coord/tile.h"
#include "log/log.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/gather.h"
#include "gamestate/component/api/build.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "gamestate/player.h"
#include "gamestate/system/idle.h"
#include "gamestate/system/move.h"
#include "pathfinding/navigation_service.h"
#include "scenario/swept_aabb.h"

namespace openage::gamestate::event {
namespace {

enum class Phase { Start, Seek, Harvest, Return, Deposit };

coord::phys3 position(GameEntity &entity, const time::time_t &at) {
	return entity.try_get_component_as<component::Position>(component::component_t::POSITION)
	    ->get_positions().get(at);
}

double reach(const GameEntity &entity, size_t axis) {
	if (not entity.has_footprint()) {
		return 1.05;
	}
	return (axis == 0 ? entity.footprint().radius_x() : entity.footprint().radius_y()) + 1.05;
}

bool nearby(const coord::phys3 &worker, const GameEntity &site, const coord::phys3 &center) {
	return std::abs((worker.ne - center.ne).to_double()) <= reach(site, 0)
	   and std::abs((worker.se - center.se).to_double()) <= reach(site, 1);
}

std::vector<coord::phys3> approach(GameState &state, GameEntity &worker, GameEntity &site,
                                     const coord::phys3 &from, const coord::phys3 &center) {
	auto navigation = state.get_map()->get_navigation_service();
	if (not navigation) {
		return {};
	}
	const auto start = from.to_tile();
	const auto base = center.to_tile();
	const auto dx = static_cast<int>(std::ceil(reach(site, 0)));
	const auto dy = static_cast<int>(std::ceil(reach(site, 1)));
	const openage::pathfinding::MoverFootprint mover{
		worker.has_footprint() ? worker.footprint().radius_x() : 0,
		worker.has_footprint() ? worker.footprint().radius_y() : 0,
	};
	const double radius_x = site.has_footprint() ? site.footprint().radius_x() : 0;
	const double radius_y = site.has_footprint() ? site.footprint().radius_y() : 0;
	using scenario::CellPosition;
	const CellPosition start_cell{static_cast<int>(start.ne), static_cast<int>(start.se)};
	const auto occupancy = state.get_map()->get_dynamic_occupancy();
	auto bounds_at = [&](const coord::phys3 &point) {
		const auto x = point.ne.to_double();
		const auto y = point.se.to_double();
		return component::FootprintBounds{x - mover.radius_x, y - mover.radius_y, x + mover.radius_x, y + mover.radius_y};
	};
	auto can_move = [&](const coord::phys3 &from, const coord::phys3 &to) {
		return occupancy->can_occupy(worker.get_id(), ::openage::scenario::swept_aabb(bounds_at(from), bounds_at(to)));
	};
	std::vector<coord::phys3> best;
	size_t shortest = std::numeric_limits<size_t>::max();
	for (int x = -dx; x <= dx; ++x) {
		for (int y = -dy; y <= dy; ++y) {
			const coord::tile tile{base.ne + x, base.se + y};
			const auto goal = tile.to_phys3_center();
			if (not nearby(goal, site, center)) {
				continue;
			}
			if (std::abs((goal.ne - center.ne).to_double()) <= radius_x + mover.radius_x
			    and std::abs((goal.se - center.se).to_double()) <= radius_y + mover.radius_y) {
				continue;
			}
			if (not occupancy->can_occupy(worker.get_id(), bounds_at(goal))) {
				continue;
			}
			const auto path = navigation->request_path(start_cell,
				{static_cast<int>(tile.ne), static_cast<int>(tile.se)}, mover, [&](CellPosition a, CellPosition b) {
				return can_move(a == start_cell ? from : coord::tile{a.x, a.y}.to_phys3_center(),
				                coord::tile{b.x, b.y}.to_phys3_center());
			});
			if (path.result == openage::pathfinding::GridAStarPathfinder::Result::PATH_FOUND
			    and (path.waypoints.size() > 1 or can_move(from, goal))
			    and path.waypoints.size() < shortest) {
				best = {from};
				for (size_t i = 1; i < path.waypoints.size(); ++i) {
					const auto cell = path.waypoints[i];
					best.push_back(coord::tile{cell.x, cell.y}.to_phys3_center());
				}
				if (best.size() == 1 and from != goal) {
					best.push_back(goal);
				}
				shortest = path.waypoints.size();
			}
		}
	}
	return best;
}

} // namespace

GatherHandler::GatherHandler(Delivery delivery) :
	OnceEventHandler{"game.gather"},
	delivery{std::move(delivery)} {}

void GatherHandler::invoke(openage::event::EventLoop &loop,
                            const std::shared_ptr<openage::event::EventEntity> &target,
                            const std::shared_ptr<openage::event::State> &state,
                            const time::time_t &time,
                            const param_map &params) {
	auto game = std::static_pointer_cast<GameState>(state);
	const auto worker_id = params.get("worker_id", entity_id_t{});
	const auto spot_id = params.get("resource_id", entity_id_t{});
	if (not game->has_game_entity(worker_id)) {
		return;
	}
	auto worker = game->get_game_entity(worker_id);
	auto gather = worker->try_get_component_as<component::Gather>(component::component_t::GATHER);
	auto worker_owner = worker->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
	if (not gather or worker->is_dead() or not worker_owner
	    or not worker->has_component(component::component_t::MOVE)) {
		return;
	}
	const auto owner = worker_owner->get_owners().get(time);
	auto can_deliver = [&](const GameEntity &site) {
		return not site.is_dead()
		    and std::ranges::find(site.drop_containers, gather->carried_container) != site.drop_containers.end();
	};
	auto owned_depot = [&](const std::shared_ptr<GameEntity> &site) {
		if (not can_deliver(*site)) {
			return false;
		}
		const auto ownership = site->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		const auto build = site->try_get_component_as<component::Build>(component::component_t::BUILD);
		return ownership and ownership->get_owners().get(time) == owner
		    and (not build or not build->is_under_construction());
	};

	auto phase = params.get("phase", Phase::Start);
	auto spot = game->has_game_entity(spot_id) ? game->get_game_entity(spot_id) : nullptr;
	const auto ability = spot and spot->resource_spot ? gather->find_ability(spot->resource_spot->definition) : nullptr;
	if (phase == Phase::Start) {
		if (not ability or spot->resource_spot->remaining <= 0) {
			return;
		}
		if (auto attack = worker->try_get_component_as<component::Attack>(component::component_t::ATTACK)) {
			attack->stop_attack();
		}
		++gather->order;
		++worker->construction_order;
		system::Move::halt(worker, game, time);
		phase = gather->carried_amount > 0 ? Phase::Return : Phase::Seek;
	}
	else if (params.get("order", uint64_t{}) != gather->order) {
		return;
	}

	auto schedule = [&](Phase next, const time::time_t &duration) {
		loop.create_event("game.gather", target, state, time + duration, {
			{"worker_id", worker_id}, {"resource_id", spot_id},
			{"phase", next}, {"order", gather->order},
		});
	};
	auto travel = [&](GameEntity &site, Phase next) {
		auto arrive = [&](const time::time_t &duration) {
			loop.create_event("game.gather", target, state, time + duration, {
				{"worker_id", worker_id}, {"resource_id", spot_id}, {"depot", site.get_id()},
				{"phase", next}, {"order", gather->order},
			});
		};
		const auto from = position(*worker, time);
		const auto center = position(site, time);
		if (nearby(from, site, center)) {
			arrive(time::time_t::zero());
			return true;
		}
		else if (auto route = approach(*game, *worker, site, from, center); not route.empty()) {
			auto duration = system::Move::move_along_path(worker, game, std::move(route), time);
			if (duration > time::time_t::zero()) {
				arrive(duration);
				return true;
			}
		}
		else {
			log::log(MSG(warn) << "No gathering route for " << worker_id << " to " << site.get_id());
		}
		return false;
	};

	switch (phase) {
	case Phase::Seek:
		if (ability and spot->resource_spot->remaining > 0) {
			travel(*spot, Phase::Harvest);
		}
		else if (worker->has_component(component::component_t::IDLE)) {
			system::Idle::idle(worker, time);
		}
		break;
	case Phase::Harvest:
		if (ability and nearby(position(*worker, time), *spot, position(*spot, time))) {
			if (spot->resource_spot->requires_hunting and not spot->is_dead()) {
				auto attack = worker->try_get_component_as<component::Attack>(component::component_t::ATTACK);
				const auto live = spot->try_get_component_as<component::Live>(component::component_t::LIVE);
				if (not attack or not live) {
					return;
				}
				const auto direction = position(*spot, time) - position(*worker, time);
				if (direction.length() > 0) {
					worker->try_get_component_as<component::Position>(component::component_t::POSITION)->set_angle(time, direction.to_angle());
				}
				worker->play_animation(attack->get_ability(), time);
				const auto damage = live->get_resistance() ? attack->get_damage_against(*live->get_resistance()) : attack->get_damage();
				loop.create_event("game.apply_discrete_effect", target, state, time, {
					{"target_id", spot_id}, {"amount", damage},
					{"attribute_fqon", attack->get_health_attribute_fqon()},
				});
				schedule(Phase::Seek, time::time_t::from_double(std::max(0.1, attack->get_cooldown())));
				break;
			}
			const auto rate = ability->get_object("Gather.gather_rate").get_float("ResourceRate.rate");
			const auto capacity = ability->get_object("Gather.container").get_int("ResourceContainer.max_amount");
			if (rate > 0 and capacity > 0 and spot->resource_spot->remaining > 0) {
				const auto amount = std::min(capacity, spot->resource_spot->remaining);
				worker->play_animation(*ability, time);
				loop.create_event("game.gather", target, state, time + time::time_t::from_double(amount / rate), {
					{"worker_id", worker_id}, {"resource_id", spot_id}, {"phase", Phase::Return},
					{"order", gather->order}, {"harvested", amount},
				});
			}
		}
		break;
	case Phase::Return:
		if (const auto earned = params.get("harvested", int64_t{}); ability and earned > 0) {
			auto &resource = *spot->resource_spot;
			gather->carried_amount = std::min(earned, resource.remaining);
			gather->carried_resource = resource.resource;
			gather->carried_container = ability->get_object("Gather.container").get_name();
			resource.remaining -= gather->carried_amount;
			if (resource.remaining == 0 and spot->is_dead()) {
				loop.create_event("game.removal", target, state, time, {{"target_id", spot_id}});
			}
		}
		if (gather->carried_amount > 0) {
			std::vector<std::shared_ptr<GameEntity>> depots;
			for (const auto &[id, site] : game->get_game_entities()) {
				if (owned_depot(site)) {
					depots.push_back(site);
				}
			}
			const auto from = position(*worker, time);
			std::ranges::sort(depots, [&](const auto &a, const auto &b) {
				return std::pair{(position(*a, time) - from).length(), a->get_id()}
				     < std::pair{(position(*b, time) - from).length(), b->get_id()};
			});
			for (const auto &depot : depots) {
				if (travel(*depot, Phase::Deposit)) {
					break;
				}
			}
		}
		break;
	case Phase::Deposit: {
		const auto depot_id = params.get("depot", entity_id_t{});
		const auto depot = game->has_game_entity(depot_id) ? game->get_game_entity(depot_id) : nullptr;
		if (not depot or not owned_depot(depot)) {
			schedule(Phase::Return, time::time_t::zero());
			break;
		}
		if (gather->carried_amount > 0 and nearby(position(*worker, time), *depot, position(*depot, time))) {
			game->get_player(owner)->add_resource(gather->carried_resource, gather->carried_amount);
			gather->carried_amount = 0;
			this->delivery(*game, owner, time);
			schedule(Phase::Seek, time::time_t::zero());
		}
		break;
	}
	case Phase::Start:
		break;
	}
}

void GatherHandler::setup_event(const std::shared_ptr<openage::event::Event> &,
                                const std::shared_ptr<openage::event::State> &) {}

time::time_t GatherHandler::predict_invoke_time(
	const std::shared_ptr<openage::event::EventEntity> &,
	const std::shared_ptr<openage::event::State> &,
	const time::time_t &at) {
	return at;
}

} // namespace openage::gamestate::event
