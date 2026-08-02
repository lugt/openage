// Copyright 2026 the openage authors. See copying.md for legal info.

#include "scenario/trigger/mission_state.h"

#include <vector>

#include "log/log.h"
#include "log/message.h"


namespace openage::scenario::trigger {


void MissionState::init_objective(const std::string &objective_id) {
	this->objectives_[objective_id] = ObjectiveState::INACTIVE;
}


bool MissionState::set_objective_state(
	const std::string &objective_id,
	ObjectiveState new_state) {

	auto it = this->objectives_.find(objective_id);
	if (it == this->objectives_.end()) {
		// The SetObjectiveState handler treats an
		// unknown objective_id as a contract violation --
		// the trigger referenced an objective that the
		// loader did not register. Log the miss and
		// refuse the transition. The handler does NOT
		// silently no-op; the spec explicitly forbids
		// silent no-op on required-lookup failure.
		log::log(MSG(err) << "MissionState::set_objective_state: "
		                   << "unknown objective_id '" << objective_id
		                   << "' (registered objectives: "
		                   << this->objectives_.size() << ")");
		return false;
	}
	it->second = new_state;
	return true;
}


ObjectiveState MissionState::objective_state(
	const std::string &objective_id) const {

	auto it = this->objectives_.find(objective_id);
	if (it == this->objectives_.end()) {
		return ObjectiveState::INACTIVE;
	}
	return it->second;
}


std::vector<std::string> MissionState::registered_objectives() const {
	std::vector<std::string> out;
	out.reserve(this->objectives_.size());
	for (auto &kv : this->objectives_) {
		out.push_back(kv.first);
	}
	return out;
}


void MissionState::declare_victory(const std::string &reason) {
	if (this->lifecycle_ == MissionLifecycle::VICTORY) {
		// A duplicate declare_victory is a no-op. The
		// spec says "completion 幂等"; a victory is a
		// terminal state.
		return;
	}
	this->lifecycle_ = MissionLifecycle::VICTORY;
	this->victory_reason_ = reason;
	log::log(MSG(info) << "MissionState::declare_victory: reason='"
	                    << reason << "' lifecycle=VICTORY");
}


}  // namespace openage::scenario::trigger
