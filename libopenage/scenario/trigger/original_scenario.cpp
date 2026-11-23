// Copyright 2026 the openage authors. See copying.md for legal info.
#include "original_scenario.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <utility>

#include "event/event_loop.h"
#include "coord/tile.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/internal/activity.h"
#include "gamestate/component/internal/commands/attack.h"
#include "gamestate/component/internal/commands/move.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/entity_factory.h"
#include "gamestate/event/economy_action.h"
#include "gamestate/game_entity.h"
#include "gamestate/manager.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "gamestate/terrain.h"
#include "gamestate/player.h"
#include "gamestate/system/move.h"
#include "gamestate/system/idle.h"
#include "log/log.h"
#include "scenario/trigger/trigger_condition.h"
#include "scenario/trigger/trigger_effect.h"
#include "scenario/trigger/trigger_instance.h"
#include "scenario/trigger/trigger_runtime.h"

namespace openage::scenario::trigger {
namespace {
using util::json::Value;
using gamestate::component::component_t;

int64_t number(const Value &value, const char *key, int64_t fallback = -1) {
    return value.has(key) and value.at(key).is_number() ? value.at(key).as_int() : fallback;
}
std::string text(const Value &value, const char *key) {
    return value.has(key) and value.at(key).is_string() ? value.at(key).as_string() : "";
}
TriggerId trigger_id(int64_t index) { return make_trigger_id("original." + std::to_string(index)); }
std::optional<gamestate::player_id_t> owner(gamestate::GameEntity &entity, time::time_t at) {
    const auto ownership = entity.try_get_component_as<gamestate::component::Ownership>(component_t::OWNERSHIP);
    return ownership ? std::make_optional(ownership->get_owners().get(at)) : std::nullopt;
}
coord::phys3 position(gamestate::GameEntity &entity, time::time_t at) {
    return entity.try_get_component_as<gamestate::component::Position>(component_t::POSITION)->get_positions().get(at);
}
bool in_area(const Value &record, const coord::phys3 &point) {
    if (number(record, "area_x1") < 0) return true;
    const auto x = std::floor(point.ne.to_double()), y = std::floor(point.se.to_double());
    return x >= number(record, "area_x1") and x <= number(record, "area_x2")
        and y >= number(record, "area_y1") and y <= number(record, "area_y2");
}

class OriginalCondition final : public TriggerCondition {
public:
    OriginalCondition(std::shared_ptr<OriginalScenario> source, Value args) : source{std::move(source)}, args{std::move(args)} {}
    bool evaluate(const gamestate::GameState &, time::time_t at) const override { return source->evaluate(args, at); }
    std::vector<DependencyKey> dependencies() const override { return {}; }
private:
    std::shared_ptr<OriginalScenario> source;
    Value args;
};

class OriginalEffect final : public TriggerEffect {
public:
    explicit OriginalEffect(Value args) : args{std::make_shared<Value>(std::move(args))} {}
    void enqueue(const std::shared_ptr<event::EventLoop> &loop,
                 const std::shared_ptr<event::EventEntity> &target,
                 const std::shared_ptr<event::State> &state, time::time_t at) const override {
        loop->create_event(event_name(), target, state, at, {{"effect", args}});
    }
    std::string event_name() const override { return "scenario.original_effect"; }
private:
    std::shared_ptr<Value> args;
};

class OriginalSelectionHandler final : public event::OnceEventHandler {
public:
    explicit OriginalSelectionHandler(std::weak_ptr<OriginalScenario> source) : OnceEventHandler{"scenario.original_selection"}, source{std::move(source)} {}
    void setup_event(const std::shared_ptr<event::Event> &, const std::shared_ptr<event::State> &) override {}
    void invoke(event::EventLoop &, const std::shared_ptr<event::EventEntity> &, const std::shared_ptr<event::State> &,
                const time::time_t &at, const param_map &params) override {
        if (auto active = source.lock()) active->select(params.get<std::vector<gamestate::entity_id_t>>("selection"), at);
    }
    time::time_t predict_invoke_time(const std::shared_ptr<event::EventEntity> &, const std::shared_ptr<event::State> &,
                                    const time::time_t &at) override { return at; }
private:
    std::weak_ptr<OriginalScenario> source;
};

class OriginalEffectHandler final : public event::OnceEventHandler {
public:
    explicit OriginalEffectHandler(std::weak_ptr<OriginalScenario> source) : OnceEventHandler{"scenario.original_effect"}, source{std::move(source)} {}
    void setup_event(const std::shared_ptr<event::Event> &, const std::shared_ptr<event::State> &) override {}
    void invoke(event::EventLoop &, const std::shared_ptr<event::EventEntity> &, const std::shared_ptr<event::State> &,
                const time::time_t &at, const param_map &params) override {
        if (auto active = source.lock()) active->execute(*params.get<std::shared_ptr<Value>>("effect"), at);
    }
    time::time_t predict_invoke_time(const std::shared_ptr<event::EventEntity> &, const std::shared_ptr<event::State> &,
                                    const time::time_t &at) override { return at; }
private:
    std::weak_ptr<OriginalScenario> source;
};
}

OriginalScenario::OriginalScenario(TriggerRuntime &runtime, std::shared_ptr<gamestate::GameState> state,
                                   std::shared_ptr<gamestate::EntityFactory> factory, Value config) :
    runtime_{runtime}, state_{std::move(state)}, factory_{std::move(factory)}, config_{std::move(config)} {}

std::shared_ptr<TriggerCondition> OriginalScenario::condition(const gamestate::MissionTriggerCondition &ir) {
    const auto type = number(ir.args, "condition_type");
    // All variants present in the two source scenes. Unknown records must fail before play.
    const std::set<int64_t> supported{1, 4, 5, 6, 8, 10, 11, 13, 15, 22};
    if (not supported.contains(type)) throw std::invalid_argument{"Unsupported original condition " + std::to_string(type)};
    return std::make_shared<OriginalCondition>(shared_from_this(), ir.args);
}
std::shared_ptr<TriggerEffect> OriginalScenario::effect(const gamestate::MissionTriggerEffect &ir) {
    const auto type = number(ir.args, "effect_type");
    const std::set<int64_t> supported{2, 8, 9, 11, 12, 13, 15, 16, 20, 56};
    if (not supported.contains(type)) throw std::invalid_argument{"Unsupported original effect " + std::to_string(type)};
    return std::make_shared<OriginalEffect>(ir.args);
}

gamestate::player_id_t OriginalScenario::player(int64_t original) const {
    return static_cast<gamestate::player_id_t>(config_.at("player_map").at(std::to_string(original)).as_int());
}
const Value *OriginalScenario::definition(const gamestate::GameEntity &entity) const {
    const auto &definitions = config_.at("definitions");
    if (entity.genie_id >= 0 and definitions.has(std::to_string(entity.genie_id))) return &definitions.at(std::to_string(entity.genie_id));
    for (const auto &[id, value] : definitions.as_object()) {
        if (text(value, "fqon") == entity.definition) return &value;
    }
    return nullptr;
}

void OriginalScenario::initialize() {
    state_->get_event_loop()->add_event_handler(std::make_shared<OriginalEffectHandler>(weak_from_this()));
    state_->get_event_loop()->add_event_handler(std::make_shared<OriginalSelectionHandler>(weak_from_this()));
    for (const auto &record : config_.at("players").as_array()) {
        const auto slot = state_->get_player(static_cast<gamestate::player_id_t>(number(record, "runtime_id")));
        for (const auto *resource : {"Food", "Wood", "Stone", "Gold"}) {
            std::string key = resource;
            std::ranges::transform(key, key.begin(), [](unsigned char c) { return std::tolower(c); });
            const auto fqon = std::string{"de2_base.data.util.resource.types."} + resource;
            slot->spend(fqon, slot->get_resource(fqon));
            slot->add_resource(fqon, number(record, key.c_str(), 0));
        }
        for (const auto &subject : record.at("disabled_subjects").as_array()) slot->disable_action(subject.as_string());
    }
    for (const auto &record : config_.at("triggers").as_array()) {
        const auto index = number(record, "trigger_id");
        if (number(record, "display_as_objective", 0) and not number(record, "header", 0)) {
            const auto label = text(record, "description");
            if (not label.empty() and label != "Main Objectives") {
                objective_text_ += (objective_text_.empty() ? "" : "\n") + label;
                const auto name = "original." + std::to_string(index);
                runtime_.add_objective(name);
                runtime_.mission_state().set_objective_state(name, ObjectiveState::ACTIVE);
            }
        }
        if (number(record, "enabled", 0)) activate(index, true, time::time_t::zero());
    }
    runtime_.mission_state().set_instruction(objective_text_);
    changed(time::time_t::zero());
}

void OriginalScenario::schedule(int64_t index, time::time_t at) {
    runtime_.schedule_evaluation(trigger_id(index), at);
    const auto instance = runtime_.trigger(trigger_id(index));
    if (not instance) return;
    for (const auto &condition : instance->definition().conditions) {
        if (condition.kind == "AoE2Condition" and number(condition.args, "condition_type") == 10) {
            runtime_.schedule_evaluation(trigger_id(index), at + time::time_t::from_double(number(condition.args, "timer", 0)));
        }
    }
}
void OriginalScenario::activate(int64_t index, bool enabled, time::time_t at) {
    const auto instance = runtime_.trigger(trigger_id(index));
    if (not instance) throw std::invalid_argument{"Unknown original trigger " + std::to_string(index)};
    const bool previously = instance->state() == TriggerState::ARMED;
    instance->set_enabled(enabled);
    if (enabled and not previously and instance->state() == TriggerState::ARMED) activated_[trigger_id(index)] = at;
    if (enabled and instance->state() == TriggerState::ARMED) {
        if (not activated_.contains(trigger_id(index))) activated_[trigger_id(index)] = at;
        schedule(index, at);
    }
}

std::vector<std::shared_ptr<gamestate::GameEntity>> OriginalScenario::matching(const Value &record, time::time_t at, bool effects) const {
    std::set<gamestate::entity_id_t> selected;
    const auto source_id = number(record, "unit_object");
    if (source_id >= 0) {
        if (auto id = state_->get_entity_id_by_instance("object_" + std::to_string(source_id))) selected.insert(*id);
    }
    if (record.has("selected_object_ids") and record.at("selected_object_ids").is_array()) {
        for (const auto &item : record.at("selected_object_ids").as_array()) {
            if (auto id = state_->get_entity_id_by_instance("object_" + std::to_string(item.as_int()))) selected.insert(*id);
        }
    }
    const bool specific = source_id >= 0 or not selected.empty();
    std::vector<std::shared_ptr<gamestate::GameEntity>> result;
    for (const auto &[id, entity] : state_->get_game_entities()) {
        if (entity->is_dead() or (specific and not selected.contains(id))) continue;
        if (number(record, "source_player") >= 0 and owner(*entity, at) != player(number(record, "source_player"))) continue;
        const auto data = definition(*entity);
        const auto unit_id = number(record, effects ? "object_list_unit_id" : "object_list");
        if (unit_id >= 0 and entity->genie_id != unit_id) {
            if (not config_.at("definitions").has(std::to_string(unit_id)) or entity->definition != text(config_.at("definitions").at(std::to_string(unit_id)), "fqon")) continue;
        }
        if (number(record, "object_type") > 0 and (not data or number(*data, "object_type") != number(record, "object_type"))) continue;
        if (number(record, "object_group") >= 0 and (not data or number(*data, "class") != number(record, "object_group"))) continue;
        if (not in_area(record, position(*entity, at))) continue;
        result.push_back(entity);
    }
    std::ranges::sort(result, {}, &gamestate::GameEntity::get_id);
    return result;
}

bool OriginalScenario::evaluate(const Value &condition, time::time_t at) const {
    const auto type = number(condition, "condition_type");
    bool value = false;
    if (type == 10) {
        const auto start = activated_.find(trigger_id(number(condition, "parent")));
        value = start != activated_.end() and at >= start->second + time::time_t::from_double(number(condition, "timer", 0));
    }
    else if (type == 8) {
        constexpr const char *resources[]{"Food", "Wood", "Stone", "Gold"};
        const auto resource = number(condition, "attribute");
        if (resource >= 0 and resource < 4) {
            value = state_->get_player(player(number(condition, "source_player")))->get_resource(
                std::string{"de2_base.data.util.resource.types."} + resources[resource]) >= number(condition, "quantity");
        }
    }
    else if (type == 22) {
        const auto found = variables_.find(number(condition, "variable"));
        const auto amount = found == variables_.end() ? 0 : found->second;
        const auto requested = number(condition, "quantity", 0);
        switch (number(condition, "comparison", 0)) {
            case 0: value = amount == requested; break;
            case 1: value = amount < requested; break;
            case 2: value = amount > requested; break;
            case 3: value = amount <= requested; break;
            case 4: value = amount >= requested; break;
            default: throw std::invalid_argument{"Unsupported variable comparison"};
        }
    }
    else if (type == 6) {
        const auto id = state_->get_entity_id_by_instance("object_" + std::to_string(number(condition, "unit_object")));
        value = id and (not state_->has_game_entity(*id) or state_->get_game_entity(*id)->is_dead());
    }
    else if (type == 15 or type == 11) {
        const auto id = state_->get_entity_id_by_instance("object_" + std::to_string(number(condition, "unit_object")));
        if (id and state_->has_game_entity(*id)) {
            if (type == 15) {
                const auto point = position(*state_->get_game_entity(*id), at).to_tile();
                const auto visible = state_->visibility(0);
                value = visible and visible->at(point.ne, point.se) == gamestate::TileVisibility::Visible;
            }
            else {
                std::lock_guard lock{input_mutex_};
                value = selected_.contains(*id);
            }
        }
    }
    else if (type == 13) {
        const auto target = player(number(condition, "source_player"));
        value = std::ranges::none_of(state_->get_game_entities(), [&](const auto &entry) {
            const auto data = definition(*entry.second);
            return not entry.second->is_dead() and owner(*entry.second, at) == target and data and number(*data, "object_type") != 1;
        });
        if (target == player(0)) value = false;
    }
    else {
        const auto count = matching(condition, at, false).size();
        const auto quantity = std::max<int64_t>(1, number(condition, "quantity", 1));
        if (type == 4) value = count <= static_cast<size_t>(std::max<int64_t>(0, number(condition, "quantity", 0)));
        else value = count >= static_cast<size_t>(quantity);
    }
    return number(condition, "inverted", 0) == 1 ? not value : value;
}

void OriginalScenario::execute(const Value &effect, time::time_t at) {
    const auto type = number(effect, "effect_type");
    const auto loop = state_->get_event_loop();
    if (type == 20) {
        runtime_.mission_state().set_instruction(text(effect, "message"));
    }
    else if (type == 8 or type == 9) activate(number(effect, "trigger_id"), type == 8, at);
    else if (type == 13) {
        runtime_.mission_state().declare_victory("original_scenario_victory");
    }
    else if (type == 56) {
        const auto variable = number(effect, "variable");
        const auto amount = number(effect, "quantity", 0);
        switch (number(effect, "operation", 1)) {
            case 1: variables_[variable] = amount; break;
            case 2: variables_[variable] += amount; break;
            case 3: variables_[variable] -= amount; break;
            default: throw std::invalid_argument{"Unsupported original variable operation"};
        }
    }
    else if (type == 16) {
        std::lock_guard lock{input_mutex_};
        camera_ = coord::scene3{double(number(effect, "location_x")), double(number(effect, "location_y")), 0};
    }
    else if (type == 2) {
        const auto technology = config_.at("technologies").at(std::to_string(number(effect, "technology"))).as_string();
        if (not gamestate::event::force_scenario_research(*state_, player(number(effect, "source_player")), technology, at)) {
            throw std::runtime_error{"Original technology could not be applied: " + technology};
        }
    }
    else if (type == 11) {
        const auto unit_id = number(effect, "object_list_unit_id");
        const auto &data = config_.at("definitions").at(std::to_string(unit_id));
        const auto target = player(number(effect, "source_player"));
        auto entity = factory_->add_game_entity(loop, state_, target, text(data, "fqon"));
        entity->genie_id = unit_id;
        coord::phys3 point{coord::phys_t::from_double(number(effect, "location_x")), coord::phys_t::from_double(number(effect, "location_y")), 0};
        point.up = coord::phys_t::from_double(state_->get_map()->get_terrain()->height_at(point.ne.to_double(), point.se.to_double()));
        entity->try_get_component_as<gamestate::component::Position>(component_t::POSITION)->set_position(at, point);
        entity->try_get_component_as<gamestate::component::Ownership>(component_t::OWNERSHIP)->set_owner(at, target);
        state_->add_game_entity(entity);
        gamestate::register_entity_in_dynamic_occupancy(*state_->get_map(), *entity, point.ne.to_double(), point.se.to_double());
        if (auto activity = entity->try_get_component_as<gamestate::component::Activity>(component_t::ACTIVITY)) {
            activity->init(at);
            entity->get_manager()->run_activity_system(at);
        }
        state_->register_named_entity("created_" + std::to_string(created_++), entity->get_id());
    }
    else if (type == 15) {
        for (const auto &entity : matching(effect, at, true)) {
            loop->create_event("game.removal", entity->get_manager(), state_, at, {{"target_id", entity->get_id()}});
        }
    }
    else if (type == 12) {
        for (const auto &entity : matching(effect, at, true)) {
            if (not entity->has_component(component_t::MOVE)) continue;
            loop->create_event("game.send_command", entity->get_manager(), state_, at,
                               {{"type", gamestate::component::command::command_t::MOVE},
                                {"entity_ids", std::vector<gamestate::entity_id_t>{entity->get_id()}},
                                {"target", coord::phys3{coord::phys_t::from_double(number(effect, "location_x")),
                                                        coord::phys_t::from_double(number(effect, "location_y")), 0}}});
        }
    }
    else throw std::invalid_argument{"Unsupported original effect"};
    changed(at);
}

void OriginalScenario::complete_objective(TriggerId id) {
    const auto instance = runtime_.trigger(id);
    if (instance and runtime_.mission_state().objective_state(instance->definition().trigger_id) != ObjectiveState::INACTIVE) {
        runtime_.mission_state().set_objective_state(instance->definition().trigger_id, ObjectiveState::COMPLETED);
    }
}
void OriginalScenario::changed(time::time_t at) {
    capture_gaia(at);
    state_->update_visibility(at);
    for (const auto &record : config_.at("triggers").as_array()) {
        const auto id = trigger_id(number(record, "trigger_id"));
        const auto instance = runtime_.trigger(id);
        if (instance and instance->state() == TriggerState::ARMED) runtime_.schedule_evaluation(id, at);
    }
    runtime_.mission_state().notify_progress();
}
void OriginalScenario::capture_gaia(time::time_t at) {
    std::vector<coord::phys3> scouts;
    for (const auto &[id, entity] : state_->get_game_entities()) {
        const auto data = definition(*entity);
        if (data and not entity->is_dead() and owner(*entity, at) == 0 and number(*data, "object_type") != 1) scouts.push_back(position(*entity, at));
    }
    for (bool captured = true; captured;) {
        captured = false;
        for (const auto &[id, entity] : state_->get_game_entities()) {
            const auto data = definition(*entity);
            if (not data or not data->at("capturable").as_bool() or entity->is_dead() or owner(*entity, at) != player(0)) continue;
            const auto point = position(*entity, at);
            const auto reach = 5.0 + data->at("radius_x").as_number();
            if (std::ranges::any_of(scouts, [&](const auto &scout) {
                return std::hypot((scout.ne - point.ne).to_double(), (scout.se - point.se).to_double()) <= reach;
            })) {
                entity->try_get_component_as<gamestate::component::Ownership>(component_t::OWNERSHIP)->set_owner(at, 0);
                scouts.push_back(point);
                captured = true;
            }
        }
    }
}
void OriginalScenario::select(const std::vector<gamestate::entity_id_t> &ids, time::time_t at) {
    {
        std::lock_guard lock{input_mutex_};
        selected_ = {ids.begin(), ids.end()};
    }
    changed(at);
}
std::optional<coord::scene3> OriginalScenario::take_camera() {
    std::lock_guard lock{input_mutex_};
    return std::exchange(camera_, std::nullopt);
}
}
