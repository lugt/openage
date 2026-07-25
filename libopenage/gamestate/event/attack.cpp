// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "attack.h"

#include <vector>

#include "gamestate/component/api/attack.h"
#include "gamestate/component/internal/command_queue.h"
#include "gamestate/component/internal/commands/attack.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/position.h"
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
		entity_id_t resolved_target = target_id;
		while (not command_queue->get_queue().empty(time)) {
			auto command = command_queue->pop_command(time);
			if (not command) {
				break;
			}
			if (command->get_type() == component::command::command_t::ATTACK) {
				auto attack_cmd = std::dynamic_pointer_cast<component::command::AttackCommand>(command);
				resolved_target = attack_cmd->get_target();
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
			continue;
		}

		// Set up persistent attack state on the attacker's Attack component
		// and compute the initial range to the target. Approach movement,
		// cooldown and damage land in later increments.
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

		auto target_entity = gstate->get_game_entity(resolved_target);
		double range = attack_comp->get_attack_range();
		bool in_range = false;
		if (not target_entity) {
			log::log(MSG(warn) << "AttackHandler: target " << resolved_target
			                   << " not found; attack will be a no-op until a "
			                   << "real target exists (increment 3 cancels)");
		}
		else {
			auto attacker_pos_comp = std::dynamic_pointer_cast<component::Position>(
				entity->get_component(component::component_t::POSITION));
			auto target_pos_comp = std::dynamic_pointer_cast<component::Position>(
				target_entity->get_component(component::component_t::POSITION));
			if (attacker_pos_comp and target_pos_comp) {
				auto attacker_pos = attacker_pos_comp->get_positions().get(time);
				auto target_pos = target_pos_comp->get_positions().get(time);
				double dist = (attacker_pos - target_pos).length();
				in_range = dist <= range;
				attack_comp->set_approaching(not in_range);
				log::log(MSG(info) << "AttackHandler: attacker=" << attacker_id
				                   << " target=" << resolved_target
				                   << " dist=" << dist
				                   << " range=" << range
				                   << " cooldown=" << attack_comp->get_cooldown()
				                   << " in_range=" << in_range
				                   << " approaching=" << (not in_range));
			}
		}

		attack_comp->start_attack(resolved_target, time);
		if (not in_range) {
			attack_comp->set_approaching(true);
		}
		// next_attack_time defaults to `now` via start_attack; increment 4
		// shifts it to now+cooldown and schedules the first hit.
	}
}

time::time_t AttackHandler::predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> & /* target */,
                                                const std::shared_ptr<openage::event::State> & /* state */,
                                                const time::time_t &at) {
	return at;
}

} // namespace openage::gamestate::event
