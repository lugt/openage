// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4A + P4.4D.2: SelectionModel -- the GUI's view of "what is
// currently selected" plus the change events the HUD subscribes
// to.
//
// P4.4D.2 multi-select contract:
//   - selected_ is a std::vector<entity_id_t> in a stable
//     sorted order (sorted by entity_id_t ascending; duplicates
//     collapse). Iterating this vector is deterministic.
//   - primary_ is the entity whose details the bottom HUD
//     panel renders (HP / attack / etc). For a single-entity
//     selection primary == entities[0]. For a multi selection
//     primary is the entity the user last clicked (FIFO by
//     most-recent add).
//   - set_selected({id}) replaces the selection (single
//     selection mode, used by LMB point selection).
//   - add_to_selection(id) / remove_from_selection(id) for
//     Shift+click add/remove.
//   - set_multi_selection(ids) replaces the whole vector;
//     primary is the first element of ids.
//   - removed entity is dropped from the vector; if it was
//     primary, primary becomes the lowest-id remaining
//     entity; if the vector is empty after removal, the
//     selection clears and SelectionChanged fires with
//     new=nullopt.
//   - on a multi selection, the move/attack command dispatches
//     to every entity in `entities` that has the relevant
//     ability (TC has no Move; TC has no Attack, etc). HUD
//     shows "selected count" + primary details.
//
// Events emitted (unchanged contract):
//   - SelectionChanged(primary_opt, prev_primary_opt)
//   - HealthChanged(entity_id, new_value)
//   - EntityRemoved(entity_id)
//   - MissionStateChanged(reason)
//
// All wiring goes through the existing event/observer plumbing --
// the SelectionModel does NOT call AttackHandler, does NOT parse
// nyan, and does NOT compute victory.

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


// P4.4D.2: stable multi-select snapshot. Used by the
// FormationPlanner, the commander, the input layer, and the
// renderer overlay. Iteration order is the same as
// SelectionModel::selected() (entity id ascending).
struct SelectionSnapshot {
	std::vector<openage::gamestate::entity_id_t> entities;
	std::optional<openage::gamestate::entity_id_t> primary;

	bool empty() const { return this->entities.empty(); }
	size_t size() const { return this->entities.size(); }
};


using SelectionChangedCallback = std::function<void(
	SelectionSnapshot new_snapshot,
	SelectionSnapshot prev_snapshot)>;
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

	/**
	 * Read-only access to the current selection. The vector
	 * is sorted ascending by entity id; primary is
	 * entities[0] for a single-element selection and the
	 * most-recently-added entity for a multi selection.
	 */
	const std::vector<openage::gamestate::entity_id_t> &selected() const {
		return this->selected_;
	}
	std::optional<openage::gamestate::entity_id_t> primary() const {
		return this->primary_;
	}

	/**
	 * Backward-compat alias for the single-entity HUD path
	 * (P4.4A SelectedEntityView). Returns the primary
	 * entity id, or nullopt for an empty selection. New
	 * code should read primary() or snapshot().
	 */
	std::optional<openage::gamestate::entity_id_t>
	selected_entity_id() const {
		return this->primary_;
	}

	/**
	 * Atomic snapshot of (entities, primary) for the
	 * FormationPlanner / commander / renderer.
	 */
	SelectionSnapshot snapshot() const {
		return SelectionSnapshot{this->selected_, this->primary_};
	}

	/**
	 * Single-entity set. Replaces any multi selection with
	 * the one entity. entity_id = nullopt clears the
	 * selection.
	 */
	void set_selected(
		std::optional<openage::gamestate::entity_id_t> entity_id);

	/**
	 * P4.4D.2: multi-entity set. Replaces the whole vector;
	 * primary = ids.front() (after sorting). Empty ids is
	 * equivalent to set_selected(nullopt).
	 */
	void set_multi_selection(
		const std::vector<openage::gamestate::entity_id_t> &ids);

	/**
	 * P4.4D.2: Shift+click add. Adds the entity if not
	 * already present; updates primary to the new entity.
	 * If the entity is removed/invalid, the call is a no-op.
	 */
	void add_to_selection(
		openage::gamestate::entity_id_t entity_id);

	/**
	 * P4.4D.2: Shift+click remove (toggle). Removes the
	 * entity from the selection; if it was primary, primary
	 * becomes the lowest-id remaining entry. Empty selection
	 * after removal -> SelectionChanged with empty snapshot.
	 */
	void remove_from_selection(
		openage::gamestate::entity_id_t entity_id);

	/**
	 * Clear the selection (equivalent to set_selected(nullopt)).
	 */
	void clear_selection() {
		this->set_selected(std::nullopt);
	}

	// Subscribe API.
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

	/**
	 * Internal helper: replace the selection atomically,
	 * firing SelectionChanged with both old and new snapshots.
	 * The caller is responsible for ensuring the new vector
	 * is sorted and deduped; this method does not re-sort.
	 */
	void replace_selection(
		std::vector<openage::gamestate::entity_id_t> new_selected,
		std::optional<openage::gamestate::entity_id_t> new_primary);

	std::shared_ptr<openage::gamestate::GameSimulation> sim_;

	// Sorted ascending by entity id; no duplicates.
	std::vector<openage::gamestate::entity_id_t> selected_;

	// The entity the HUD shows details for. Empty for an
	// empty selection.
	std::optional<openage::gamestate::entity_id_t> primary_;

	std::vector<SelectionChangedCallback> sel_cbs_;
	std::vector<HealthChangedCallback> hp_cbs_;
	std::vector<EntityRemovedCallback> rm_cbs_;
	std::vector<MissionStateChangedCallback> ms_cbs_;
};


} // namespace openage::presenter::viewmodel
