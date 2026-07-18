// Copyright 2018-2023 the openage authors. See copying.md for legal info.

#include "player.h"

#include "nyan/nyan.h"


namespace openage::gamestate {

Player::Player(player_id_t id,
               const std::shared_ptr<nyan::View> &db_view) :
	id{id},
	db_view{db_view},
	resources{} {
}

std::shared_ptr<Player> Player::copy(entity_id_t id) {
	auto copy = std::shared_ptr<Player>(new Player(*this));
	copy->set_id(id);

	return copy;
}

player_id_t Player::get_id() const {
	return this->id;
}

const std::shared_ptr<nyan::View> &Player::get_db_view() const {
	return this->db_view;
}

void Player::add_resource(const nyan::fqon_t &resource_fqon, int64_t amount) {
	if (amount <= 0) {
		return;
	}
	this->resources[resource_fqon] += amount;
}

int64_t Player::get_resource(const nyan::fqon_t &resource_fqon) const {
	auto it = this->resources.find(resource_fqon);
	if (it == this->resources.end()) {
		return 0;
	}
	return it->second;
}

bool Player::has_enough(const nyan::fqon_t &resource_fqon, int64_t amount) const {
	return this->get_resource(resource_fqon) >= amount;
}

bool Player::spend(const nyan::fqon_t &resource_fqon, int64_t amount) {
	if (amount <= 0) {
		// Refusing to deduct zero is intentional: callers should not rely
		// on the return value to mean "the spend happened" when there was
		// nothing to deduct in the first place. Returning true keeps this
		// path a no-op while still being semantically "success".
		return true;
	}
	int64_t current = this->get_resource(resource_fqon);
	if (current < amount) {
		return false;
	}
	this->resources[resource_fqon] = current - amount;
	return true;
}

const std::unordered_map<nyan::fqon_t, int64_t> &Player::get_resources() const {
	return this->resources;
}

void Player::set_id(entity_id_t id) {
	this->id = id;
}

} // namespace openage::gamestate
