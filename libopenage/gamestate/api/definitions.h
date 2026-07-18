// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#pragma once

#include <memory>
#include <utility>

#include <nyan/nyan.h>

#include "datastructure/constexpr_map.h"
#include "gamestate/activity/condition/command_in_queue.h"
#include "gamestate/activity/condition/next_command.h"
#include "gamestate/activity/event/command_in_queue.h"
#include "gamestate/activity/event/wait.h"
#include "gamestate/activity/types.h"
#include "gamestate/activity/xor_event_gate.h"
#include "gamestate/activity/xor_gate.h"
#include "gamestate/api/types.h"
#include "gamestate/system/types.h"


namespace openage::gamestate::api {

/**
 * Maps internal ability types to nyan API values.
 *
 * Keep in sync with ability_t (api/types.h). entity_factory.cpp:166-211 looks
 * up the parent class of every nyan ability via ability_obj.get_parents()[0]
 * and matches the FQON against this table's values. An entry missing here
 * means the nyan ability loads as a no-op (silently dropped).
 */
static const auto ABILITY_DEFS = datastructure::create_const_map<ability_t, nyan::ValueHolder>(
	std::pair(ability_t::IDLE,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.Idle"))),
	std::pair(ability_t::MOVE,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.Move"))),
	std::pair(ability_t::LIVE,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.Live"))),
	std::pair(ability_t::TURN,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.Turn"))),
	std::pair(ability_t::GATHER,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.Gather"))),
	std::pair(ability_t::DROP_RESOURCES,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.DropResources"))),
	std::pair(ability_t::HARVESTABLE,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.Harvestable"))),
	std::pair(ability_t::REGENERATE_RESOURCE_SPOT,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.RegenerateResourceSpot"))),
	std::pair(ability_t::CONSTRUCTABLE,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.Constructable"))),
	std::pair(ability_t::FOUNDATION,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.Foundation"))),
	std::pair(ability_t::CREATE,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.Create"))),
	std::pair(ability_t::PRODUCTION_QUEUE,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.ProductionQueue"))),
	std::pair(ability_t::RESEARCH,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.Research"))),
	std::pair(ability_t::ATTACK,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.Attack"))),
	std::pair(ability_t::RESISTANCE,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.Resistance"))),
	std::pair(ability_t::PASSIVE_TRANSFORM_TO,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.PassiveTransformTo"))),
	std::pair(ability_t::DESPAWN,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.Despawn"))),
	std::pair(ability_t::PROVIDE_CONTINGENT,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.type.ProvideContingent"))));

/**
 * Maps internal property types to nyan API values.
 */
static const auto ABILITY_PROPERTY_DEFS = datastructure::create_const_map<ability_property_t, nyan::ValueHolder>(
	std::pair(ability_property_t::ANIMATED,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.property.type.Animated"))),
	std::pair(ability_property_t::ANIMATION_OVERRIDE,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.property.type.AnimationOverride"))),
	std::pair(ability_property_t::COMMAND_SOUND,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.property.type.CommandSound"))),
	std::pair(ability_property_t::EXECUTION_SOUND,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.property.type.ExecutionSound"))),
	std::pair(ability_property_t::DIPLOMATIC,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.property.type.Diplomatic"))),
	std::pair(ability_property_t::LOCK,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.ability.property.type.Lock"))));

/**
 * Maps API activity node types to engine node types.
 */
static const auto ACTIVITY_NODE_DEFS = datastructure::create_const_map<std::string, activity::node_t>(
	std::pair("engine.util.activity.node.type.Start",
              activity::node_t::START),
	std::pair("engine.util.activity.node.type.End",
              activity::node_t::END),
	std::pair("engine.util.activity.node.type.Ability",
              activity::node_t::TASK_SYSTEM),
	std::pair("engine.util.activity.node.type.XORGate",
              activity::node_t::XOR_GATE),
	std::pair("engine.util.activity.node.type.XOREventGate",
              activity::node_t::XOR_EVENT_GATE));

/**
 * Maps API activity task system types to engine system types.
 *
 * When entity_factory (init_activity) processes a TaskSystem node that
 * references an ability FQON, the loader looks the FQON up here and attaches
 * the matching engine system_id_t to the corresponding TaskSystemNode
 * (entity_factory.cpp:264-269). Without an entry, the node is created but
 * has no system bound and will throw "Unhandled subsystem" at runtime
 * (system/activity.cpp:127-143).
 *
 * Container-style abilities (Foundation/Resistance/Despawn/...) and event
 * driven abilities (Attack) are deliberately omitted: they don't appear as
 * Activity.task_system nodes in nyan and are wired via other paths.
 */
static const auto ACTIVITY_TASK_SYSTEM_DEFS = datastructure::create_const_map<std::string, system::system_id_t>(
	std::pair("engine.ability.type.Idle",
              system::system_id_t::IDLE),
	std::pair("engine.ability.type.Move",
              system::system_id_t::MOVE_COMMAND),
	std::pair("engine.ability.type.Gather",
              system::system_id_t::GATHER),
	std::pair("engine.ability.type.DropResources",
              system::system_id_t::DROP_RESOURCES),
	std::pair("engine.ability.type.Constructable",
              system::system_id_t::BUILD_PROGRESS),
	std::pair("engine.ability.type.Create",
              system::system_id_t::TRAIN),
	std::pair("engine.ability.type.Research",
              system::system_id_t::RESEARCH));

/**
 * Maps API activity condition types to engine condition types.
 */
static const auto ACTIVITY_CONDITIONS = datastructure::create_const_map<std::string, activity::condition_t>(
	std::pair("engine.util.activity.condition.type.CommandInQueue",
              std::function(gamestate::activity::command_in_queue)),
	std::pair("engine.util.activity.condition.type.NextCommandIdle",
              std::function(gamestate::activity::next_command_idle)),
	std::pair("engine.util.activity.condition.type.NextCommandMove",
              std::function(gamestate::activity::next_command_move)));

static const auto ACTIVITY_EVENT_PRIMERS = datastructure::create_const_map<std::string, activity::event_primer_t>(
	std::pair("engine.util.activity.event.type.CommandInQueue",
              std::function(gamestate::activity::primer_command_in_queue)),
	std::pair("engine.util.activity.event.type.Wait",
              std::function(gamestate::activity::primer_wait)),
	std::pair("engine.util.activity.event.type.WaitAbility",
              std::function(gamestate::activity::primer_wait)));

/**
 * Maps internal patch property types to nyan API values.
 */
static const auto PATCH_PROPERTY_DEFS = datastructure::create_const_map<patch_property_t, nyan::ValueHolder>(
	std::pair(patch_property_t::DIPLOMATIC,
              nyan::ValueHolder(std::make_shared<nyan::ObjectValue>("engine.patch.property.type.Diplomatic"))));

} // namespace openage::gamestate::api
