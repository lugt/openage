// Copyright 2023-2025 the openage authors. See copying.md for legal info.

#include "entity_factory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_set>

#include "error/error.h"
#include "assets/mod_manager.h"

#include "curve/container/queue.h"
#include "curve/discrete.h"
#include "event/event_loop.h"
#include "gamestate/activity/activity.h"
#include "gamestate/activity/condition/command_in_queue.h"
#include "gamestate/activity/end_node.h"
#include "gamestate/activity/event/command_in_queue.h"
#include "gamestate/activity/event/wait.h"
#include "gamestate/activity/start_node.h"
#include "gamestate/activity/task_system_node.h"
#include "gamestate/activity/xor_event_gate.h"
#include "gamestate/activity/xor_gate.h"
#include "gamestate/api/activity.h"
#include "gamestate/api/animation.h"
#include "gamestate/api/patch.h"
#include "gamestate/api/util.h"
#include "gamestate/api/player_setup.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/build.h"
#include "gamestate/component/api/footprint.h"
#include "gamestate/component/api/gather.h"
#include "gamestate/component/api/idle.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/api/production.h"
#include "gamestate/component/api/resource_storage.h"
#include "gamestate/component/api/selectable.h"
#include "gamestate/component/api/train.h"
#include "gamestate/component/api/turn.h"
#include "gamestate/component/internal/activity.h"
#include "gamestate/component/internal/command_queue.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/manager.h"
#include "gamestate/player.h"
#include "gamestate/system/types.h"
#include "log/log.h"
#include "log/message.h"
#include "renderer/render_factory.h"
#include "time/time.h"
#include "util/fixed_point.h"

import nyan;

