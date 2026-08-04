// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4D.4: SemanticActionResolver -- the SINGLE source of truth for
// "what does a right-click mean?". Decoupled from Qt, the
// simulation, and the presenter.
//
// Inputs:
//   - SelectionSnapshot: the currently selected entity ids (from
//     SelectionModel) plus their live ownership / ability info.
//   - PickResult: the world coord the pointer resolves to, plus
//     the entity id at that coord (if any).
//   - WorldPosition: optional explicit world target for the case
//     where the pick returns "no entity" (right-click on empty
//     ground). PickResult already carries this, but the resolver
//     treats "ground" as a separate case for clarity.
//
// Outputs:
//   - SemanticAction: a tag-only union of MOVE / ATTACK / NO_OP
//     / UNSUPPORTED / REJECTED with stable strings for logging
//     and diagnostics.
//
// Hard invariants:
//   - The resolver NEVER consults FQON strings, display names, or
//     entity class hierarchies to decide friendly/hostile. The
//     only signal is owner/player/diplomacy data on the entity
//     (Player::owns / Player::is_hostile).
//   - The resolver NEVER degenerates ATTACK into MOVE. If
//     ATTACK is rejected (no Attack ability, target not hostile,
//     target removed, self target), the resolver returns a
//     non-ATTACK action (NO_OP / UNSUPPORTED / REJECTED), not
//     a MOVE.
//   - The resolver does NOT mutate state. It is a pure
//     decision function over its inputs.

#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gamestate/types.h"


namespace openage::gamestate {
class GameState;
class GameEntity;
}


namespace openage::input {


/**
 * Read-only snapshot of the current selection as the resolver
 * sees it. Filled by the presenter from SelectionModel just
 * before each resolve_context_action() call; passed by const
 * reference to keep the resolver pure.
 *
 * The snapshot carries the Live ownership/ability info per
 * selected entity so the resolver can decide whether each
 * selected unit can issue MOVE / ATTACK without having to
 * re-query the simulation.
 */
struct SelectionSnapshot {
	/**
	 * One selected entity, with the metadata the resolver needs.
	 */
	struct Entry {
		openage::gamestate::entity_id_t id{0};
		openage::gamestate::player_id_t owner{0};
		bool has_move_ability{false};
		bool has_attack_ability{false};
	};

	std::vector<Entry> entries;

	bool empty() const { return this->entries.empty(); }
};


/**
 * Result of a single pick operation. The presenter fills this
 * in by combining the world coords from coord::picking with
 * the entity-under-coord query.
 */
struct PickResult {
	/**
	 * True iff the pick resolved to a real entity (entity != 0).
	 * False means the pick hit empty ground at `world_*`.
	 */
	bool has_entity{false};
	openage::gamestate::entity_id_t entity{0};
	openage::gamestate::player_id_t entity_owner{0};

	/**
	 * World position the pointer resolved to. For "ground" picks
	 * this is the cell center on the terrain plane; for
	 * "entity" picks it is the entity's center tile. The
	 * Move / Attack runtime converts this to phys3 before
	 * dispatching a command.
	 */
	double world_x{0.0};
	double world_y{0.0};
};


/**
 * Stable enum for the resolved action. The runtime branches
 * on this tag and never on FQON or display strings.
 */
enum class SemanticActionKind : uint8_t {
	NO_OP = 0,       // no selection, nothing to do
	UNSUPPORTED,     // action shape is rejected for this MVP
	REJECTED,        // action shape is correct but state refuses
	MOVE,            // MoveCommand to a ground position
	ATTACK,          // AttackCommand to a hostile entity
};


/**
 * Tagged result. The `target_entity` is the entity the action
 * is bound to (move: none; attack: the hostile entity).
 */
struct SemanticAction {
	SemanticActionKind kind{SemanticActionKind::NO_OP};

	/**
	 * For MOVE: the world position to move to. For ATTACK:
	 * the entity id of the target.
	 */
	std::optional<double> world_x;
	std::optional<double> world_y;
	std::optional<openage::gamestate::entity_id_t> target_entity;

	/**
	 * Stable diagnostic string. Used by the HUD log + demo
	 * pins to record WHY the resolver chose what it chose.
	 * Examples: "empty_selection", "no_attack_ability",
	 * "self_target", "not_hostile", "missing_pick".
	 */
	std::string reason;

	std::string kind_string() const {
		switch (this->kind) {
		case SemanticActionKind::NO_OP: return "NO_OP";
		case SemanticActionKind::UNSUPPORTED: return "UNSUPPORTED";
		case SemanticActionKind::REJECTED: return "REJECTED";
		case SemanticActionKind::MOVE: return "MOVE";
		case SemanticActionKind::ATTACK: return "ATTACK";
		}
		return "UNKNOWN";
	}
};


/**
 * Resolve a right-click (or "current command" in HUD mode) into
 * a SemanticAction. Pure function. Does not mutate state.
 *
 * The presenter / input controller is responsible for:
 *   1. building a SelectionSnapshot from the current
 *      SelectionModel
 *   2. building a PickResult from coord::picking
 *   3. calling resolve_context_action
 *   4. dispatching the result to Commander
 *
 * @param state       The current GameState. Used to look up
 *                    ownership/ability/metadata for the
 *                    selection and the picked entity.
 * @param selection   Currently selected entities + their
 *                    ownership/ability metadata.
 * @param pick        The world-coord pick (entity or ground).
 * @param self_player The player_id of the local user; used
 *                    for the friendly/hostile classification
 *                    (Player::is_hostile / Player::is_ally).
 */
SemanticAction resolve_context_action(
	const std::shared_ptr<openage::gamestate::GameState> &state,
	const SelectionSnapshot &selection,
	const PickResult &pick,
	openage::gamestate::player_id_t self_player);


} // namespace openage::input
