// Copyright 2026 the openage authors. See copying.md for legal info.
#pragma once

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

#include "coord/scene.h"
#include "gamestate/types.h"
#include "scenario/mission_pack.h"
#include "scenario/trigger/trigger_definition.h"

namespace openage::gamestate { class EntityFactory; class GameState; class GameEntity; }
namespace openage::event { class EventLoop; }
namespace openage::scenario::trigger {
class TriggerRuntime;
class TriggerCondition;
class TriggerEffect;

/** Original AoE2 records executed through the existing trigger/event runtime. */
class OriginalScenario : public std::enable_shared_from_this<OriginalScenario> {
public:
    OriginalScenario(TriggerRuntime &runtime, std::shared_ptr<gamestate::GameState> state,
                     std::shared_ptr<gamestate::EntityFactory> factory, util::json::Value config);
    void initialize();
    std::shared_ptr<TriggerCondition> condition(const gamestate::MissionTriggerCondition &ir);
    std::shared_ptr<TriggerEffect> effect(const gamestate::MissionTriggerEffect &ir);
    bool evaluate(const util::json::Value &condition, time::time_t at) const;
    void execute(const util::json::Value &effect, time::time_t at);
    void changed(time::time_t at);
    void select(const std::vector<gamestate::entity_id_t> &ids, time::time_t at);
    std::optional<coord::scene3> take_camera();
    void complete_objective(TriggerId id);
    const util::json::Value &config() const { return this->config_; }

private:
    void activate(int64_t index, bool enabled, time::time_t at);
    void schedule(int64_t index, time::time_t at);
    gamestate::player_id_t player(int64_t original) const;
    const util::json::Value *definition(const gamestate::GameEntity &entity) const;
    std::vector<std::shared_ptr<gamestate::GameEntity>> matching(const util::json::Value &record, time::time_t at, bool effects) const;
    void capture_gaia(time::time_t at);
    std::string substitute_variables(const std::string &tmpl) const;
    TriggerRuntime &runtime_;
    std::shared_ptr<gamestate::GameState> state_;
    std::shared_ptr<gamestate::EntityFactory> factory_;
    util::json::Value config_;
    std::map<TriggerId, time::time_t> activated_;
    std::map<int64_t, int64_t> variables_;
    std::map<std::string, int64_t> variable_ids_;
    mutable std::mutex input_mutex_;
    std::set<gamestate::entity_id_t> selected_;
    std::optional<coord::scene3> camera_;
    uint64_t created_{0};
    std::string objective_text_;
    std::string current_instruction_;
};
}
