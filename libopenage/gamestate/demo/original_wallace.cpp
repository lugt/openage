// Copyright 2026 the openage authors. See copying.md for legal info.
#include "original_wallace.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
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
}
