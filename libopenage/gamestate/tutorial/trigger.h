// Copyright 2025-2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gamestate/types.h"
#include "time/time.h"


namespace openage::gamestate {

class GameState;

/**
 * TutorialTrigger
 *
 * Lightweight state machine that drives a linear tutorial scenario by
 * checking one step at a time. Each step has a completion predicate
 * (closure) that the trigger evaluates on every \p on_update() call.
 * When the current step is satisfied, the trigger advances to the next
 * step; after the last step, the trigger is considered finished.
 *
 * The trigger is intentionally engine-agnostic: it does not run on a
 * timer itself, it relies on the caller (GameSimulation or HUD tick)
 * to feed it the current time. Steps are immutable once added; if the
 * scenario needs to reset (e.g. after a debug reload) the caller
 * constructs a new TutorialTrigger.
 *
 * Typical use:
 *
 *     auto trigger = create_default_tutorial_trigger(state);
 *     // every tick
 *     trigger->on_update(now);
 *     // HUD reads
 *     hud_label = trigger->get_current_hint();
 *     hud_progress = trigger->current_step();
 */
class TutorialTrigger : public std::enable_shared_from_this<TutorialTrigger> {
public:
	/**
	 * Tutorial step identifier. Each step in a tutorial scenario is
	 * assigned a unique id by the scenario author; ids do not have to
	 * be sequential (gaps are allowed for editorial reasons).
	 */
	using step_id_t = uint32_t;

	/**
	 * Completion predicate for a step. Receives the shared game state
	 * and returns true when the step's objective is met. The closure
	 * must be safe to call repeatedly: on_update polls the predicate
	 * each tick until it returns true.
	 */
	using completion_check_t = std::function<bool(const std::shared_ptr<GameState> &)>;

	/**
	 * Create a tutorial trigger for the given game state.
	 *
	 * The trigger starts with no steps and is_finished()==true until
	 * at least one step is registered via \p add_step().
	 *
	 * @param state Game state the trigger queries each tick.
	 */
	explicit TutorialTrigger(const std::shared_ptr<GameState> &state);

	~TutorialTrigger() = default;

	/**
	 * Tick the state machine. Evaluates the current step's completion
	 * predicate; if it returns true, advances to the next step. The
	 * \p now parameter is unused today but reserved for future time-
	 * gated steps (e.g. "complete within 60 seconds").
	 *
	 * @param now Current simulation time.
	 */
	void on_update(const time::time_t &now);

	/**
	 * Get the id of the step the trigger is currently waiting on.
	 *
	 * Returns 0 when no steps are registered or the scenario has been
	 * completed (use \p is_finished() to distinguish). The HUD uses
	 * this to render "Step X / Y" progress.
	 *
	 * @return Current step id, or 0 if none.
	 */
	step_id_t current_step() const;

	/**
	 * Register a new step in the scenario. Steps are appended in
	 * insertion order; the trigger walks them sequentially. Adding a
	 * step whose id already exists is a no-op for that id (the first
	 * registration wins) - we silently keep the original to avoid
	 * surprising the HUD mid-run.
	 *
	 * @param id Unique step id.
	 * @param name Short identifier (for logging / debug).
	 * @param hint_text Hint shown in the HUD while this step is active.
	 * @param check Completion predicate; evaluated each tick.
	 */
	void add_step(step_id_t id,
	              std::string name,
	              std::string hint_text,
	              completion_check_t check);

	/**
	 * Jump to a specific step id. Used for scripted replays and for
	 * future save-game resume. If the id is unknown, the trigger stays
	 * where it is.
	 *
	 * @param id Step id to activate.
	 */
	void set_current_step(step_id_t id);

	/**
	 * Externally mark a step as completed and advance past it. Used
	 * for steps that are not polllable (e.g. a scripted enemy dying
	 * once the player issues a kill command). If the id does not
	 * match the current step, this is a no-op.
	 *
	 * @param id Step id to mark complete.
	 */
	void mark_completed(step_id_t id);

	/**
	 * Get the hint text for the current step. The reference is stable
	 * until the step changes; if the HUD caches it across ticks it
	 * must re-read on step transitions.
	 *
	 * @return Hint string; empty when finished or before any steps.
	 */
	const std::string &get_current_hint() const;

