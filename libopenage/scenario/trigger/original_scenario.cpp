// Copyright 2026 the openage authors. See copying.md for legal info.
#include "original_scenario.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <stdexcept>
#include <utility>

#include "event/event_loop.h"
#include "error/error.h"
#include "coord/tile.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/build.h"
#include "gamestate/component/api/live.h"
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
bool in_area(int64_t area_x1, int64_t area_x2, int64_t area_y1, int64_t area_y2, const coord::phys3 &point) {
    if (area_x1 < 0) return true;
    const auto x = std::floor(point.ne.to_double()), y = std::floor(point.se.to_double());
    return x >= area_x1 and x <= area_x2 and y >= area_y1 and y <= area_y2;
}

bool compare_value(int64_t lhs, int64_t rhs, int64_t op) {
    switch (op) {
        case 0: return lhs == rhs;
        case 1: return lhs < rhs;
        case 2: return lhs > rhs;
        case 3: return lhs <= rhs;
        case 4: return lhs >= rhs;
        default: return false;
    }
}
std::optional<std::string> resource_fqon(int64_t attribute) {
    constexpr const char *resources[]{"Food", "Wood", "Stone", "Gold"};
    if (attribute >= 0 and attribute < 4) {
        return std::string{"de2_base.data.util.resource.types."} + resources[attribute];
    }
    return std::nullopt;
}
gamestate::DiplomaticStance to_stance(int64_t value) {
    switch (value) {
        case 0: return gamestate::DiplomaticStance::Ally;
        case 3: return gamestate::DiplomaticStance::Enemy;
        default: return gamestate::DiplomaticStance::Neutral;
    }
}
int64_t from_stance(gamestate::DiplomaticStance stance) {
    switch (stance) {
        case gamestate::DiplomaticStance::Self:
        case gamestate::DiplomaticStance::Ally: return 0;
        case gamestate::DiplomaticStance::Enemy: return 3;
        case gamestate::DiplomaticStance::Neutral: return 1;
    }
    return 1;
}

