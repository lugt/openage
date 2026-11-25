// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <algorithm>
#include <array>
#include <chrono>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "coord/tile.h"
#include "error/error.h"
#include "event/event_loop.h"
#include "gamestate/api/util.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/build.h"
#include "gamestate/component/api/gather.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/event/economy_action.h"
#include "gamestate/event/send_command.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "gamestate/player.h"
#include "log/log.h"
#include "pathfinding/grid_astar.h"
#include "scenario/static_occupancy_grid.h"
#include "testing/testing.h"
#include "util/json_minimal.h"

export module openage.gamestate.ai;

import nyan;

export namespace openage::gamestate::ai {

/** Resolved deterministic computer-player profile; limits apply across all modules. */
struct Profile {
	player_id_t player = 1;
	int64_t actions_per_minute = 48;
	int64_t actions_per_second = 2;
	int64_t reaction_ms = 1000;
	uint64_t seed = 1;
	bool economy = true;
	bool construction = true;
	bool research = true;
	bool scouting = true;
	bool defense = true;
	bool attacks = false;
	bool retreat = true;
	int64_t workers = 20;
	int64_t army = 12;
	int64_t max_orders = 10000;
	std::set<entity_id_t> protected_units;
};

/** Sliding windows and spacing prevent both sustained overload and saved-token bursts. */
class ActionBudget {
public:
	ActionBudget(int64_t per_minute, int64_t per_second) : minute{per_minute}, second{per_second} {
		if (per_minute < 1 or per_second < 1) {
			throw Error{MSG(err) << "AI action budgets must be positive"};
		}
	}
	bool consume(int64_t now) {
		while (not this->orders.empty() and this->orders.front() <= now - 60000) {
			this->orders.pop_front();
		}
		const auto recent = std::ranges::count_if(this->orders, [&](auto at) { return at > now - 1000; });
		if (this->orders.size() >= static_cast<size_t>(this->minute) or recent >= this->second
		    or (not this->orders.empty() and now - this->orders.back() < (60000 + this->minute - 1) / this->minute)) {
			return false;
		}
		this->orders.push_back(now);
		return true;
	}

private:
	int64_t minute;
	int64_t second;
	std::deque<int64_t> orders;
};

} // namespace openage::gamestate::ai

