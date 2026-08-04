// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4D.2.1: SelectionOverlay -- the renderer-side view of
// "which entities need a footprint highlight + what colour".
//
// This class is the SINGLE source of overlay state for the
// renderer's selection pass. It subscribes to SelectionModel's
// SelectionChanged event and rebuilds the overlay entry list
// on every change. The renderer queries the entry list at
// draw time; it does NOT maintain its own selection copy.
//
// Overlay colour rules (per spec):
//   - PlayerVillager / TownCenter selected    -> PLAYER_FRIENDLY
//     (green outline in the renderer).
//   - EnemyVillager selected (inspection)     -> PLAYER_HOSTILE
//     (red outline).
//   - Neutral / other factions               -> NEUTRAL
//     (yellow or grey outline).
//   - Unselected entities                    -> no outline.
//
// SelectionOverlay is purely a presentation layer: it does
// NOT mutate GameState, does NOT participate in picking,
// and does NOT change the gameplay collision state.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "gamestate/component/api/footprint.h"
#include "gamestate/types.h"
#include "presenter/viewmodel/selection_model.h"


namespace openage::presenter::viewmodel {


enum class SelectionOverlayColor : uint8_t {
	NONE = 0,        // no overlay
	PLAYER_FRIENDLY, // green
	PLAYER_HOSTILE,  // red
	NEUTRAL,         // yellow or grey
};


/**
 * One overlay entry: the entity id + the colour the
 * renderer should use for the outline. The renderable
 * itself looks up the entity's current Footprint at draw
 * time (overlay geometry must follow entity position).
 */
struct SelectionOverlayEntry {
	openage::gamestate::entity_id_t entity;
	SelectionOverlayColor color;
};


class SelectionOverlay {
public:
	explicit SelectionOverlay(
		const std::shared_ptr<SelectionModel> &model);
	~SelectionOverlay();

	/**
	 * Snapshot of the current overlay set. The renderer
	 * calls this every frame; the result is in stable
	 * entity-id ascending order.
	 */
	const std::vector<SelectionOverlayEntry> &entries() const {
		return this->entries_;
	}

	/**
	 * Quick lookup: is the entity currently in the overlay?
	 */
	bool has(openage::gamestate::entity_id_t entity) const;

	/**
	 * Return the colour for a specific entity (NONE if not
	 * in the overlay).
	 */
	SelectionOverlayColor color_of(
		openage::gamestate::entity_id_t entity) const;

private:
	void rebuild();
	void determine_color(
		openage::gamestate::entity_id_t entity,
		openage::gamestate::player_id_t selected_owner,
		openage::gamestate::player_id_t self_player,
		SelectionOverlayColor &out_color) const;

	std::shared_ptr<SelectionModel> model_;
	std::vector<SelectionOverlayEntry> entries_;
	std::map<openage::gamestate::entity_id_t, SelectionOverlayColor> color_map_;
};


} // namespace openage::presenter::viewmodel
