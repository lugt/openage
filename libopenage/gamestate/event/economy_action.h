// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include <deque>
#include <unordered_map>
#include "event/eventhandler.h"
#include "gamestate/action_option.h"
#include "gamestate/types.h"

namespace openage::gamestate {
class EntityFactory;
class GameState;
class Player;

namespace event {

/** Query the same prerequisites used when executing a production order. */
bool action_available(const ActionOption &option, const GameState &game, const Player &player, const time::time_t &at);

bool force_scenario_research(GameState &game, player_id_t player, const nyan::fqon_t &technology, const time::time_t &at);

class EconomyActionHandler final : public openage::event::OnceEventHandler {
public:
	explicit EconomyActionHandler(std::shared_ptr<EntityFactory> factory);

	void setup_event(const std::shared_ptr<openage::event::Event> &,
	                 const std::shared_ptr<openage::event::State> &) override;
	void invoke(openage::event::EventLoop &, const std::shared_ptr<openage::event::EventEntity> &,
	            const std::shared_ptr<openage::event::State> &, const time::time_t &, const param_map &) override;
	time::time_t predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> &,
	                                const std::shared_ptr<openage::event::State> &,
	                                const time::time_t &) override;

	/** Committed production, including jobs in all producers owned by this player. */
	int64_t queued_population(player_id_t player) const;
	bool busy(entity_id_t actor) const;

private:
	struct Job {
		player_id_t owner;
		ActionOption option;
		int64_t population = 0;
	};
	std::shared_ptr<EntityFactory> factory;
	std::unordered_map<entity_id_t, std::deque<Job>> jobs;
};

} // namespace event
} // namespace openage::gamestate
