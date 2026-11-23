// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "event/eventhandler.h"
#include <functional>
#include "gamestate/types.h"

namespace openage::gamestate { class GameState; }

namespace openage::gamestate::event {

class GatherHandler final : public openage::event::OnceEventHandler {
public:
	using Delivery = std::function<void(GameState &, player_id_t, time::time_t)>;
	explicit GatherHandler(Delivery delivery);

	void invoke(openage::event::EventLoop &loop,
	            const std::shared_ptr<openage::event::EventEntity> &target,
	            const std::shared_ptr<openage::event::State> &state,
	            const time::time_t &time,
	            const param_map &params) override;

	void setup_event(const std::shared_ptr<openage::event::Event> &event,
	                 const std::shared_ptr<openage::event::State> &state) override;

	time::time_t predict_invoke_time(
		const std::shared_ptr<openage::event::EventEntity> &target,
		const std::shared_ptr<openage::event::State> &state,
		const time::time_t &at) override;
private:
	Delivery delivery;
};

} // namespace openage::gamestate::event
