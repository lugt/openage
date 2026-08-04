// Copyright 2026 the openage authors. See copying.md for legal info.

#include "semantic_action.h"

#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/player.h"
#include "log/log.h"
#include "log/message.h"


namespace openage::input {


namespace {


// Look up the owner of `entity` in `state`. Returns nullopt if
// the entity has been removed or has no Ownership component.
std::optional<openage::gamestate::player_id_t>
entity_owner(const std::shared_ptr<openage::gamestate::GameState> &state,
             openage::gamestate::entity_id_t entity) {
	if (not state->has_game_entity(entity)) {
		return std::nullopt;
	}
	auto e = state->get_game_entity(entity);
	if (not e->has_component(
		    openage::gamestate::component::component_t::OWNERSHIP)) {
		return std::nullopt;
	}
	auto own = std::dynamic_pointer_cast<
		openage::gamestate::component::Ownership>(
		e->get_component(
			openage::gamestate::component::component_t::OWNERSHIP));
	if (not own) {
		return std::nullopt;
	}
	// Ownership is a Discrete<player_id_t> curve; read the
	// current value at the simulation's reference time (the
	// resolver is called from the input layer with the live
	// simulation clock, so the time::time_t::zero() sample is
	// the active owner; the move / attack commands are
	// dispatched at time::time_t::from_int(0) by the input
	// layer so this matches the runtime).
	return own->get_owners().get(openage::time::time_t{0});
}


// Check whether the entity has the Attack ability. The
// component_t::ATTACK component is mounted by EntityFactory from
// the nyan Attack ability; this is the single source of truth.
bool entity_has_attack_ability(
	const std::shared_ptr<openage::gamestate::GameState> &state,
	openage::gamestate::entity_id_t entity) {
	if (not state->has_game_entity(entity)) {
		return false;
	}
	auto e = state->get_game_entity(entity);
	if (not e->has_component(
		    openage::gamestate::component::component_t::ATTACK)) {
		return false;
	}
	auto atk = std::dynamic_pointer_cast<
		openage::gamestate::component::Attack>(
		e->get_component(
			openage::gamestate::component::component_t::ATTACK));
	return atk != nullptr;
}


// Classify the relationship between `self_player` and `other_player`.
// For the MVP we use a simple model: same player == SELF;
// different player == HOSTILE. Diplomacy (ally, neutral) is
// out of scope for P4.4D; P4.4E / P5 will wire Player::diplomacy
// to a proper relation table. We deliberately do NOT call this
// "hostile by default" -- it's "hostile until diplomacy says
// otherwise" and the openage/player IR exposes a placeholder
// for future neutral/ally states.
enum class Relation : uint8_t {
	SELF,
	HOSTILE,
};
Relation relation_of(openage::gamestate::player_id_t self_player,
                     openage::gamestate::player_id_t other_player) {
	if (self_player == other_player) {
		return Relation::SELF;
	}
	return Relation::HOSTILE;
}


}  // namespace


SemanticAction resolve_context_action(
	const std::shared_ptr<openage::gamestate::GameState> &state,
	const SelectionSnapshot &selection,
	const PickResult &pick,
	openage::gamestate::player_id_t self_player) {
	SemanticAction out;

	// Pin A: empty selection -> NO_OP. Never degenerate to MOVE.
	if (selection.empty()) {
		out.kind = SemanticActionKind::NO_OP;
		out.reason = "empty_selection";
		return out;
	}

	// Pin B: no pick at all (e.g. pointer over a GUI panel that
	// swallowed the ray cast). NO_OP.
	if (not pick.has_entity
	    && (pick.world_x == 0.0 && pick.world_y == 0.0)) {
		// Distinguish "no entity pick" from "ground pick at (0,0)".
		// The presenter / picking layer must set has_entity=false
		// + a real world_x/world_y for ground picks; we treat
		// (0,0) + no entity as "missing_pick" defensively. The
		// input controller should not be calling the resolver
		// in this case; this is a guard rail.
		out.kind = SemanticActionKind::NO_OP;
		out.reason = "missing_pick";
		return out;
	}

	// Pin C: every selected entity must be alive (not removed
	// by a removal event between selection capture and click).
	for (const auto &entry : selection.entries) {
		if (not state->has_game_entity(entry.id)) {
			out.kind = SemanticActionKind::NO_OP;
			out.reason = "selection_removed:entity=" + std::to_string(entry.id);
			return out;
		}
	}

	// Pin D: every selected entity must be friendly to self_player
	// (a player cannot issue commands for an enemy's units). If
	// any entry is hostile, drop the whole selection's command.
	for (const auto &entry : selection.entries) {
		auto own = entity_owner(state, entry.id);
		if (not own.has_value()) {
			out.kind = SemanticActionKind::NO_OP;
			out.reason = "selection_no_owner:entity=" + std::to_string(entry.id);
			return out;
		}
		if (relation_of(self_player, *own) == Relation::HOSTILE) {
			out.kind = SemanticActionKind::NO_OP;
			out.reason = "selection_hostile:entity=" + std::to_string(entry.id);
			return out;
		}
	}

	if (pick.has_entity) {
		// ---- Pointer hit an entity ----

		// Self-target: right-clicking a friendly unit is not
		// a no-op MOVE in P4.4D MVP -- it is UNSUPPORTED so
		// the HUD log makes the rejection obvious.
		auto pick_own = entity_owner(state, pick.entity);
		if (pick_own.has_value()
		    && relation_of(self_player, *pick_own) == Relation::SELF) {
			// Right-clicking your own selected villager: same
			// owner as self, so self-target. UNSUPPORTED, not
			// NO_OP, so the diagnostic stream clearly says
			// "self_target" and not "nothing to do".
			out.kind = SemanticActionKind::UNSUPPORTED;
			out.reason = "self_target:entity=" + std::to_string(pick.entity);
			return out;
		}

		// Friendly non-self entity: also UNSUPPORTED for the
		// MVP. (A future patch may add "follow" or "heal"
		// actions on friendlies.)
		if (pick_own.has_value()
		    && relation_of(self_player, *pick_own) == Relation::SELF) {
			// (already handled above; defensive)
			out.kind = SemanticActionKind::UNSUPPORTED;
			out.reason = "friendly_target:entity="
			             + std::to_string(pick.entity);
			return out;
		}

		// Hostile entity: ATTACK if the selection can attack.
		// Check the FIRST entry's Attack ability (a unit without
		// Attack cannot be ordered to attack; reject). A
		// future patch may split per-entry attack eligibility.
		if (not selection.entries.front().has_attack_ability) {
			out.kind = SemanticActionKind::REJECTED;
			out.reason = "no_attack_ability:entity="
			             + std::to_string(selection.entries.front().id);
			return out;
		}

		// OK: ATTACK.
		out.kind = SemanticActionKind::ATTACK;
		out.target_entity = pick.entity;
		out.reason = "hostile_target:entity=" + std::to_string(pick.entity);
		return out;
	}
	else {
		// ---- Pointer hit ground (no entity) ----
		// MOVE iff the selection has at least one entity with
		// a Move ability. Otherwise REJECTED.
		bool has_mover = false;
		for (const auto &entry : selection.entries) {
			if (entry.has_move_ability) {
				has_mover = true;
				break;
			}
		}
		if (not has_mover) {
			out.kind = SemanticActionKind::REJECTED;
			out.reason = "no_move_ability";
			return out;
		}
		out.kind = SemanticActionKind::MOVE;
		out.world_x = pick.world_x;
		out.world_y = pick.world_y;
		out.reason = "ground_pick";
		return out;
	}
}


} // namespace openage::input
