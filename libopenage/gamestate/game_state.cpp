// Copyright 2023-2024 the openage authors. See copying.md for legal info.

#include "game_state.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "error/error.h"
#include "log/log.h"

#include "gamestate/component/api/build.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/game_entity.h"
#include "gamestate/map.h"
#include "gamestate/player.h"

import nyan;


namespace openage::gamestate {

GameState::GameState(const std::shared_ptr<nyan::Database> &db,
                     const std::shared_ptr<openage::event::EventLoop> &event_loop) :
	event::State{event_loop},
	db_view{db->new_view()},
	event_loop{event_loop} {
}


void GameState::set_event_loop(const std::shared_ptr<openage::event::EventLoop> &loop) {
	if (this->event_loop && this->event_loop != loop) {
		log::log(MSG(warn) << "GameState::set_event_loop: replacing existing loop "
		                   << "handle (P4.4C observability hook); new loop will "
		                   << "receive future dispatch requests");
	}
	this->event_loop = loop;
}

const std::shared_ptr<nyan::View> &GameState::get_db_view() {
	return this->db_view;
}

void GameState::add_game_entity(const std::shared_ptr<GameEntity> &entity) {
	if (this->game_entities.contains(entity->get_id())) [[unlikely]] {
		throw Error(MSG(err) << "Game entity with ID " << entity->get_id() << " already exists");
	}
	this->game_entities[entity->get_id()] = entity;
}

void GameState::add_player(const std::shared_ptr<Player> &player) {
	if (this->players.contains(player->get_id())) [[unlikely]] {
		throw Error(MSG(err) << "Player with ID " << player->get_id() << " already exists");
	}
	this->players[player->get_id()] = player;
}

void GameState::set_map(const std::shared_ptr<Map> &map) {
	this->map = map;
}

const std::shared_ptr<GameEntity> &GameState::get_game_entity(entity_id_t id) const {
	if (!this->game_entities.contains(id)) [[unlikely]] {
		throw Error(MSG(err) << "Game entity with ID " << id << " does not exist");
	}
	return this->game_entities.at(id);
}

bool GameState::has_game_entity(entity_id_t id) const {
	return this->game_entities.contains(id);
}

void GameState::remove_game_entity(entity_id_t id) {
	auto it = this->game_entities.find(id);
	if (it == this->game_entities.end()) [[unlikely]] {
		log::log(MSG(warn) << "GameState: remove_game_entity called for ID "
		                   << id << " which is not in the index (already removed?)");
		return;
	}
	it->second->remove_from_renderer();
	this->game_entities.erase(it);
	log::log(MSG(info) << "GameState: removed game entity " << id
	                   << " from index (" << this->game_entities.size()
	                   << " remaining)");
}

DiplomaticStance GameState::get_stance(player_id_t source, player_id_t target) const {
	if (source == target) {
		return DiplomaticStance::Self;
	}
	std::shared_lock lock{this->diplomacy_mutex};
	const auto found = this->diplomacy.find({source, target});
	return found == this->diplomacy.end() ? DiplomaticStance::Enemy : found->second;
}

bool GameState::set_stance(player_id_t source, player_id_t target, DiplomaticStance stance, bool reciprocal) {
	if (source == target or stance == DiplomaticStance::Self
	    or not this->players.contains(source) or not this->players.contains(target)) {
		return false;
	}
	std::unique_lock lock{this->diplomacy_mutex};
	this->diplomacy[{source, target}] = stance;
	if (reciprocal) {
		this->diplomacy[{target, source}] = stance;
	}
	return true;
}

void GameState::set_player_name(player_id_t player, std::string name) {
	this->player_names.insert_or_assign(player, std::move(name));
}

std::string GameState::get_player_name(player_id_t player) const {
	const auto found = this->player_names.find(player);
	return found == this->player_names.end() ? std::to_string(player) : found->second;
}

std::shared_ptr<const VisibilitySnapshot> GameState::visibility(player_id_t player) const {
	std::shared_lock lock{this->visibility_mutex};
	const auto found = this->visibility_by_player.find(player);
	return found == this->visibility_by_player.end() ? nullptr : found->second;
}

bool GameState::can_see(player_id_t player, entity_id_t entity) const {
	const auto view = this->visibility(player);
	return not view or view->contains(entity);
}

PlayerEconomy GameState::player_economy(player_id_t player) const {
	std::shared_lock lock{this->economy_mutex};
	const auto found = this->player_economies.find(player);
	return found == this->player_economies.end() ? PlayerEconomy{} : found->second;
}

std::string GameState::action_feedback(player_id_t player) const {
	std::shared_lock lock{this->economy_mutex};
	const auto found = this->economy_feedback.find(player);
	return found == this->economy_feedback.end() ? std::string{} : found->second;
}

void GameState::set_action_feedback(player_id_t player, std::string message) {
	std::unique_lock lock{this->economy_mutex};
	this->economy_feedback.insert_or_assign(player, std::move(message));
}

void GameState::update_player_economies(time::time_t at) {
	decltype(this->player_economies) next;
	for (const auto &[id, player] : this->players) {
		next.emplace(id, PlayerEconomy{
			.wood = player->get_resource("de2_base.data.util.resource.types.Wood"),
			.food = player->get_resource("de2_base.data.util.resource.types.Food"),
			.gold = player->get_resource("de2_base.data.util.resource.types.Gold"),
			.stone = player->get_resource("de2_base.data.util.resource.types.Stone"),
		});
	}
	for (const auto &[id, entity] : this->game_entities) {
		if (entity->is_dead() or (entity->population_used == 0 and entity->population_provided == 0)) {
			continue;
		}
		const auto ownership = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		if (not ownership) {
			continue;
		}
		const auto found = next.find(ownership->get_owners().get(at));
		if (found == next.end()) {
			continue;
		}
		auto &economy = found->second;
		economy.population += entity->population_used;
		const auto construction = entity->try_get_component_as<component::Build>(component::component_t::BUILD);
		if (not construction or not construction->is_under_construction()) {
			economy.population_capacity += entity->population_provided;
		}
	}
	std::unique_lock lock{this->economy_mutex};
	this->player_economies = std::move(next);
}

void GameState::update_visibility(time::time_t at) {
	if (not this->fog_of_war or not this->map) {
		return;
	}
	struct SightSource {
		VisibleEntity entity;
		double radius;
		bool alive;
	};
	std::vector<SightSource> sources;
	for (const auto &[id, entity] : this->game_entities) {
		auto ownership = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		auto position = entity->try_get_component_as<component::Position>(component::component_t::POSITION);
		if (ownership and position and (not entity->is_dead() or entity->is_harvestable_carcass())) {
			auto current = position->get_positions().get(at);
			sources.push_back({{id, ownership->get_owners().get(at), current.ne.to_double(), current.se.to_double()},
			                   entity->line_of_sight, not entity->is_dead()});
		}
	}

	const auto &size = this->map->get_size();
	decltype(this->visibility_by_player) next;
	for (const auto &[viewer, player] : this->players) {
		auto snapshot = std::make_shared<VisibilitySnapshot>();
		snapshot->width = size[0];
		snapshot->height = size[1];
		snapshot->tiles.resize(size[0] * size[1], TileVisibility::Unexplored);
		if (auto previous = this->visibility(viewer); previous
		    and previous->width == snapshot->width and previous->height == snapshot->height) {
			std::ranges::transform(previous->tiles, snapshot->tiles.begin(), [](auto tile) {
				return tile == TileVisibility::Visible ? TileVisibility::Explored : tile;
			});
		}
		for (const auto &source : sources) {
			const auto [id, owner, x, y] = source.entity;
			const bool contributes = owner == viewer
			                      or (this->share_allied_vision and this->get_stance(viewer, owner) == DiplomaticStance::Ally);
			if (not contributes or not source.alive or source.radius <= 0) {
				continue;
			}
			const double radius = std::min(source.radius, std::hypot(size[0], size[1]));
			const double radius_squared = radius * radius;
			const auto x0 = std::max<int64_t>(0, std::floor(x - radius));
			const auto y0 = std::max<int64_t>(0, std::floor(y - radius));
			const auto x1 = std::min<int64_t>(size[0] - 1, std::floor(x + radius));
			const auto y1 = std::min<int64_t>(size[1] - 1, std::floor(y + radius));
			// Compare tile centers in map units so a diagonal tile is only
			// revealed if it lies inside the same radius as a cardinal tile.
			for (auto row = y0; row <= y1; ++row) {
				for (auto col = x0; col <= x1; ++col) {
					const double dx = col + 0.5 - x;
					const double dy = row + 0.5 - y;
					if (dx * dx + dy * dy <= radius_squared) {
						snapshot->tiles[row * size[0] + col] = TileVisibility::Visible;
					}
				}
			}
			if (x >= 0 and y >= 0 and x < size[0] and y < size[1]) {
				snapshot->tiles[static_cast<size_t>(y) * size[0] + static_cast<size_t>(x)] = TileVisibility::Visible;
			}
		}
		for (const auto &source : sources) {
			const auto &marker = source.entity;
			if ((source.alive and marker.owner == viewer)
			    or snapshot->at(std::floor(marker.x), std::floor(marker.y)) == TileVisibility::Visible) {
				snapshot->entities.push_back(marker);
			}
		}
		std::ranges::sort(snapshot->entities, {}, &VisibleEntity::id);
		next.emplace(viewer, std::move(snapshot));
	}
	std::unique_lock lock{this->visibility_mutex};
	this->visibility_by_player = std::move(next);
}

void GameState::register_named_entity(const nyan::fqon_t &instance_fqon,
                                      entity_id_t id) {
	this->named_entities[instance_fqon] = id;
}

std::optional<entity_id_t> GameState::get_entity_id_by_instance(
	const nyan::fqon_t &instance_fqon) const {
	auto it = this->named_entities.find(instance_fqon);
	if (it == this->named_entities.end()) {
		return std::nullopt;
	}
	return it->second;
}

const std::unordered_map<nyan::fqon_t, entity_id_t> &GameState::get_named_entities() const {
	return this->named_entities;
}

const std::unordered_map<entity_id_t, std::shared_ptr<GameEntity>> &GameState::get_game_entities() const {
	return this->game_entities;
}

const std::shared_ptr<Player> &GameState::get_player(player_id_t id) const {
	if (!this->players.contains(id)) [[unlikely]] {
		throw Error(MSG(err) << "Player with ID " << id << " does not exist");
	}
	return this->players.at(id);
}

const std::unordered_map<player_id_t, std::shared_ptr<Player>> &GameState::get_players() const {
	return this->players;
}

const std::shared_ptr<Map> &GameState::get_map() const {
	return this->map;
}

const std::shared_ptr<assets::ModManager> &GameState::get_mod_manager() const {
	return this->mod_manager;
}

void GameState::set_mod_manager(const std::shared_ptr<assets::ModManager> &mod_manager) {
	this->mod_manager = mod_manager;
}

} // namespace openage::gamestate
