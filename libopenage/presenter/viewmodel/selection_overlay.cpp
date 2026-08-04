// Copyright 2026 the openage authors. See copying.md for legal info.

#include "presenter/viewmodel/selection_overlay.h"

#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/types.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"

#include <algorithm>


namespace openage::presenter::viewmodel {


namespace {


openage::gamestate::player_id_t entity_owner(
	const std::shared_ptr<openage::gamestate::GameState> &state,
	openage::gamestate::entity_id_t entity) {
	if (not state or not state->has_game_entity(entity)) {
		return 0;
	}
	auto e = state->get_game_entity(entity);
	if (not e->has_component(
		    openage::gamestate::component::component_t::OWNERSHIP)) {
		return 0;
	}
	auto own = std::dynamic_pointer_cast<
		openage::gamestate::component::Ownership>(
		e->get_component(
			openage::gamestate::component::component_t::OWNERSHIP));
	if (not own) {
		return 0;
	}
	return own->get_owners().get(openage::time::time_t{0});
}


}  // namespace


SelectionOverlay::SelectionOverlay(
	const std::shared_ptr<SelectionModel> &model) :
	model_{model} {
	if (not this->model_) {
		return;
	}
	this->model_->subscribe_selection_changed(
		[this](SelectionSnapshot,
		       SelectionSnapshot) {
			this->rebuild();
		});
	this->rebuild();
}


SelectionOverlay::~SelectionOverlay() = default;


void SelectionOverlay::rebuild() {
	this->entries_.clear();
	this->color_map_.clear();
	if (not this->model_) {
		return;
	}
	auto snap = this->model_->snapshot();
	if (snap.empty()) {
		return;
	}
	// All entities in the selection get a colour. The
	// "self" here is the player_id of the primary entity
	// (the most recently added); the rest of the
	// selection is compared against primary's owner for
	// friendly/hostile classification.
	auto state_holder = this->model_->get_state();
	openage::gamestate::player_id_t self_player = 0;
	if (snap.primary.has_value()) {
		self_player = entity_owner(
			state_holder, *snap.primary);
	}
	for (auto id : snap.entities) {
		openage::gamestate::player_id_t owner =
			entity_owner(state_holder, id);
		SelectionOverlayColor c = SelectionOverlayColor::NONE;
		if (owner == 0 or self_player == 0) {
			c = SelectionOverlayColor::NEUTRAL;
		}
		else if (owner == self_player) {
			c = SelectionOverlayColor::PLAYER_FRIENDLY;
		}
		else {
			c = SelectionOverlayColor::PLAYER_HOSTILE;
		}
		this->entries_.push_back({id, c});
		this->color_map_[id] = c;
	}
	// entries_ is already sorted (SelectionModel keeps
	// selected_ sorted ascending).
}


bool SelectionOverlay::has(
	openage::gamestate::entity_id_t entity) const {
	return this->color_map_.find(entity) != this->color_map_.end();
}


SelectionOverlayColor SelectionOverlay::color_of(
	openage::gamestate::entity_id_t entity) const {
	auto it = this->color_map_.find(entity);
	if (it == this->color_map_.end()) {
		return SelectionOverlayColor::NONE;
	}
	return it->second;
}


} // namespace openage::presenter::viewmodel
