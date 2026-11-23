// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.2 trigger runtime: mission / objective / victory state.
//
// The MissionState is the central object that records the
// current mission-level state (RUNNING / VICTORY / DEFEAT),
// the per-objective state (INACTIVE / ACTIVE / COMPLETED), and
// the methods that the SetObjectiveState and DeclareVictory
// event handlers invoke to mutate that state.
//
// MissionState is owned by the GameSimulation (or the
// Scenario subsystem that the simulation delegates to). The
// trigger runtime talks to it ONLY through the
// SetObjectiveStateHandler and DeclareVictoryHandler event
// handlers -- direct mutation from a trigger is forbidden
// (the spec says "DeclareVictoryEffect 不负责 UI，只修改
// simulation mission state 并发布语义事件"; the effect
// publishes the event, the handler mutates the state).
//
// The objective state is stored as a map from objective_id
// (string) to ObjectiveState. The mission state is a single
// enum. A future spike can replace the map with a nyan
// Curve-backed value if persistent time-series state is
// needed; for now a plain std::unordered_map is enough.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>


namespace openage::gamestate {
class GameState;
}


namespace openage::scenario::trigger {


/**
 * Per-objective state machine. A trigger's
 * SetObjectiveStateEffect moves an objective through
 * INACTIVE -> ACTIVE -> COMPLETED.
 */
enum class ObjectiveState : uint8_t {
	INACTIVE = 0,
	ACTIVE = 1,
	COMPLETED = 2,
};


/**
 * Mission-level state machine. RUNNING is the initial state
 * after the Scenario loader wires the runtime. VICTORY is
 * entered when DeclareVictoryEffect's handler fires. DEFEAT
 * is reserved for a future "fail" trigger (out of scope for
 * P4.2).
 */
enum class MissionLifecycle : uint8_t {
	RUNNING = 0,
	VICTORY = 1,
	DEFEAT = 2,
};


class MissionState {
public:
	MissionState() = default;
	~MissionState() = default;

	// --- Objective state -------------------------------------------------

	/**
	 * Initialize an objective in INACTIVE state. Called by
	 * the loader at scenario setup. Existing entries are
	 * overwritten (a re-init is a re-init).
	 */
	void init_objective(const std::string &objective_id);

	/**
	 * Set the state of an objective. The handler uses this
	 * when a SetObjectiveStateEffect's event is delivered.
	 * Returns true if the transition was applied; false if
	 * the objective_id is unknown (the handler logs the
	 * miss and the trigger runtime records the diagnostic).
	 */
	bool set_objective_state(
		const std::string &objective_id,
		ObjectiveState new_state);

	/**
	 * Read the state of an objective. Returns the current
	 * state, or INACTIVE if the objective_id is unknown.
	 */
	ObjectiveState objective_state(
		const std::string &objective_id) const;

	bool is_objective_completed(const std::string &objective_id) const {
		return this->objective_state(objective_id) ==
		       ObjectiveState::COMPLETED;
	}

	/**
	 * Return the set of objective ids the mission has
	 * registered. Used by HUD / save-game paths; out of
	 * scope for P4.2 but kept for forward compatibility.
	 */
	std::vector<std::string> registered_objectives() const;

	// --- Mission lifecycle ----------------------------------------------

    void set_instruction(std::string text) { this->instruction_ = std::move(text); this->notify_progress(); }
    const std::string &instruction() const { return this->instruction_; }

	MissionLifecycle lifecycle() const { return this->lifecycle_; }

	bool is_victory() const {
		return this->lifecycle_ == MissionLifecycle::VICTORY;
	}

	/**
	 * Declare victory. The handler invokes this when a
	 * DeclareVictoryEffect's event is delivered. The
	 * transition is one-way; once VICTORY, always VICTORY.
	 * A future "fail" trigger can transition to DEFEAT
	 * before the first VICTORY; after VICTORY the state is
	 * frozen.
	 */
	void declare_victory(const std::string &reason);

	/**
	 * Read the reason of the latest victory declaration.
	 * Empty if no victory has been declared. The handler
	 * uses this to populate MissionCompletedEvent params.
	 */
	const std::string &victory_reason() const { return this->victory_reason_; }

	// --- Change notification (P4.4A) ------------------------------------

	/**
	 * Set a callback fired exactly once when declare_victory is
	 * called for the first time (lifecycle transition
	 * RUNNING/DEFEAT -> VICTORY). Subsequent calls are no-ops.
	 *
	 * Used by the ViewModel layer to push the MissionStateChanged
	 * event to the HUD without polling.
	 */
	void set_victory_listener(std::function<void(const std::string &reason)> cb) {
		this->victory_listener_ = std::move(cb);
	}

	void notify_progress() {
		if (this->objective_listener_) {
			this->objective_listener_();
		}
	}

	void set_objective_listener(std::function<void()> cb) {
		this->objective_listener_ = std::move(cb);
	}

private:
	std::unordered_map<std::string, ObjectiveState> objectives_;
	MissionLifecycle lifecycle_{MissionLifecycle::RUNNING};
	std::string victory_reason_;
    std::string instruction_;
	std::function<void(const std::string &reason)> victory_listener_;
	std::function<void()> objective_listener_;
};


}  // namespace openage::scenario::trigger
