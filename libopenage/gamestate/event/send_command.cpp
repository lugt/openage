// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#include "send_command.h"

#include <algorithm>
#include <vector>

#include "coord/phys.h"
#include "event/event_loop.h"
#include "gamestate/component/internal/command_queue.h"
#include "gamestate/component/internal/commands/attack.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/system/idle.h"
#include "gamestate/system/move.h"
#include "gamestate/component/internal/commands/move.h"
#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"
#include "gamestate/component/api/gather.h"
#include "gamestate/game_state.h"
#include "gamestate/types.h"
#include "log/log.h"


namespace openage::gamestate {
namespace component {
class CommandQueue;

namespace command {
class IdleCommand;
class MoveCommand;
} // namespace command
} // namespace component

namespace event {

Commander::Commander(const std::shared_ptr<openage::event::EventLoop> &loop) :
	openage::event::EventEntity{loop} {
}

size_t Commander::id() const {
	return 0;
}

std::string Commander::idstr() const {
	return "command_target";
}


SendCommandHandler::SendCommandHandler() :
	openage::event::OnceEventHandler{"game.send_command"} {
}

void SendCommandHandler::setup_event(const std::shared_ptr<openage::event::Event> & /* event */,
                                     const std::shared_ptr<openage::event::State> & /* state */) {
	// TODO
}

void SendCommandHandler::invoke(openage::event::EventLoop &loop,
                                const std::shared_ptr<openage::event::EventEntity> &target,
                                const std::shared_ptr<openage::event::State> &state,
                                const time::time_t &time,
                                const param_map &params) {
	auto gstate = std::dynamic_pointer_cast<openage::gamestate::GameState>(state);

	auto command_type = params.get("type", component::command::command_t::NONE);
	std::vector<gamestate::entity_id_t> ids = params.get("entity_ids",
	                                                     std::vector<gamestate::entity_id_t>{});
	const bool has_issuer = params.check_type<player_id_t>("issuer");
	const auto issuer = params.get("issuer", player_id_t{});
	std::erase_if(ids, [&](entity_id_t id) {
		if (not gstate->has_game_entity(id)) {
			return true;
		}
		const auto entity = gstate->get_game_entity(id);
		const auto owner = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		return entity->is_dead() or (has_issuer and (not owner or owner->get_owners().get(time) != issuer));
	});
	if (command_type == component::command::command_t::GATHER) {
		for (auto id : ids) {
			openage::event::EventHandler::param_map::map_t order{
				{"worker_id", id},
				{"resource_id", params.get("target_id", entity_id_t{})},
			};
			loop.create_event("game.gather", target, state, time, order);
		}
		return;
	}
	for (auto id : ids) {
		auto entity = gstate->get_game_entity(id);
		++entity->construction_order;
		auto command_queue = std::dynamic_pointer_cast<component::CommandQueue>(
			entity->get_component(component::component_t::COMMANDQUEUE));

		if (auto gather = entity->try_get_component_as<component::Gather>(component::component_t::GATHER);
		    gather and (command_type == component::command::command_t::MOVE
		                or command_type == component::command::command_t::ATTACK
		                or command_type == component::command::command_t::IDLE)) {
			++gather->order;
		}
		switch (command_type) {
		case component::command::command_t::IDLE: {
			while (not command_queue->get_queue().empty(time)) {
				command_queue->pop_command(time);
			}
			if (auto attack = entity->try_get_component_as<component::Attack>(component::component_t::ATTACK)) {
				attack->stop_attack();
			}
			system::Move::halt(entity, gstate, time);
			if (entity->has_component(component::component_t::IDLE)) {
				system::Idle::idle(entity, time);
			}
			break;
		}
		case component::command::command_t::MOVE: {
			auto dest = params.get("target", coord::phys3{0, 0, 0});
			log::log(MSG(info) << "SendCommandHandler: MOVE for " << ids.size()
			                   << " entities at t=" << time
			                   << " dest=(" << dest.ne << "," << dest.se << ")");
			command_queue->add_command(
				time,
				std::make_shared<component::command::MoveCommand>(dest));
			break;
		}

		case component::command::command_t::ATTACK: {
			auto target_id = params.get("target_id", gamestate::entity_id_t{0});
			log::log(MSG(info) << "SendCommandHandler: ATTACK for " << ids.size()
			                   << " entities at t=" << time
			                   << " target_id=" << target_id);
			// Enqueue the AttackCommand (carries attacker+target) so it flows
			// through the standard CommandQueue dispatch path. The command is
			// consumed by AttackHandler (game.attack event) below -- it is NOT
			// processed by the activity graph.
			command_queue->add_command(
				time,
				std::make_shared<component::command::AttackCommand>(id, target_id));
			break;
		}

		// TODO(Phase A6/C1-C3): construct the matching command class for each
		// remaining type. We intentionally log instead of silently dropping so test
		// data wiring problems surface instead of looking like no-op orders.
		case component::command::command_t::BUILD:
		case component::command::command_t::TRAIN:
		default:
			log::log(MSG(warn) << "SendCommandHandler: unhandled command_type "
			                   << static_cast<int>(command_type)
			                   << " (command class not yet implemented)");
			break;
		}
	}

	// ATTACK is processed by the event-driven AttackSystem, not the activity
	// graph. Emit one "game.attack" event (carrying all attacker IDs + the
	// target ID) so AttackHandler can consume the AttackCommand(s) we just
	// enqueued. create_event() pushes this directly into the event_queue, so
	// it fires in the current execute_events pass -- before the add_command()
	// change reschedules process_command (which travels through the changes
	// set and fires in a later reach_time iteration). That ordering means the
	// ATTACK is consumed before the activity graph looks at the queue, so the
	// activity never loops on an unconsumed ATTACK command.
	if (command_type == component::command::command_t::ATTACK) {
		auto target_id = params.get("target_id", gamestate::entity_id_t{0});
		openage::event::EventHandler::param_map::map_t attack_params{
			{"entity_ids", ids},
			{"target_id", target_id},
		};
		loop.create_event("game.attack", target, state, time, attack_params);
	}
}

time::time_t SendCommandHandler::predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> & /* target */,
                                                     const std::shared_ptr<openage::event::State> & /* state */,
                                                     const time::time_t &at) {
	return at;
}

} // namespace event
} // namespace openage::gamestate