namespace openage::gamestate::ai {
namespace {

using component::component_t;
using component::command::command_t;

int64_t integer(const util::json::Value &value, const char *key, int64_t fallback) {
	if (value.has(key) and not value.at(key).is_int()) {
		throw Error{MSG(err) << "AI integer required: " << key};
	}
	return value.has(key) ? value.at(key).as_int() : fallback;
}

bool flag(const util::json::Value &value, const char *key, bool fallback) {
	if (value.has(key) and not value.at(key).is_bool()) {
		throw Error{MSG(err) << "AI boolean required: " << key};
	}
	return value.has(key) ? value.at(key).as_bool() : fallback;
}

Profile profile(const util::json::Value &value) {
	if (not value.is_object()) {
		throw Error{MSG(err) << "AI player profile must be an object"};
	}
	const std::set<std::string> fields{"player", "difficulty", "seed", "enabled", "actions_per_minute", "actions_per_second", "protected_instances", "reaction_ms", "economy", "construction", "research", "scouting", "defense", "attacks", "retreat", "workers", "army", "max_orders"};
	for (const auto &[key, item] : value.as_object()) {
		if (not fields.contains(key)) {
			throw Error{MSG(err) << "Unsupported AI profile field: " << key};
		}
	}
	Profile result;
	const std::string difficulty = value.has("difficulty") ? value.at("difficulty").as_string() : "moderate";
	const std::map<std::string, std::array<int64_t, 3>> presets{
		{"easiest", {12, 1, 2500}},
		{"easy", {24, 1, 1800}},
		{"moderate", {48, 2, 1200}},
		{"hard", {72, 2, 800}},
		{"hardest", {108, 3, 600}},
		{"extreme", {144, 3, 400}},
	};
	const auto found = presets.find(difficulty);
	if (found == presets.end()) {
		throw Error{MSG(err) << "Unknown AI difficulty: " << difficulty};
	}
	result.actions_per_minute = integer(value, "actions_per_minute", found->second[0]);
	result.actions_per_second = integer(value, "actions_per_second", found->second[1]);
	result.reaction_ms = integer(value, "reaction_ms", found->second[2]);
	const auto player = integer(value, "player", 1);
	const auto seed = integer(value, "seed", 1);
	if (player < 0 or seed < 0 or result.actions_per_minute < 1 or result.actions_per_minute > 600
	    or result.actions_per_second < 1 or result.actions_per_second > 9 or result.reaction_ms < 100
	    or result.reaction_ms > 60000) {
		throw Error{MSG(err) << "Invalid AI player, seed or action limits"};
	}
	result.player = player;
	result.seed = seed;
	result.economy = flag(value, "economy", true);
	result.construction = flag(value, "construction", true);
	result.research = flag(value, "research", true);
	result.scouting = flag(value, "scouting", true);
	result.defense = flag(value, "defense", true);
	result.attacks = flag(value, "attacks", false);
	result.retreat = flag(value, "retreat", true);
	result.workers = integer(value, "workers", 20);
	result.army = integer(value, "army", 12);
	result.max_orders = integer(value, "max_orders", 10000);
	if (result.workers < 0 or result.army < 0 or result.max_orders < 1 or result.max_orders > 100000) {
		throw Error{MSG(err) << "Invalid AI population targets or trace bound"};
	}
	return result;
}

player_id_t owner(GameEntity &entity, time::time_t at) {
	const auto ownership = entity.try_get_component_as<component::Ownership>(component_t::OWNERSHIP);
	return ownership ? ownership->get_owners().get(at) : UINT64_MAX;
}

coord::phys3 position(GameEntity &entity, time::time_t at) {
	return entity.try_get_component_as<component::Position>(component_t::POSITION)->get_positions().get(at);
}

int64_t distance(const coord::phys3 &a, const coord::phys3 &b) {
	return std::abs((a.ne - b.ne).get_raw_value()) + std::abs((a.se - b.se).get_raw_value());
}

struct Order {
	std::string kind;
	entity_id_t actor = 0;
	entity_id_t target = 0;
	nyan::fqon_t option{};
	coord::phys3 destination{0, 0, 0};
};

class Controller final : public openage::event::OnceEventHandler {
public:
	Controller(Profile settings, std::shared_ptr<event::EconomyActionHandler> production) :
		OnceEventHandler{"game.ai." + std::to_string(settings.player)}, settings{settings},
		production{std::move(production)}, budget{settings.actions_per_minute, settings.actions_per_second}, random{settings.seed} {}

	void setup_event(const std::shared_ptr<openage::event::Event> &, const std::shared_ptr<openage::event::State> &) override {}
	time::time_t predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> &,
	                                 const std::shared_ptr<openage::event::State> &,
	                                 const time::time_t &at) override {
		return at;
	}

