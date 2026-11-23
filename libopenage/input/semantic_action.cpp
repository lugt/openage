// Copyright 2026 the openage authors. See copying.md for legal info.

#include "semantic_action.h"

#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"


namespace openage::input {


namespace {


// Look up the owner of `entity` in `state`. Returns nullopt if
// the entity has been removed or has no Ownership component.
std::optional<openage::gamestate::player_id_t>
entity_owner(const std::shared_ptr<openage::gamestate::GameState> &state,
             openage::gamestate::entity_id_t entity, time::time_t now) {
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
	return own->get_owners().get(now);
}


}  // namespace


SemanticAction resolve_context_action(
	const std::shared_ptr<openage::gamestate::GameState> &state,
	const SelectionSnapshot &selection,
	const PickResult &pick,
	openage::gamestate::player_id_t self_player, time::time_t now) {
	SemanticAction out;

	// Pin A: empty selection -> NO_OP. Never degenerate to MOVE.
	if (selection.empty()) {
		out.kind = SemanticActionKind::NO_OP;
		out.reason = "empty_selection";
		return out;
	}

	// Inspectable allied or enemy units can coexist with own units in a
	// selection. Only living entities owned by the issuing player act.
	std::vector<gamestate::entity_id_t> movers;
	std::vector<gamestate::entity_id_t> attackers;
	bool has_owned_entity = false;
	for (const auto &entry : selection.entries) {
		if (entity_owner(state, entry.id, now) != self_player
		    or state->get_game_entity(entry.id)->is_dead()) {
			continue;
		}
		has_owned_entity = true;
		if (entry.has_move_ability) {
			movers.push_back(entry.id);
		}
		if (entry.has_attack_ability) {
			attackers.push_back(entry.id);
		}
	}
	if (not has_owned_entity) {
		out.reason = "selection_hostile_or_missing";
		return out;
	}

	if (pick.has_entity) {
		// ---- Pointer hit an entity ----

		// Self-target: right-clicking a friendly unit is not
		// a no-op MOVE in P4.4D MVP -- it is UNSUPPORTED so
		// the HUD log makes the rejection obvious.
		auto pick_own = entity_owner(state, pick.entity, now);
		if (pick_own.has_value()
		    && *pick_own == self_player) {
			// Right-clicking your own selected villager: same
			// owner as self, so self-target. UNSUPPORTED, not
			// NO_OP, so the diagnostic stream clearly says
			// "self_target" and not "nothing to do".
			out.kind = SemanticActionKind::UNSUPPORTED;
			out.reason = "self_target:entity=" + std::to_string(pick.entity);
			return out;
		}

		if (not pick_own or state->get_stance(self_player, *pick_own) != gamestate::DiplomaticStance::Enemy) {
			out.kind = SemanticActionKind::UNSUPPORTED;
			out.reason = "non_hostile_target:entity=" + std::to_string(pick.entity);
			return out;
		}

		if (attackers.empty()) {
			out.kind = SemanticActionKind::REJECTED;
			out.reason = "no_attack_ability";
			return out;
		}

		// OK: ATTACK.
		out.kind = SemanticActionKind::ATTACK;
		out.target_entity = pick.entity;
		out.actors = std::move(attackers);
		out.reason = "hostile_target:entity=" + std::to_string(pick.entity);
		return out;
	}
	else {
		if (movers.empty()) {
			out.kind = SemanticActionKind::REJECTED;
			out.reason = "no_move_ability";
			return out;
		}
		out.kind = SemanticActionKind::MOVE;
		out.actors = std::move(movers);
		out.world_x = pick.world_x;
		out.world_y = pick.world_y;
		out.reason = "ground_pick";
		return out;
	}
}


} // namespace openage::input
