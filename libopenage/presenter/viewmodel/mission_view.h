// Copyright 2026 the openage authors. See copying.md for legal info.
//
// P4.4A: read-only ViewModel for the mission-level state. Reads the
// TriggerRuntime's MissionState (the single source of truth for
// objective lifecycle and victory). The HUD's objective panel and
// victory panel bind to this; the GUI never polls the simulation
// for victory itself.

#pragma once

#include <string>
#include <vector>


namespace openage::scenario::trigger {
class MissionState;
}


namespace openage::presenter::viewmodel {


struct ObjectiveView {
	std::string id;
	/// "INACTIVE" / "ACTIVE" / "COMPLETED".
	std::string state;
};


class MissionView {
public:
	/**
	 * Build a view from the runtime's MissionState. The test passes
	 * `runtime->mission_state()` directly; the production wiring
	 * (P4.4A step 5) will hand the simulation's TriggerRuntime here.
	 */
	explicit MissionView(
		const openage::scenario::trigger::MissionState &state);

	/// "RUNNING" / "VICTORY" / "DEFEAT".
	std::string lifecycle() const { return this->lifecycle_; }

	bool is_victory() const { return this->lifecycle_ == "VICTORY"; }
	bool is_defeat() const { return this->lifecycle_ == "DEFEAT"; }

	const std::vector<ObjectiveView> &objectives() const { return this->objectives_; }

	/// Reason string the runtime recorded on declare_victory().
	std::string victory_reason() const { return this->victory_reason_; }

private:
	std::string lifecycle_{"RUNNING"};
	std::vector<ObjectiveView> objectives_;
	std::string victory_reason_;
};


} // namespace openage::presenter::viewmodel