// Copyright 2023-2024 the openage authors. See copying.md for legal info.

#include "scenario.h"

#include <cstdint>
#include <memory>
#include <vector>

#include <nyan/nyan.h>


namespace openage::gamestate::api {

bool APIScenario::is_scenario(const nyan::Object &obj) {
	nyan::fqon_t immediate_parent = obj.get_parents()[0];
	return immediate_parent == "engine.util.scenario.Scenario";
}

const util::Vector2s APIScenario::get_size(const nyan::Object &scenario) {
	nyan::Object size_obj = scenario.get_object("Scenario.size");
	return read_size(size_obj);
}

const std::vector<nyan::Object> APIScenario::get_chunks(const nyan::Object &scenario) {
	std::vector<nyan::Object> result;

	auto db_view = scenario.get_view();
	auto chunks = scenario.get_orderedset("Scenario.terrain_chunks");
	for (auto &chunk_val : chunks) {
		auto chunk_obj_val = std::dynamic_pointer_cast<nyan::ObjectValue>(chunk_val.get_ptr());
		auto chunk_obj = db_view->get_object(chunk_obj_val->get_name());
		result.push_back(chunk_obj);
	}

	return result;
}

const std::vector<nyan::Object> APIScenario::get_initial_entities(const nyan::Object &scenario) {
	std::vector<nyan::Object> result;

	auto db_view = scenario.get_view();
	auto entities = scenario.get_orderedset("Scenario.initial_entities");
	for (auto &entity_val : entities) {
		auto entity_obj_val = std::dynamic_pointer_cast<nyan::ObjectValue>(entity_val.get_ptr());
		auto entity_obj = db_view->get_object(entity_obj_val->get_name());
		result.push_back(entity_obj);
	}

	return result;
}

const util::Vector2s APIScenario::read_size(const nyan::Object &size_obj) {
	// engine.util.terrain.Size has x and y int members.
	// Reading via memberid "Size.x" / "Size.y" keeps the lookup scoped to
	// the Size type so an inherited override cannot accidentally alias.
	int64_t x = size_obj.get_int("Size.x");
	int64_t y = size_obj.get_int("Size.y");
	return util::Vector2s{
		static_cast<size_t>(x),
		static_cast<size_t>(y),
	};
}

} // namespace openage::gamestate::api