// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "config.h"
#include "cvar/cvar.h"
#include "error/error.h"
#include "event/event_loop.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/build.h"
#include "gamestate/component/api/gather.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/internal/command_queue.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/entity_factory.h"
#include "gamestate/event/economy_action.h"
#include "gamestate/event/send_command.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "gamestate/player.h"
#include "gamestate/simulation.h"
#include "gamestate/system/idle.h"
#include "gamestate/system/move.h"
#include "testing/testing.h"
#include "time/time_loop.h"
#include "util/fslike/directory.h"
#include "util/json_minimal.h"

export module openage.gamestate.wildlife;

import nyan;

namespace openage::gamestate::wildlife {
namespace {

using component::component_t;

struct Profile {
	std::string kind;
	coord::phys_t speed;
	coord::phys_t sight;
};

struct Person {
	entity_id_t id;
	coord::phys3 position;
	bool villager;
};

using Cell = std::pair<int64_t, int64_t>;
using Neighbors = std::map<Cell, std::vector<Person>>;

Cell cell(const coord::phys3 &position) {
	return {position.ne.to_int() / 8, position.se.to_int() / 8};
}

coord::phys_t distance(const coord::phys3 &a, const coord::phys3 &b) {
	const auto ne = a.ne - b.ne;
	const auto se = a.se - b.se;
	return ne * ne + se * se;
}

std::optional<Person> nearest(const Neighbors &people, const coord::phys3 &position, coord::phys_t range, bool villagers_only) {
	std::optional<Person> result;
	const auto [x, y] = cell(position);
	const auto extent = range.to_int() / 8 + 1;
	for (auto i = x - extent; i <= x + extent; ++i) {
		for (auto j = y - extent; j <= y + extent; ++j) {
			const auto found = people.find({i, j});
			if (found == people.end()) {
				continue;
			}
			for (const auto &person : found->second) {
				const auto d = distance(position, person.position);
				if ((not villagers_only or person.villager) and d <= range * range
				    and (not result or d < distance(position, result->position)
				         or (d == distance(position, result->position) and person.id < result->id))) {
					result = person;
				}
			}
		}
	}
	return result;
}

class Controller final : public openage::event::OnceEventHandler {
public:
	explicit Controller(std::map<std::string, Profile> profiles) :
		OnceEventHandler{"game.wildlife"}, profiles{std::move(profiles)} {}

	void setup_event(const std::shared_ptr<openage::event::Event> &, const std::shared_ptr<openage::event::State> &) override {}
	time::time_t predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> &,
	                                 const std::shared_ptr<openage::event::State> &,
	                                 const time::time_t &at) override {
		return at;
	}

