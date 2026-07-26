// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "attack.h"

#include <vector>

#include "coord/phys.h"
#include "event/event_loop.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/internal/command_queue.h"
#include "gamestate/component/internal/commands/attack.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/system/move.h"
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

void AttackHandler::invoke(openage::event::EventLoop &loop,
                           const std::shared_ptr<openage::event::EventEntity> &target,
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

		std::shared_ptr<component::Attack> attack_comp;
		if (entity->has_component(component::component_t::ATTACK)) {
			attack_comp = std::dynamic_pointer_cast<component::Attack>(
				entity->get_component(component::component_t::ATTACK));
		}
		if (not attack_comp) {
			log::log(MSG(warn) << "AttackHandler: attacker " << attacker_id
			                   << " has no Attack component (ability not mounted?");
			continue;
		}

		// A fresh ATTACK (SendCommandHandler) enqueues an AttackCommand and
		// emits this event; a follow-up re-evaluation (scheduled below after
		// an approach move) carries no new command. Distinguish by whether an
		// ATTACK command is waiting in the queue: if so, this is a new attack
		// (which also overrides any pending command); otherwise reuse the
		// target already recorded on the Attack component.
		bool is_new_attack = false;
		entity_id_t resolved_target = target_id;
		auto command_queue = std::dynamic_pointer_cast<component::CommandQueue>(
			entity->get_component(component::component_t::COMMANDQUEUE));
		if (command_queue and not command_queue->get_queue().empty(time)) {
			while (not command_queue->get_queue().empty(time)) {
				auto command = command_queue->pop_command(time);
				if (not command) {
					break;
				}
				if (command->get_type() == component::command::command_t::ATTACK) {
					auto attack_cmd = std::dynamic_pointer_cast<component::command::AttackCommand>(command);
					resolved_target = attack_cmd->get_target();
					is_new_attack = true;
					break;
				}
				log::log(MSG(warn) << "AttackHandler: discarded pending command type="
				                   << static_cast<int>(command->get_type())
				                   << " for attacker " << attacker_id
				                   << " (overridden by new ATTACK)");
			}
		}
		if (not is_new_attack) {
			if (not attack_comp->is_active()) {
				log::log(MSG(warn) << "AttackHandler: no AttackCommand and no active "
				                   << "attack for attacker " << attacker_id
				                   << " (spurious event)");
				continue;
			}
			resolved_target = attack_comp->get_target_id();
		}

		auto target_entity = gstate->get_game_entity(resolved_target);
		if (not target_entity) {
			log::log(MSG(warn) << "AttackHandler: target " << resolved_target
			                   << " not found; "
			                   << (is_new_attack ? "attack dropped"
			                                     : "target gone, stopping attack"));
			if (attack_comp->is_active()) {
				attack_comp->stop_attack();
			}
			continue;
		}

		auto attacker_pos_comp = std::dynamic_pointer_cast<component::Position>(
			entity->get_component(component::component_t::POSITION));
		auto target_pos_comp = std::dynamic_pointer_cast<component::Position>(
			target_entity->get_component(component::component_t::POSITION));
		if (not(attacker_pos_comp and target_pos_comp)) {
			log::log(MSG(warn) << "AttackHandler: attacker " << attacker_id
			                   << " or target " << resolved_target
			                   << " missing Position component");
			continue;
		}

		double range = attack_comp->get_attack_range();
		auto attacker_pos = attacker_pos_comp->get_positions().get(time);
		auto target_pos = target_pos_comp->get_positions().get(time);
		double dist = (attacker_pos - target_pos).length();
		bool in_range = dist <= range;
		log::log(MSG(info) << "AttackHandler: attacker=" << attacker_id
		                   << " target=" << resolved_target
		                   << " dist=" << dist
		                   << " range=" << range
		                   << " in_range=" << in_range
		                   << " new_attack=" << is_new_attack);

		if (is_new_attack) {
			attack_comp->start_attack(resolved_target, time);
		}

		if (in_range) {
			attack_comp->set_approaching(false);
			// In range: ready for cooldown + damage (increment 4).
		}
		else {
			attack_comp->set_approaching(true);
			// Out of range: reuse the Move system to approach, then schedule a
			// follow-up game.attack at the move's completion to re-evaluate
			// range (and eventually land hits in increment 4).
			auto move_time = openage::gamestate::system::Move::move_default(
				entity, gstate, target_pos, time);
			if (move_time > time::time_t::zero()) {
				openage::event::EventHandler::param_map::map_t followup_params{
					{"entity_ids", std::vector<gamestate::entity_id_t>{attacker_id}},
					{"target_id", resolved_target},
				};
				loop.create_event("game.attack", target, state,
				                  time + move_time, followup_params);
				log::log(MSG(info) << "AttackHandler: attacker=" << attacker_id
				                   << " approaching target=" << resolved_target
				                   << " move_time=" << move_time
				                   << " re-evaluate at t=" << (time + move_time));
			}
			else {
				log::log(MSG(warn) << "AttackHandler: attacker=" << attacker_id
				                   << " cannot approach target=" << resolved_target
				                   << " (move_time=0; stuck approaching)");
			}
		}
	}
}

time::time_t AttackHandler::predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> & /* target */,
                                                const std::shared_ptr<openage::event::State> & /* state */,
                                                const time::time_t &at) {
	return at;
}

} // namespace openage::gamestate::event
