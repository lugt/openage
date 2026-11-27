// Copyright 2026 the openage authors. See copying.md for legal info.

#include "economy_action.h"

#include <algorithm>
#include <cmath>
#include <ranges>
#include <set>
#include <utility>

#include "coord/tile.h"
#include "event/event_loop.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/build.h"
#include "gamestate/component/api/gather.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/internal/activity.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/entity_factory.h"
#include "gamestate/event/send_command.h"
#include "gamestate/api/util.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/manager.h"
#include "gamestate/map.h"
#include "gamestate/player.h"
#include "gamestate/system/idle.h"
#include "gamestate/system/move.h"
#include "scenario/swept_aabb.h"

import openage.gamestate.score;

namespace openage::gamestate::event {

namespace {

using component::component_t;
using component::FootprintBounds;
using scenario::CellPosition;
const nyan::fqon_t health = "de2_base.data.util.attribute.types.Health";

std::optional<player_id_t> owner_of(GameEntity &entity, const time::time_t &at) {
	const auto owner = entity.try_get_component_as<component::Ownership>(component_t::OWNERSHIP);
	return owner ? std::make_optional(owner->get_owners().get(at)) : std::nullopt;
}

coord::phys3 position_of(GameEntity &entity, const time::time_t &at) {
	return entity.try_get_component_as<component::Position>(component_t::POSITION)->get_positions().get(at);
}

bool finished(GameEntity &entity) {
	auto build = entity.try_get_component_as<component::Build>(component_t::BUILD);
	return not build or not build->is_under_construction();
}

std::vector<nyan::Object> objects(const nyan::Object &object, const char *field) {
	std::vector<nyan::Object> result;
	for (const auto &entry : object.get_set(field)) {
		auto reference = std::dynamic_pointer_cast<nyan::ObjectValue>(entry.get_ptr());
		result.push_back(object.get_view()->get_object(reference->get_name()));
	}
	return result;
}

std::optional<bool> requirement(const nyan::Object &node, const GameState &game,
                                const Player &player, const time::time_t &at,
                                std::set<nyan::fqon_t> &ancestry) {
	if (node.extends("engine.util.logic.literal.type.TechResearched")) {
		return player.has_researched(node.get_object("TechResearched.tech").get_name());
	}
	if (node.extends("engine.util.logic.literal.type.GameEntityProgress")) {
		const auto subject = node.get_object("GameEntityProgress.game_entity").get_name();
		const auto status = node.get_object("GameEntityProgress.progress_status");
		if (not status.get_object("ProgressStatus.progress_type").extends("engine.util.progress_type.type.Construct")) {
			return std::nullopt;
		}
		const auto needed = status.get_float("ProgressStatus.progress");
		return std::ranges::any_of(game.get_game_entities(), [&](const auto &entry) {
			GameEntity &entity = *entry.second;
			if (entity.is_dead() or entity.definition != subject or owner_of(entity, at) != player.get_id()) {
				return false;
			}
			const auto build = entity.try_get_component_as<component::Build>(component_t::BUILD);
			return build and build->get_progress() >= needed;
		});
	}
	if (not node.extends("engine.util.logic.gate.LogicGate") or not ancestry.insert(node.get_name()).second) {
		return std::nullopt;
	}
	const auto inputs = objects(node, "LogicGate.inputs");
	int64_t satisfied = 0;
	int64_t unknown = 0;
	for (const auto &input : inputs) {
		const auto value = requirement(input, game, player, at, ancestry);
		if (not value.has_value()) {
			++unknown;
		}
		else if (*value) {
			++satisfied;
		}
	}
	ancestry.erase(node.get_name());
	const auto total = static_cast<int64_t>(inputs.size());
	auto at_least = [&](int64_t minimum) -> std::optional<bool> {
		if (satisfied >= minimum) {
			return true;
		}
		if (satisfied + unknown < minimum) {
			return false;
		}
		return std::nullopt;
	};
	if (node.extends("engine.util.logic.gate.type.AND")) {
		return at_least(total);
	}
	if (node.extends("engine.util.logic.gate.type.OR")) {
		return at_least(1);
	}
	if (node.extends("engine.util.logic.gate.type.SUBSETMIN")) {
		return at_least(node.get_int("SUBSETMIN.size"));
	}
	if (node.extends("engine.util.logic.gate.type.NOT") and unknown == 0) {
		return satisfied == 0;
	}
	return std::nullopt;
}

bool can_start(const ActionOption &option, const GameState &game, const Player &player, const time::time_t &at) {
	if (option.kind == ActionKind::Transform) {
		return true;
	}
	const auto record = player.get_db_view()->get_object(option.id);
	const auto member = option.kind == ActionKind::Research ? "ResearchableTech.condition" : "CreatableGameEntity.condition";
	std::set<nyan::fqon_t> ancestry;
	return std::ranges::all_of(objects(record, member), [&](const auto &node) {
		return requirement(node, game, player, at, ancestry).value_or(false);
	});
}

FootprintBounds bounds_at(const GameEntity &entity, const coord::phys3 &pos) {
	auto footprint = entity.has_footprint() ? entity.footprint() : component::Footprint{};
	footprint.set_center(pos.ne.to_double(), pos.se.to_double());
	return footprint.bounds();
}

bool can_place(const Map &map, const GameEntity &entity, const coord::phys3 &pos) {
	auto nav = map.get_navigation_service();
	const auto b = bounds_at(entity, pos);
	const auto &size = map.get_size();
	if (not nav or b.min_x < 0 or b.min_y < 0 or b.max_x > size[0] or b.max_y > size[1]
	    or not map.get_dynamic_occupancy()->can_occupy(entity.get_id(), b)) {
		return false;
	}
	for (int y = std::floor(b.min_y); y < std::max(std::ceil(b.max_y), std::floor(b.min_y) + 1); ++y) {
		for (int x = std::floor(b.min_x); x < std::max(std::ceil(b.max_x), std::floor(b.min_x) + 1); ++x) {
			if (not nav->grid()->is_walkable({x, y})) {
				return false;
			}
		}
	}
	return true;
}

std::vector<coord::phys3> neighbours(const GameEntity &site, const coord::phys3 &center) {
	const auto bounds = bounds_at(site, center);
	std::vector<coord::phys3> result;
	for (int y = std::floor(bounds.min_y - 1); y <= std::floor(bounds.max_y + 1); ++y) {
		for (int x = std::floor(bounds.min_x - 1); x <= std::floor(bounds.max_x + 1); ++x) {
			result.push_back(coord::tile{x, y}.to_phys3_center());
		}
	}
	return result;
}

std::vector<coord::phys3> build_route(const Map &map, const GameEntity &worker, const coord::phys3 &from,
                                     const GameEntity &building, const coord::phys3 &center) {
	auto nav = map.get_navigation_service();
	if (not nav) {
		return {};
	}
	const auto building_bounds = bounds_at(building, center);
	const auto tile = from.to_tile();
	const CellPosition start{static_cast<int>(tile.ne), static_cast<int>(tile.se)};
	const auto fp = worker.footprint();
	const auto occupancy = map.get_dynamic_occupancy();
	auto traversable = [&](const coord::phys3 &a, const coord::phys3 &b) {
		auto swept = ::openage::scenario::swept_aabb(bounds_at(worker, a), bounds_at(worker, b));
		return not building_bounds.overlaps(swept) and occupancy->can_occupy(worker.get_id(), swept);
	};

	std::vector<coord::phys3> best;
	for (const auto &candidate : neighbours(building, center)) {
		const auto x = candidate.ne.to_double();
		const auto y = candidate.se.to_double();
		if (x < building_bounds.min_x - 1.05 or x > building_bounds.max_x + 1.05
		    or y < building_bounds.min_y - 1.05 or y > building_bounds.max_y + 1.05
		    or not can_place(map, worker, candidate) or building_bounds.overlaps(bounds_at(worker, candidate))) {
			continue;
		}
		const auto goal = candidate.to_tile();
		auto path = nav->request_path(start, {static_cast<int>(goal.ne), static_cast<int>(goal.se)},
		                              {fp.radius_x(), fp.radius_y()}, [&](CellPosition a, CellPosition b) {
			return traversable(a == start ? from : coord::tile{a.x, a.y}.to_phys3_center(),
			                   coord::tile{b.x, b.y}.to_phys3_center());
		});
		if (path.waypoints.empty() or (path.waypoints.size() == 1 and not traversable(from, candidate))
		    or (not best.empty() and path.waypoints.size() >= best.size())) {
			continue;
		}
		best = {from};
		for (const auto &cell : path.waypoints | std::views::drop(1)) {
			best.push_back(coord::tile{cell.x, cell.y}.to_phys3_center());
		}
		if (best.size() == 1 and from != candidate) {
			best.push_back(candidate);
		}
	}
	return best;
}

double build_seconds(const GameEntity &worker, const nyan::Object &building) {
	if (not worker.construct_ability) {
		return 0;
	}
	for (const auto &ability : objects(building, "GameEntity.abilities")) {
		if (not ability.extends("engine.ability.type.Resistance")) {
			continue;
		}
		for (const auto &resistance : objects(ability, "Resistance.resistances")) {
			if (not resistance.get_name().ends_with(".ConstructProgress")) {
				continue;
			}
			const auto type = resistance.get_object("TimeRelativeProgressChange.type").get_name();
			for (const auto &effect : objects(*worker.construct_ability, "ApplyContinuousEffect.effects")) {
				if (effect.get_name().ends_with("ConstructProgressEffect")
				    and effect.get_object("TimeRelativeProgressChange.type").get_name() == type) {
					return effect.get_float("TimeRelativeProgressChange.total_change_time");
				}
			}
		}
	}
	return 0;
}

struct RepairSpec {
	double rate;
	std::map<nyan::fqon_t, double> cost;
};

std::optional<RepairSpec> repair_spec(const GameEntity &worker, component::Live &live) {
	if (not worker.repair_ability or not live.get_resistance()) {
		return std::nullopt;
	}
	for (const auto &resistance : objects(*live.get_resistance(), "Resistance.resistances")) {
		if (not resistance.extends("engine.resistance.continuous.flat_attribute_change.type.FlatAttributeChangeIncrease")) {
			continue;
		}
		for (const auto &effect : objects(*worker.repair_ability, "ApplyContinuousEffect.effects")) {
			if (effect.get_object("FlatAttributeChange.type").get_name() != resistance.get_object("FlatAttributeChange.type").get_name()) {
				continue;
			}
			RepairSpec result{effect.get_object("FlatAttributeChange.change_rate").get_float("AttributeRate.rate")
			                    - resistance.get_object("FlatAttributeChange.block_rate").get_float("AttributeRate.rate"), {}};
			for (const auto &[type, value] : effect.get_dict("Effect.properties")) {
				const auto ref = std::dynamic_pointer_cast<nyan::ObjectValue>(value.get_ptr());
				const auto property = effect.get_view()->get_object(ref->get_name());
				if (property.extends("engine.effect.property.type.Cost")) {
					for (const auto &amount : objects(property.get_object("Cost.cost"), "ResourceCost.amount")) {
						result.cost[amount.get_object("ResourceAmount.type").get_name()] = amount.get_float("ResourceAmount.amount");
					}
				}
			}
			return result;
		}
	}
	return std::nullopt;
}

void place(GameState &game, const std::shared_ptr<GameEntity> &entity, player_id_t owner,
           const coord::phys3 &position, const time::time_t &at) {
	auto component = entity->try_get_component_as<component::Position>(component_t::POSITION);
	component->set_position(at, position);
	entity->try_get_component_as<component::Ownership>(component_t::OWNERSHIP)->set_owner(at, owner);
	game.add_game_entity(entity);
	register_entity_in_dynamic_occupancy(*game.get_map(), *entity,
	                                     position.ne.to_double(), position.se.to_double());
	if (auto activity = entity->try_get_component_as<component::Activity>(component_t::ACTIVITY)) {
		activity->init(at);
		entity->get_manager()->run_activity_system(at);
	}
}

void begin_build(openage::event::EventLoop &loop,
                 const std::shared_ptr<openage::event::EventEntity> &target,
                 const std::shared_ptr<GameState> &game, const time::time_t &at,
                 const std::shared_ptr<GameEntity> &worker, GameEntity &site,
                 component::Build &build, double seconds, std::vector<coord::phys3> route) {
	++worker->repair_order;
	if (auto gather = worker->try_get_component_as<component::Gather>(component_t::GATHER)) {
		++gather->order;
	}
	if (auto attack = worker->try_get_component_as<component::Attack>(component_t::ATTACK)) {
		attack->stop_attack();
	}
	if (worker->building_site != 0) {
		loop.create_event("game.construction", target, game, at, {
			{"entity_id", worker->building_site}, {"worker", worker->get_id()}, {"depart", true},
		});
	}
	const auto arrives = at + system::Move::move_along_path(worker, game, std::move(route), at);
	const auto order = ++worker->construction_order;
	worker->building_site = site.get_id();
	if (build.build_seconds <= 0.0) {
		build.build_seconds = seconds;
	}
	build.assignments[worker->get_id()] = order;
	loop.create_event("game.construction", target, game, arrives, {
		{"entity_id", site.get_id()}, {"worker", worker->get_id()}, {"order", order}, {"arrive", true},
	});
}

bool apply_research(GameState &game, Player &player, const ActionOption &option, const time::time_t &at) {
	const auto view = player.get_db_view();
	const auto tech = view->get_object(option.subject);
	const auto updates = tech.get_orderedset("Tech.updates");
	std::vector<std::pair<std::shared_ptr<component::Live>, int64_t>> affected;
	for (const auto &[id, entity] : game.get_game_entities()) {
		if (not entity->is_dead() and owner_of(*entity, at) == player.get_id()) {
			auto live = entity->try_get_component_as<component::Live>(component_t::LIVE);
			if (live) {
				affected.emplace_back(live, live->get_attribute_max(health).value_or(0));
			}
		}
	}
	if (updates.begin() != updates.end()) {
		auto transaction = view->new_transaction(at.get_raw_value());
		for (const auto &entry : updates) {
			auto wrapper = view->get_object(std::dynamic_pointer_cast<nyan::ObjectValue>(entry.get_ptr())->get_name());
			const auto patch = wrapper.get_object("Patch.patch");
			if (patch.get_target() == nullptr) {
				return false;
			}
			transaction.add(patch);
		}
		if (not transaction.commit()) {
			return false;
		}
	}
	player.record_research(tech.get_name());
	for (const auto &[live, old_max] : affected) {
		const auto new_max = live->get_attribute_max(health).value_or(0);
		const auto hp = live->get_attribute_value(at, health);
		if (hp and new_max > 0 and new_max != old_max) {
			live->set_attribute(at, health, std::clamp(*hp + new_max - old_max, int64_t{1}, new_max));
		}
	}
	return true;
}

} // namespace

bool action_available(const ActionOption &option, const GameState &game, const Player &player, const time::time_t &at) {
	return player.allows_action(option.subject)
	    and (option.kind != ActionKind::Research or not player.has_researched(option.subject))
	    and can_start(option, game, player, at);
}

int64_t EconomyActionHandler::queued_population_locked(player_id_t player) const {
	int64_t result = 0;
	for (const auto &[id, queue] : this->jobs) {
		for (const auto &job : queue) {
			if (job.owner == player) {
				result += job.population;
			}
		}
	}
	return result;
}

int64_t EconomyActionHandler::queued_population(player_id_t player) const {
	std::lock_guard lock{this->jobs_mutex};
	return this->queued_population_locked(player);
}

bool EconomyActionHandler::busy(entity_id_t actor) const {
	std::lock_guard lock{this->jobs_mutex};
	const auto found = this->jobs.find(actor);
	return found != this->jobs.end() and not found->second.empty();
}

std::vector<EconomyActionHandler::QueueEntry> EconomyActionHandler::queue_snapshot(
	entity_id_t actor, const time::time_t &at) const {
	std::lock_guard lock{this->jobs_mutex};
	std::vector<QueueEntry> result;
	const auto found = this->jobs.find(actor);
	if (found == this->jobs.end()) {
		return result;
	}
	result.reserve(found->second.size());
	for (size_t i = 0; i < found->second.size(); ++i) {
		const auto &job = found->second[i];
		double progress = 0.0;
		if (i == 0) {
			if (job.duration <= 0.0) {
				progress = 1.0;
			}
			else {
				progress = std::clamp((at - job.started_at).to_double() / job.duration, 0.0, 1.0);
			}
		}
		result.push_back(QueueEntry{job.option, i == 0, progress});
	}
	return result;
}

bool EconomyActionHandler::is_research_queued(player_id_t player, const nyan::fqon_t &subject) const {
	std::lock_guard lock{this->jobs_mutex};
	return std::ranges::any_of(this->jobs, [&](const auto &entry) {
		return std::ranges::any_of(entry.second, [&](const auto &job) {
			return job.owner == player and job.option.kind == ActionKind::Research and job.option.subject == subject;
		});
	});
}

bool force_scenario_research(GameState &game, player_id_t id, const nyan::fqon_t &technology, const time::time_t &at) {
    auto player = game.get_player(id);
    if (player->has_researched(technology)) return true;
    return apply_research(game, *player, ActionOption{ActionKind::Research, technology, technology, {}, 0}, at);
}

EconomyActionHandler::EconomyActionHandler(std::shared_ptr<EntityFactory> factory) :
	OnceEventHandler{"game.economy_action"}, factory{std::move(factory)} {}

void EconomyActionHandler::invoke(openage::event::EventLoop &loop,
                                   const std::shared_ptr<openage::event::EventEntity> &target,
                                   const std::shared_ptr<openage::event::State> &state,
                                   const time::time_t &at, const param_map &params) {
	auto game = std::static_pointer_cast<GameState>(state);
	const auto id = params.get("actor", entity_id_t{});
	const auto actor = game->has_game_entity(id) ? game->get_game_entity(id) : nullptr;
	if (params.get("finish", false)) {
		std::optional<Job> finished_job;
		{
			std::lock_guard lock{this->jobs_mutex};
			auto it = this->jobs.find(id);
			if (it == this->jobs.end() or it->second.empty()) {
				return;
			}
			if (params.check_type<uint64_t>("token")
			    and it->second.front().token != params.get("token", uint64_t{0})) {
				return;
			}
			finished_job = it->second.front();
			it->second.pop_front();
		}
		const auto owner = finished_job->owner;
		const auto &option = finished_job->option;
		auto player = game->get_player(owner);
		bool success = false;
		if (actor and not actor->is_dead() and owner_of(*actor, at) == owner) {
			if (option.kind == ActionKind::Transform) {
				actor->unpacked = not actor->unpacked;
				success = true;
			}
			else if (option.kind == ActionKind::Research) {
				success = apply_research(*game, *player, option, at);
				if (success) {
					for (const auto &[resource, amount] : option.cost) {
						if (score::is_scored_resource(resource)) {
							player->score_ledger.research_value += amount;
						}
					}
				}
			}
			else {
				const auto map = game->get_map();
				auto trained = this->factory->add_game_entity(game->get_event_loop(), game, owner, option.subject, false);
				game->update_player_economies(at);
				const auto economy = game->player_economy(owner);
				if (economy.population + trained->population_used <= economy.population_capacity) {
					const auto center = position_of(*actor, at);
					auto places = neighbours(*actor, center);
					std::ranges::stable_sort(places, {}, [&](const auto &pos) { return (pos - center).length(); });
					for (const auto &pos : places) {
						if (not bounds_at(*actor, center).overlaps(bounds_at(*trained, pos))
						    and can_place(*map, *trained, pos)) {
							this->factory->attach_entity_renderer(trained);
							place(*game, trained, owner, pos, at);
							success = true;
							break;
						}
					}
				}
			}
		}
		if (not success) {
			for (const auto &[resource, amount] : option.cost) {
				player->add_resource(resource, amount);
			}
		}
		std::optional<std::pair<double, uint64_t>> next_event;
		std::deque<Job> refund_queue;
		{
			std::lock_guard lock{this->jobs_mutex};
			auto it = this->jobs.find(id);
			if (it != this->jobs.end()) {
				if (actor and not actor->is_dead() and not it->second.empty()
				    and owner_of(*actor, at) == it->second.front().owner) {
					auto &next = it->second.front();
					const double delay = game->fast_actions() ? 0.05 : next.option.seconds;
					next.started_at = at;
					next.duration = delay;
					next_event = {delay, next.token};
				}
				else {
					refund_queue = std::move(it->second);
					this->jobs.erase(it);
				}
			}
		}
		if (next_event) {
			loop.create_event("game.economy_action", target, state,
			                  at + time::time_t::from_double(next_event->first),
			                  {{"actor", id}, {"finish", true}, {"token", next_event->second}});
		}
		else {
			for (const auto &remaining : refund_queue) {
				auto remaining_player = game->get_player(remaining.owner);
				for (const auto &[resource, amount] : remaining.option.cost) {
					remaining_player->add_resource(resource, amount);
				}
			}
		}
		game->set_action_feedback(owner,
		                          success ? "Completed" : "Could not complete the order. Resources returned.",
		                          not success);
		return;
	}
	const auto owner = params.get("issuer", player_id_t{});
	if (not actor or actor->is_dead() or owner_of(*actor, at) != owner or not finished(*actor)) {
		return;
	}
	auto player = game->get_player(owner);
	auto feedback = [&](std::string message) { game->set_action_feedback(owner, std::move(message), false); };
	auto error_feedback = [&](std::string message) { game->set_action_feedback(owner, std::move(message), true); };
	if (params.check_type<entity_id_t>("repair_target")) {
		const auto target_id = params.get("repair_target", entity_id_t{});
		const bool followup = params.check_type<uint64_t>("repair_order");
		if (followup and params.get("repair_order", uint64_t{}) != actor->repair_order) {
			return;
		}
		const auto movement = actor->try_get_component_as<component::Move>(component_t::MOVE);
		if (followup and (not movement or movement->generation != params.get("move_generation", uint64_t{}))) {
			return;
		}
		if (not game->has_game_entity(target_id)) {
			if (followup) {
				system::Idle::idle(actor, at);
			}
			return;
		}
		auto site = game->get_game_entity(target_id);
		auto live = site->try_get_component_as<component::Live>(component_t::LIVE);
		const auto spec = live ? repair_spec(*actor, *live) : std::nullopt;
		if (site->is_dead() or owner_of(*site, at) != owner or not finished(*site)
		    or not movement or not site->has_component(component_t::BUILD) or not spec or spec->rate <= 0) {
			if (followup) {
				system::Idle::idle(actor, at);
			}
			error_feedback("Select a damaged, completed building you own.");
			return;
		}
		const auto hp = live->get_attribute_value(at, health).value();
		const auto max_hp = live->get_attribute_max(health).value();
		if (hp >= max_hp) {
			system::Idle::idle(actor, at);
			feedback("Repair complete.");
			return;
		}
		if (not followup) {
			auto route = build_route(*game->get_map(), *actor, position_of(*actor, at), *site, position_of(*site, at));
			if (route.empty()) {
				error_feedback("The villager cannot reach this building.");
				return;
			}
			SendCommandHandler stop;
			stop.invoke(loop, target, state, at, {{"type", component::command::command_t::IDLE},
			                                   {"entity_ids", std::vector<entity_id_t>{id}}, {"issuer", owner}});
			const auto arrives = at + system::Move::move_along_path(actor, game, std::move(route), at);
			loop.create_event("game.economy_action", target, state, arrives, {
				{"actor", id}, {"issuer", owner}, {"repair_target", target_id}, {"repair_order", actor->repair_order},
				{"move_generation", movement->generation},
			});
			feedback("Repairing building.");
			return;
		}
		auto repaired = params.get("repaired", int64_t{});
		const auto increment = std::min(params.get("repair_hp", int64_t{}), max_hp - hp);
		if (increment > 0) {
			std::map<nyan::fqon_t, int64_t> payment;
			for (const auto &[resource, full_cost] : spec->cost) {
				payment[resource] = static_cast<int64_t>(std::ceil((repaired + increment) * full_cost / max_hp)
				                                         - std::ceil(repaired * full_cost / max_hp));
			}
			if (not std::ranges::all_of(payment, [&](const auto &cost) { return player->has_enough(cost.first, cost.second); })) {
				system::Idle::idle(actor, at);
				error_feedback("Not enough resources to continue repairing.");
				return;
			}
			for (const auto &[resource, amount] : payment) {
				if (amount > 0) {
					player->spend(resource, amount);
				}
			}
			live->set_attribute(at, health, hp + increment);
			repaired += increment;
		}
		if (hp + increment >= max_hp) {
			system::Idle::idle(actor, at);
			feedback("Repair complete.");
			return;
		}
		actor->play_animation(*actor->repair_ability, at);
		const auto next_hp = std::min<int64_t>(max_hp - hp - increment, static_cast<int64_t>(std::ceil(spec->rate)));
		loop.create_event("game.economy_action", target, state, at + time::time_t::from_double(next_hp / spec->rate), {
			{"actor", id}, {"issuer", owner}, {"repair_target", target_id}, {"repair_order", actor->repair_order},
			{"move_generation", movement->generation},
			{"repaired", repaired}, {"repair_hp", next_hp},
		});
		return;
	}
	if (params.check_type<int64_t>("cancel_index")) {
		const auto idx = params.get("cancel_index", int64_t{-1});
		std::optional<Job> cancelled;
		std::optional<std::pair<double, uint64_t>> next_event;
		{
			std::lock_guard lock{this->jobs_mutex};
			auto it = this->jobs.find(id);
			if (it == this->jobs.end() or idx < 0 or static_cast<size_t>(idx) >= it->second.size()) {
				return;
			}
			if (it->second[static_cast<size_t>(idx)].owner != owner) {
				return;
			}
			cancelled = it->second[static_cast<size_t>(idx)];
			it->second.erase(it->second.begin() + idx);
			if (it->second.empty()) {
				this->jobs.erase(it);
			}
			else if (idx == 0) {
				auto &next = it->second.front();
				const double delay = game->fast_actions() ? 0.05 : next.option.seconds;
				next.started_at = at;
				next.duration = delay;
				next_event = {delay, next.token};
			}
		}
		for (const auto &[resource, amount] : cancelled->option.cost) {
			player->add_resource(resource, amount);
		}
		if (next_event) {
			loop.create_event("game.economy_action", target, state,
			                  at + time::time_t::from_double(next_event->first),
			                  {{"actor", id}, {"finish", true}, {"token", next_event->second}});
		}
		feedback("Order cancelled.");
		return;
	}
	if (params.check_type<entity_id_t>("site")) {
		const auto site_id = params.get("site", entity_id_t{});
		if (not actor->construct_ability or not game->has_game_entity(site_id)) {
			return;
		}
		auto site = game->get_game_entity(site_id);
		auto build = site->try_get_component_as<component::Build>(component_t::BUILD);
		if (site->is_dead() or owner_of(*site, at) != owner or not build
		    or not build->ordered_by_player or not build->is_under_construction()) {
			return;
		}
		if (const auto assigned = build->assignments.find(id);
		    assigned != build->assignments.end() and assigned->second == actor->construction_order) {
			feedback("This villager is already constructing this building.");
			return;
		}
		const auto seconds = build_seconds(*actor, player->get_db_view()->get_object(site->definition));
		auto route = build_route(*game->get_map(), *actor, position_of(*actor, at), *site, position_of(*site, at));
		if (std::isfinite(seconds) and seconds > 0 and not route.empty()) {
			begin_build(loop, target, game, at, actor, *site, *build, seconds, std::move(route));
			feedback("Construction resumed.");
		}
		else {
			error_feedback("The villager cannot reach this building site.");
		}
		return;
	}

	const auto requested = params.get("option", nyan::fqon_t{});
	auto option = std::ranges::find(actor->action_options, requested, &ActionOption::id);
	if (option == actor->action_options.end() or not player->allows_action(option->subject)) {
		return;
	}
	if (option->kind == ActionKind::Research
	    and (player->has_researched(option->subject) or this->is_research_queued(owner, option->subject))) {
		error_feedback("This technology is already researched or in progress.");
		return;
	}
	if (not can_start(*option, *game, *player, at)) {
		error_feedback("Prerequisites are missing.");
		return;
	}
	if (not std::ranges::all_of(option->cost, [&](const auto &amount) {
		    return player->has_enough(amount.first, amount.second);
	    })) {
		error_feedback("Not enough resources.");
		return;
	}
	if (option->kind == ActionKind::Build) {
		if (not actor->construct_ability or not params.check_type<coord::phys3>("position")) {
			return;
		}
		const auto center = params.get("position", coord::phys3{0, 0, 0}).to_tile().to_phys3();
		const auto definition = player->get_db_view()->get_object(option->subject);
		const auto seconds = build_seconds(*actor, definition);
		if (not std::isfinite(seconds) or seconds <= 0) {
			error_feedback("This building's construction is unavailable.");
			return;
		}
		auto building = this->factory->add_game_entity(game->get_event_loop(), game, owner, option->subject, false);
		auto build = building->try_get_component_as<component::Build>(component_t::BUILD);
		const auto visibility = game->visibility(owner);
		const auto tile = center.to_tile();
		if (not build or not can_place(*game->get_map(), *building, center)
		    or (visibility and visibility->at(tile.ne, tile.se) == TileVisibility::Unexplored)) {
			error_feedback("Cannot build at this location.");
			return;
		}
		const auto from = position_of(*actor, at);
		auto route = build_route(*game->get_map(), *actor, from, *building, center);
		if (route.empty()) {
			error_feedback("The villager cannot reach this building site.");
			return;
		}
		for (const auto &[resource, amount] : option->cost) {
			player->spend(resource, amount);
		}
		build->set_progress(0);
		build->ordered_by_player = true;
		if (auto live = building->try_get_component_as<component::Live>(component_t::LIVE)) {
			live->set_attribute(at, health, 1);
		}
		this->factory->attach_entity_renderer(building);
		place(*game, building, owner, center, at);
		begin_build(loop, target, game, at, actor, *building, *build, seconds, std::move(route));
		const auto selected_helpers = params.get("builders", std::vector<entity_id_t>{});
		const std::set<entity_id_t> helpers{selected_helpers.begin(), selected_helpers.end()};
		for (auto helper_id : helpers) {
			if (helper_id == id or not game->has_game_entity(helper_id)) {
				continue;
			}
			const auto helper = game->get_game_entity(helper_id);
			if (helper->is_dead() or not helper->construct_ability or owner_of(*helper, at) != owner) {
				continue;
			}
			const auto helper_seconds = build_seconds(*helper, definition);
			auto helper_route = build_route(*game->get_map(), *helper, position_of(*helper, at), *building, center);
			if (std::isfinite(helper_seconds) and helper_seconds > 0 and not helper_route.empty()) {
				begin_build(loop, target, game, at, helper, *building, *build, helper_seconds, std::move(helper_route));
			}
		}
		feedback("Construction ordered.");
		return;
	}

	if (not std::isfinite(option->seconds) or option->seconds < 0) {
		return;
	}
	constexpr size_t max_queue_size = 15;
	if (option->kind == ActionKind::Transform) {
		if (auto attack = actor->try_get_component_as<component::Attack>(component_t::ATTACK)) {
			attack->stop_attack();
		}
		system::Move::halt(actor, game, at);
		bool start_now = false;
		uint64_t token = 0;
		{
			std::lock_guard lock{this->jobs_mutex};
			auto &queue = this->jobs[id];
			if (queue.size() >= max_queue_size) {
				error_feedback("Training queue is full.");
				return;
			}
			start_now = queue.empty();
			token = this->next_token++;
			queue.push_back(Job{owner, *option, 0, at, option->seconds, token});
		}
		if (start_now) {
			loop.create_event("game.economy_action", target, state, at + time::time_t::from_double(option->seconds),
			                  {{"actor", id}, {"finish", true}, {"token", token}});
			feedback(actor->unpacked ? "Packing Trebuchet." : "Unpacking Trebuchet.");
		}
		return;
	}
	int64_t population = 0;
	if (option->kind == ActionKind::Train) {
		population = api::population_cost(player->get_db_view(), option->subject);
		game->update_player_economies(at);
	}
	const double delay = game->fast_actions() ? 0.05 : option->seconds;
	bool start_now = false;
	size_t queue_size = 0;
	uint64_t token = 0;
	{
		std::lock_guard lock{this->jobs_mutex};
		auto &queue = this->jobs[id];
		if (queue.size() >= max_queue_size) {
			error_feedback("Training queue is full.");
			return;
		}
		if (option->kind == ActionKind::Train) {
			const auto economy = game->player_economy(owner);
			if (economy.population + this->queued_population_locked(owner) + population > economy.population_capacity) {
				if (queue.empty()) {
					this->jobs.erase(id);
				}
				error_feedback("Population capacity is full.");
				return;
			}
		}
		for (const auto &[resource, amount] : option->cost) {
			player->spend(resource, amount);
		}
		start_now = queue.empty();
		token = this->next_token++;
		queue.push_back(Job{owner, *option, population, start_now ? at : time::time_t{0}, start_now ? delay : 0.0, token});
		queue_size = queue.size();
	}
	if (start_now) {
		loop.create_event("game.economy_action", target, state, at + time::time_t::from_double(delay),
		                  {{"actor", id}, {"finish", true}, {"token", token}});
		feedback(option->kind == ActionKind::Research ? "Research in progress." : "Training in progress.");
	}
	else {
		feedback("Queued (" + std::to_string(queue_size) + " in queue).");
	}
}

void EconomyActionHandler::setup_event(const std::shared_ptr<openage::event::Event> &,
                                       const std::shared_ptr<openage::event::State> &) {}

time::time_t EconomyActionHandler::predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> &,
                                                        const std::shared_ptr<openage::event::State> &,
                                                        const time::time_t &at) {
	return at;
}

} // namespace openage::gamestate::event
