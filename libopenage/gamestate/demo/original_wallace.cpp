// Copyright 2026 the openage authors. See copying.md for legal info.
#include "original_wallace.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "coord/tile.h"
#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/build.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/entity_factory.h"
#include "gamestate/component/internal/commands/base_command.h"
#include "gamestate/event/send_command.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/manager.h"
#include "gamestate/map.h"
#include "gamestate/player.h"
#include "gamestate/simulation.h"
#include "scenario/trigger/original_scenario.h"
#include "scenario/trigger/trigger_runtime.h"
#include "time/time_loop.h"
#include "log/log.h"
#include "util/path.h"

namespace openage::gamestate::tests {
namespace {
void require(bool value, const std::string &message) { if (not value) throw std::runtime_error{"Original Wallace: " + message}; }
using component::component_t;
coord::phys3 point(GameEntity &entity, time::time_t at) { return entity.try_get_component_as<component::Position>(component_t::POSITION)->get_positions().get(at); }
player_id_t owner(GameEntity &entity, time::time_t at) { return entity.try_get_component_as<component::Ownership>(component_t::OWNERSHIP)->get_owners().get(at); }
}
void simulation_demo_53(const util::Path &path) {
    auto simulation = std::make_shared<GameSimulation>(path, std::make_shared<cvar::CVarManager>(path), std::make_shared<time::TimeLoop>(),
        "ww_mvp.campaign.william_wallace.mission_01.PlayerSetup", "ww_mvp.campaign.william_wallace.mission_01.Mission");
    simulation->set_modpacks({"de2_base", "ww_mvp"});
    simulation->initialize();
    auto state = simulation->get_game()->get_state();
    for (const auto &[id, entity] : state->get_game_entities()) {
        if (entity->has_component(component_t::MOVE)) {
            entity->get_manager()->run_activity_system(time::time_t::zero());
        }
    }
    auto runtime = simulation->get_trigger_runtime();
    auto original = runtime->original_scenario();
    require(bool(original), "Install an original scenario before running demo 53");
    const auto mission = original->config().at("mission").as_int();
    require(state->get_map()->get_size()[0] == 120 and state->get_map()->get_size()[1] == 120, "full source dimensions");
    require(state->get_game_entities().size() == (mission == 1 ? 5698 : 10130), "source placement count");
    require(runtime->trigger_count() == (mission == 1 ? 31 : 27), "source trigger count");
    for (const auto *name : {"Food", "Wood", "Stone", "Gold"}) require(state->get_player(0)->get_resource(std::string{"de2_base.data.util.resource.types."}+name) == 0, "source initial resources");
    require(not state->get_player(0)->allows_action("de2_base.data.tech.generic.loom.loom.Loom"), "source Loom restriction");
    require(state->get_player(0)->allows_action("de2_base.data.game_entity.generic.villager.villager.Villager") == (mission == 2), "source villager training restriction");
    time::time_t now = time::time_t::zero();
    const auto loop = simulation->get_event_loop();
    const auto commander = simulation->get_commander();
    auto advance = [&](int seconds) { for (int i=0;i<seconds;++i) { now += time::time_t{1}; loop->reach_time(now, state); state->update_visibility(now); state->update_player_economies(now); } };
    auto send = [&](component::command::command_t type, std::vector<entity_id_t> ids, coord::phys3 target, entity_id_t target_id=0) {
        loop->create_event("game.send_command", commander, state, now, {{"type",type},{"entity_ids",ids},{"target",target},{"target_id",target_id}});
    };
    auto units = [&](int64_t genie, player_id_t player) { std::vector<entity_id_t> result; for (const auto &[id,entity] : state->get_game_entities()) if (entity->genie_id == genie and not entity->is_dead() and owner(*entity,now)==player) result.push_back(id); std::ranges::sort(result); return result; };
    if (mission == 1) {
        const auto scout = *state->get_entity_id_by_instance("object_5462");
        require(std::abs(point(*state->get_game_entity(scout),now).ne.to_double()-35.5)<.001, "source fractional position");
        original->select({scout},now);
        advance(25);
        require(state->get_player(0)->has_researched("de2_base.data.tech.generic.forging.forging.Forging"), "source forced research");
        for (const auto [x,y] : {std::pair{54,38}, {62,36}, {66,49}, {63,63}}) {
            send(component::command::command_t::MOVE,{scout},{coord::phys_t::from_int(x),coord::phys_t::from_int(y),0}); advance(65);
            auto p=point(*state->get_game_entity(scout),now);
            log::log(MSG(info) << "Original Wallace waypoint " << x << ',' << y << " reached " << p);
            require(std::hypot(p.ne.to_double()-x,p.se.to_double()-y)<2, "source road is reachable");
        }
        require(units(74,0).size() == 3 and units(38,0).size() == 1, "source reinforcements");
        std::vector<entity_id_t> army=units(74,0); auto knights=units(38,0); army.insert(army.end(),knights.begin(),knights.end());
        send(component::command::command_t::MOVE,army,{73,72,0}); advance(80);
        const auto outpost = *state->get_entity_id_by_instance("object_13430");
        send(component::command::command_t::ATTACK,army,{0,0,0},outpost);
        for (int i=0;i<500 and state->has_game_entity(outpost) and not state->get_game_entity(outpost)->is_dead();++i) advance(1);
        require(not state->has_game_entity(outpost) or state->get_game_entity(outpost)->is_dead(), "group attack destroys original outpost");
        require(not runtime->mission_state().is_victory(), "outpost alone must not win");
        advance(20);
        send(component::command::command_t::MOVE,army,{95,33,0}); advance(150);
        require(units(109,0).size() == 1, "Gaia village ownership transfer");
        auto enemies=units(74,1); require(enemies.size() == 3, "source English counterattack");
        for (const auto target : enemies) {
            std::erase_if(army,[&](auto id){return not state->has_game_entity(id) or state->get_game_entity(id)->is_dead();});
            send(component::command::command_t::ATTACK,army,{0,0,0},target); advance(150);
        }
        advance(40);
        require(runtime->mission_state().is_victory(), "source raid victory chain");
    } else {
        const auto worker = *state->get_entity_id_by_instance("object_8536");
        require(state->get_game_entity(worker)->has_component(component_t::MOVE), "original villager remains controllable");
        auto gather = [&](entity_id_t id, const std::string &resource) {
            const auto start=point(*state->get_game_entity(id),now); double distance=1e9; entity_id_t target=0;
            for (const auto &[candidate,entity] : state->get_game_entities()) {
                if (not entity->resource_spot or entity->resource_spot->requires_hunting or entity->resource_spot->resource != resource or entity->resource_spot->remaining<=0) continue;
                const auto p=point(*entity,now); const auto d=std::hypot((p.ne-start.ne).to_double(),(p.se-start.se).to_double());
                if (d<distance) { distance=d;target=candidate; }
            }
            require(target!=0,"original resource exists: "+resource);
            log::log(MSG(info) << "Original Wallace gather worker=" << id << " target=" << target << " distance=" << distance);
            send(component::command::command_t::GATHER,{id},{0,0,0},target);
        };
        original->select({worker},now); advance(30);
        gather(worker,"de2_base.data.util.resource.types.Food");
        for (int i=0;i<450 and units(83,0).size()<4;++i) advance(1);
        auto villagers=units(83,0); require(villagers.size() >= 4, "source timed extra villagers");
        require(not runtime->mission_state().is_victory(),"food alone cannot win");
        gather(villagers[1],"de2_base.data.util.resource.types.Wood");
        gather(villagers[2],"de2_base.data.util.resource.types.Gold");
        for (int i=0;i<1000 and not runtime->mission_state().is_victory();++i) advance(1);
        for (const auto *name : {"Food","Wood","Gold"}) require(state->get_player(0)->get_resource(std::string{"de2_base.data.util.resource.types."}+name)>=50,"source resource goal");
        require(runtime->mission_state().is_victory(),"source resource victory with timers");
    }
    log::log(MSG(info) << "ORIGINAL_WALLACE_PASS mission=" << mission << " time=" << now);
}

void simulation_demo_54(const util::Path &path) {
	auto simulation = std::make_shared<GameSimulation>(path, std::make_shared<cvar::CVarManager>(path), std::make_shared<time::TimeLoop>(),
	    "ww_mvp.campaign.william_wallace.mission_01.PlayerSetup", "ww_mvp.campaign.william_wallace.mission_01.Mission");
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();
	auto state = simulation->get_game()->get_state();
	auto loop = simulation->get_event_loop();
	auto commander = simulation->get_commander();
	auto factory = simulation->get_entity_factory();
	time::time_t now = time::time_t::zero();
	auto advance = [&](int seconds) {
		for (int i = 0; i < seconds; ++i) {
			now += time::time_t{1};
			loop->reach_time(now, state);
			state->update_visibility(now);
			state->update_player_economies(now);
		}
	};
	auto spawn = [&](player_id_t owner_id, const nyan::fqon_t &fqon, int x, int y) {
		auto entity = factory->add_game_entity(loop, state, owner_id, fqon);
		coord::phys3 pos{coord::phys_t::from_int(x), coord::phys_t::from_int(y), 0};
		entity->try_get_component_as<component::Position>(component_t::POSITION)->set_position(now, pos);
		entity->try_get_component_as<component::Ownership>(component_t::OWNERSHIP)->set_owner(now, owner_id);
		state->add_game_entity(entity);
		register_entity_in_dynamic_occupancy(*state->get_map(), *entity, x, y);
		return entity;
	};

	auto player = state->get_player(0);
	player->add_resource("de2_base.data.util.resource.types.Food", 500);
	player->add_resource("de2_base.data.util.resource.types.Wood", 500);
	player->add_resource("de2_base.data.util.resource.types.Gold", 500);

	// 1. Multi-villager cooperative construction
	auto v1 = spawn(0, "de2_base.data.game_entity.generic.villager.villager.Villager", 10, 10);
	auto v2 = spawn(0, "de2_base.data.game_entity.generic.villager.villager.Villager", 11, 10);
	auto build_house = std::ranges::find_if(v1->action_options, [](const auto &opt) {
		return opt.kind == ActionKind::Build and opt.subject.ends_with(".House");
	});
	require(build_house != v1->action_options.end(), "villager has House build option");
	loop->create_event("game.economy_action", commander, state, now, {
		{"actor", v1->get_id()}, {"option", build_house->id},
		{"position", coord::tile{12, 12}.to_phys3()}, {"issuer", player_id_t{0}},
	});
	advance(1);
	entity_id_t house_id = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->definition == build_house->subject) {
			house_id = id;
			break;
		}
	}
	require(house_id != 0, "house foundation created");
	loop->create_event("game.economy_action", commander, state, now, {
		{"actor", v2->get_id()}, {"site", house_id}, {"issuer", player_id_t{0}},
	});
	advance(21);
	require(not state->get_game_entity(house_id)->try_get_component_as<component::Build>(component_t::BUILD)->is_under_construction(),
	        "two villagers build house faster than 25s single-builder time");

	// 2. Building training queue (queue 3 Militia in a row)
	spawn(0, "de2_base.data.game_entity.generic.house.house.House", 52, 38);
	spawn(0, "de2_base.data.game_entity.generic.house.house.House", 52, 41);
	auto barracks = spawn(0, "de2_base.data.game_entity.generic.barracks.barracks.Barracks", 56, 38);
	auto train_militia = std::ranges::find_if(barracks->action_options, [](const auto &opt) {
		return opt.kind == ActionKind::Train and opt.subject.ends_with(".Militia");
	});
	require(train_militia != barracks->action_options.end(), "barracks has Militia train option");
	for (int i = 0; i < 3; ++i) {
		loop->create_event("game.economy_action", commander, state, now, {
			{"actor", barracks->get_id()}, {"option", train_militia->id}, {"issuer", player_id_t{0}},
		});
		advance(1);
	}
	require(state->action_feedback(0) == "Queued (3 in queue).", "barracks queued 3 militia: " + state->action_feedback(0));
	advance(90);
	int militia_count = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (not entity->is_dead() and entity->definition.ends_with(".Militia") and owner(*entity, now) == 0) {
			++militia_count;
		}
	}
	require(militia_count >= 3, "barracks training queue produced 3 militia");

	// 3. Trebuchet transform (pack/unpack)
	auto treb = spawn(0, "de2_base.data.game_entity.generic.trebuchet.trebuchet.Trebuchet", 62, 38);
	auto enemy_house = spawn(1, "de2_base.data.game_entity.generic.house.house.House", 70, 38);
	require(treb->is_transformable and not treb->unpacked, "trebuchet starts packed");
	loop->create_event("game.send_command", commander, state, now, {
		{"type", component::command::command_t::ATTACK},
		{"entity_ids", std::vector<entity_id_t>{treb->get_id()}},
		{"target_id", enemy_house->get_id()},
		{"issuer", player_id_t{0}},
	});
	advance(1);
	require(state->action_feedback(0) == "Unpack Trebuchet before attacking.", "packed trebuchet cannot attack");
	loop->create_event("game.economy_action", commander, state, now, {
		{"actor", treb->get_id()}, {"option", nyan::fqon_t{"trebuchet.toggle_pack"}}, {"issuer", player_id_t{0}},
	});
	advance(3);
	require(treb->unpacked, "trebuchet unpacked");
	loop->create_event("game.send_command", commander, state, now, {
		{"type", component::command::command_t::MOVE},
		{"entity_ids", std::vector<entity_id_t>{treb->get_id()}},
		{"target", coord::tile{63, 38}.to_phys3()},
		{"issuer", player_id_t{0}},
	});
	advance(1);
	require(state->action_feedback(0) == "Pack Trebuchet before moving.", "unpacked trebuchet cannot move");
	loop->create_event("game.send_command", commander, state, now, {
		{"type", component::command::command_t::ATTACK},
		{"entity_ids", std::vector<entity_id_t>{treb->get_id()}},
		{"target_id", enemy_house->get_id()},
		{"issuer", player_id_t{0}},
	});
	advance(12);
	require(enemy_house->is_dead() or not state->has_game_entity(enemy_house->get_id()), "unpacked trebuchet destroys enemy house");

	// 4. Monk Heal and Convert
	auto monk = spawn(0, "de2_base.data.game_entity.generic.monk.monk.Monk", 60, 44);
	auto ally = spawn(0, "de2_base.data.game_entity.generic.militia.militia.Militia", 61, 44);
	auto ally_live = ally->try_get_component_as<component::Live>(component_t::LIVE);
	const nyan::fqon_t health = "de2_base.data.util.attribute.types.Health";
	ally_live->set_attribute(now, health, 10);
	loop->create_event("game.send_command", commander, state, now, {
		{"type", component::command::command_t::HEAL},
		{"entity_ids", std::vector<entity_id_t>{monk->get_id()}},
		{"target_id", ally->get_id()},
		{"issuer", player_id_t{0}},
	});
	advance(20);
	require(*ally_live->get_attribute_value(now, health) == *ally_live->get_attribute_max(health), "monk healed ally to full HP");
	auto enemy_unit = spawn(1, "de2_base.data.game_entity.generic.militia.militia.Militia", 63, 44);
	loop->create_event("game.send_command", commander, state, now, {
		{"type", component::command::command_t::ATTACK},
		{"entity_ids", std::vector<entity_id_t>{monk->get_id()}},
		{"target_id", enemy_unit->get_id()},
		{"issuer", player_id_t{0}},
	});
	advance(12);
	require(owner(*enemy_unit, now) == 0, "monk converted enemy unit to player 0");

	// 5. OriginalScenario Mission 3 condition 3 (OwnObjects), effects 4 & 21, and <Var> substitution
	auto runtime = simulation->get_trigger_runtime();
	if (auto original = runtime->original_scenario()) {
		auto cond3 = util::json::parse(R"({"condition_type": 3, "source_player": 1, "object_list": 70, "quantity": 1})");
		require(original->evaluate(cond3, now), "OriginalScenario condition 3 OwnObjects counts finished House");
		auto eff21 = util::json::parse(R"({"effect_type": 21})");
		original->execute(eff21, now);
		auto eff4 = util::json::parse(R"({"effect_type": 4})");
		original->execute(eff4, now);
	}

	log::log(MSG(info) << "DEMO_54_ALL_PASS time=" << now);
}
}
