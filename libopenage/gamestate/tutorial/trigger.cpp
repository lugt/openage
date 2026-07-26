// Copyright 2025-2026 the openage authors. See copying.md for legal info.

#include "trigger.h"

#include <nyan/nyan.h>

#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/player.h"
#include "gamestate/types.h"


namespace openage::gamestate {

// ---------------------------------------------------------------------------
// TutorialTrigger
// ---------------------------------------------------------------------------

TutorialTrigger::TutorialTrigger(const std::shared_ptr<GameState> &state) :
	state{state},
	current_idx{0},
	finished{false} {
}

void TutorialTrigger::on_update(const time::time_t & /* now */) {
	if (this->finished || this->steps.empty()) {
		return;
	}

	// Clamp current_idx in case steps were removed externally (shouldn't
	// happen, but defensive). We never shrink steps from public API today.
	if (this->current_idx >= this->steps.size()) {
		this->finished = true;
		return;
	}

	auto &step = this->steps[this->current_idx];
	if (step.completed) {
		// Already done - just move on. This handles the case where a
		// step was marked_completed() externally; on_update shouldn't
		// re-fire it.
		this->advance();
		return;
	}

	// Poll the predicate. Missing predicates are treated as never-
	// completing - the user spec promises the API compiles even when
	// the engine hasn't wired the underlying query yet.
	bool done = false;
	if (step.check) {
		done = step.check(this->state);
	}

	if (done) {
		step.completed = true;
		this->advance();
	}
}

TutorialTrigger::step_id_t TutorialTrigger::current_step() const {
	if (this->steps.empty() || this->finished) {
		return 0;
	}
	if (this->current_idx >= this->steps.size()) {
		return 0;
	}
	return this->steps[this->current_idx].id;
}

void TutorialTrigger::add_step(TutorialTrigger::step_id_t id,
                               std::string name,
                               std::string hint_text,
                               completion_check_t check) {
	// First-registration-wins semantics: duplicate ids are ignored so
	// the HUD doesn't see a step flip mid-run.
	for (const auto &existing : this->steps) {
		if (existing.id == id) {
			return;
		}
	}

	this->steps.push_back(Step{
		id,
		std::move(name),
		std::move(hint_text),
		std::move(check),
		false,
	});
}

void TutorialTrigger::set_current_step(TutorialTrigger::step_id_t id) {
	for (size_t i = 0; i < this->steps.size(); ++i) {
		if (this->steps[i].id == id) {
			this->current_idx = i;
			// Re-entering a step that was already completed doesn't
			// uncomplete it - we just stop at it until its predicate
			// fires again (which for our tutorial always means the
			// player actually went back and did it).
			this->finished = false;
			return;
		}
	}
	// Unknown id: stay put. Log here in the future if we ever add a
	// log handle to the trigger.
}

void TutorialTrigger::mark_completed(TutorialTrigger::step_id_t id) {
	if (this->finished || this->steps.empty()) {
		return;
	}

	// Only the current step can be externally completed - completing a
	// future step silently does nothing. That matches the linear
	// scenario shape: we never want the trigger to skip ahead based on
	// a scripted event that fires out of order.
	if (this->steps[this->current_idx].id != id) {
		return;
	}

	this->steps[this->current_idx].completed = true;
	this->advance();
}

const std::string &TutorialTrigger::get_current_hint() const {
	if (this->steps.empty() || this->finished) {
		return this->empty_hint;
	}
	if (this->current_idx >= this->steps.size()) {
		return this->empty_hint;
	}
	return this->steps[this->current_idx].hint_text;
}

bool TutorialTrigger::is_finished() const {
	return this->finished;
}

size_t TutorialTrigger::step_count() const {
	return this->steps.size();
}

void TutorialTrigger::advance() {
	if (this->steps.empty()) {
		this->finished = true;
		return;
	}

	// Skip past already-completed steps defensively (e.g. if a step
	// was completed out of band). This keeps current_step() consistent
	// with the first uncompleted step.
	size_t next = this->current_idx + 1;
	while (next < this->steps.size() && this->steps[next].completed) {
		++next;
	}

	if (next >= this->steps.size()) {
		this->current_idx = this->steps.size() - 1;
		this->finished = true;
		return;
	}

	this->current_idx = next;
	this->finished = false;
}

// ---------------------------------------------------------------------------
// Per-mission tutorial triggers.
//
// The 5-mission William Wallace Learning Campaign is shipped one mission
// at a time as the corresponding Phase C capability lands:
//
//   Mission 1 - "First Steps"            : gather wood       (Phase B)
//   Mission 2 - "Build a Village"        : house + food      (Phase C2/C1)
//   Mission 3 - "Train an Army"          : barracks + train  (Phase C3)
//   Mission 4 - "First Strike"           : combat + scripted (Phase C4)
//   Mission 5 - "The Battle of Stirling" : archer range + multi-enemy TC (Phase C3+C4)
//
// Each factory returns a TutorialTrigger with the steps relevant to its
// mission. Predicates that aren't yet implemented return false so the
// HUD stays on the teaching prompt — the trigger never silently passes
// a step the player hasn't actually completed.
// ---------------------------------------------------------------------------

namespace {

// Balance thresholds per mission. Tweak here rather than inside lambdas
// so the design intent is documented in one place.
constexpr int64_t MISSION_3_MILITIA_TARGET = 3;

} // namespace

std::shared_ptr<TutorialTrigger> create_mission_1_trigger(
	const std::shared_ptr<GameState> &state) {

	auto trigger = std::make_shared<TutorialTrigger>(state);

	// Combat vertical slice (increment 6): the mission is won when the
	// scripted enemy villager is destroyed. The tutorial scenario is
	// single-player with every initial entity owned by player 0 (game.cpp
	// gives PlacedEntity no owner member), so the enemy cannot be told
	// apart by ownership. Instead we snapshot the movable entities at
	// trigger creation -- the scenario spawns exactly two (the player
	// villager and the enemy villager) -- and the step completes as soon
	// as one of them is gone from the index. The player villager never
	// dies in this slice (the enemy does not fight back), so a missing
	// movable unambiguously means the enemy was killed.
	std::vector<entity_id_t> movable_ids;
	for (const auto &[eid, ent] : state->get_game_entities()) {
		if (ent->has_component(component::component_t::MOVE)) {
			movable_ids.push_back(eid);
		}
	}

	trigger->add_step(
		1,
		"defeat_enemy",
		"Select your villager, press A and click the enemy villager. "
		"The mission is won when the enemy is destroyed.",
		[movable_ids](const std::shared_ptr<GameState> &s) -> bool {
			for (auto eid : movable_ids) {
				if (not s->has_game_entity(eid)) {
					return true;
				}
			}
			return false;
		});

	return trigger;
}

std::shared_ptr<TutorialTrigger> create_mission_2_trigger(
	const std::shared_ptr<GameState> &state) {

	auto trigger = std::make_shared<TutorialTrigger>(state);

	// Step 1 - build a house. Wired once Phase C2 (build chain) lands.
	trigger->add_step(
		1,
		"build_house",
		"Build a house near the town center to raise the population "
		"limit. Houses cost 25 wood.",
		[state](const std::shared_ptr<GameState> & /* s */) -> bool {
			// TODO Phase C2: iterate get_game_entities(), filter by
			// owner==0 and nyan type "House".
			(void)state;
			return false;
		});

	// Step 2 - gather food from a bush. Wired once Phase C1 (gather chain)
	// lands — needs the resource_spot + villager activity chain.
	trigger->add_step(
		2,
		"gather_food",
		"Build a mill on a forage bush, then send a villager to gather "
		"food from the bush.",
		[state](const std::shared_ptr<GameState> & /* s */) -> bool {
			// TODO Phase C1: detect "villager currently gathering
			// food" activity OR player food > 0.
			(void)state;
			return false;
		});

	return trigger;
}

std::shared_ptr<TutorialTrigger> create_mission_3_trigger(
	const std::shared_ptr<GameState> &state) {

	auto trigger = std::make_shared<TutorialTrigger>(state);

	// Step 1 - train militia. Wired once Phase C3 (train chain) lands.
	trigger->add_step(
		1,
		"train_militia",
		"Select the barracks and press T to train militia. Train at "
		"least " + std::to_string(MISSION_3_MILITIA_TARGET) + ".",
		[state](const std::shared_ptr<GameState> & /* s */) -> bool {
			// TODO Phase C3: count owner==0 entities with nyan type
			// "Militia" (or its tutorial placeholder equivalent).
			(void)state;
			return false;
		});

	return trigger;
}

std::shared_ptr<TutorialTrigger> create_mission_4_trigger(
	const std::shared_ptr<GameState> &state) {

	auto trigger = std::make_shared<TutorialTrigger>(state);

	// Step 1 - defeat the scripted enemy villager. Wired once Phase C4
	// (combat + scripted enemy system) lands.
	trigger->add_step(
		1,
		"defeat_enemy",
		"Select a militia, press A and click the enemy villager. The "
		"mission completes when the enemy is destroyed.",
		[state](const std::shared_ptr<GameState> & /* s */) -> bool {
			// TODO Phase C4: count alive entities with owner!=0. The
			// scripted enemy system can also call mark_completed(1)
			// when the enemy HP reaches 0.
			(void)state;
			return false;
		});

	return trigger;
}

std::shared_ptr<TutorialTrigger> create_mission_5_trigger(
	const std::shared_ptr<GameState> &state) {

	auto trigger = std::make_shared<TutorialTrigger>(state);

	// Step 1 - build an archery range. Wired once Phase C2 (build chain)
	// can place a non-barracks military building.
	trigger->add_step(
		1,
		"build_archer_range",
		"Build an Archery Range to unlock archer training.",
		[state](const std::shared_ptr<GameState> & /* s */) -> bool {
			// TODO Phase C2: filter owner==0 entities by nyan type
			// "ArcheryRange".
			(void)state;
			return false;
		});

	// Step 2 - defeat the enemy army.
	trigger->add_step(
		2,
		"defeat_enemy_army",
		"Use militia + archers to defeat all enemy military units.",
		[state](const std::shared_ptr<GameState> & /* s */) -> bool {
			// TODO Phase C4: count alive entities with owner!=0 and
			// military type. Needs scripted enemy spawn loop.
			(void)state;
			return false;
		});

	// Step 3 - destroy the enemy town center.
	trigger->add_step(
		3,
		"destroy_enemy_tc",
		"Move your army to the enemy base and destroy their town center.",
		[state](const std::shared_ptr<GameState> & /* s */) -> bool {
			// TODO Phase C4: count alive enemy TC entities.
			(void)state;
			return false;
		});

	return trigger;
}

// Backwards-compat wrapper: keep `create_default_tutorial_trigger` so
// any old callers / docs continue to work. It now returns Mission 1.
std::shared_ptr<TutorialTrigger> create_default_tutorial_trigger(
	const std::shared_ptr<GameState> &state) {
	return create_mission_1_trigger(state);
}

} // namespace openage::gamestate