// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#include "animation.h"

#include <deque>

#include "gamestate/api/util.h"
#include "gamestate/api/ability.h"
#include "gamestate/api/property.h"
#include "gamestate/api/types.h"

import nyan;


namespace openage::gamestate::api {

bool APIAnimation::is_animation(nyan::Object &obj) {
	nyan::fqon_t immediate_parent = obj.get_parents()[0];
	return immediate_parent == "engine.ability.property.Property";
}

const std::string APIAnimation::get_animation_path(const nyan::Object &animation) {
	auto path = animation.get_file("Animation.sprite");
	const std::string shared_prefix = "../shared/graphics/";
	if (path.starts_with(shared_prefix)) {
		// Legacy converted sprites use a fixed relative path even when the
		// referencing unit is nested below game_entity/generic.
		const auto dir = animation.get_info().get_namespace().to_dirpath();
		const std::string root = "/game_entity/";
		auto location = dir.find(root);
		if (location != std::string::npos) {
			return dir.substr(0, location + root.size()) + path.substr(3);
		}
	}
	return resolve_file_path(animation, path);
}

std::string APIAnimation::get_ability_animation_path(const nyan::Object &ability) {
	const auto type = ability_property_t::ANIMATED;
	if (not APIAbility::check_property(ability, type)) {
		return {};
	}
	auto animations = APIAbilityProperty::get_animations(APIAbility::get_property(ability, type));
	return animations.empty() ? std::string{} : get_animation_path(animations.front());
}

const std::vector<std::string> APIAnimation::get_animation_paths(const std::vector<nyan::Object> &animations) {
	std::vector<std::string> result;

	for (auto &animation : animations) {
		result.push_back(get_animation_path(animation));
	}

	return result;
}

} // namespace openage::gamestate::api
