// Copyright 2026 the openage authors. See copying.md for legal info.

#include "presenter/viewmodel/selected_entity_view.h"

#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "time/time.h"

#include <nyan/nyan.h>


namespace openage::presenter::viewmodel {


SelectedEntityView::SelectedEntityView(
	const std::shared_ptr<openage::gamestate::GameState> &state,
	openage::gamestate::entity_id_t entity_id) :
	entity_id_{entity_id} {

	auto entity = state->get_game_entity(entity_id);
	if (not entity) {
		return;
	}

	// scenario_object_id: reverse-lookup the entity in the named-
	// entity map. The loader registers both the full instance FQON
	// and the short id, so a match on the entity id against the
	// short ids is the canonical way to get the player-facing label.
	for (const auto &kv : state->get_named_entities()) {
		if (kv.second == entity_id) {
			// Heuristic: the short id contains no '.' and is
			// not a nyan namespace; the full FQON does. Take
			// the entry whose name is the shorter form.
			const auto &name = kv.first;
			if (name.find('.') == std::string::npos) {
				this->scenario_object_id_ = name;
			}
			else if (not this->entity_fqon_.has_value()) {
				this->entity_fqon_ = name;
			}
		}
	}

	// Owner.
	// try_get_component_as<T> checks the cast result; a bare
	// try_get_component() + dynamic_pointer_cast() would leave a failed
	// cast to be dereferenced right below the `if`, which segfaults.
	auto own = entity->try_get_component_as<openage::gamestate::component::Ownership>(
		openage::gamestate::component::component_t::OWNERSHIP);
	if (own) {
		// Ownership is a curve over time; the demo runs at
		// sim_time=0, so we read the owner at that time.
		auto now = openage::time::time_t{0};
		auto &owners = own->get_owners();
		this->owner_player_id_ = static_cast<int64_t>(owners.get(now));
	}

	// Health (Live component curve).
	auto live = entity->try_get_component_as<openage::gamestate::component::Live>(
		openage::gamestate::component::component_t::LIVE);
	if (live) {
		// Resolve the Health attribute FQON via the Attack component
		// (if present) so the view reads the same attribute the
		// AttackSystem damages. For non-combat entities we leave
		// both nullopt.
		nyan::fqon_t health_fqon;
		auto attack_comp = entity->try_get_component_as<
			openage::gamestate::component::Attack>(
			openage::gamestate::component::component_t::ATTACK);
		if (attack_comp) {
			health_fqon = attack_comp->get_health_attribute_fqon();
		}
		if (not health_fqon.empty()) {
			auto now = openage::time::time_t{0};
			auto at_zero = openage::time::time_t{0};
			auto cur = live->get_attribute_value(now, health_fqon);
			auto maxv = live->get_attribute_value(at_zero, health_fqon);
			if (cur.has_value()) this->hp_current_ = cur;
			if (maxv.has_value()) this->hp_max_ = maxv;
		}
	}

	// Attack component.
	auto attack = entity->try_get_component_as<openage::gamestate::component::Attack>(
		openage::gamestate::component::component_t::ATTACK);
	if (attack) {
		this->attack_damage_ = attack->get_damage();
		this->attack_range_ = attack->get_attack_range();
		this->attack_cooldown_ = attack->get_cooldown();
		this->attack_is_active_ = attack->is_active();
		this->attack_is_approaching_ = attack->is_approaching();
		auto tid = attack->get_target_id();
		if (tid != 0) {
			this->attack_target_id_ = tid;
		}
	}

	// Position (current).
	auto pos = entity->try_get_component_as<openage::gamestate::component::Position>(
		openage::gamestate::component::component_t::POSITION);
	if (pos) {
		auto now = openage::time::time_t{0};
		auto &positions = pos->get_positions();
		this->position_ = positions.get(now);
	}
}


} // namespace openage::presenter::viewmodel