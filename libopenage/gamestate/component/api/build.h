// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <string>
#include <optional>
#include <utility>

#include "gamestate/component/api_component.h"
#include "gamestate/component/types.h"
#include "gamestate/types.h"


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
	void set_progress(int64_t progress) { this->progress = progress; }
	std::string get_construction_animation() const;
	bool ordered_by_player = false;
	std::optional<std::pair<entity_id_t, uint64_t>> assignment;

private:
	int64_t progress = 100;
};

} // namespace openage::gamestate::component
