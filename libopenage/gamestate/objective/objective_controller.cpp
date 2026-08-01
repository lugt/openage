// Copyright 2025-2026 the openage authors. See copying.md for legal info.

#include "objective_controller.h"

#include <algorithm>
#include <nyan/nyan.h>

#include "gamestate/game_state.h"
#include "gamestate/player.h"
#include "gamestate/types.h"
#include "log/log.h"
#include "log/message.h"


namespace openage::gamestate {

// ---------------------------------------------------------------------------
// WilliamWallaceMission01ObjectiveController
// ---------------------------------------------------------------------------

WilliamWallaceMission01ObjectiveController::WilliamWallaceMission01ObjectiveController(const std::shared_ptr<GameState> &state) :
	state{state},
	current_idx{0},
	finished{false} {
}

void WilliamWallaceMission01ObjectiveController::on_update(const time::time_t & /* now */) {
	// P4.1: the polling path is removed. P3 demos that call on_update
	// (or the H0.5 GameSimulation::run loop) are now event-driven
	// for any step that registers a removal target. Steps without a
	// removal target retain their legacy polling predicate so the
	// not-yet-event-driven missions (2-5) keep working without a
	// forced migration.
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

	// P4.1: event-driven steps do not poll. If the step registered
	// removal targets, it can only be completed via
	// notify_entity_removed(); the on_update() call is a no-op for
	// that step. Legacy steps without removal targets keep the
	// polling predicate.
	if (not step.removal_targets_remaining.empty()) {
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

void WilliamWallaceMission01ObjectiveController::notify_entity_removed(entity_id_t entity_id) {
	if (this->finished || this->steps.empty()) {
		return;
	}
	if (this->current_idx >= this->steps.size()) {
		this->finished = true;
		return;
	}

	auto &step = this->steps[this->current_idx];
	if (step.completed) {
		// Idempotent: a duplicate removal event for an already-completed
		// step is a silent no-op (matches the contract of the
		// production RemovalEventHandler).
		return;
	}

	// Event-driven step: only act if the step registered removal
	// targets. If the step does not use the event-driven path
	// (empty removal_targets_remaining), this is a no-op.
	if (step.removal_targets_remaining.empty()) {
		return;
	}

	// Find and remove the entity id. Multi-target objectives stay
	// ACTIVE until every target is removed; single-target objectives
	// complete on the first matching removal.
	auto it = std::find(
		step.removal_targets_remaining.begin(),
		step.removal_targets_remaining.end(),
		entity_id);
	if (it == step.removal_targets_remaining.end()) {
		// Removal of a non-target entity: silent no-op.
		return;
	}
	step.removal_targets_remaining.erase(it);

	if (step.removal_targets_remaining.empty()) {
		step.completed = true;
		this->advance();
	}
}

WilliamWallaceMission01ObjectiveController::step_id_t WilliamWallaceMission01ObjectiveController::current_step() const {
	if (this->steps.empty() || this->finished) {
		return 0;
	}
	if (this->current_idx >= this->steps.size()) {
		return 0;
	}
	return this->steps[this->current_idx].id;
}

void WilliamWallaceMission01ObjectiveController::add_step(WilliamWallaceMission01ObjectiveController::step_id_t id,
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
		{},  // no removal_targets; legacy polling path
		false,
	});
}

void WilliamWallaceMission01ObjectiveController::add_event_step(
	WilliamWallaceMission01ObjectiveController::step_id_t id,
	std::string name,
	std::string hint_text,
	std::vector<entity_id_t> targets) {
	// First-registration-wins semantics (matches add_step).
	for (const auto &existing : this->steps) {
		if (existing.id == id) {
			return;
		}
	}

	this->steps.push_back(Step{
		id,
		std::move(name),
		std::move(hint_text),
		nullptr,  // no polling predicate; event-driven only
		std::move(targets),
		false,
	});
}

void WilliamWallaceMission01ObjectiveController::set_current_step(WilliamWallaceMission01ObjectiveController::step_id_t id) {
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

void WilliamWallaceMission01ObjectiveController::mark_completed(WilliamWallaceMission01ObjectiveController::step_id_t id) {
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

const std::string &WilliamWallaceMission01ObjectiveController::get_current_hint() const {
	if (this->steps.empty() || this->finished) {
		return this->empty_hint;
	}
	if (this->current_idx >= this->steps.size()) {
		return this->empty_hint;
	}
	return this->steps[this->current_idx].hint_text;
}

bool WilliamWallaceMission01ObjectiveController::is_finished() const {
	return this->finished;
}

size_t WilliamWallaceMission01ObjectiveController::step_count() const {
	return this->steps.size();
}

void WilliamWallaceMission01ObjectiveController::advance() {
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
// Per-mission objective factories (currently only Mission 1).
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
// Each factory returns a WilliamWallaceMission01ObjectiveController with the steps relevant to its
// mission. Predicates that aren't yet implemented return false so the
// HUD stays on the teaching prompt — the trigger never silently passes
// a step the player hasn't actually completed.
// ---------------------------------------------------------------------------

namespace {

// Balance thresholds per mission. Tweak here rather than inside lambdas
// so the design intent is documented in one place.
constexpr int64_t MISSION_3_MILITIA_TARGET = 3;

// PlacedEntity instance FQONs that are objective targets in each mission.
// These are the PlacedEntity nyan instances declared in the scenario file
// (e.g. assets/nyan/ww_mvp/campaign/william_wallace/mission_01.nyan defines
// `EnemyVillager(engine.util.game_entity.PlacedEntity)`), NOT the
// referenced GameEntity FQON. The scenario loader registers each placed
// instance FQON -> spawned entity ID via GameState::register_named_entity,
// so the trigger resolves the live enemy entity without spawn-order or
// "all movables" assumptions.
constexpr const char *MISSION_1_ENEMY_INSTANCE_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.EnemyVillager";

} // namespace

std::shared_ptr<WilliamWallaceMission01ObjectiveController> make_defeat_enemy_objective(
	const std::shared_ptr<GameState> &state,
	const std::vector<entity_id_t> &target_ids) {

	auto trigger = std::make_shared<WilliamWallaceMission01ObjectiveController>(state);

	// P4.1: snapshot the target set by value (so later mutation of the
	// caller's vector cannot change the predicate) and register it as
	// an event-driven step. The step is completed by the
	// ObjectiveRemovalHandler when a game.removal event fires for any
	// of the target ids. Empty set => never completes (the scenario
	// registered no enemy instance; failing closed prevents a false
	// victory). Removal of a non-target entity does not complete the
	// step because notify_entity_removed() checks the membership
	// explicitly.
	std::vector<entity_id_t> targets = target_ids;
	trigger->add_event_step(
		1,
		"defeat_enemy",
		"Destroy the enemy unit(s). The mission is won when every "
		"objective target is eliminated.",
		std::move(targets));

	return trigger;
}

std::shared_ptr<WilliamWallaceMission01ObjectiveController> create_mission_1_trigger(
	const std::shared_ptr<GameState> &state) {

	// Resolve the named enemy instance(s) to runtime entity IDs via the
	// scenario loader's instance->ID map. If the scenario never spawned
	// the expected instance (authoring drift, wrong scenario loaded), we
	// hand make_defeat_enemy_objective an empty target set and it fails
	// closed -- no false victory -- rather than guessing by spawn order.
	std::vector<entity_id_t> target_ids;
	auto enemy_id = state->get_entity_id_by_instance(MISSION_1_ENEMY_INSTANCE_FQON);
	if (enemy_id.has_value()) {
		target_ids.push_back(enemy_id.value());
	}
	else {
		log::log(MSG(warn) << "create_mission_1_trigger: no entity registered "
		                   << "for instance " << MISSION_1_ENEMY_INSTANCE_FQON
		                   << "; defeat_enemy step will not complete");
	}

	return make_defeat_enemy_objective(state, target_ids);
}

std::shared_ptr<WilliamWallaceMission01ObjectiveController> create_mission_2_trigger(
	const std::shared_ptr<GameState> &state) {

	auto trigger = std::make_shared<WilliamWallaceMission01ObjectiveController>(state);

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

std::shared_ptr<WilliamWallaceMission01ObjectiveController> create_mission_3_trigger(
	const std::shared_ptr<GameState> &state) {

	auto trigger = std::make_shared<WilliamWallaceMission01ObjectiveController>(state);

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

std::shared_ptr<WilliamWallaceMission01ObjectiveController> create_mission_4_trigger(
	const std::shared_ptr<GameState> &state) {

	auto trigger = std::make_shared<WilliamWallaceMission01ObjectiveController>(state);

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

std::shared_ptr<WilliamWallaceMission01ObjectiveController> create_mission_5_trigger(
	const std::shared_ptr<GameState> &state) {

	auto trigger = std::make_shared<WilliamWallaceMission01ObjectiveController>(state);

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

// Backwards-compat wrapper: keep `create_default_mission_objective` so
// any old callers / docs continue to work. It now returns Mission 1.
std::shared_ptr<WilliamWallaceMission01ObjectiveController> create_default_mission_objective(
	const std::shared_ptr<GameState> &state) {
	return create_mission_1_trigger(state);
}

} // namespace openage::gamestate