	void invoke(openage::event::EventLoop &loop, const std::shared_ptr<openage::event::EventEntity> &target, const std::shared_ptr<openage::event::State> &state, const time::time_t &at, const param_map &params) override {
		if (params.check_type<std::string>("recorded")) {
			this->replay(params.get("recorded", std::string{}), loop, target, state);
			return;
		}
		const auto started = std::chrono::steady_clock::now();
		const auto game = std::static_pointer_cast<GameState>(state);
		game->update_visibility(at);
		game->update_player_economies(at);
		auto view = game->visibility(this->settings.player);
		if (not game->is_fog_enabled()) {
			auto visible = std::make_shared<VisibilitySnapshot>();
			const auto size = game->get_map()->get_size();
			visible->width = size[0];
			visible->height = size[1];
			visible->tiles.assign(size[0] * size[1], TileVisibility::Visible);
			for (const auto &[id, entity] : game->get_game_entities()) {
				if (entity->has_component(component_t::OWNERSHIP) and entity->has_component(component_t::POSITION)
				    and (not entity->is_dead() or entity->is_harvestable_carcass())) {
					const auto pos = position(*entity, at);
					visible->entities.push_back({id, owner(*entity, at), pos.ne.to_double(), pos.se.to_double()});
				}
			}
			std::ranges::sort(visible->entities, {}, &VisibleEntity::id);
			view = std::move(visible);
		}
		if (not view) {
			throw Error{MSG(err) << "AI requires a visibility snapshot; enable fog observation"};
		}
		const auto ms = (at.get_raw_value() * 1000) / time::time_t{1}.get_raw_value();
		if (this->pending) {
			const auto order = *this->pending;
			this->pending.reset();
			if (game->has_game_entity(order.actor) and not game->get_game_entity(order.actor)->is_dead()
			    and owner(*game->get_game_entity(order.actor), at) == this->settings.player
			    and ((order.kind != "ATTACK" and order.kind != "GATHER")
			         or (game->has_game_entity(order.target) and view->contains(order.target)))
			    and this->budget.consume(ms)) {
				this->gathering.erase(order.actor);
				if (order.kind == "PRODUCE" or order.kind == "BUILD") {
					this->production->invoke(loop, target, state, at, {
																		  {"actor", order.actor},
																		  {"option", order.option},
																		  {"position", order.destination},
																		  {"issuer", this->settings.player},
																	  });
				}
				else {
					const auto kind = order.kind == "ATTACK" ? command_t::ATTACK : order.kind == "GATHER" ? command_t::GATHER :
					                                                                                        command_t::MOVE;
					event::SendCommandHandler commands;
					commands.invoke(loop, target, state, at, {
																 {"type", kind},
																 {"entity_ids", std::vector<entity_id_t>{order.actor}},
																 {"target_id", order.target},
																 {"target", order.destination},
																 {"issuer", this->settings.player},
															 });
				}
				if (order.kind == "GATHER") {
					this->gathering.insert_or_assign(order.actor, std::pair{order.target, position(*game->get_game_entity(order.target), at)});
				}
				this->cooldown[order.actor] = at + time::time_t{10};
				std::ostringstream line;
				line << "AI ms=" << ms << " player=" << this->settings.player << " seq=" << ++this->sequence
					 << " time=" << at.get_raw_value() << " " << order.kind << " actor=" << order.actor << " target=" << order.target
					 << " option=" << order.option << " ne=" << order.destination.ne.get_raw_value()
					 << " se=" << order.destination.se.get_raw_value() << "\n";
				this->lines.push_back(line.str());
				if (this->lines.size() > static_cast<size_t>(this->settings.max_orders)) {
					this->lines.pop_front();
				}
				log::log(DBG << "AI player=" << this->settings.player << " order=" << order.kind << " actor=" << order.actor);
			}
		}
		else {
			this->pending = this->choose(*game, *view, at);
		}
		if (not params.get("replay", false)) {
			const auto delay = this->pending ? this->settings.reaction_ms : 250;
			loop.create_event("game.ai." + std::to_string(this->settings.player), target, state, at + time::time_t::from_double(delay / 1000.0));
		}
		const auto finished = std::chrono::steady_clock::now();
		const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(finished - started).count();
		this->total_us += elapsed;
		this->maximum_us = std::max<int64_t>(this->maximum_us, elapsed);
		++this->samples;
		if (finished - this->report_at >= std::chrono::seconds{10}) {
			log::log(INFO << "[PERF] ai player=" << this->settings.player << " calls=" << this->samples
			              << " orders=" << this->sequence << " total_ms=" << this->total_us / 1000.0
			              << " max_ms=" << this->maximum_us / 1000.0);
			this->report_at = finished;
			this->total_us = 0;
			this->maximum_us = 0;
			this->samples = 0;
		}
	}

