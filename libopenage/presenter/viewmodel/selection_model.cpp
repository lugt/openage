// Copyright 2026 the openage authors. See copying.md for legal info.

#include "presenter/viewmodel/selection_model.h"

#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/types.h"
#include "gamestate/event/removal.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "scenario/trigger/mission_state.h"
#include "scenario/trigger/trigger_runtime.h"

#include <nyan/nyan.h>


namespace openage::presenter::viewmodel {


SelectionModel::SelectionModel(
	const std::shared_ptr<openage::gamestate::GameSimulation> &sim) :
	sim_{sim} {

	// Wire the removal observer on the production RemovalEventHandler.
	// On every removal, fire EntityRemoved; if the removed entity is
	// the current selection, clear the selection and emit
	// SelectionChanged (new=nullopt) BEFORE the EntityRemoved event
	// so the HUD can null-out the panel before reporting the death.
	if (this->sim_) {
		this->sim_->add_removal_observer(
			[this](openage::gamestate::entity_id_t removed_id) {
				bool was_selected = this->selected_.has_value()
					&& *this->selected_ == removed_id;
				if (was_selected) {
					auto old = this->selected_;
					this->selected_.reset();
					for (auto &cb : this->sel_cbs_) {
						cb(std::nullopt, old);
					}
				}
				for (auto &cb : this->rm_cbs_) {
					cb(removed_id);
				}
			});

		// Wire the mission-state victory listener.
		auto &ms = this->sim_->get_trigger_runtime()->mission_state();
		ms.set_victory_listener(
			[this](const std::string &reason) {
				for (auto &cb : this->ms_cbs_) {
					cb(reason);
				}
			});
	}
}


SelectionModel::~SelectionModel() = default;


void SelectionModel::set_selected(
	std::optional<openage::gamestate::entity_id_t> entity_id) {

	auto old = this->selected_;
	if (old == entity_id) {
		return;
	}
	this->selected_ = entity_id;

	// Emit SelectionChanged.
	for (auto &cb : this->sel_cbs_) {
		cb(entity_id, old);
	}

	// Re-wire the HealthChanged callback for the newly selected
	// entity. We register a fresh callback on each set_selected;
	// the previous Live instance is left alone (callbacks fire
	// only on set_attribute, and the previous entity is no
	// longer selected so no HUD will see its events). When
	// selection clears (nullopt), we don't register anything.
	if (entity_id.has_value() && this->sim_) {
		auto state = this->sim_->get_game()->get_state();
		if (state) {
			auto entity = state->get_game_entity(*entity_id);
			if (entity) {
				this->wire_health_callback(entity);
			}
		}
	}
}


void SelectionModel::wire_health_callback(
	const std::shared_ptr<openage::gamestate::GameEntity> &entity) {

	// Find the Health attribute FQON via the Attack component (the
	// attribute the AttackSystem damages). Fall back to a no-op
	// wiring for entities with no Attack component.
	nyan::fqon_t health_fqon;
	auto attack = std::dynamic_pointer_cast<
		openage::gamestate::component::Attack>(
		entity->get_component(
			openage::gamestate::component::component_t::ATTACK));
	if (attack) {
		health_fqon = attack->get_health_attribute_fqon();
	}
	if (health_fqon.empty()) {
		return;
	}

	auto live = std::dynamic_pointer_cast<
		openage::gamestate::component::Live>(
		entity->get_component(
			openage::gamestate::component::component_t::LIVE));
	if (not live) {
		return;
	}

	auto entity_id = entity->get_id();
	live->on_attribute_change(
		health_fqon,
		[this, entity_id](int64_t new_value) {
			for (auto &cb : this->hp_cbs_) {
				cb(entity_id, new_value);
			}
		});
}


} // namespace openage::presenter::viewmodel