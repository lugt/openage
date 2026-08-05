// Copyright 2023-2024 the openage authors. See copying.md for legal info.

#include "game_state.h"

#include <nyan/nyan.h>

#include "error/error.h"
#include "log/log.h"

#include "gamestate/game_entity.h"
#include "gamestate/player.h"


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
	this->game_entities.erase(it);
	log::log(MSG(info) << "GameState: removed game entity " << id
	                   << " from index (" << this->game_entities.size()
	                   << " remaining)");
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