	/**
	 * Whether the scenario has run to completion. Once true, the
	 * trigger stops advancing and \p current_step() keeps returning
	 * the id of the last step.
	 *
	 * @return true iff the final step has been completed.
	 */
	bool is_finished() const;

	/**
	 * Total number of registered steps. Used by the HUD progress bar
	 * to render "step X / total".
	 *
	 * @return Number of steps added so far.
	 */
	size_t step_count() const;

private:
	/**
	 * One entry in the linear scenario. \p check is the predicate the
	 * trigger polls each tick; \p completed is flipped on first
	 * satisfaction (or via \p mark_completed) and prevents double-
	 * advancing if \p check returns true repeatedly.
	 */
	struct Step {
		step_id_t id;
		std::string name;
		std::string hint_text;
		completion_check_t check;
		bool completed = false;
	};

	/**
	 * Move the cursor to the next uncompleted step. Wraps \p current_idx
	 * and updates \p finished. No-op if no more steps remain.
	 */
	void advance();

	/**
	 * Game state used to evaluate completion predicates.
	 */
	std::shared_ptr<GameState> state;

	/**
	 * Registered steps in scenario order. Append-only via add_step.
	 */
	std::vector<Step> steps;

	/**
	 * Index of the active step inside \p steps. Meaningful only when
	 * \p steps is non-empty and \p finished is false.
	 */
	size_t current_idx;

	/**
	 * True once the last step has been marked completed.
	 */
	bool finished;

	/**
	 * Empty hint returned when no current step exists. Lives on the
	 * object so we can return a stable reference from
	 * \p get_current_hint() in the empty / finished case.
	 */
	std::string empty_hint;
};

/**
 * Build a trigger whose single step completes only when every entity in
 * \p target_ids has been removed from the state index (i.e. killed).
 *
 * This is the enemy-specific victory primitive: the step fires iff the
 * objective target set is non-empty AND every target is gone. An empty
 * target set never completes (no false victory when the scenario has no
 * registered enemy instance). A non-target entity dying (player unit,
 * bystander) does not affect the predicate because only \p target_ids
 * are polled.
 *
 * Exposed so mission factories can resolve named instances to IDs and
 * so tests can construct multi-target triggers directly.
 *
 * @param state Game state the trigger queries each tick.
 * @param target_ids Entity IDs that must all be removed for victory.
 * @return Shared trigger with one "defeat_enemy" step.
 */
std::shared_ptr<TutorialTrigger> make_defeat_enemy_trigger(
	const std::shared_ptr<GameState> &state,
	const std::vector<entity_id_t> &target_ids);

/**
 * Per-mission tutorial trigger factories.
 *
 * The William Wallace Learning Campaign ships as 5 missions; each one
 * adds a single new mechanic to what the player has already learned.
 * Each mission therefore gets its own factory function that registers
 * exactly the steps needed to validate that mission's objective.
 *
 * As Phase C (gather / build / train / combat) lands, the corresponding
 * mission factories get their predicates wired. Today only Mission 1
 * is fully playable; the others return a trigger whose first step is
 * a "mission not implemented" placeholder so the HUD can still show
 * progress without crashing.
 *
 * @param state Game state the trigger queries each tick.
 * @return Shared trigger ready to be ticked.
 */
std::shared_ptr<TutorialTrigger> create_mission_1_trigger(
	const std::shared_ptr<GameState> &state);

std::shared_ptr<TutorialTrigger> create_mission_2_trigger(
	const std::shared_ptr<GameState> &state);

std::shared_ptr<TutorialTrigger> create_mission_3_trigger(
	const std::shared_ptr<GameState> &state);

std::shared_ptr<TutorialTrigger> create_mission_4_trigger(
	const std::shared_ptr<GameState> &state);

std::shared_ptr<TutorialTrigger> create_mission_5_trigger(
	const std::shared_ptr<GameState> &state);

/**
 * @deprecated Kept as a thin wrapper for any old callers that still
 * reference it. New code should use \p create_mission_1_trigger.
 */
std::shared_ptr<TutorialTrigger> create_default_tutorial_trigger(
	const std::shared_ptr<GameState> &state);

} // namespace openage::gamestate