	void invoke(openage::event::EventLoop &loop, const std::shared_ptr<openage::event::EventEntity> &target, const std::shared_ptr<openage::event::State> &state, const time::time_t &at, const param_map &) override {
		const auto game = std::static_pointer_cast<GameState>(state);
		Neighbors people;
		std::map<entity_id_t, std::shared_ptr<GameEntity>> animals;
		for (const auto &[id, entity] : game->get_game_entities()) {
			if (entity->is_dead() or not entity->has_component(component_t::MOVE)) {
				continue;
			}
			const auto owner = entity->try_get_component_as<component::Ownership>(component_t::OWNERSHIP);
			const auto position = entity->try_get_component_as<component::Position>(component_t::POSITION);
			if (not owner or not position) {
				continue;
			}
			const auto profile = this->profiles.find(entity->definition);
			if (game->get_player(owner->get_owners().get(at))->is_gaia) {
				if (profile != this->profiles.end()) {
					animals.emplace(id, entity);
				}
			}
			else if (profile == this->profiles.end()) {
				const auto point = position->get_positions().get(at);
				people[cell(point)].push_back({id, point, entity->construct_ability.has_value()});
			}
		}
		std::erase_if(this->memory, [&](const auto &entry) { return not animals.contains(entry.first); });
		// Bound pathfinding work while keeping scheduling independent of frame rate.
		auto it = animals.upper_bound(this->cursor);
		for (size_t n = 0; n < std::min(size_t{16}, animals.size()); ++n) {
			if (it == animals.end()) {
				it = animals.begin();
			}
			const auto [id, entity] = *it++;
			this->cursor = id;
			const auto &profile = this->profiles.at(entity->definition);
			auto &memory = this->memory[id];
			auto pos = entity->try_get_component_as<component::Position>(component_t::POSITION);
			auto move = entity->try_get_component_as<component::Move>(component_t::MOVE);
			const auto point = pos->get_positions().get(at);
			const auto queue = entity->try_get_component_as<component::CommandQueue>(component_t::COMMANDQUEUE);
			if (queue and not queue->get_queue().empty(at)) {
				continue;
			}
			// Scripted/player movement takes precedence until it has finished.
			if (move->generation != memory.generation and pos->get_positions().get(at + time::time_t{1}) != point) {
				continue;
			}
			const auto attack = entity->try_get_component_as<component::Attack>(component_t::ATTACK);
			if (attack and attack->is_active()) {
				continue;
			}
			if (profile.kind == "wolf") {
				entity->wildlife_predator = true;
				if (const auto person = nearest(people, point, profile.sight, true); person and attack) {
					loop.create_event("game.send_command", target, state, at, {
																				  {"type", component::command::command_t::ATTACK},
																				  {"entity_ids", std::vector<entity_id_t>{id}},
																				  {"target_id", person->id},
																				  {"issuer", entity->try_get_component_as<component::Ownership>(component_t::OWNERSHIP)->get_owners().get(at)},
																			  });
					continue;
				}
			}
			const auto threat = profile.kind == "prey" or profile.kind == "herdable" ? nearest(people, point, coord::phys_t{4}, false) : std::nullopt;
			if (at < memory.next and (not threat or at < memory.flee_again)) {
				continue;
			}
			coord::phys3 destination = point;
			const auto size = game->get_map()->get_size();
			if (profile.kind == "bird") {
				if (memory.outside) {
					memory.outside = false;
				}
				else {
					memory.outside = this->random(id, memory) % 8 == 0;
				}
				destination = {coord::phys_t::from_int(1 + this->random(id, memory) % (size[0] - 2)),
				               coord::phys_t::from_int(1 + this->random(id, memory) % (size[1] - 2)),
				               point.up};
				if (memory.outside) {
					destination.ne = coord::phys_t{-4};
				}
				const auto delta = destination - point;
				const auto length = std::abs(delta.ne) + std::abs(delta.se);
				const auto duration = time::time_t::from_raw_value((length / profile.speed).get_raw_value());
				pos->set_position(at, point);
				pos->set_position(at + duration, destination);
				if (length > 0) {
					pos->set_angle(at, delta.to_angle());
				}
				entity->play_animation(move->get_ability(), at);
				memory.next = at + duration + time::time_t::from_int(memory.outside ? 12 : 1);
			}
			else {
				if (threat) {
					const auto ne = point.ne >= threat->position.ne ? 5 : -5;
					const auto se = point.se >= threat->position.se ? 5 : -5;
					destination.ne += coord::phys_t::from_int(ne);
					destination.se += coord::phys_t::from_int(se);
					memory.flee_again = at + time::time_t{3};
				}
				else {
					destination.ne += coord::phys_t::from_int(static_cast<int>(this->random(id, memory) % 5) - 2);
					destination.se += coord::phys_t::from_int(static_cast<int>(this->random(id, memory) % 5) - 2);
				}
				destination.ne = std::clamp(destination.ne, coord::phys_t{1}, coord::phys_t::from_int(size[0] - 1));
				destination.se = std::clamp(destination.se, coord::phys_t{1}, coord::phys_t::from_int(size[1] - 1));
				const auto duration = system::Move::move_default(entity, game, destination, at);
				memory.generation = move->generation;
				memory.next = at + duration + time::time_t::from_int(3 + this->random(id, memory) % 8);
			}
		}
		loop.create_event("game.wildlife", target, state, at + time::time_t::from_double(0.5));
	}

private:
	struct Memory {
		time::time_t next = time::time_t::zero();
		time::time_t flee_again = time::time_t::zero();
		uint64_t seed = 0;
		uint64_t generation = 0;
		bool outside = false;
	};
	uint64_t random(entity_id_t id, Memory &memory) {
		if (memory.seed == 0) {
			memory.seed = id + 1;
		}
		memory.seed = memory.seed * 6364136223846793005ULL + 1442695040888963407ULL;
		return memory.seed >> 32;
	}
	std::map<std::string, Profile> profiles;
	std::map<entity_id_t, Memory> memory;
	entity_id_t cursor = 0;
};

} // namespace
} // namespace openage::gamestate::wildlife