	void replay(const std::string &line, openage::event::EventLoop &loop, const std::shared_ptr<openage::event::EventEntity> &target, const std::shared_ptr<openage::event::State> &state) {
		std::istringstream input{line};
		std::string word;
		std::map<std::string, std::string> fields;
		Order order;
		while (input >> word) {
			const auto split = word.find('=');
			if (split != std::string::npos) {
				fields.emplace(word.substr(0, split), word.substr(split + 1));
			}
			else if (word != "AI") {
				order.kind = word;
			}
		}
		if (std::stoull(fields.at("player")) != this->settings.player
		    or std::stoll(fields.at("seq")) != this->sequence + 1
		    or (order.kind != "ATTACK" and order.kind != "MOVE" and order.kind != "GATHER"
		        and order.kind != "PRODUCE" and order.kind != "BUILD")) {
			throw Error{MSG(err) << "Invalid recorded AI operation"};
		}
		order.actor = std::stoull(fields.at("actor"));
		order.target = std::stoull(fields.at("target"));
		order.option = fields.at("option");
		order.destination = coord::phys3{coord::phys_t::from_raw_value(std::stoll(fields.at("ne"))),
		                                 coord::phys_t::from_raw_value(std::stoll(fields.at("se"))),
		                                 0};
		const auto at = time::time_t::from_raw_value(std::stoll(fields.at("time")));
		this->pending = order;
		const auto before = this->sequence;
		this->invoke(loop, target, state, at, {{"replay", true}});
		if (this->sequence != before + 1) {
			throw Error{MSG(err) << "Recorded AI operation failed validation during replay"};
		}
	}

