// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "attack.h"


namespace openage::gamestate::component::command {

AttackCommand::AttackCommand(entity_id_t attacker, entity_id_t target) :
	attacker{attacker}, target{target} {}

entity_id_t AttackCommand::get_attacker() const {
	return this->attacker;
}

entity_id_t AttackCommand::get_target() const {
	return this->target;
}

} // namespace openage::gamestate::component::command