namespace openage::gamestate {

/**
 * Create a simple test activity for the game entity.
 *
 * The activity is as follows:
 *                      |------------------------------------------------------|
 *                      |                                                      v
 * Start -> Idle -> Condition -> Condition -> Wait for command -> Move -> Wait for move -> End
 *            ^                                                                |
 *            |----------------------------------------------------------------|
 *
 * TODO: Replace with config
 */
std::shared_ptr<activity::Activity> create_test_activity() {
	auto start = std::make_shared<activity::StartNode>(0);
	auto idle = std::make_shared<activity::TaskSystemNode>(1, "Idle");
	auto condition_moveable = std::make_shared<activity::XorGate>(2);
	auto condition_command = std::make_shared<activity::XorGate>(3);
	auto wait_for_command = std::make_shared<activity::XorEventGate>(4);
	auto move = std::make_shared<activity::TaskSystemNode>(5, "Move");
	auto wait_for_move = std::make_shared<activity::XorEventGate>(6);
	auto end = std::make_shared<activity::EndNode>(7);

	start->add_output(idle);

	// idle after start
	idle->add_output(condition_moveable);
	idle->set_system_id(system::system_id_t::IDLE);

	// branch 1: check if the entity is moveable
	activity::condition_t command_branch = [&](const time::time_t & /* time */,
	                                           const std::shared_ptr<gamestate::GameEntity> &entity) {
		return entity->has_component(component::component_t::MOVE);
	};
	condition_moveable->add_output(condition_command, command_branch);

	// default: if it's not moveable, go straight to the end
	condition_moveable->set_default(end);

	// branch 1: check if there is already a command in the queue
	condition_command->add_output(move, gamestate::activity::command_in_queue);

	// default: if there is no command, wait for a command
	condition_command->set_default(wait_for_command);

	// wait for a command event
	wait_for_command->add_output(move, gamestate::activity::primer_command_in_queue);

	// move
	move->add_output(wait_for_move);
	move->set_system_id(system::system_id_t::MOVE_COMMAND);

	// branch 1: wait for move event to finish
	wait_for_move->add_output(idle, gamestate::activity::primer_wait);

	// branch 2: wait for a new command event
	wait_for_move->add_output(move, gamestate::activity::primer_command_in_queue);

	return std::make_shared<activity::Activity>(0, start, "test");
}

EntityFactory::EntityFactory() :
	next_entity_id{0},
	next_player_id{0},
	render_factory{nullptr} {
}

std::shared_ptr<GameEntity> EntityFactory::add_game_entity(const std::shared_ptr<openage::event::EventLoop> &loop,
                                                           const std::shared_ptr<GameState> &state,
                                                           player_id_t owner_id,
                                                           const nyan::fqon_t &nyan_entity,
                                                           bool attach_renderer) {
	auto entity = std::make_shared<GameEntity>(this->get_next_entity_id());
	entity->set_manager(std::make_shared<GameEntityManager>(loop, state, entity));

	// use the owner's data to initialize the entity
	// this ensures that only the owner's tech upgrades apply
	auto db_view = state->get_player(owner_id)->get_db_view();
	entity->definition = nyan_entity;
	init_components(loop, db_view, entity, nyan_entity);

	// Converted damage overlays can be empty when the game uses particles.
	// Register only animations that have frames in the installed assets.
	if (entity->has_component(component::component_t::BUILD) and state->get_mod_manager()) {
		const auto definition = db_view->get_object(nyan_entity);
		auto objects = [&](const nyan::Object &obj, const char *member) {
			std::vector<nyan::Object> result;
			for (const auto &value : obj.get_set(member)) {
				auto ref = std::dynamic_pointer_cast<nyan::ObjectValue>(value.get_ptr());
				result.push_back(db_view->get_object(ref->get_name()));
			}
			return result;
		};
		for (const auto &tracker : objects(definition, "GameEntity.abilities")) {
			if (not tracker.extends("engine.ability.type.AttributeChangeTracker")
			    or tracker.get_object("AttributeChangeTracker.attribute").get_name()
			           != "de2_base.data.util.attribute.types.Health") {
				continue;
			}
			for (const auto &stage : objects(tracker, "AttributeChangeTracker.change_progress")) {
				for (const auto &[type, value] : stage.get_dict("Progress.properties")) {
					auto ref = std::dynamic_pointer_cast<nyan::ObjectValue>(value.get_ptr());
					auto property = stage.get_view()->get_object(ref->get_name());
					if (not property.extends("engine.util.progress.property.type.AnimationOverlay")) {
						continue;
					}
					for (const auto &animation : objects(property, "AnimationOverlay.overlays")) {
						auto path = api::APIAnimation::get_animation_path(animation);
						const auto slash = path.find('/');
						if (slash == std::string::npos) {
							continue;
						}
						const auto mod = state->get_mod_manager()->get_modpack(path.substr(0, slash));
						const auto file = mod->get_info().path / path.substr(slash + 1);
						if (file.is_file() and file.open().read().find("\nframe ") != std::string::npos) {
							entity->add_damage_animation(stage.get_float("Progress.right_boundary"), path);
						}
					}
				}
			}
		}
	}

	if (attach_renderer) {
		this->attach_entity_renderer(entity);
	}

	return entity;
}

void EntityFactory::attach_entity_renderer(const std::shared_ptr<GameEntity> &entity) {
	if (this->render_factory) {
		entity->set_render_entity(this->render_factory->add_world_render_entity());
	}
}

std::shared_ptr<Player> EntityFactory::add_player(const std::shared_ptr<openage::event::EventLoop> &loop,
                                                  const std::shared_ptr<GameState> &state,
                                                  const nyan::fqon_t &player_setup,
                                                  std::optional<player_id_t> id) {
	(void)loop;

	const auto player_id = id.value_or(this->next_player_id);
	this->next_player_id = std::max(this->next_player_id, player_id + 1);
	auto player = std::make_shared<Player>(player_id,
	                                       state->get_db_view()->new_child());

	// Empty PlayerSetup FQON -> debug/observer player with neither civ
	// patches nor starting resources. The Mission 1 reconstruction uses
	// this for additional players (e.g. the EnemyVillager's owner)
	// whose PlayerSetup is not yet modelled in the IR.
	if (player_setup.empty()) {
		log::log(MSG(info) << "add_player: empty PlayerSetup FQON; "
		                   << "creating observer-only player id="
		                   << player->get_id());
		return player;
	}

	// Resolve the PlayerSetup nyan object. Authoring errors (missing FQON,
	// wrong type) surface here rather than later when patches reference
	// non-existent targets.
	nyan::Object setup_obj;
	try {
		setup_obj = state->get_db_view()->get_object(player_setup);
	}
	catch (const std::exception &e) {
		throw Error{ERR << "add_player: failed to load PlayerSetup "
		                << player_setup << ": " << e.what()};
	}
	if (not api::APIPlayerSetup::is_player_setup(setup_obj)) {
		throw Error{ERR << "add_player: " << player_setup
		                << " is not a PlayerSetup (parent="
		                << setup_obj.get_parents()[0] << ")"};
	}

	// Apply civ-specific game_setup patches to the *player's* view rather
	// than the root game_state view, so each player gets their own civ
	// modifications independently. Applying to game_state.db_view would
	// leak the civ patch into every other player - defeating per-player
	// civ selection. The player's view was just created via new_child();
	// commits here only affect this player's entity tree.
	{
		auto patches = api::APIPlayerSetup::get_patches(setup_obj);
		if (not patches.empty()) {
			// order=0: civ setup happens at game initialization, before any
			// simulation tick, so time-axis ordering has no consequence.
			auto tx = player->get_db_view()->new_transaction(0);
			for (const auto &patch : patches) {
				// A patch with no resolvable target is an authoring defect
				// in the nyan data; skip with a warning rather than letting
				// nyan throw InternalError during commit (transaction.cpp:88).
				if (patch.get_target() == nullptr) {
					log::log(MSG(warn) << "add_player: PlayerSetup " << player_setup
					                   << " contains patch with unresolvable target, skipping");
					continue;
				}
				tx.add(patch);
			}
			if (not tx.commit()) {
				throw Error{ERR << "add_player: failed to commit PlayerSetup patches for player "
				                << player->get_id()};
			}
		}
	}

	// Pour PlayerSetup.starting_resources into the player's stockpile.
	// Each ResourceAmount(type, amount) maps to
	// player.resources[ResourceAmount.type.fqon] += amount.
	for (const auto &res : api::APIPlayerSetup::get_start_resources(setup_obj)) {
		nyan::Object type_obj = res.get_object("ResourceAmount.type");
		int64_t amount = api::get_nyan_int_or_float(res, "ResourceAmount.amount");
		player->add_resource(type_obj.get_name(), amount);
	}

	// TODO: diplomacy components (post-tutorial).

	return player;
}

void EntityFactory::attach_renderer(const std::shared_ptr<renderer::RenderFactory> &render_factory) {
	std::unique_lock lock{this->mutex};

	this->render_factory = render_factory;
}

void EntityFactory::init_components(const std::shared_ptr<openage::event::EventLoop> &loop,
                                    const std::shared_ptr<nyan::View> &owner_db_view,
                                    const std::shared_ptr<GameEntity> &entity,
                                    const nyan::fqon_t &nyan_entity) {
	auto position = std::make_shared<component::Position>(loop);
	entity->add_component(position);

	auto ownership = std::make_shared<component::Ownership>(loop);
	entity->add_component(ownership);

	auto command_queue = std::make_shared<component::CommandQueue>(loop);
	entity->add_component(command_queue);

	auto nyan_obj = owner_db_view->get_object(nyan_entity);
	nyan::set_t abilities = nyan_obj.get_set("GameEntity.abilities");

	std::optional<nyan::Object> activity_ability;
	std::optional<nyan::Object> resistance_ability;
	std::optional<nyan::Object> foundation_ability;
	for (const auto &ability_val : abilities) {
		auto ability_fqon = std::dynamic_pointer_cast<nyan::ObjectValue>(ability_val.get_ptr())->get_name();
		auto ability_obj = owner_db_view->get_object(ability_fqon);

		auto ability_parent = ability_obj.get_parents()[0];
		if (ability_parent == "engine.ability.type.Move") {
			auto move = std::make_shared<component::Move>(loop, ability_obj);
			entity->add_component(move);
		}
		else if (ability_parent == "engine.ability.type.Turn") {
			auto turn = std::make_shared<component::Turn>(loop, ability_obj);
			entity->add_component(turn);
		}
		else if (ability_parent == "engine.ability.type.Idle") {
			auto idle = std::make_shared<component::Idle>(loop, ability_obj);
			entity->add_component(idle);
		}
		else if (ability_parent == "engine.ability.type.LineOfSight") {
			entity->line_of_sight = std::max(0.0, ability_obj.get_float("LineOfSight.range"));
		}
		else if (ability_parent == "engine.ability.type.Live") {
			auto live = std::make_shared<component::Live>(loop, ability_obj);
			entity->add_component(live);

			auto attr_settings = ability_obj.get_set("Live.attributes");
			for (auto &setting : attr_settings) {
				auto setting_obj_val = std::dynamic_pointer_cast<nyan::ObjectValue>(setting.get_ptr());
				auto setting_obj = owner_db_view->get_object(setting_obj_val->get_name());
				auto attribute = setting_obj.get_object("AttributeSetting.attribute");
				// attribute settings are floats so tech multipliers stay exact
				auto start_value = api::get_nyan_int_or_float(setting_obj, "AttributeSetting.starting_value");

				live->add_attribute(time::TIME_MIN,
				                    attribute.get_name(),
				                    std::make_shared<curve::Discrete<int64_t>>(loop,
				                                                               0,
				                                                               "",
				                                                               nullptr,
				                                                               start_value));
			}
		}
		else if (ability_parent == "engine.ability.type.Activity") {
			activity_ability = ability_obj;
		}
		else if (ability_parent == "engine.ability.type.Selectable") {
			auto selectable = std::make_shared<component::Selectable>(loop, ability_obj);
			entity->add_component(selectable);
		}
		// ---- tutorial-level ability decoder extensions ----
		// These branches swallow abilities that previously were silently
		// dropped (the so-called "zombie nyan" - nyan data is generated
		// for every AoE2 ability but entity_factory only loaded 6 of them).
		// Each branch mounts a corresponding API component; behaviour is
		// a stub for now - Phase C wires activity graphs / systems on top.
		else if (ability_parent == "engine.ability.type.Gather") {
			auto gather = entity->try_get_component_as<component::Gather>(component::component_t::GATHER);
			if (not gather) {
				gather = std::make_shared<component::Gather>(loop, ability_obj);
				entity->add_component(gather);
			}
			gather->add_ability(ability_obj);
		}
		else if (ability_parent == "engine.ability.type.DropResources") {
			auto storage = std::make_shared<component::ResourceStorage>(loop, ability_obj);
			entity->add_component(storage);
		}
		else if (ability_parent == "engine.ability.type.Harvestable") {
			auto spot = ability_obj.get_object("Harvestable.resources");
			const auto types = nyan_obj.get_set("GameEntity.types");
			const bool animal = std::ranges::any_of(types, [](const auto &value) {
				const auto name = std::dynamic_pointer_cast<nyan::ObjectValue>(value.get_ptr())->get_name();
				return name.ends_with(".Herdable") or name.ends_with(".AnimalPrey") or name.ends_with(".AnimalPredator");
			});
			entity->resource_spot = GameEntity::ResourceSpot{
				spot.get_name(), spot.get_object("ResourceSpot.resource").get_name(),
				spot.get_int("ResourceSpot.starting_amount"), animal,
			};
		}
		else if (ability_parent == "engine.ability.type.DropSite") {
			for (const auto &entry : ability_obj.get_set("DropSite.accepts_from")) {
				entity->drop_containers.push_back(std::dynamic_pointer_cast<nyan::ObjectValue>(entry.get_ptr())->get_name());
			}
		}
		else if (ability_parent == "engine.ability.type.RegenerateResourceSpot") {
			// Same: regeneration is owned by the terrain / spot handler.
		}
		else if (ability_parent == "engine.ability.type.Constructable") {
			auto build = std::make_shared<component::Build>(loop, ability_obj);
			entity->add_component(build);
		}
		else if (ability_parent == "engine.ability.type.Foundation") {
			foundation_ability = ability_obj;
		}
		else if (ability_parent == "engine.ability.type.Create"
		         or ability_parent == "engine.ability.type.Research") {
			const bool research = ability_parent == "engine.ability.type.Research";
			const auto member = research ? "Research.researchables" : "Create.creatables";
			for (const auto &entry : ability_obj.get_set(member)) {
				const auto option = owner_db_view->get_object(std::dynamic_pointer_cast<nyan::ObjectValue>(entry.get_ptr())->get_name());
				const auto subject = option.get_object(research ? "ResearchableTech.tech" : "CreatableGameEntity.game_entity");
				const auto cost = option.get_object(research ? "ResearchableTech.cost" : "CreatableGameEntity.cost");
				if (not cost.extends("engine.util.cost.type.ResourceCost")) {
					continue;
				}
				auto kind = research ? ActionKind::Research : ActionKind::Train;
				if (not research) {
					const auto subject_abilities = subject.get_set("GameEntity.abilities");
					const bool water_only = std::ranges::any_of(subject_abilities, [&](const auto &entry) {
						const auto ref = std::dynamic_pointer_cast<nyan::ObjectValue>(entry.get_ptr());
						const auto requirement = owner_db_view->get_object(ref->get_name());
						if (not requirement.extends("engine.ability.type.TerrainRequirement")) {
							return false;
						}
						const auto types = requirement.get_set("TerrainRequirement.allowed_types");
						return types.size() == 1 and std::ranges::all_of(types, [](const auto &value) {
							return std::dynamic_pointer_cast<nyan::ObjectValue>(value.get_ptr())->get_name().ends_with(".Water");
						});
					});
					if (water_only) {
						continue; // Placement and ejection currently route on land.
					}
					bool eject = false;
					for (const auto &mode : option.get_set("CreatableGameEntity.placement_modes")) {
						const auto placement = owner_db_view->get_object(std::dynamic_pointer_cast<nyan::ObjectValue>(mode.get_ptr())->get_name());
						eject = eject or placement.extends("engine.util.placement_mode.type.Eject");
						if (placement.extends("engine.util.placement_mode.type.Place")) {
							kind = ActionKind::Build;
							break;
						}
					}
					if (kind == ActionKind::Train and not eject) {
						continue;
					}
				}
				ActionOption decoded{kind, option.get_name(), subject.get_name(), {},
				                     option.get_float(research ? "ResearchableTech.research_time" : "CreatableGameEntity.creation_time")};
				for (const auto &value : cost.get_set("ResourceCost.amount")) {
					const auto amount = owner_db_view->get_object(std::dynamic_pointer_cast<nyan::ObjectValue>(value.get_ptr())->get_name());
					decoded.cost[amount.get_object("ResourceAmount.type").get_name()] += api::get_nyan_int_or_float(amount, "ResourceAmount.amount");
				}
				entity->action_options.push_back(std::move(decoded));
			}
		}
		else if (ability_parent == "engine.ability.type.ProductionQueue") {
			auto production = std::make_shared<component::Production>(loop, ability_obj);
			entity->add_component(production);
		}
		else if (ability_parent == "engine.ability.type.ApplyContinuousEffect" and ability_fqon.ends_with(".Construct")) {
			entity->construct_ability = ability_obj;
		}
		else if (ability_parent == "engine.ability.type.RangedContinuousEffect" and ability_fqon.ends_with(".Heal")) {
			entity->heal_ability = ability_obj;
		}
		else if (ability_parent == "engine.ability.type.ApplyDiscreteEffect"
		      || ability_parent == "engine.ability.type.RangedDiscreteEffect"
		      || ability_parent == "engine.ability.type.ShootProjectile") {
			// Phase 2 event-driven AttackSystem: mount the Attack component
			// (ability data + runtime attack state). The converter emits
			// Attack abilities as ApplyDiscreteEffect (melee, e.g. villager)
			// or RangedDiscreteEffect (ranged, with min/max_range); both are
			// served by the same component. The old "engine.ability.type.
			// Attack" branch was dead code -- no ability has that parent.
			auto attack = std::make_shared<component::Attack>(loop, ability_obj);
			entity->add_component(attack);
		}
		else if (ability_parent == "engine.ability.type.Resistance") {
			resistance_ability = ability_obj;
		}
		else if (ability_parent == "engine.ability.type.Collision") {
			// P4.4B.1 §1: REQUIRED Footprint. The entity
			// declares the Collision ability, so a valid
			// gameplay Footprint is required. A missing or
			// malformed hitbox throws with full context
			// (entity FQON + ability + source + values).
			// TownCenter and Villager both have valid
			// hitboxes in de2_base; non-collision entities
			// (e.g., ambience) never reach this branch.
			double rx = 0.0;
			double ry = 0.0;
			std::string hitbox_fqon;
			try {
				// Collision.hitbox references a Hitbox
				// object (VillagerHitbox, TownCenterHitbox, ...).
				// ObjectValue only stores the fqon; resolve
				// it via the db view to get a nyan::Object
				// that exposes get_float on Hitbox.radius_x/y.
				auto hitbox_val = ability_obj.get<nyan::ObjectValue>(
					"Collision.hitbox");
				hitbox_fqon = hitbox_val->get_name();
				auto hitbox_obj = owner_db_view->get_object(hitbox_fqon);
				rx = hitbox_obj.get_float("Hitbox.radius_x");
				ry = hitbox_obj.get_float("Hitbox.radius_y");
			}
			catch (const std::exception &e) {
				throw std::runtime_error{
					"EntityFactory: REQUIRED Footprint parse "
					"failed for entity=" + std::string{nyan_entity}
					+ " ability=Collision member=Collision.hitbox"
					+ " source=" + hitbox_fqon
					+ " error=" + e.what()};
			}
			if (!std::isfinite(rx) || !std::isfinite(ry)
			    || rx < 0.0 || ry < 0.0) {
				throw std::runtime_error{
					"EntityFactory: REQUIRED Footprint invalid "
					"for entity=" + std::string{nyan_entity}
					+ " ability=Collision member=Collision.hitbox"
					+ " source=" + hitbox_fqon
					+ " width=" + std::to_string(rx * 2.0)
					+ " height=" + std::to_string(ry * 2.0)};
			}
			// 1 world unit = 1 tile (FixedPoint(16) with
			// 65536 phys per tile), so the radius is
			// already in tile units. blocks_* defaults: a
			// Collision-having entity blocks placement
			// and pathfinding; the loader upgrades static
			// buildings via Mission Pack override.
			auto fp = component::Footprint::make_rectangle(
				rx, ry,
				/*blocks_placement=*/true,
				/*blocks_pathfinding=*/true,
				/*is_static=*/false);
			entity->set_footprint(fp);
		}
		else if (ability_parent == "engine.ability.type.PassiveTransformTo") {
			if (ability_fqon.ends_with(".Death")) {
				entity->set_death_animation(api::APIAnimation::get_ability_animation_path(ability_obj),
				                            ability_obj.get_float("PassiveTransformTo.transform_time"));
			}
		}
		else if (ability_parent == "engine.ability.type.Despawn") {
			entity->set_corpse_animation(api::APIAnimation::get_ability_animation_path(ability_obj),
			                             ability_obj.get_float("Despawn.despawn_time"));
		}
		else if (ability_parent == "engine.ability.type.ProvideContingent"
		         or ability_parent == "engine.ability.type.UseContingent") {
			const bool provides = ability_parent == "engine.ability.type.ProvideContingent";
			auto &population = provides ? entity->population_provided : entity->population_used;
			for (const auto &entry : ability_obj.get_set(provides ? "ProvideContingent.amount" : "UseContingent.amount")) {
				const auto amount = owner_db_view->get_object(std::dynamic_pointer_cast<nyan::ObjectValue>(entry.get_ptr())->get_name());
				if (amount.get_object("ResourceAmount.type").get_name() == "de2_base.data.util.resource.types.PopulationSpace") {
					population += api::get_nyan_int_or_float(amount, "ResourceAmount.amount");
				}
			}
		}
		else {
			// Unrecognised abilities are not silent: log them so that
			// nyan drifts (e.g. a converter emitting a new ability name)
			// are visible at startup.
			log::log(MSG(warn) << "EntityFactory: unhandled ability parent "
			                   << ability_parent << " on entity "
			                   << nyan_entity);
		}
	}

	if (nyan_entity.ends_with(".Trebuchet")) {
		entity->is_transformable = true;
		entity->action_options.push_back(ActionOption{ActionKind::Transform, "trebuchet.toggle_pack", nyan_entity, {}, 2.0});
	}
	std::ranges::sort(entity->action_options, {}, &ActionOption::subject);

	if (foundation_ability and not entity->has_component(component::component_t::BUILD)) {
		entity->add_component(std::make_shared<component::Build>(loop, *foundation_ability));
	}
	if (resistance_ability) {
		auto live = entity->try_get_component_as<component::Live>(component::component_t::LIVE);
		if (live) {
			live->set_resistance(*resistance_ability);
		}
	}

	if (activity_ability) {
		init_activity(loop, owner_db_view, entity, activity_ability.value());
	}
	else {
		auto activity = std::make_shared<component::Activity>(loop, create_test_activity());
		entity->add_component(activity);
	}
}

void EntityFactory::init_activity(const std::shared_ptr<openage::event::EventLoop> &loop,
                                  const std::shared_ptr<nyan::View> &owner_db_view,
                                  const std::shared_ptr<GameEntity> &entity,
                                  const nyan::Object &ability) {
	nyan::Object graph = ability.get_object("Activity.graph");

	// Check if the activity is already exists in the cache
	if (this->activity_cache.contains(graph.get_name())) {
		auto activity = this->activity_cache.at(graph.get_name());
		auto component = std::make_shared<component::Activity>(loop, activity);
		entity->add_component(component);

		return;
	}

	auto start_obj = api::APIActivity::get_start(graph);

	size_t node_id = 0;

	std::deque<nyan::Object> nyan_nodes;
	std::unordered_map<size_t, std::shared_ptr<activity::Node>> node_id_map{};
	std::unordered_map<nyan::fqon_t, size_t> visited{};
	std::shared_ptr<activity::Node> start_node;

	// First pass: create all nodes using breadth-first search
	nyan_nodes.push_back(start_obj);
	while (!nyan_nodes.empty()) {
		auto node = nyan_nodes.front();
		nyan_nodes.pop_front();

		if (visited.contains(node.get_name())) {
			continue;
		}

		// Create the node
		switch (api::APIActivityNode::get_type(node)) {
		case activity::node_t::END:
			break;
		case activity::node_t::START:
			start_node = std::make_shared<activity::StartNode>(node_id);
			node_id_map[node_id] = start_node;
			break;
		case activity::node_t::TASK_SYSTEM: {
			auto task_node = std::make_shared<activity::TaskSystemNode>(node_id);
			task_node->set_system_id(api::APIActivityNode::get_system_id(node));
			node_id_map[node_id] = task_node;
			break;
		}
		case activity::node_t::XOR_GATE:
			node_id_map[node_id] = std::make_shared<activity::XorGate>(node_id);
			break;
		case activity::node_t::XOR_EVENT_GATE:
			node_id_map[node_id] = std::make_shared<activity::XorEventGate>(node_id);
			break;
		default:
			throw Error{ERR << "Unknown activity node type of node: " << node.get_name()};
		}

		// Get the node's outputs
		auto next_nodes = api::APIActivityNode::get_next(node);
		nyan_nodes.insert(nyan_nodes.end(), next_nodes.begin(), next_nodes.end());

		visited.insert({node.get_name(), node_id});
		node_id++;
	}

	// Second pass: connect the nodes
	for (const auto &current_node : visited) {
		auto nyan_node = owner_db_view->get_object(current_node.first);
		auto activity_node = node_id_map[current_node.second];

		switch (activity_node->get_type()) {
		case activity::node_t::END:
			break;
		case activity::node_t::START: {
			auto start = std::static_pointer_cast<activity::StartNode>(activity_node);
			auto output_fqon = nyan_node.get<nyan::ObjectValue>("Start.next")->get_name();
			auto output_id = visited[output_fqon];
			auto output_node = node_id_map[output_id];
			start->add_output(output_node);
			break;
		}
		case activity::node_t::TASK_SYSTEM: {
			auto task_system = std::static_pointer_cast<activity::TaskSystemNode>(activity_node);
			auto output_fqon = nyan_node.get<nyan::ObjectValue>("Ability.next")->get_name();
			auto output_id = visited[output_fqon];
			auto output_node = node_id_map[output_id];
			task_system->add_output(output_node);
			break;
		}
		case activity::node_t::XOR_GATE: {
			auto xor_gate = std::static_pointer_cast<activity::XorGate>(activity_node);
			auto conditions = nyan_node.get<nyan::OrderedSet>("XORGate.next");
			for (auto &condition : conditions->get()) {
				auto condition_value = std::dynamic_pointer_cast<nyan::ObjectValue>(condition.get_ptr());
				auto condition_obj = owner_db_view->get_object(condition_value->get_name());

				auto output_value = condition_obj.get<nyan::ObjectValue>("Condition.next")->get_name();
				auto output_id = visited[output_value];
				auto output_node = node_id_map[output_id];

				xor_gate->add_output(output_node, api::APIActivityCondition::get_condition(condition_obj));
			}

			auto default_fqon = nyan_node.get<nyan::ObjectValue>("XORGate.default")->get_name();
			auto default_id = visited[default_fqon];
			auto default_node = node_id_map[default_id];
			xor_gate->set_default(default_node);
			break;
		}
		case activity::node_t::XOR_EVENT_GATE: {
			auto xor_event_gate = std::static_pointer_cast<activity::XorEventGate>(activity_node);
			auto next = nyan_node.get<nyan::Dict>("XOREventGate.next");
			for (auto &next_node : next->get()) {
				auto event_value = std::dynamic_pointer_cast<nyan::ObjectValue>(next_node.first.get_ptr());
				auto event_obj = owner_db_view->get_object(event_value->get_name());

				auto next_node_value = std::dynamic_pointer_cast<nyan::ObjectValue>(next_node.second.get_ptr());
				auto next_node_obj = owner_db_view->get_object(next_node_value->get_name());

				auto output_id = visited[next_node_obj.get_name()];
				auto output_node = node_id_map[output_id];

				xor_event_gate->add_output(output_node, api::APIActivityEvent::get_primer(event_obj));
			}
			break;
		}
		default:
			throw Error{ERR << "Unknown activity node type of node: " << current_node.first};
		}
	}

	auto activity = std::make_shared<activity::Activity>(0, start_node, graph.get_name());
	this->activity_cache.insert({graph.get_name(), activity});

	auto component = std::make_shared<component::Activity>(loop, activity);
	entity->add_component(component);
}

entity_id_t EntityFactory::get_next_entity_id() {
	auto new_id = this->next_entity_id;
	this->next_entity_id++;

	return new_id;
}

} // namespace openage::gamestate
