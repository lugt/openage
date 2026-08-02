// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4A: SelectionModel -- the GUI's view of "what is currently
// selected" plus the change events the HUD subscribes to.
//
// Events emitted:
//   - SelectionChanged(entity_id_t new, entity_id_t old) on every
//     set_selected; also fires when the selected entity is
//     removed (new = nullopt).
//   - HealthChanged(entity_id_t, int64_t new_value) whenever the
//     selected entity's Health attribute is written via
//     Live::set_attribute. Wired by re-registering the Live
//     callback each time selection changes.
//   - EntityRemoved(entity_id_t) for every entity removed; also
//     drives the "safe clear on death" path for selection.
//   - MissionStateChanged(reason) once, when the runtime declares
//     victory.
//
// All wiring goes through the existing event/observer plumbing --
// the SelectionModel does NOT call AttackHandler, does NOT parse
// nyan, and does NOT compute victory. The HUD binds to the
// callback streams; the views (SelectedEntityView / MissionView)
// are rebuilt by the HUD on each event.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gamestate/types.h"


namespace openage::gamestate {
class GameSimulation;
class GameState;
class GameEntity;
}


namespace openage::presenter::viewmodel {


using SelectionChangedCallback = std::function<void(
	std::optional<openage::gamestate::entity_id_t> new_id,
	std::optional<openage::gamestate::entity_id_t> old_id)>;
using HealthChangedCallback = std::function<void(
	openage::gamestate::entity_id_t entity_id, int64_t new_hp)>;
using EntityRemovedCallback = std::function<void(
	openage::gamestate::entity_id_t entity_id)>;
using MissionStateChangedCallback = std::function<void(
	const std::string &reason)>;


class SelectionModel {
public:
	SelectionModel(
		const std::shared_ptr<openage::gamestate::GameSimulation> &sim);
	~SelectionModel();

	std::optional<openage::gamestate::entity_id_t>
	selected_entity_id() const { return this->selected_; }

	/**
	 * Set or clear the current selection. Passing nullopt clears
	 * the selection. Fires SelectionChanged; re-wires the
	 * HealthChanged callback for the newly selected entity.
	 */
	void set_selected(
		std::optional<openage::gamestate::entity_id_t> entity_id);

	// Subscribe API. Each subscribe_* returns a connection-like
	// handle (an id) that can be used to unsubscribe; for the
	// MVP we just append and rely on the SelectionModel outliving
	// the HUD.
	void subscribe_selection_changed(SelectionChangedCallback cb) {
		this->sel_cbs_.push_back(std::move(cb));
	}
	void subscribe_health_changed(HealthChangedCallback cb) {
		this->hp_cbs_.push_back(std::move(cb));
	}
	void subscribe_entity_removed(EntityRemovedCallback cb) {
		this->rm_cbs_.push_back(std::move(cb));
	}
	void subscribe_mission_state_changed(MissionStateChangedCallback cb) {
		this->ms_cbs_.push_back(std::move(cb));
	}

private:
	void wire_health_callback(
		const std::shared_ptr<openage::gamestate::GameEntity> &entity);

	std::shared_ptr<openage::gamestate::GameSimulation> sim_;
	std::optional<openage::gamestate::entity_id_t> selected_;

	// Subscription lists.
	std::vector<SelectionChangedCallback> sel_cbs_;
	std::vector<HealthChangedCallback> hp_cbs_;
	std::vector<EntityRemovedCallback> rm_cbs_;
	std::vector<MissionStateChangedCallback> ms_cbs_;
};


} // namespace openage::presenter::viewmodel