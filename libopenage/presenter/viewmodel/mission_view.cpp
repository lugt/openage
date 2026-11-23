// Copyright 2026 the openage authors. See copying.md for legal info.

#include "presenter/viewmodel/mission_view.h"

#include "scenario/trigger/mission_state.h"


namespace openage::presenter::viewmodel {


MissionView::MissionView(
	const openage::scenario::trigger::MissionState &state) {

	if (state.is_victory()) {
		this->lifecycle_ = "VICTORY";
	}
	else {
		// MissionLifecycle enum: RUNNING=0, VICTORY=1, DEFEAT=2.
		// We expose the string for QML/HUD binding. Defeat is not
		// declared by any current effect, but the mapping is here
		// so the HUD's red defeat panel can light up when the
		// runtime adds DeclareDefeat.
		auto raw = static_cast<int>(state.lifecycle());
		if (raw == 2) {
			this->lifecycle_ = "DEFEAT";
		}
		else {
			this->lifecycle_ = "RUNNING";
		}
	}
	this->victory_reason_ = state.victory_reason();

	for (const auto &id : state.registered_objectives()) {
		auto s = state.objective_state(id);
		std::string state_str;
		switch (static_cast<int>(s)) {
		case 0: state_str = "INACTIVE"; break;
		case 1: state_str = "ACTIVE"; break;
		case 2: state_str = "COMPLETED"; break;
		default: state_str = "UNKNOWN"; break;
		}
		ObjectiveView v;
		v.id = id;
		v.state = state_str;
		this->objectives_.push_back(v);
	}
}


} // namespace openage::presenter::viewmodel