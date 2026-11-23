// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "event/eventhandler.h"

namespace openage::gamestate::event {

class ConstructionHandler final : public openage::event::OnceEventHandler {
public:
	ConstructionHandler();

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
};

} // namespace openage::gamestate::event