class OriginalCondition final : public TriggerCondition {
public:
    OriginalCondition(std::shared_ptr<OriginalScenario> source, Value args) : source{std::move(source)}, args{std::move(args)} {}
    bool evaluate(const gamestate::GameState &, time::time_t at) const override { return source->evaluate(args, at); }
    std::vector<DependencyKey> dependencies() const override { return {}; }
    bool is_or_separator() const override { return number(this->args, "condition_type") == 29; }
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
    runtime_{runtime}, state_{std::move(state)}, factory_{std::move(factory)}, config_{std::move(config)} {
    if (this->config_.has("command_categories")) {
        for (const auto &[subject, category] : this->config_.at("command_categories").as_object()) {
            const auto name = category.as_string();
            if (name != "economic" and name != "military") {
                throw Error{MSG(err) << "Unknown build command category: " << name};
            }
            this->state_->command_categories[subject] = name;
        }
    }
    for (const auto &[orig_key, runtime_val] : this->config_.at("player_map").as_object()) {
        this->player_map_[std::stoll(orig_key)] = static_cast<gamestate::player_id_t>(runtime_val.as_int());
    }
    const auto &definitions = this->config_.at("definitions").as_object();
    this->defs_by_genie_id_.reserve(definitions.size());
    this->defs_by_fqon_.reserve(definitions.size());
    for (const auto &[id_key, value] : definitions) {
        DefinitionMeta meta{
            number(value, "object_type"),
            number(value, "class"),
            value.has("capturable") and value.at("capturable").is_bool() ? value.at("capturable").as_bool() : false,
            value.has("radius_x") and value.at("radius_x").is_number() ? value.at("radius_x").as_number() : 0.0,
            text(value, "fqon"),
        };
        const auto genie_id = std::stoll(id_key);
        auto [it, inserted] = this->defs_by_genie_id_.emplace(genie_id, std::move(meta));
        if (not it->second.fqon.empty()) {
            this->defs_by_fqon_[it->second.fqon] = &it->second;
        }
    }
    const auto &triggers = this->config_.at("triggers").as_array();
    this->trigger_ids_.reserve(triggers.size());
    for (const auto &record : triggers) {
        this->trigger_ids_.push_back(trigger_id(number(record, "trigger_id")));
    }
}

std::shared_ptr<TriggerCondition> OriginalScenario::condition(const gamestate::MissionTriggerCondition &ir) {
    return std::make_shared<OriginalCondition>(shared_from_this(), ir.args);
}
std::shared_ptr<TriggerEffect> OriginalScenario::effect(const gamestate::MissionTriggerEffect &ir) {
    return std::make_shared<OriginalEffect>(ir.args);
}

gamestate::player_id_t OriginalScenario::player(int64_t original) const {
    return this->player_map_.at(original);
}
const OriginalScenario::DefinitionMeta *OriginalScenario::definition(const gamestate::GameEntity &entity) const {
    if (entity.genie_id >= 0) {
        if (const auto it = this->defs_by_genie_id_.find(entity.genie_id); it != this->defs_by_genie_id_.end()) {
            return &it->second;
        }
    }
    if (const auto it = this->defs_by_fqon_.find(entity.definition); it != this->defs_by_fqon_.end()) {
        return it->second;
    }
    return nullptr;
}

void OriginalScenario::initialize() {
    state_->get_event_loop()->add_event_handler(std::make_shared<OriginalEffectHandler>(weak_from_this()));
    state_->get_event_loop()->add_event_handler(std::make_shared<OriginalSelectionHandler>(weak_from_this()));
    if (config_.has("variables") and config_.at("variables").is_array()) {
        for (const auto &var : config_.at("variables").as_array()) {
            const auto name = text(var, "name");
            const auto id = number(var, "variable_id", -1);
            if (not name.empty() and id >= 0) {
                variable_ids_[name] = id;
            }
        }
    }
    for (const auto &record : config_.at("players").as_array()) {
        const auto slot = state_->get_player(static_cast<gamestate::player_id_t>(number(record, "runtime_id")));
        slot->is_gaia = slot->get_id() == player(0);
        if (record.has("allowed_subjects")) {
            std::unordered_set<nyan::fqon_t> allowed;
            for (const auto &subject : record.at("allowed_subjects").as_array()) allowed.insert(subject.as_string());
            slot->set_allowed_actions(std::move(allowed));
        }
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
    current_instruction_ = objective_text_;
    runtime_.mission_state().set_instruction(substitute_variables(current_instruction_));
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

template <typename Fn>
void OriginalScenario::for_each_matching(const Value &record, time::time_t at, bool effects, Fn &&fn) const {
    std::vector<gamestate::entity_id_t> selected;
    const auto source_id = number(record, "unit_object");
    if (source_id >= 0) {
        if (auto id = state_->get_entity_id_by_instance("object_" + std::to_string(source_id))) {
            selected.push_back(*id);
        }
    }
    if (record.has("selected_object_ids") and record.at("selected_object_ids").is_array()) {
        for (const auto &item : record.at("selected_object_ids").as_array()) {
            if (auto id = state_->get_entity_id_by_instance("object_" + std::to_string(item.as_int()))) {
                selected.push_back(*id);
            }
        }
    }
    const bool specific = source_id >= 0 or not selected.empty();
    if (specific and selected.empty()) {
        return;
    }
    if (selected.size() > 1) {
        std::ranges::sort(selected);
        const auto [first, last] = std::ranges::unique(selected);
        selected.erase(first, last);
    }

    const auto raw_source_player = number(record, "source_player");
    const std::optional<gamestate::player_id_t> required_owner =
        raw_source_player >= 0 ? std::make_optional(this->player(raw_source_player)) : std::nullopt;
    const auto unit_id = number(record, effects ? "object_list_unit_id" : "object_list");
    const std::string *target_fqon = nullptr;
    if (unit_id >= 0) {
        if (const auto def_it = this->defs_by_genie_id_.find(unit_id); def_it != this->defs_by_genie_id_.end()) {
            target_fqon = &def_it->second.fqon;
        }
    }
    const auto object_type = number(record, "object_type");
    const auto object_group = number(record, "object_group");
    const bool check_construction = not effects and number(record, "condition_type") == 3;
    const auto area_x1 = number(record, "area_x1");
    const auto area_x2 = number(record, "area_x2");
    const auto area_y1 = number(record, "area_y1");
    const auto area_y2 = number(record, "area_y2");

    auto check_and_visit = [&](const std::shared_ptr<gamestate::GameEntity> &entity) {
        if (not entity or entity->is_dead()) {
            return;
        }
        if (required_owner and owner(*entity, at) != *required_owner) {
            return;
        }
        if (unit_id >= 0 and entity->genie_id != unit_id) {
            if (not target_fqon or entity->definition != *target_fqon) {
                return;
            }
        }
        if (object_type > 0 or object_group >= 0) {
            const auto *data = this->definition(*entity);
            if (object_type > 0 and (not data or data->object_type != object_type)) {
                return;
            }
            if (object_group >= 0 and (not data or data->object_class != object_group)) {
                return;
            }
        }
        if (check_construction) {
            if (const auto build = entity->try_get_component_as<gamestate::component::Build>(component_t::BUILD);
                build and build->is_under_construction()) {
                return;
            }
        }
        if (area_x1 >= 0 and not in_area(area_x1, area_x2, area_y1, area_y2, position(*entity, at))) {
            return;
        }
        fn(entity);
    };

    if (specific) {
        for (const auto id : selected) {
            if (state_->has_game_entity(id)) {
                check_and_visit(state_->get_game_entity(id));
            }
        }
        return;
    }

    for (const auto &[id, entity] : state_->get_game_entities()) {
        check_and_visit(entity);
    }
}

size_t OriginalScenario::count_matching(const Value &record, time::time_t at) const {
    size_t count = 0;
    this->for_each_matching(record, at, false, [&count](const std::shared_ptr<gamestate::GameEntity> &) {
        ++count;
    });
    return count;
}

std::vector<std::shared_ptr<gamestate::GameEntity>> OriginalScenario::matching(const Value &record, time::time_t at, bool effects) const {
    std::vector<std::shared_ptr<gamestate::GameEntity>> result;
    this->for_each_matching(record, at, effects, [&result](const std::shared_ptr<gamestate::GameEntity> &entity) {
        result.push_back(entity);
    });
    std::ranges::sort(result, {}, &gamestate::GameEntity::get_id);
    return result;
}

bool OriginalScenario::evaluate(const Value &condition, time::time_t at) const {
    const auto type = number(condition, "condition_type");
    bool value = false;
    if (type == 0 or type == 29 or type == 34 or type == 57) {
        value = true;
    }
    else if (type == 10) {
        const auto start = activated_.find(trigger_id(number(condition, "parent")));
        value = start != activated_.end() and at >= start->second + time::time_t::from_double(number(condition, "timer", 0));
    }
    else if (type == 8) {
        const auto attribute = number(condition, "attribute");
        const auto target = player(number(condition, "source_player"));
        const auto requested = number(condition, "quantity", 0);
        if (const auto fqon = resource_fqon(attribute)) {
            value = state_->get_player(target)->get_resource(*fqon) >= requested;
        }
        else if (attribute == 11) {
            int64_t pop = 0;
            for (const auto &[id, entity] : state_->get_game_entities()) {
                if (entity->is_dead() or owner(*entity, at) != target) {
                    continue;
                }
                const auto *data = this->definition(*entity);
                if (data and data->object_type != 1 and data->object_type != 2) {
                    ++pop;
                }
            }
            value = pop >= requested;
        }
        else {
            value = false;
        }
    }
    else if (type == 22) {
        const auto found = variables_.find(number(condition, "variable"));
        const auto amount = found == variables_.end() ? 0 : found->second;
        const auto requested = number(condition, "quantity", 0);
        value = compare_value(amount, requested, number(condition, "comparison", 0));
    }
    else if (type == 6) {
        const auto id = state_->get_entity_id_by_instance("object_" + std::to_string(number(condition, "unit_object")));
        value = id and (not state_->has_game_entity(*id) or state_->get_game_entity(*id)->is_dead());
    }
    else if (type == 7) {
        const auto id = state_->get_entity_id_by_instance("object_" + std::to_string(number(condition, "unit_object")));
        if (id and state_->has_game_entity(*id)) {
            const auto &entity = state_->get_game_entity(*id);
            value = not entity->is_dead() and owner(*entity, at) == player(number(condition, "source_player"));
        }
    }
    else if (type == 2) {
        const auto id1 = state_->get_entity_id_by_instance("object_" + std::to_string(number(condition, "unit_object")));
        const auto id2 = state_->get_entity_id_by_instance("object_" + std::to_string(number(condition, "next_object")));
        if (id1 and id2 and state_->has_game_entity(*id1) and state_->has_game_entity(*id2)) {
            const auto &e1 = state_->get_game_entity(*id1);
            const auto &e2 = state_->get_game_entity(*id2);
            if (not e1->is_dead() and not e2->is_dead()) {
                const auto p1 = position(*e1, at);
                const auto p2 = position(*e2, at);
                value = std::hypot((p1.ne - p2.ne).to_double(), (p1.se - p2.se).to_double()) <= 2.5;
            }
        }
    }
    else if (type == 9 or type == 21) {
        const auto target = player(number(condition, "source_player"));
        const auto tech_id = number(condition, "technology");
        bool researched = false;
        if (const auto it = researched_techs_.find(target); it != researched_techs_.end()) {
            researched = it->second.contains(tech_id);
        }
        if (not researched and config_.at("technologies").has(std::to_string(tech_id))) {
            const auto fqon = config_.at("technologies").at(std::to_string(tech_id)).as_string();
            researched = state_->get_player(target)->has_researched(fqon);
        }
        if (type == 9) {
            value = researched;
        }
        else {
            const auto state_val = researched ? 3 : 1;
            value = state_val == number(condition, "quantity", 3);
        }
    }
    else if (type == 12 or type == 30) {
        value = ai_signals_.contains(number(condition, "ai_signal"));
    }
    else if (type == 15 or type == 16 or type == 27 or type == 11 or type == 26) {
        const auto id = state_->get_entity_id_by_instance("object_" + std::to_string(number(condition, "unit_object")));
        if (id and state_->has_game_entity(*id)) {
            if (type == 15 or type == 16 or type == 27) {
                const auto point = position(*state_->get_game_entity(*id), at).to_tile();
                const auto visible = state_->visibility(0);
                const bool is_visible = visible and visible->at(point.ne, point.se) == gamestate::TileVisibility::Visible;
                value = (type == 16) ? not is_visible : is_visible;
            }
            else {
                std::lock_guard lock{input_mutex_};
                value = selected_.contains(*id);
            }
        }
        else if (type == 16) {
            value = true;
        }
    }
    else if (type == 19) {
        value = number(condition, "quantity", 2) == this->difficulty_;
    }
    else if (type == 20) {
        const auto pct = number(condition, "quantity", 0);
        const auto h = static_cast<int64_t>((static_cast<uint64_t>(at.to_double() * 1000) + static_cast<uint64_t>(number(condition, "parent", 0)) * 2654435761ULL) % 100);
        value = h < pct;
    }
    else if (type == 23) {
        const auto id = state_->get_entity_id_by_instance("object_" + std::to_string(number(condition, "unit_object")));
        if (id and state_->has_game_entity(*id) and not state_->get_game_entity(*id)->is_dead()) {
            if (const auto live = state_->get_game_entity(*id)->try_get_component_as<gamestate::component::Live>(component_t::LIVE)) {
                if (const auto hp = live->get_attribute_value(at, "de2_base.data.util.attribute.types.Health")) {
                    value = compare_value(*hp, number(condition, "quantity", 0), number(condition, "comparison", 0));
                }
            }
        }
    }
    else if (type == 24) {
        const auto src = player(number(condition, "source_player"));
        const auto dst = player(number(condition, "target_player"));
        value = from_stance(state_->get_stance(src, dst)) == number(condition, "quantity", 0);
    }
    else if (type == 14 or type == 17 or type == 18 or type == 25 or type == 28 or type == 54 or type == 55 or type == 56) {
        value = false;
    }
    else if (type == 13) {
        const auto target = player(number(condition, "source_player"));
        value = std::ranges::none_of(state_->get_game_entities(), [&](const auto &entry) {
            if (entry.second->is_dead() or owner(*entry.second, at) != target) {
                return false;
            }
            const auto *data = this->definition(*entry.second);
            return data and data->object_type != 1;
        });
        if (target == player(0)) value = false;
    }
    else if (type == 1 or type == 3 or type == 4 or type == 5) {
        const auto count = this->count_matching(condition, at);
        const auto quantity = std::max<int64_t>(1, number(condition, "quantity", 1));
        if (type == 4) value = count <= static_cast<size_t>(std::max<int64_t>(0, number(condition, "quantity", 0)));
        else value = count >= static_cast<size_t>(quantity);
    }
    else {
        log::log(MSG(warn) << "OriginalScenario: unhandled condition_type " << type);
        value = false;
    }
    return number(condition, "inverted", 0) == 1 ? not value : value;
}

void OriginalScenario::execute(const Value &effect, time::time_t at) {
    const auto type = number(effect, "effect_type");
    const auto loop = state_->get_event_loop();
    if (type == 20) {
        current_instruction_ = text(effect, "message");
        runtime_.mission_state().set_instruction(substitute_variables(current_instruction_));
    }
    else if (type == 21) {
        current_instruction_ = objective_text_;
        runtime_.mission_state().set_instruction(substitute_variables(current_instruction_));
    }
    else if (type == 3) {
        log::log(MSG(info) << "OriginalScenario chat: " << text(effect, "message"));
    }
    else if (type == 1) {
        const auto src = player(number(effect, "source_player"));
        const auto dst = player(number(effect, "target_player"));
        state_->set_stance(src, dst, to_stance(number(effect, "diplomacy", 1)));
    }
    else if (type == 5) {
        if (const auto fqon = resource_fqon(number(effect, "attribute"))) {
            const auto amount = number(effect, "quantity", 0);
            const auto src = state_->get_player(player(number(effect, "source_player")));
            const auto dst = state_->get_player(player(number(effect, "target_player")));
            if (amount > 0) {
                const auto available = std::min(amount, src->get_resource(*fqon));
                if (available > 0) src->spend(*fqon, available);
                dst->add_resource(*fqon, amount);
            }
        }
    }
    else if (type == 8 or type == 9) {
        const auto tid = number(effect, "trigger_id");
        if (runtime_.trigger(trigger_id(tid))) {
            activate(tid, type == 8, at);
        }
        else {
            log::log(MSG(warn) << "OriginalScenario: ignoring unknown trigger " << tid << " in effect " << type);
        }
    }
    else if (type == 13) {
        const auto raw_player = number(effect, "source_player", -1);
        if (raw_player < 0 or player(raw_player) == 0) {
            runtime_.mission_state().declare_victory("original_scenario_victory");
        }
    }
    else if (type == 56) {
        const auto variable = number(effect, "variable");
        const auto amount = number(effect, "quantity", 0);
        switch (number(effect, "operation", 1)) {
            case 1: variables_[variable] = amount; break;
            case 2: variables_[variable] += amount; break;
            case 3: variables_[variable] -= amount; break;
            case 4: variables_[variable] *= amount; break;
            case 5: if (amount != 0) variables_[variable] /= amount; break;
            default: log::log(MSG(warn) << "OriginalScenario: unsupported variable operation " << number(effect, "operation")); break;
        }
        runtime_.mission_state().set_instruction(substitute_variables(current_instruction_));
    }
    else if (type == 52 or type == 53) {
        if (const auto fqon = resource_fqon(number(effect, "attribute"))) {
            const auto target = state_->get_player(player(number(effect, "source_player")));
            int64_t amount = number(effect, "quantity", 0);
            if (type == 53) {
                const auto it = variables_.find(number(effect, "variable"));
                amount = it != variables_.end() ? it->second : 0;
            }
            const auto op = number(effect, "operation", 2);
            if (op == 1) {
                target->spend(*fqon, target->get_resource(*fqon));
                if (amount > 0) target->add_resource(*fqon, amount);
            }
            else if (op == 3) {
                if (amount > 0) target->spend(*fqon, std::min(amount, target->get_resource(*fqon)));
            }
            else {
                if (amount > 0) target->add_resource(*fqon, amount);
                else if (amount < 0) target->spend(*fqon, std::min(-amount, target->get_resource(*fqon)));
            }
        }
    }
    else if (type == 16) {
        std::lock_guard lock{input_mutex_};
        camera_ = coord::scene3{double(number(effect, "location_x")), double(number(effect, "location_y")), 0};
    }
    else if (type == 2) {
        const auto tech_id = number(effect, "technology");
        const auto key = std::to_string(tech_id);
        const auto target_player = player(number(effect, "source_player"));
        this->researched_techs_[target_player].insert(tech_id);
        const auto &techs = config_.at("technologies");
        if (techs.has(key)) {
            const auto technology = techs.at(key).as_string();
            try {
                if (not gamestate::event::force_scenario_research(*state_, target_player, technology, at)) {
                    log::log(MSG(warn) << "OriginalScenario: technology could not be applied: " << technology);
                }
            }
            catch (const std::exception &err) {
                log::log(MSG(warn) << "OriginalScenario: technology '" << technology << "' failed: " << err.what());
            }
        }
        else {
            log::log(MSG(warn) << "OriginalScenario: unmapped technology id " << tech_id);
        }
    }
    else if (type == 11 or type == 49) {
        const auto unit_id = number(effect, "object_list_unit_id");
        const auto key = std::to_string(unit_id);
        if (unit_id >= 0 and config_.at("definitions").has(key)) {
            const auto &data = config_.at("definitions").at(key);
            const auto target = player(number(effect, "source_player"));
            std::string fqon = text(data, "fqon");
            if (fqon == "de2_base.data.game_entity.generic.fire_trireme.fire_trireme.FireTrireme") {
                fqon = "de2_base.data.game_entity.generic.fire_galley.fire_galley.FireGalley";
            }
            else if (fqon == "de2_base.data.game_entity.generic.demo_ship.demo_ship.DemolitionShip") {
                fqon = "de2_base.data.game_entity.generic.demolition_raft.demolition_raft.DemolitionRaft";
            }
            auto entity = factory_->add_game_entity(loop, state_, target, fqon);
            entity->genie_id = unit_id;
            const auto map_size = state_->get_map()->get_size();
            const double lx = std::clamp(double(number(effect, "location_x", 0)), 0.0, std::max(0.0, double(map_size[0]) - 0.01));
            const double ly = std::clamp(double(number(effect, "location_y", 0)), 0.0, std::max(0.0, double(map_size[1]) - 0.01));
            coord::phys3 point{coord::phys_t::from_double(lx), coord::phys_t::from_double(ly), 0};
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
            if (const auto *meta = this->definition(*entity); meta and meta->capturable) {
                this->capturable_entities_.push_back(entity->get_id());
            }
        }
    }
    else if (type == 14 or type == 15) {
        for (const auto &entity : matching(effect, at, true)) {
            loop->create_event("game.removal", entity->get_manager(), state_, at, {{"target_id", entity->get_id()}});
        }
    }
    else if (type == 18) {
        const auto target_owner = player(number(effect, "target_player"));
        for (const auto &entity : matching(effect, at, true)) {
            if (auto own = entity->try_get_component_as<gamestate::component::Ownership>(component_t::OWNERSHIP)) {
                own->set_owner(at, target_owner);
            }
        }
    }
    else if (type == 12 or type == 19 or type == 30) {
        for (const auto &entity : matching(effect, at, true)) {
            if (not entity->has_component(component_t::MOVE)) continue;
            loop->create_event("game.send_command", entity->get_manager(), state_, at,
                               {{"type", gamestate::component::command::command_t::MOVE},
                                {"entity_ids", std::vector<gamestate::entity_id_t>{entity->get_id()}},
                                {"target", coord::phys3{coord::phys_t::from_double(number(effect, "location_x")),
                                                        coord::phys_t::from_double(number(effect, "location_y")), 0}}});
        }
    }
    else if (type == 24 or type == 34 or type == 27) {
        const auto amount = number(effect, "quantity", 0);
        const nyan::fqon_t health = "de2_base.data.util.attribute.types.Health";
        for (const auto &entity : matching(effect, at, true)) {
            if (auto live = entity->try_get_component_as<gamestate::component::Live>(component_t::LIVE)) {
                if (const auto cur = live->get_attribute_value(at, health)) {
                    int64_t next = *cur;
                    if (type == 24) next = std::max<int64_t>(0, *cur - amount);
                    else if (type == 34) {
                        const auto max_hp = live->get_attribute_max(health).value_or(*cur + amount);
                        next = std::min(max_hp, *cur + amount);
                    }
                    else next = std::max<int64_t>(1, amount);
                    live->set_attribute(at, health, next);
                }
            }
        }
    }
    else if (type == 35) {
        const auto map_size = state_->get_map()->get_size();
        const double lx = std::clamp(double(number(effect, "location_x", 0)), 0.0, std::max(0.0, double(map_size[0]) - 0.01));
        const double ly = std::clamp(double(number(effect, "location_y", 0)), 0.0, std::max(0.0, double(map_size[1]) - 0.01));
        coord::phys3 point{coord::phys_t::from_double(lx), coord::phys_t::from_double(ly), 0};
        point.up = coord::phys_t::from_double(state_->get_map()->get_terrain()->height_at(point.ne.to_double(), point.se.to_double()));
        for (const auto &entity : matching(effect, at, true)) {
            if (auto pos = entity->try_get_component_as<gamestate::component::Position>(component_t::POSITION)) {
                pos->set_position(at, point);
            }
        }
    }
    else if (type == 38 or type == 39) {
        // Enable/disable object or technology: disable_action when disabling.
        const auto slot = state_->get_player(player(number(effect, "source_player")));
        if (number(effect, "quantity", 0) == 0) {
            if (type == 38) {
                const auto key = std::to_string(number(effect, "object_list_unit_id"));
                if (config_.at("definitions").has(key)) {
                    slot->disable_action(text(config_.at("definitions").at(key), "fqon"));
                }
            }
            else {
                const auto key = std::to_string(number(effect, "technology"));
                if (config_.at("technologies").has(key)) {
                    slot->disable_action(config_.at("technologies").at(key).as_string());
                }
            }
        }
    }
    else if (type == 0 or type == 4 or type == 6 or type == 7 or type == 10 or type == 17 or type == 22
             or type == 25 or type == 26 or type == 28 or type == 29 or type == 31 or type == 32 or type == 33
             or type == 36 or type == 37 or type == 40 or type == 41 or type == 42 or type == 43 or type == 44
             or type == 45 or type == 46 or type == 47 or type == 48 or type == 50 or type == 51 or type == 54
             or type == 55 or type == 57 or type == 58 or type == 59 or type == 60 or type == 61 or type == 62
             or type == 63 or type == 64 or type == 65 or type == 66 or type == 69 or type == 70 or type == 71
             or type == 72 or type == 73 or type == 74 or type == 79 or type == 80 or type == 86 or type == 87
             or type == 88 or type == 89 or type == 90 or type == 91 or type == 98 or type == 99 or type == 100
             or type == 101 or type == 102 or type == 103 or type == 104 or type == 105 or type == 106 or type == 107
             or type == 108 or type == 109) {
        // Sound, UI/visual/naming, gate lock, or stat-modifier effects not yet modeled in runtime.
    }
    else {
        log::log(MSG(warn) << "OriginalScenario: unhandled effect_type " << type);
    }
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
    for (const auto &id : this->trigger_ids_) {
        const auto instance = runtime_.trigger(id);
        if (instance and instance->state() == TriggerState::ARMED) runtime_.schedule_evaluation(id, at);
    }
    runtime_.mission_state().notify_progress();
}
void OriginalScenario::capture_gaia(time::time_t at) {
    if (not this->capturable_indexed_) {
        this->capturable_entities_.clear();
        for (const auto &[id, entity] : state_->get_game_entities()) {
            const auto *data = this->definition(*entity);
            if (data and data->capturable) {
                this->capturable_entities_.push_back(id);
            }
        }
        this->capturable_indexed_ = true;
    }
    if (this->capturable_entities_.empty()) {
        return;
    }
    const auto gaia_player = player(0);
    bool has_gaia_capturable = false;
    for (const auto id : this->capturable_entities_) {
        if (state_->has_game_entity(id)) {
            const auto &entity = state_->get_game_entity(id);
            if (not entity->is_dead() and owner(*entity, at) == gaia_player) {
                has_gaia_capturable = true;
                break;
            }
        }
    }
    if (not has_gaia_capturable) {
        return;
    }
    std::vector<coord::phys3> scouts;
    for (const auto &[id, entity] : state_->get_game_entities()) {
        if (entity->is_dead() or owner(*entity, at) != 0) {
            continue;
        }
        const auto *data = this->definition(*entity);
        if (data and data->object_type != 1) {
            scouts.push_back(position(*entity, at));
        }
    }
    if (scouts.empty()) {
        return;
    }
    for (bool captured = true; captured;) {
        captured = false;
        for (const auto id : this->capturable_entities_) {
            if (not state_->has_game_entity(id)) continue;
            const auto &entity = state_->get_game_entity(id);
            if (entity->is_dead() or owner(*entity, at) != gaia_player) continue;
            const auto *data = this->definition(*entity);
            if (not data or not data->capturable) continue;
            const auto point = position(*entity, at);
            const auto reach = 5.0 + data->radius_x;
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
std::string OriginalScenario::substitute_variables(const std::string &tmpl) const {
    if (tmpl.find('<') == std::string::npos) {
        return tmpl;
    }
    std::string out;
    out.reserve(tmpl.size());
    size_t pos = 0;
    while (pos < tmpl.size()) {
        const auto open = tmpl.find('<', pos);
        if (open == std::string::npos) {
            out.append(tmpl, pos, std::string::npos);
            break;
        }
        const auto close = tmpl.find('>', open + 1);
        if (close == std::string::npos) {
            out.append(tmpl, pos, std::string::npos);
            break;
        }
        out.append(tmpl, pos, open - pos);
        const auto name = tmpl.substr(open + 1, close - open - 1);
        if (const auto id_it = variable_ids_.find(name); id_it != variable_ids_.end()) {
            const auto val_it = variables_.find(id_it->second);
            out.append(std::to_string(val_it != variables_.end() ? val_it->second : 0));
        }
        else {
            out.append(tmpl, open, close - open + 1);
        }
        pos = close + 1;
    }
    return out;
}
}
