// Copyright 2026 the openage authors. See copying.md for legal info.

#include "build.h"

#include "gamestate/api/animation.h"

#include "gamestate/component/types.h"


namespace openage::gamestate::component {

component_t Build::get_type() const {
	return component_t::BUILD;
}

std::string Build::get_construction_animation() const {
	const auto &ability = this->get_ability();
	if (not this->is_under_construction() or not ability.extends("engine.ability.type.Constructable")) {
		return {};
	}

	auto view = ability.get_view();
	auto objects = [&](const nyan::Object &obj, const char *member) {
		std::vector<nyan::Object> result;
		for (const auto &value : obj.get_set(member)) {
			auto ref = std::dynamic_pointer_cast<nyan::ObjectValue>(value.get_ptr());
			result.push_back(view->get_object(ref->get_name()));
		}
		return result;
	};
	for (const auto &stage : objects(ability, "Constructable.construction_progress")) {
		const auto left = stage.get_float("Progress.left_boundary");
		const auto right = stage.get_float("Progress.right_boundary");
		if (this->progress < left or (this->progress >= right and left != right)) {
			continue;
		}
		if (left == right and this->progress != left) {
			continue;
		}
		for (const auto &[type, value] : stage.get_dict("Progress.properties")) {
			auto ref = std::dynamic_pointer_cast<nyan::ObjectValue>(value.get_ptr());
			auto property = stage.get_view()->get_object(ref->get_name());
			if (not property.extends("engine.util.progress.property.type.Animated")) {
				continue;
			}
			for (const auto &override : objects(property, "Animated.overrides")) {
				if (not override.get_object("AnimationOverride.ability").extends("engine.ability.type.Idle")) {
					continue;
				}
				auto animations = objects(override, "AnimationOverride.animations");
				if (not animations.empty()) {
					return api::APIAnimation::get_animation_path(animations.front());
				}
			}
		}
	}
	return {};
}

} // namespace openage::gamestate::component
