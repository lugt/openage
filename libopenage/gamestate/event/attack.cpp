// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#include "attack.h"

#include <cstdint>
#include <utility>
#include <vector>

#include "coord/phys.h"
#include "coord/tile.h"
#include "event/event_loop.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/build.h"
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
#include "gamestate/player.h"
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
		if (not attack_comp or (entity->is_transformable and not entity->unpacked)) {
			log::log(MSG(warn) << "AttackHandler: attacker " << attacker_id
			                   << " has no Attack component or is packed");
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
		    and not (entity->wildlife_predator and gstate->get_player(source_owner->get_owners().get(time))->is_gaia)
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
				if (attack_comp->is_convert()) {
					auto attacker_owner = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
					auto target_owner = target_entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
					if (not attacker_owner or not target_owner
					    or target_entity->has_component(component::component_t::BUILD)
					    or target_entity->definition.ends_with(".Monk")
					    or target_owner->get_owners().get(time) == attacker_owner->get_owners().get(time)) {
						attack_comp->stop_attack();
						continue;
					}
					++attack_comp->convert_rounds;
					if (attack_comp->convert_rounds >= 4) {
						gstate->record_enemy_loss(resolved_target, attacker_owner->get_owners().get(time), time);
						target_owner->set_owner(time, attacker_owner->get_owners().get(time));
						if (auto target_attack = target_entity->try_get_component_as<component::Attack>(component::component_t::ATTACK)) {
							target_attack->stop_attack();
						}
						attack_comp->stop_attack();
						if (entity->has_component(component::component_t::IDLE)) {
							system::Idle::idle(entity, time);
						}
						continue;
					}
					auto next_hit = time + time::time_t::from_double(attack_comp->get_cooldown());
					attack_comp->set_next_attack_time(next_hit);
					schedule_followup(next_hit);
					continue;
				}
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
						{"killer", source_owner ? source_owner->get_owners().get(time) : player_id_t{UINT64_MAX}},
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

HealHandler::HealHandler() :
	openage::event::OnceEventHandler{"game.heal"} {}

void HealHandler::setup_event(const std::shared_ptr<openage::event::Event> &,
                              const std::shared_ptr<openage::event::State> &) {}

void HealHandler::invoke(openage::event::EventLoop &loop,
                         const std::shared_ptr<openage::event::EventEntity> &target,
                         const std::shared_ptr<openage::event::State> &state,
                         const time::time_t &time,
                         const param_map &params) {
	auto gstate = std::dynamic_pointer_cast<openage::gamestate::GameState>(state);
	const auto target_id = params.get("target_id", entity_id_t{0});
	if (not gstate->has_game_entity(target_id)) {
		return;
	}
	auto patient = gstate->get_game_entity(target_id);
	auto patient_live = patient->try_get_component_as<component::Live>(component::component_t::LIVE);
	auto patient_pos_comp = patient->try_get_component_as<component::Position>(component::component_t::POSITION);
	auto patient_owner = patient->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
	if (patient->is_dead() or not patient_live or not patient_pos_comp or not patient_owner) {
		return;
	}
	const nyan::fqon_t health = "de2_base.data.util.attribute.types.Health";
	const auto current_hp = patient_live->get_attribute_value(time, health);
	const auto max_hp = patient_live->get_attribute_max(health);
	if (not current_hp or not max_hp or *current_hp >= *max_hp) {
		return;
	}
	const auto patient_pos = patient_pos_comp->get_positions().get(time);
	const bool followup = params.get("followup", false);
	for (const auto healer_id : params.get("entity_ids", std::vector<entity_id_t>{})) {
		if (not gstate->has_game_entity(healer_id)) {
			continue;
		}
		auto healer = gstate->get_game_entity(healer_id);
		auto healer_pos_comp = healer->try_get_component_as<component::Position>(component::component_t::POSITION);
		auto healer_owner = healer->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		if (healer->is_dead() or not healer->heal_ability or not healer_pos_comp or not healer_owner
		    or healer_owner->get_owners().get(time) != patient_owner->get_owners().get(time)
		    or patient->has_component(component::component_t::BUILD)) {
			continue;
		}
		const auto order = followup ? params.get("order_id", uint64_t{0}) : ++healer->heal_order;
		if (followup and order != healer->heal_order) {
			continue;
		}
		const auto healer_pos = healer_pos_comp->get_positions().get(time);
		const double dist = (patient_pos - healer_pos).length();
		const double max_range = healer->heal_ability->get_float("RangedContinuousEffect.max_range");
		if (dist <= max_range + 0.5) {
			healer->play_animation(*healer->heal_ability, time);
			const auto next_hp = std::min(*max_hp, *current_hp + 2);
			patient_live->set_attribute(time, health, next_hp);
			if (next_hp < *max_hp) {
				loop.create_event("game.heal", target, state, time + time::time_t::from_double(0.5), {
					{"entity_ids", std::vector<entity_id_t>{healer_id}},
					{"target_id", target_id},
					{"followup", true},
					{"order_id", order},
				});
			}
			else if (healer->has_component(component::component_t::IDLE)) {
				system::Idle::idle(healer, time);
			}
		}
		else if (healer->has_component(component::component_t::MOVE)) {
			const auto move_time = system::Move::move_default(healer, gstate, patient_pos, time);
			if (move_time > time::time_t::zero()) {
				loop.create_event("game.heal", target, state, time + move_time, {
					{"entity_ids", std::vector<entity_id_t>{healer_id}},
					{"target_id", target_id},
					{"followup", true},
					{"order_id", order},
				});
			}
		}
	}
}

time::time_t HealHandler::predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> &,
                                              const std::shared_ptr<openage::event::State> &,
                                              const time::time_t &at) {
	return at;
}

} // namespace openage::gamestate::event
