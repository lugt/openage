// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "attack.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "coord/phys.h"
#include "coord/tile.h"
#include "event/event_loop.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/command_queue.h"
#include "gamestate/component/internal/commands/attack.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "gamestate/system/idle.h"
#include "gamestate/system/move.h"
#include "gamestate/types.h"
#include "log/log.h"
#include "log/message.h"
#include "pathfinding/attack_approach.h"


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
		if (not gstate->has_game_entity(attacker_id)) {
			log::log(MSG(warn) << "AttackHandler: attacker " << attacker_id
			                   << " not found in state");
			continue;
		}
		auto entity = gstate->get_game_entity(attacker_id);

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
		const bool followup = params.get("followup", false);
		bool is_new_attack = false;
		entity_id_t resolved_target = target_id;
		auto command_queue = std::dynamic_pointer_cast<component::CommandQueue>(
			entity->get_component(component::component_t::COMMANDQUEUE));
		if (not followup and command_queue and not command_queue->get_queue().empty(time)) {
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
			if (followup and (resolved_target != target_id
			                  or params.get("order_id", uint64_t{0}) != attack_comp->get_order_id())) {
				continue;
			}
		}

		if (not gstate->has_game_entity(resolved_target)) {
			log::log(MSG(warn) << "AttackHandler: target " << resolved_target
			                   << " not found; "
			                   << (is_new_attack ? "attack dropped"
			                                     : "target gone, stopping attack"));
			if (attack_comp->is_active()) {
				if (attack_comp->is_approaching()) {
					system::Move::halt(entity, gstate, time);
				}
				attack_comp->stop_attack();
				if (entity->has_component(component::component_t::IDLE)) {
					system::Idle::idle(entity, time);
				}
			}
			continue;
		}
		auto target_entity = gstate->get_game_entity(resolved_target);
		auto source_owner = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		auto target_owner = target_entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		if (source_owner and target_owner
		    and gstate->get_stance(source_owner->get_owners().get(time), target_owner->get_owners().get(time))
		            != DiplomaticStance::Enemy) {
			if (attack_comp->is_active()) {
				if (attack_comp->is_approaching()) {
					system::Move::halt(entity, gstate, time);
				}
				attack_comp->stop_attack();
				if (entity->has_component(component::component_t::IDLE)) {
					system::Idle::idle(entity, time);
				}
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
		double dist = pathfinding::attack_distance(*entity, attacker_pos, *target_entity, target_pos);
		bool in_range = dist <= range;
		log::log(MSG(info) << "AttackHandler: attacker=" << attacker_id
		                   << " target=" << resolved_target
		                   << " dist=" << dist
		                   << " range=" << range
		                   << " in_range=" << in_range
		                   << " new_attack=" << is_new_attack);

		if (is_new_attack) {
			if (entity->has_component(component::component_t::MOVE)) {
				system::Move::halt(entity, gstate, time);
				if (entity->has_component(component::component_t::IDLE)) {
					system::Idle::idle(entity, time);
				}
			}
			attack_comp->start_attack(resolved_target, time);
		}

		auto schedule_followup = [&](const time::time_t &when, int retries = 0) {
			openage::event::EventHandler::param_map::map_t followup_params{
				{"entity_ids", std::vector<entity_id_t>{attacker_id}},
				{"target_id", resolved_target},
				{"followup", true},
				{"order_id", attack_comp->get_order_id()},
				{"approach_retries", retries},
			};
			loop.create_event("game.attack", target, state, when, followup_params);
		};
		if (in_range) {
			attack_comp->set_approaching(false);

			// Cooldown gate: only land a hit when the cooldown has
			// elapsed since the last hit. The first hit is allowed
			// immediately because start_attack() initialised
			// next_attack_time to the attack start time.
			if (time >= attack_comp->get_next_attack_time()) {
				attacker_pos_comp->set_position(time, attacker_pos);
				if (attacker_pos != target_pos) {
					attacker_pos_comp->set_angle(time, (target_pos - attacker_pos).to_angle());
				}
				entity->play_animation(attack_comp->get_ability(), time);
				const auto live = target_entity->try_get_component_as<component::Live>(
					component::component_t::LIVE);
				int64_t damage = live and live->get_resistance()
				               ? attack_comp->get_damage_against(*live->get_resistance())
				               : attack_comp->get_damage();
				auto health_fqon = attack_comp->get_health_attribute_fqon();

				if (damage > 0 and not health_fqon.empty()) {
					// P3.2: route damage through the dedicated
					// game.apply_discrete_effect event so the Live curve
					// is mutated by ApplyDiscreteEffectHandler rather
					// than by direct set_attribute here. The handler is
					// responsible for the read-modify-write; AttackHandler
					// only knows the damage amount and target attribute.
					openage::event::EventHandler::param_map::map_t effect_params{
						{"target_id", resolved_target},
						{"amount", damage},
						{"attribute_fqon", health_fqon},
					};
					loop.create_event("game.apply_discrete_effect",
					                  target, state, time, effect_params);
					log::log(MSG(info) << "AttackHandler: hit attacker=" << attacker_id
					                   << " target=" << resolved_target
					                   << " damage=" << damage
					                   << " dispatched ApplyDiscreteEffect event at t=" << time);
				}
				else {
					log::log(MSG(warn) << "AttackHandler: cannot hit attacker=" << attacker_id
					                   << " target=" << resolved_target
					                   << " damage=" << damage
					                   << " health_fqon_set=" << (not health_fqon.empty()));
				}

				// Schedule the next cooldown tick regardless of whether
				// the effect was dispatched. next_attack_time is set on
				// the Attack component so a concurrent re-entry from the
				// same event pass does not double-fire.
				auto next_hit = time + time::time_t::from_double(attack_comp->get_cooldown());
				attack_comp->set_next_attack_time(next_hit);

				schedule_followup(next_hit);
			}
			else {
				schedule_followup(attack_comp->get_next_attack_time());
			}
		}
		else if (entity->has_component(component::component_t::MOVE)) {
			attack_comp->set_approaching(true);
			auto map = gstate->get_map();
			auto navigation = map ? map->get_navigation_service() : nullptr;
			auto move_time = time::time_t::zero();
			if (navigation) {
				const auto approach = pathfinding::find_attack_approach(
					gstate, attacker_id, resolved_target, *navigation, time);
				if (approach.status == pathfinding::AttackApproachStatus::APPROACH_FOUND) {
					std::vector<coord::phys3> route;
					route.reserve(approach.waypoints.size() + 1);
					for (const auto &cell : approach.waypoints) {
						route.push_back(coord::tile{cell.x, cell.y}.to_phys3_center());
					}
					if (route.size() == 1) {
						// The current tile's center can be in range even when
						// the unit's current position in that tile is not.
						route.insert(route.begin(), attacker_pos);
					}
					move_time = system::Move::move_along_path(entity, gstate, std::move(route), time);
				}
			}
			else {
				move_time = system::Move::move_default(entity, gstate, target_pos, time);
			}

			const auto retries = params.get("approach_retries", 0);
			if (move_time > time::time_t::zero()) {
				schedule_followup(time + move_time);
			}
			else if (navigation and retries < 6) {
				// Nearby troops may still be moving into position. Retry
				// briefly, without keeping unreachable orders alive forever.
				schedule_followup(time + time::time_t::from_double(0.5), retries + 1);
			}
			else {
				attack_comp->stop_attack();
			}
		}
		else {
			attack_comp->stop_attack();
		}
	}
}

time::time_t AttackHandler::predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> & /* target */,
                                                const std::shared_ptr<openage::event::State> & /* state */,
                                                const time::time_t &at) {
	return at;
}

} // namespace openage::gamestate::event
