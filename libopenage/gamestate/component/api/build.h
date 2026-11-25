// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "gamestate/component/api_component.h"
#include "gamestate/component/types.h"
#include "gamestate/types.h"
#include "time/time.h"


namespace openage::gamestate::component {

/**
 * Tracks construction progress for placed buildings. Completed buildings
 * start at 100 percent unless their scenario placement schedules construction.
 */
class Build final : public APIComponent {
public:
	using APIComponent::APIComponent;

	component_t get_type() const override;
	bool is_under_construction() const { return this->progress < 100; }
	int64_t get_progress() const { return this->progress; }
	void set_progress(int64_t progress) {
		this->progress = progress;
		this->exact_progress = static_cast<double>(progress);
	}
	std::string get_construction_animation() const;
	bool ordered_by_player = false;
	std::map<entity_id_t, uint64_t> assignments;
	std::set<entity_id_t> active_builders;
	double exact_progress = 100.0;
	double build_seconds = 0.0;
	time::time_t last_update{};
	uint64_t schedule_gen{0};

private:
	int64_t progress = 100;
};

} // namespace openage::gamestate::component