	player_id_t player_id() const {
		return this->settings.player;
	}
	std::string trace() const {
		std::ostringstream out;
		out << "AI_PROFILE version=1 player=" << this->settings.player << " seed=" << this->settings.seed
			<< " apm=" << this->settings.actions_per_minute << " aps=" << this->settings.actions_per_second
			<< " reaction_ms=" << this->settings.reaction_ms << " economy=" << this->settings.economy
			<< " construction=" << this->settings.construction << " research=" << this->settings.research
			<< " scouting=" << this->settings.scouting << " defense=" << this->settings.defense
			<< " attacks=" << this->settings.attacks << " retreat=" << this->settings.retreat
			<< " workers=" << this->settings.workers << " army=" << this->settings.army
			<< " dropped=" << this->sequence - static_cast<int64_t>(this->lines.size()) << "\n";
		for (const auto &line : this->lines) {
			out << line;
		}
		return out.str();
	}

private:
	std::optional<Order> choose(GameState &game, const VisibilitySnapshot &view, time::time_t at) {
		std::erase_if(this->cooldown, [&](const auto &entry) {
			return not game.has_game_entity(entry.first) or game.get_game_entity(entry.first)->is_dead();
		});
		std::erase_if(this->gathering, [&](const auto &entry) {
			return not game.has_game_entity(entry.first) or game.get_game_entity(entry.first)->is_dead();
		});
		std::vector<std::shared_ptr<GameEntity>> owned;
		std::vector<std::shared_ptr<GameEntity>> enemies;
		std::vector<std::shared_ptr<GameEntity>> resources;
		for (const auto &[id, entity] : game.get_game_entities()) {
			if (entity->is_dead() or not entity->has_component(component_t::POSITION)) {
				continue;
			}
			const auto player = owner(*entity, at);
			if (player == this->settings.player and not this->settings.protected_units.contains(id)) {
				owned.push_back(entity);
			}
			else if (view.contains(id) and game.get_stance(this->settings.player, player) == DiplomaticStance::Enemy
			         and not game.get_player(player)->is_gaia) {
				enemies.push_back(entity);
			}
			if (view.contains(id) and entity->resource_spot and entity->resource_spot->remaining > 0) {
				resources.push_back(entity);
			}
		}
		auto sort = [](auto &entities) { std::ranges::sort(entities, {}, [](const auto &entity) { return entity->get_id(); }); };
		sort(owned);
		sort(enemies);
		sort(resources);
		const auto player = game.get_player(this->settings.player);
		const auto economy = game.player_economy(this->settings.player);
		const auto workers = std::ranges::count_if(owned, [](const auto &entity) { return entity->has_component(component_t::GATHER); });
		const auto army = std::ranges::count_if(owned, [](const auto &entity) {
			return entity->has_component(component_t::ATTACK) and not entity->has_component(component_t::GATHER);
		});
		std::map<nyan::fqon_t, int64_t> demand;
		for (const auto &entity : owned) {
			for (const auto &option : entity->action_options) {
				if ((option.kind == ActionKind::Train or option.kind == ActionKind::Build)
				    and event::action_available(option, game, *player, at)) {
					for (const auto &[resource, amount] : option.cost) {
						demand[resource] = std::max(demand[resource], amount * 2);
					}
				}
			}
		}
		std::map<nyan::fqon_t, int64_t> assigned;
		for (const auto &[worker, assignment] : this->gathering) {
			const auto spot = assignment.first;
			if (view.contains(spot) and game.has_game_entity(spot) and game.get_game_entity(spot)->resource_spot) {
				++assigned[game.get_game_entity(spot)->resource_spot->resource];
			}
		}
		auto nearest = [&](const auto &entity, const auto &candidates) {
			return std::ranges::min_element(candidates, [&](const auto &a, const auto &b) {
				return distance(position(*entity, at), position(*a, at)) < distance(position(*entity, at), position(*b, at));
			});
		};
		for (const auto &entity : owned) {
			const auto id = entity->get_id();
			if (this->cooldown.contains(id) and this->cooldown.at(id) > at) {
				continue;
			}
			if (entity->building_site != 0) {
				continue;
			}
			const auto attack = entity->try_get_component_as<component::Attack>(component_t::ATTACK);
			const auto live = entity->try_get_component_as<component::Live>(component_t::LIVE);
			if (this->settings.retreat and attack and live and not enemies.empty()) {
				const auto hp = live->get_attribute_value(at, "de2_base.data.util.attribute.types.Health");
				const auto maximum = live->get_attribute_max("de2_base.data.util.attribute.types.Health");
				if (hp and maximum and *hp * 3 < *maximum) {
					const auto home = std::ranges::find_if(owned, [](const auto &unit) {
						return unit->has_component(component_t::BUILD);
					});
					if (home != owned.end() and distance(position(*entity, at), position(**home, at)) > coord::phys_t{3}.get_raw_value()) {
						return Order{"MOVE", id, 0, {}, position(**home, at)};
					}
				}
			}
			if (attack and not attack->is_active() and not entity->has_component(component_t::GATHER) and not enemies.empty()) {
				const auto enemy = *nearest(entity, enemies);
				const auto range = distance(position(*entity, at), position(*enemy, at));
				if (this->settings.attacks or (this->settings.defense and range < coord::phys_t{8}.get_raw_value())) {
					return Order{"ATTACK", id, enemy->get_id()};
				}
			}
			if (attack and attack->is_active()) {
				continue;
			}
			if (this->settings.construction and entity->construct_ability
			    and economy.population_capacity < game.get_population_limit()
			    and std::ranges::none_of(owned, [](const auto &unit) {
				    const auto build = unit->template try_get_component_as<component::Build>(component_t::BUILD);
				    return build and build->is_under_construction() and unit->population_provided > 0; }) and economy.population + this->production->queued_population(this->settings.player) + 2 >= economy.population_capacity) {
				const auto house = std::ranges::find_if(entity->action_options, [](const auto &option) {
					return option.kind == ActionKind::Build and option.subject.ends_with(".House");
				});
				if (house != entity->action_options.end() and event::action_available(*house, game, *player, at)
				    and std::ranges::all_of(house->cost, [&](const auto &cost) { return player->has_enough(cost.first, cost.second); })) {
					const auto home = position(*entity, at).to_tile();
					for (int offset = 0; offset < 16; ++offset) {
						const auto x = home.ne + (offset % 4 - 2) * 3;
						const auto y = home.se + (offset / 4 - 2) * 3;
						if (view.at(x, y) != TileVisibility::Unexplored and not this->building_tiles.contains({x, y})) {
							this->building_tiles.emplace(x, y);
							return Order{"BUILD", id, 0, house->id, coord::phys3{coord::phys_t{static_cast<double>(x)}, coord::phys_t{static_cast<double>(y)}, 0}};
						}
					}
				}
			}
			if (not this->production->busy(id)) {
				for (const auto &option : entity->action_options) {
					const bool worker = option.subject.ends_with(".Villager");
					const bool train = this->settings.economy and option.kind == ActionKind::Train
					                   and ((worker and workers < this->settings.workers) or (not worker and army < this->settings.army));
					if ((not train and not(this->settings.research and option.kind == ActionKind::Research))
					    or not event::action_available(option, game, *player, at)
					    or not std::ranges::all_of(option.cost, [&](const auto &cost) { return player->has_enough(cost.first, cost.second); })) {
						continue;
					}
					if (train and economy.population + this->production->queued_population(this->settings.player) + api::population_cost(player->get_db_view(), option.subject) > economy.population_capacity) {
						continue;
					}
					return Order{"PRODUCE", id, 0, option.id};
				}
			}
			const auto gather = entity->try_get_component_as<component::Gather>(component_t::GATHER);
			if (this->gathering.contains(id)) {
				const auto [spot, remembered] = this->gathering.at(id);
				const auto tile = remembered.to_tile();
				if (view.at(tile.ne, tile.se) == TileVisibility::Visible
				    and (not game.has_game_entity(spot) or not game.get_game_entity(spot)->resource_spot
				         or game.get_game_entity(spot)->resource_spot->remaining <= 0)) {
					this->gathering.erase(id);
				}
			}
			if (this->settings.economy and gather and not this->gathering.contains(id)) {
				std::vector<std::shared_ptr<GameEntity>> allowed;
				for (const auto &resource : resources) {
					if (gather->find_ability(resource->resource_spot->definition)
					    and std::ranges::none_of(enemies, [&](const auto &enemy) {
								return distance(position(*resource, at), position(*enemy, at)) < coord::phys_t{7}.get_raw_value();
							})) {
						allowed.push_back(resource);
					}
				}
				if (not allowed.empty()) {
					const auto best = std::ranges::max_element(allowed, [&](const auto &a, const auto &b) {
						auto priority = [&](const auto &resource) {
							const auto type = resource->resource_spot->resource;
							const auto deficit = std::max<int64_t>(0, demand[type] - player->get_resource(type));
							const auto travel = distance(position(*entity, at), position(*resource, at)) / coord::phys_t{1}.get_raw_value();
							return (100 + deficit) * 100 / ((1 + assigned[type]) * (1 + travel));
						};
						return priority(a) < priority(b);
					});
					const auto spot = *best;
					return Order{"GATHER", id, spot->get_id()};
				}
			}
			if (this->settings.scouting and not gather and entity->has_component(component_t::MOVE)
			    and entity->has_component(component_t::COMMANDQUEUE)) {
				const auto origin = position(*entity, at).to_tile();
				const auto navigation = game.get_map()->get_navigation_service();
				this->random = this->random * 6364136223846793005ULL + 1442695040888963407ULL;
				const auto start = this->random % 441;
				for (int64_t candidate = 0; candidate < 128; ++candidate) {
					const int64_t tile = (start + candidate) % 441;
					const int64_t x = origin.ne + tile % 21 - 10;
					const int64_t y = origin.se + tile / 21 - 10;
					if (x < 1 or y < 1 or x + 1 >= static_cast<int64_t>(view.width) or y + 1 >= static_cast<int64_t>(view.height)
					    or view.at(x, y) == TileVisibility::Unexplored or not navigation
					    or not navigation->grid()->is_walkable({static_cast<int>(x), static_cast<int>(y)})) {
						continue;
					}
					if (view.at(x + 1, y) == TileVisibility::Unexplored or view.at(x - 1, y) == TileVisibility::Unexplored
					    or view.at(x, y + 1) == TileVisibility::Unexplored or view.at(x, y - 1) == TileVisibility::Unexplored) {
						return Order{"MOVE", id, 0, {}, coord::phys3{coord::phys_t{static_cast<double>(x)}, coord::phys_t{static_cast<double>(y)}, 0}};
					}
				}
			}
		}
		return std::nullopt;
	}

