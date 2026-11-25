// Copyright 2026 the openage authors. See copying.md for legal info.

#include "presenter/viewmodel/selection_model.h"

#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/types.h"
#include "gamestate/event/removal.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "scenario/trigger/mission_state.h"
#include "scenario/trigger/trigger_runtime.h"

#include <nyan/nyan.h>

#include <algorithm>


namespace openage::presenter::viewmodel {


namespace {


// Sort + dedup a vector of entity ids. Stable for the
// multi-selection contract: the resulting vector is in
// strictly ascending order, with duplicates removed.
std::vector<openage::gamestate::entity_id_t>
sort_dedup(std::vector<openage::gamestate::entity_id_t> v) {
	std::sort(v.begin(), v.end());
	v.erase(std::unique(v.begin(), v.end()), v.end());
	return v;
}


}  // namespace


SelectionModel::SelectionModel(
	const std::shared_ptr<openage::gamestate::GameSimulation> &sim) :
	sim_{sim} {

	// Wire the removal observer. On every removal, drop
	// the entity from the selection; if it was primary,
	// promote the lowest-id remaining; if the selection is
	// now empty, fire SelectionChanged with empty snapshot.
	if (this->sim_) {
		this->sim_->add_removal_observer(
			[this](openage::gamestate::entity_id_t removed_id) {
				// P4.4F.1: free the entity's DynamicOccupancy
				// slot on removal so the next mover doesn't
				// see a stale blocker.
				if (this->sim_) {
					auto g = this->sim_->get_game();
					if (g) {
						auto st = g->get_state();
						if (st) {
							auto m = st->get_map();
							if (m) {
								auto occ =
									m->get_dynamic_occupancy();
								if (occ) {
									occ->remove_entity(
										removed_id);
								}
							}
						}
					}
				}
				auto it = std::find(
					this->selected_.begin(),
					this->selected_.end(),
					removed_id);
				if (it == this->selected_.end()) {
					for (auto &cb : this->rm_cbs_) {
						cb(removed_id);
					}
					return;
				}
				// Capture old snapshot BEFORE mutation so
				// the SelectionChanged callback sees the
				// pre-removal state.
				SelectionSnapshot prev{
					this->selected_, this->primary_};
				this->selected_.erase(it);
				if (this->primary_ == removed_id) {
					if (this->selected_.empty()) {
						this->primary_.reset();
					}
					else {
						// Lowest-id remaining; vector is
						// sorted, so front is min.
						this->primary_ = this->selected_.front();
					}
				}
				SelectionSnapshot next{
					this->selected_, this->primary_};
				if (prev.entities != next.entities
				    or prev.primary != next.primary) {
					for (auto &cb : this->sel_cbs_) {
						cb(next, prev);
					}
				}
				for (auto &cb : this->rm_cbs_) {
					cb(removed_id);
				}
			});

		// Ordinary games have no mission runtime to subscribe to.
		if (const auto &runtime = this->sim_->get_trigger_runtime()) {
			runtime->mission_state().set_victory_listener(
				[this](const std::string &reason) {
					for (auto &cb : this->ms_cbs_) {
						cb(reason);
					}
				});
		}
	}
}


SelectionModel::~SelectionModel() = default;


std::shared_ptr<openage::gamestate::GameSimulation>
SelectionModel::get_simulation() const {
	return this->sim_;
}


std::shared_ptr<openage::gamestate::GameState>
SelectionModel::get_state() const {
	if (not this->sim_) return nullptr;
	auto g = this->sim_->get_game();
	if (not g) return nullptr;
	return g->get_state();
}


void SelectionModel::replace_selection(
	std::vector<openage::gamestate::entity_id_t> new_selected,
	std::optional<openage::gamestate::entity_id_t> new_primary) {
	SelectionSnapshot prev{this->selected_, this->primary_};
	this->selected_ = std::move(new_selected);
	this->primary_ = new_primary;
	SelectionSnapshot next{this->selected_, this->primary_};
	if (prev.entities != next.entities or prev.primary != next.primary) {
		for (auto &cb : this->sel_cbs_) {
			cb(next, prev);
		}
	}
	// Re-wire the health callback for the new primary. We
	// don't unsubscribe the old one (the callback lives on
	// the previous Live instance; it cannot fire because
	// the previous entity is no longer the primary).
	if (new_primary.has_value() && this->sim_) {
		auto state = this->sim_->get_game()->get_state();
		if (state) {
			auto entity = state->get_game_entity(*new_primary);
			if (entity) {
				this->wire_health_callback(entity);
			}
		}
	}
}


void SelectionModel::set_selected(
	std::optional<openage::gamestate::entity_id_t> entity_id) {
	std::vector<openage::gamestate::entity_id_t> new_selected;
	if (entity_id.has_value()) {
		new_selected.push_back(*entity_id);
	}
	this->replace_selection(
		std::move(new_selected), entity_id);
}


void SelectionModel::set_multi_selection(
	const std::vector<openage::gamestate::entity_id_t> &ids) {
	auto sorted = sort_dedup(ids);
	std::optional<openage::gamestate::entity_id_t> primary;
	if (not sorted.empty()) {
		primary = sorted.front();
	}
	this->replace_selection(std::move(sorted), primary);
}


void SelectionModel::add_to_selection(
	openage::gamestate::entity_id_t entity_id) {
	if (this->sim_) {
		auto state = this->sim_->get_game()->get_state();
		if (not state or not state->has_game_entity(entity_id)) {
			return;
		}
	}
	auto it = std::find(
		this->selected_.begin(), this->selected_.end(),
		entity_id);
	if (it != this->selected_.end()) {
		// Already selected: promote to primary (FIFO
		// "most-recent").
		SelectionSnapshot prev{this->selected_, this->primary_};
		this->primary_ = entity_id;
		SelectionSnapshot next{this->selected_, this->primary_};
		for (auto &cb : this->sel_cbs_) {
			cb(next, prev);
		}
		return;
	}
	SelectionSnapshot prev{this->selected_, this->primary_};
	this->selected_.push_back(entity_id);
	std::sort(this->selected_.begin(), this->selected_.end());
	this->primary_ = entity_id;
	SelectionSnapshot next{this->selected_, this->primary_};
	for (auto &cb : this->sel_cbs_) {
		cb(next, prev);
	}
}


void SelectionModel::remove_from_selection(
	openage::gamestate::entity_id_t entity_id) {
	auto it = std::find(
		this->selected_.begin(), this->selected_.end(),
		entity_id);
	if (it == this->selected_.end()) {
		return;
	}
	SelectionSnapshot prev{this->selected_, this->primary_};
	this->selected_.erase(it);
	if (this->primary_ == entity_id) {
		if (this->selected_.empty()) {
			this->primary_.reset();
		}
		else {
			this->primary_ = this->selected_.front();
		}
	}
	SelectionSnapshot next{this->selected_, this->primary_};
	for (auto &cb : this->sel_cbs_) {
		cb(next, prev);
	}
}


void SelectionModel::wire_health_callback(
	const std::shared_ptr<openage::gamestate::GameEntity> &entity) {

	// Find the Health attribute FQON via the Attack component.
	//
	// The Attack and Live components are OPTIONAL: a TownCenter is a
	// building with no attack ability, so the entity factory never
	// mounts an Attack component on it (see entity_factory.cpp: Attack
	// is only mounted for ApplyDiscreteEffect / RangedDiscreteEffect
	// abilities). Selecting such an entity must not abort the whole
	// selection -- it simply has no health attribute to watch.
	//
	// This is why the non-throwing try_get_component_as() is used here and
	// not get_component(): the latter throws for a missing component,
	// which terminated the whole Mission 1 windowed launch with
	// "std::out_of_range: unordered_map::at: key not found" as soon as
	// the player dragged a selection box over the TownCenter.
	//
	// try_get_component_as<T> (rather than try_get_component() followed by
	// a bare dynamic_pointer_cast) additionally checks the cast result, so
	// an enum/class mismatch reports itself instead of dereferencing a
	// null pointer.
	nyan::fqon_t health_fqon;
	auto attack = entity->try_get_component_as<
		openage::gamestate::component::Attack>(
		openage::gamestate::component::component_t::ATTACK);
	if (attack) {
		health_fqon = attack->get_health_attribute_fqon();
	}
	if (health_fqon.empty()) {
		return;
	}

	auto live = entity->try_get_component_as<
		openage::gamestate::component::Live>(
		openage::gamestate::component::component_t::LIVE);
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