export namespace openage::gamestate::wildlife {

/** Start neutral wildlife independently of optional strategic computer players. */
void start(const std::shared_ptr<openage::event::EventLoop> &loop,
           const std::shared_ptr<openage::event::EventEntity> &target,
           const std::shared_ptr<GameState> &state,
           const util::json::Value &config) {
	std::map<std::string, Profile> profiles;
	for (const auto &[id, definition] : config.at("definitions").as_object()) {
		if (not definition.has("wildlife")) {
			continue;
		}
		const auto &wildlife = definition.at("wildlife");
		const auto kind = wildlife.at("kind").as_string();
		const auto speed = wildlife.at("speed").as_number();
		const auto sight = definition.at("sight").as_number();
		if ((kind != "bird" and kind != "prey" and kind != "herdable" and kind != "wolf")
		    or not std::isfinite(speed) or not std::isfinite(sight)
		    or speed < coord::phys_t::from_raw_value(1).to_double() or sight < 0 or sight > 64) {
			throw Error{MSG(err) << "Invalid wildlife metadata for " << id};
		}
		profiles.emplace(definition.at("fqon").as_string(), Profile{kind, coord::phys_t::from_double(speed), coord::phys_t::from_double(sight)});
	}
	if (not profiles.empty()) {
		const auto size = state->get_map()->get_size();
		if (size[0] < 3 or size[1] < 3) {
			throw Error{MSG(err) << "Wildlife requires a map at least three tiles across"};
		}
		auto handler = std::make_shared<Controller>(std::move(profiles));
		loop->add_event_handler(handler);
		loop->create_event(handler, target, state, time::time_t::zero());
	}
}

namespace tests {

/** Measure observation indexing and local queries on a large synthetic map. */
void spatial_benchmark() {
	Neighbors people;
	for (entity_id_t id = 0; id < 14053; ++id) {
		const coord::phys3 point{coord::phys_t::from_int(id % 200), coord::phys_t::from_int(id / 200), 0};
		people[cell(point)].push_back({id, point, id % 3 == 0});
	}
	size_t found = 0;
	for (int index = 0; index < 200; ++index) {
		const coord::phys3 point{coord::phys_t::from_int(index), 40, 0};
		found += nearest(people, point, coord::phys_t{8}, true).has_value();
	}
	TESTEQUALS(found, 200);
}

/** Spatial queries include cell boundaries, sight limits, stable ties and villager filtering. */
void neighbors() {
	Neighbors people;
	for (const auto &person : {Person{3, {8, 8, 0}, false}, Person{2, {8, 8, 0}, true}, Person{1, {12, 8, 0}, true}}) {
		people[cell(person.position)].push_back(person);
	}
	TESTEQUALS(nearest(people, {7, 8, 0}, coord::phys_t{1}, false)->id == 2, true);
	TESTEQUALS(nearest(people, {7, 8, 0}, coord::phys_t{0}, false).has_value(), false);
	TESTEQUALS(nearest(people, {12, 8, 0}, coord::phys_t{4}, true)->id == 1, true);
	people[cell({8, 8, 0})].erase(people[cell({8, 8, 0})].begin() + 1);
	TESTEQUALS(nearest(people, {8, 8, 0}, coord::phys_t{1}, true).has_value(), false);
}

/** Exercise real movement, combat, capture and repair events without a renderer. */
void behavior() {
	class OpenGrid final : public scenario::NavigationGrid {
	public:
		bool is_walkable(scenario::CellPosition point) const override {
			return point.x >= 0 and point.y >= 0 and point.x < 20 and point.y < 20;
		}
		double cost(scenario::CellPosition) const override {
			return 1;
		}
	};
	auto directory = std::make_shared<util::fslike::Directory>(config::buildsystem_sourcefile_dir);
	const auto root = directory->root();
	auto simulation = std::make_shared<GameSimulation>(root, std::make_shared<cvar::CVarManager>(root), std::make_shared<time::TimeLoop>());
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();
	const auto game = simulation->get_game()->get_state();
	const auto loop = simulation->get_event_loop();
	const auto factory = simulation->get_entity_factory();
	game->get_map()->set_navigation_service(std::make_shared<OpenGrid>(), {});
	const auto gaia = factory->add_player(loop, game, "", player_id_t{1});
	gaia->is_gaia = true;
	game->add_player(gaia);
	game->set_stance(1, 0, DiplomaticStance::Neutral, true);
	auto spawn = [&](const std::string &definition, player_id_t owner, coord::phys3 point) {
		auto entity = factory->add_game_entity(loop, game, owner, definition);
		entity->try_get_component_as<component::Position>(component_t::POSITION)->set_position(0, point);
		entity->try_get_component_as<component::Ownership>(component_t::OWNERSHIP)->set_owner(0, owner);
		game->add_game_entity(entity);
		register_entity_in_dynamic_occupancy(*game->get_map(), *entity, point.ne.to_double(), point.se.to_double());
		return entity;
	};
	const std::string original = "ww_mvp.campaign.william_wallace.original_entities.Unit";
	const std::string villager = "de2_base.data.game_entity.generic.villager.villager.Villager";
	auto deer = spawn(original + "65", 1, {8, 8, 0});
	spawn(villager, 0, {7, 8, 0});
	auto bird = spawn(original + "96", 1, {4, 15, 0});
	auto sheep = spawn(original + "594", 1, {14, 15, 0});
	auto cow = spawn(original + "705", 1, {14, 4, 0});
	spawn(villager, 0, {15, 4, 0});
	auto wolf = spawn(original + "126", 1, {2, 2, 0});
	auto victim = spawn(villager, 0, {3, 2, 0});
	const auto gathering = victim->try_get_component_as<component::Gather>(component_t::GATHER);
	TESTEQUALS(cow->resource_spot->remaining, 150);
	TESTEQUALS(gathering->find_ability(cow->resource_spot->definition) != nullptr, true);
	TESTEQUALS(wolf->resource_spot.has_value(), false);
	spawn("de2_base.data.game_entity.generic.militia.militia.Militia", 0, {2.5, 2, 0});
	std::map<std::string, Profile> profiles{
		{original + "65", {"prey", coord::phys_t{1}, coord::phys_t{4}}},
		{original + "96", {"bird", coord::phys_t{1}, coord::phys_t{4}}},
		{original + "594", {"herdable", coord::phys_t{1}, coord::phys_t{4}}},
		{original + "705", {"herdable", coord::phys_t{1}, coord::phys_t{4}}},
		{original + "126", {"wolf", coord::phys_t{1}, coord::phys_t{4}}},
	};
	auto handler = std::make_shared<Controller>(std::move(profiles));
	loop->add_event_handler(handler);
	loop->create_event(handler, simulation->get_commander(), game, time::time_t::zero());
	loop->reach_time(time::time_t{1}, game);
	auto point_at = [](const auto &entity, time::time_t at) {
		return entity->template try_get_component_as<component::Position>(component_t::POSITION)->get_positions().get(at);
	};
	TESTEQUALS(point_at(deer, time::time_t{1}).ne > coord::phys_t{8}, true);
	TESTEQUALS(point_at(cow, time::time_t{1}).ne < coord::phys_t{14}, true);
	const auto attacking = wolf->try_get_component_as<component::Attack>(component_t::ATTACK);
	TESTEQUALS(attacking->is_active(), true);
	TESTEQUALS(attacking->get_target_id(), victim->get_id());
	const nyan::fqon_t health = "de2_base.data.util.attribute.types.Health";
	const auto victim_live = victim->try_get_component_as<component::Live>(component_t::LIVE);
	loop->reach_time(time::time_t{4}, game);
	TESTEQUALS(victim_live->get_attribute_value(time::time_t{4}, health).value() < victim_live->get_attribute_max(health).value(), true);
	TESTEQUALS(game->get_stance(1, 0) == DiplomaticStance::Neutral, true);
	game->remove_game_entity(wolf->get_id());
	system::Move::halt(sheep, game, time::time_t{4});
	sheep->try_get_component_as<component::Ownership>(component_t::OWNERSHIP)->set_owner(time::time_t{4}, 0);
	const auto captured_at = point_at(sheep, time::time_t{4});
	bool outside = false;
	bool returned = false;
	for (int second = 5; second <= 600; ++second) {
		const auto now = time::time_t::from_int(second);
		loop->reach_time(now, game);
		const auto point = point_at(bird, now);
		const bool inside = point.ne >= 0 and point.ne < 20 and point.se >= 0 and point.se < 20;
		returned = returned or (outside and inside);
		outside = outside or not inside;
	}
	TESTEQUALS(outside, true);
	TESTEQUALS(returned, true);
	TESTEQUALS(point_at(sheep, time::time_t{600}) == captured_at, true);

	auto repairer = spawn(villager, 0, {11, 11, 0});
	auto house = spawn("de2_base.data.game_entity.generic.house.house.House", 0, {15, 11, 0});
	const auto house_live = house->try_get_component_as<component::Live>(component_t::LIVE);
	const auto max_hp = house_live->get_attribute_max(health).value();
	const time::time_t at{601};
	house_live->set_attribute(at, health, max_hp - 100);
	game->get_player(0)->add_resource("de2_base.data.util.resource.types.Wood", 1000);
	loop->create_event("game.economy_action", simulation->get_commander(), game, at, {
																						 {"actor", repairer->get_id()},
																						 {"issuer", player_id_t{0}},
																						 {"repair_target", house->get_id()},
																					 });
	loop->reach_time(time::time_t{630}, game);
	TESTEQUALS(house_live->get_attribute_value(time::time_t{630}, health).value_or(0), max_hp);
	TESTEQUALS(game->get_player(0)->get_resource("de2_base.data.util.resource.types.Wood") < 1000, true);
	house_live->set_attribute(time::time_t{631}, health, max_hp - 100);
	loop->create_event("game.economy_action", simulation->get_commander(), game, time::time_t{631}, {
																										{"actor", repairer->get_id()},
																										{"issuer", player_id_t{0}},
																										{"repair_target", house->get_id()},
																									});
	loop->reach_time(time::time_t{633}, game);
	loop->create_event("game.send_command", simulation->get_commander(), game, time::time_t{633}, {
																									  {"type", component::command::command_t::IDLE},
																									  {"entity_ids", std::vector<entity_id_t>{repairer->get_id()}},
																									  {"issuer", player_id_t{0}},
																								  });
	loop->reach_time(time::time_t{633}, game);
	const auto stopped_hp = house_live->get_attribute_value(time::time_t{633}, health).value();
	loop->reach_time(time::time_t{660}, game);
	TESTEQUALS(house_live->get_attribute_value(time::time_t{660}, health).value_or(0), stopped_hp);

	auto helper = spawn(villager, 0, {10, 17, 0});
	const auto option = std::ranges::find_if(repairer->action_options, [](const auto &candidate) {
		return candidate.kind == ActionKind::Build and candidate.subject.ends_with(".House");
	});
	TESTEQUALS(option != repairer->action_options.end(), true);
	const nyan::fqon_t wood = "de2_base.data.util.resource.types.Wood";
	const auto wood_before = game->get_player(0)->get_resource(wood);
	loop->create_event("game.economy_action", simulation->get_commander(), game, time::time_t{661}, {
		{"actor", repairer->get_id()}, {"issuer", player_id_t{0}}, {"option", option->id},
		{"position", coord::phys3{9, 15, 0}},
		{"builders", std::vector<entity_id_t>{repairer->get_id(), helper->get_id(), helper->get_id()}},
	});
	loop->reach_time(time::time_t{674}, game);
	TESTEQUALS(repairer->building_site != 0, true);
	TESTEQUALS(helper->building_site, repairer->building_site);
	const auto site = game->get_game_entity(repairer->building_site);
	const auto construction = site->try_get_component_as<component::Build>(component_t::BUILD);
	TESTEQUALS(construction->active_builders.size(), 2);
	TESTEQUALS(wood_before - game->get_player(0)->get_resource(wood), option->cost.at(wood));
	loop->reach_time(time::time_t{720}, game);
	TESTEQUALS(construction->get_progress(), 100);
}

} // namespace tests
} // namespace openage::gamestate::wildlife