	Profile settings;
	std::shared_ptr<event::EconomyActionHandler> production;
	ActionBudget budget;
	uint64_t random;
	int64_t sequence = 0;
	std::optional<Order> pending;
	std::map<entity_id_t, time::time_t> cooldown;
	std::map<entity_id_t, std::pair<entity_id_t, coord::phys3>> gathering;
	std::set<std::pair<int64_t, int64_t>> building_tiles;
	std::deque<std::string> lines;
	std::chrono::steady_clock::time_point report_at{std::chrono::steady_clock::now()};
	int64_t total_us = 0;
	int64_t maximum_us = 0;
	int64_t samples = 0;
};

} // namespace
} // namespace openage::gamestate::ai

export namespace openage::gamestate::ai {

/** Attach only explicitly configured players; existing scenario behavior is unchanged otherwise. */
std::vector<std::shared_ptr<openage::event::EventHandler>> configure(
	const std::string &json, const std::shared_ptr<openage::event::EventLoop> &loop, const std::shared_ptr<openage::event::EventEntity> &target, const std::shared_ptr<GameState> &state, const std::shared_ptr<event::EconomyActionHandler> &production) {
	const auto parsed = util::json::parse(json);
	const auto &config = parsed.has("ai") ? parsed.at("ai") : parsed;
	if (not config.is_object() or not config.at("players").is_array()) {
		throw Error{MSG(err) << "AI configuration requires a players array"};
	}
	for (const auto &[key, value] : config.as_object()) {
		if (key != "players" and key != "population_limit") {
			throw Error{MSG(err) << "Unknown AI configuration field: " << key};
		}
	}
	std::vector<std::shared_ptr<openage::event::EventHandler>> result;
	std::set<player_id_t> players;
	for (const auto &entry : config.at("players").as_array()) {
		if (not flag(entry, "enabled", true)) {
			continue;
		}
		auto settings = profile(entry);
		if (entry.has("protected_instances")) {
			if (not entry.at("protected_instances").is_array()) {
				throw Error{MSG(err) << "protected_instances must be an array"};
			}
			for (const auto &name : entry.at("protected_instances").as_array()) {
				const auto id = state->get_entity_id_by_instance(name.as_string());
				if (not id) {
					throw Error{MSG(err) << "Unknown protected scenario instance: " << name.as_string()};
				}
				settings.protected_units.insert(*id);
			}
		}
		if (not state->get_players().contains(settings.player) or state->get_player(settings.player)->is_gaia
		    or not players.insert(settings.player).second) {
			throw Error{MSG(err) << "AI requires distinct existing non-Gaia player IDs"};
		}
		log::log(INFO << "AI configured player=" << settings.player << " apm=" << settings.actions_per_minute
		              << " aps=" << settings.actions_per_second << " reaction_ms=" << settings.reaction_ms);
		result.push_back(std::make_shared<Controller>(settings, production));
	}
	std::ranges::sort(result, {}, [](const auto &handler) {
		return std::dynamic_pointer_cast<Controller>(handler)->player_id();
	});
	if (config.has("population_limit")) {
		state->set_population_limit(config.at("population_limit").as_int());
	}
	for (const auto &handler : result) {
		loop->add_event_handler(handler);
		if (not parsed.has("ai_replay_operations")) {
			loop->create_event(handler, target, state, time::time_t::zero());
		}
		else {
			for (const auto &record : parsed.at("ai_replay_operations").as_array()) {
				const auto line = record.as_string();
				const auto player_start = line.find("player=");
				const auto time_start = line.find("time=");
				if (player_start == std::string::npos or time_start == std::string::npos) {
					throw Error{MSG(err) << "Recorded AI operation lacks a player or exact time"};
				}
				if (std::stoull(line.substr(player_start + 7)) == std::dynamic_pointer_cast<Controller>(handler)->player_id()) {
					loop->create_event(handler, target, state, time::time_t::from_raw_value(std::stoll(line.substr(time_start + 5))), {{"recorded", line}});
				}
			}
		}
	}
	return result;
}

/** Bounded canonical operation log; callers compare independent player streams in ID order. */
std::string snapshot(const std::vector<std::shared_ptr<openage::event::EventHandler>> &controllers,
                     const GameState &game,
                     time::time_t at) {
	std::ostringstream out;
	for (const auto &handler : controllers) {
		const auto player = std::dynamic_pointer_cast<Controller>(handler)->player_id();
		const auto economy = game.player_economy(player);
		out << "AI_STATE player=" << player << " wood=" << economy.wood << " food=" << economy.food
			<< " gold=" << economy.gold << " stone=" << economy.stone << " pop=" << economy.population
			<< "/" << economy.population_capacity << "\n";
		std::vector<entity_id_t> ids;
		for (const auto &[id, entity] : game.get_game_entities()) {
			if (owner(*entity, at) == player and not entity->is_dead() and entity->has_component(component_t::POSITION)) {
				ids.push_back(id);
			}
		}
		std::ranges::sort(ids);
		for (const auto id : ids) {
			const auto &entity = game.get_game_entity(id);
			const auto pos = position(*entity, at);
			const auto live = entity->try_get_component_as<component::Live>(component_t::LIVE);
			out << "AI_UNIT id=" << id << " type=" << entity->definition << " ne=" << pos.ne.get_raw_value()
				<< " se=" << pos.se.get_raw_value() << " hp="
				<< (live ? live->get_attribute_value(at, "de2_base.data.util.attribute.types.Health").value_or(-1) : -1) << "\n";
		}
	}
	return out.str();
}

std::string trace(const std::vector<std::shared_ptr<openage::event::EventHandler>> &controllers) {
	std::string result;
	for (const auto &handler : controllers) {
		result += std::dynamic_pointer_cast<Controller>(handler)->trace();
	}
	return result;
}

namespace tests {

void action_limits() {
	TESTTHROWS(ActionBudget(0, 1));
	TESTTHROWS(ActionBudget(1, 0));
	Player player{0, nullptr};
	player.set_allowed_actions({"villager"});
	TESTEQUALS(player.allows_action("villager"), true);
	TESTEQUALS(player.allows_action("foreign_unique_unit"), false);
	player.disable_action("villager");
	TESTEQUALS(player.allows_action("villager"), false);
	ActionBudget budget{48, 2};
	TESTEQUALS(budget.consume(0), true);
	TESTEQUALS(budget.consume(0), false);
	TESTEQUALS(budget.consume(1000), false);
	TESTEQUALS(budget.consume(1250), true);
	TESTEQUALS(budget.consume(2500), true);
	ActionBudget burst{600, 2};
	TESTEQUALS(burst.consume(0), true);
	TESTEQUALS(burst.consume(100), true);
	TESTEQUALS(burst.consume(200), false);
	TESTEQUALS(burst.consume(1000), true);
	TESTEQUALS(profile(util::json::parse("{\"difficulty\":\"easiest\"}")).actions_per_minute, 12);
	TESTTHROWS(profile(util::json::parse("{\"difficulty\":\"unknown\"}")));
}

} // namespace tests
} // namespace openage::gamestate::ai
