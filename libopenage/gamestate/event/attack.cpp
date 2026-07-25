// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "attack.h"

#include <vector>

#include "gamestate/component/internal/command_queue.h"
#include "gamestate/component/internal/commands/attack.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/types.h"
#include "log/log.h"
#include "log/message.h"


namespace openage::gamestate::event {

AttackHandler::AttackHandler() :
	openage::event::OnceEventHandler{"game.attack"} {
}

void AttackHandler::setup_event(const std::shared_ptr<openage::event::Event> & /* event */,
                                const std::shared_ptr<openage::event::State> & /* state */) {
	// TODO
}

void AttackHandler::invoke(openage::event::EventLoop & /* loop */,
                           const std::shared_ptr<openage::event::EventEntity> & /* target */,
                           const std::shared_ptr<openage::event::State> &state,
                           const time::time_t &time,
                           const param_map &params) {
	auto gstate = std::dynamic_pointer_cast<openage::gamestate::GameState>(state);

	std::vector<gamestate::entity_id_t> ids = params.get(
		"entity_ids",
		std::vector<gamestate::entity_id_t>{});
	auto target_id = params.get("target_id", gamestate::entity_id_t{0});

	log::log(MSG(info) << "AttackHandler: invoke for " << ids.size()
	                   << " attacker(s) at t=" << time
	                   << " target_id=" << target_id);

	for (auto attacker_id : ids) {
		auto entity = gstate->get_game_entity(attacker_id);
		if (not entity) {
			log::log(MSG(warn) << "AttackHandler: attacker " << attacker_id
			                   << " not found in state");
			continue;
		}
		auto command_queue = std::dynamic_pointer_cast<component::CommandQueue>(
			entity->get_component(component::component_t::COMMANDQUEUE));

		// Consume the AttackCommand we just enqueued. A new attack also
		// overrides any pending Move/Attack command in the queue, so pop
		// until we find our ATTACK (discarding the overridden commands).
		// Leaving the ATTACK in the queue would make the activity graph loop
		// Idle -> CheckQueue -> BranchCommand (default Idle, never pops).
		bool dispatched = false;
		while (not command_queue->get_queue().empty(time)) {
			auto command = command_queue->pop_command(time);
			if (not command) {
				break;
			}
			if (command->get_type() == component::command::command_t::ATTACK) {
				auto attack_cmd = std::dynamic_pointer_cast<component::command::AttackCommand>(command);
				log::log(MSG(info) << "AttackHandler: dispatched ATTACK attacker="
				                   << attack_cmd->get_attacker()
				                   << " target=" << attack_cmd->get_target()
				                   << " (persistent attack state = increment 2)");
				dispatched = true;
				break;
			}
			log::log(MSG(warn) << "AttackHandler: discarded pending command type="
			                   << static_cast<int>(command->get_type())
			                   << " for attacker " << attacker_id
			                   << " (overridden by new ATTACK)");
		}
		if (not dispatched) {
			log::log(MSG(warn) << "AttackHandler: no AttackCommand found for attacker "
			                   << attacker_id
			                   << " (queue was empty or only held other types)");
		}
	}
}

time::time_t AttackHandler::predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> & /* target */,
                                                const std::shared_ptr<openage::event::State> & /* state */,
                                                const time::time_t &at) {
	return at;
}

} // namespace openage::gamestate::event
