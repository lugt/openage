// Copyright 2021-2023 the openage authors. See copying.md for legal info.
//
// P4.4D: Right-click binding now goes through
// input::resolve_context_action (semantic action resolver). The
// resolver sees a SelectionSnapshot (built from the controller's
// current selection) and a PickResult (entity at the pointer's
// world coord, or empty ground), and returns one of:
//   - MOVE   -> send MoveCommand to the selection
//   - ATTACK -> send AttackCommand to the picked hostile entity
//   - REJECTED / UNSUPPORTED / NO_OP -> no command (logged)
//
// The resolver is a pure function; this binding is the only
// place that knows the difference between MoveCommand and
// AttackCommand. The Move / Attack runtime (system/move.cpp,
// event/attack.cpp) does NOT branch on FQON or display name;
// they branch on command_t::MOVE vs command_t::ATTACK.

#include "controller.h"

#include <cmath>
#include <limits>

#include "log/log.h"

#include "event/event_loop.h"
#include "event/evententity.h"
#include "event/state.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/build.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/api/move.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/event/send_command.h"
#include "gamestate/event/spawn_entity.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/component/api/gather.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "input/controller/game/binding_context.h"
#include "input/semantic_action.h"
#include "time/clock.h"
#include "time/time_loop.h"

#include "coord/phys.h"
#include "renderer/camera/camera.h"


namespace openage::input::game {

namespace {

SelectionSnapshot make_selection_snapshot(
	const std::shared_ptr<gamestate::GameState> &state,
	const std::vector<gamestate::entity_id_t> &selected,
	const time::time_t &now) {
	SelectionSnapshot snapshot;
	for (auto id : selected) {
		SelectionSnapshot::Entry entry;
		entry.id = id;
		if (not state->has_game_entity(id)) {
			snapshot.entries.push_back(std::move(entry));
			continue;
		}

		auto entity = state->get_game_entity(id);
		entry.has_move_ability = entity->try_get_component_as<gamestate::component::Move>(
			                         gamestate::component::component_t::MOVE)
		                         != nullptr;
		entry.has_attack_ability = entity->try_get_component_as<gamestate::component::Attack>(
			                           gamestate::component::component_t::ATTACK)
		                           != nullptr;
		auto ownership = entity->try_get_component_as<gamestate::component::Ownership>(
			gamestate::component::component_t::OWNERSHIP);
		if (ownership != nullptr) {
			entry.owner = ownership->get_owners().get(now);
		}
		snapshot.entries.push_back(std::move(entry));
	}
	return snapshot;
}


PickResult pick_entity_at(
	const std::shared_ptr<gamestate::GameState> &state,
	const coord::phys3 &world_pos,
	const time::time_t &now,
	gamestate::player_id_t viewer) {
	PickResult pick;
	pick.world_x = world_pos.ne.to_double();
	pick.world_y = world_pos.se.to_double();

	double closest_distance = std::numeric_limits<double>::infinity();
	for (const auto &[id, entity] : state->get_game_entities()) {
		if ((entity->is_dead() and not entity->is_harvestable_carcass()) or not state->can_see(viewer, id)) {
			continue;
		}
		auto position = entity->try_get_component_as<gamestate::component::Position>(
			gamestate::component::component_t::POSITION);
		if (position == nullptr) {
			continue;
		}
		auto center = position->get_positions().get(now);
		double dx = pick.world_x - center.ne.to_double();
		double dy = pick.world_y - center.se.to_double();

		// Gameplay footprints provide deterministic picking for buildings.
		// Units with a zero/small footprint still get a half-tile hit area,
		// matching the MVP's center-based unit picking.
		double radius_x = 0.5;
		double radius_y = 0.5;
		if (entity->has_footprint()) {
			radius_x = std::max(radius_x, entity->footprint().radius_x());
			radius_y = std::max(radius_y, entity->footprint().radius_y());
		}
		if (std::abs(dx) > radius_x or std::abs(dy) > radius_y) {
			continue;
		}

		double distance = dx * dx + dy * dy;
		if (distance < closest_distance
		    or (distance == closest_distance and id < pick.entity)) {
			closest_distance = distance;
			pick.has_entity = true;
			pick.entity = id;
			auto ownership = entity->try_get_component_as<gamestate::component::Ownership>(
				gamestate::component::component_t::OWNERSHIP);
			if (ownership != nullptr) {
				pick.entity_owner = ownership->get_owners().get(now);
			}
		}
	}
	return pick;
}


PickResult context_pick(const std::shared_ptr<gamestate::GameState> &state, const coord::phys3 &world,
                        std::optional<uint32_t> sprite, gamestate::player_id_t viewer, time::time_t now) {
	if (sprite and state->has_game_entity(*sprite) and state->can_see(viewer, *sprite)
	    and (not state->get_game_entity(*sprite)->is_dead() or state->get_game_entity(*sprite)->is_harvestable_carcass())) {
		auto ownership = state->get_game_entity(*sprite)->try_get_component_as<gamestate::component::Ownership>(
			gamestate::component::component_t::OWNERSHIP);
		return {true, *sprite, ownership ? ownership->get_owners().get(now) : viewer,
		        world.ne.to_double(), world.se.to_double()};
	}
	return pick_entity_at(state, world, now, viewer);
}

} // namespace


bool is_attack_target(const std::shared_ptr<gamestate::GameState> &state, const coord::phys3 &world,
                      std::optional<uint32_t> sprite, gamestate::player_id_t viewer, time::time_t now) {
	const auto pick = context_pick(state, world, sprite, viewer, now);
	return pick.has_entity and not state->get_game_entity(pick.entity)->is_dead()
	    and state->get_stance(viewer, pick.entity_owner) == gamestate::DiplomaticStance::Enemy
	    and state->get_game_entity(pick.entity)->has_component(gamestate::component::component_t::LIVE);
}

Controller::Controller(const std::unordered_set<size_t> &controlled_factions,
                       size_t active_faction_id) :
	controlled_factions{controlled_factions},
	active_faction_id{active_faction_id},
	outqueue{} {}

void Controller::set_control(size_t faction_id) {
	std::unique_lock lock{this->mutex};

	if (this->controlled_factions.find(faction_id) != this->controlled_factions.end()) {
		this->active_faction_id = faction_id;
	}
}

size_t Controller::get_controlled() const {
	std::unique_lock lock{this->mutex};

	return this->active_faction_id;
}

std::vector<gamestate::entity_id_t> Controller::get_selected() const {
	std::unique_lock lock{this->mutex};

	return this->selected;
}

void Controller::set_selected(const std::vector<gamestate::entity_id_t> ids) {
	std::unique_lock lock{this->mutex};

	log::log(DBG << "Selected " << ids.size() << " entities");

	this->selected = ids;

	// Mirror into the presentation-layer SelectionModel (single
	// view for HUD + overlay renderer). The callback must not
	// re-enter the Controller; it only pushes into the model.
	if (this->selection_cb) {
		this->selection_cb(ids);
	}
}

bool Controller::process(const event_arguments &ev_args, const std::shared_ptr<BindingContext> &ctx) {
	// BindingContext is filled at setup time and only read here; no lock needed.
	log::log(DBG << "game::Controller::process: checking binding for event");
	if (not ctx->is_bound(ev_args.e)) {
		log::log(DBG << "game::Controller::process: event NOT bound in game context");
		return false;
	}
	log::log(DBG << "game::Controller::process: event IS bound; spawning");

	// TODO: check if action is allowed
	auto bind = ctx->lookup(ev_args.e);
	auto controller = this->shared_from_this();

	// NOTE: bind.transform() may call back into this Controller (get_controlled,
	// get_selected, ...) and into EventLoop::create_event (via the input
	// bindings). It must run WITHOUT holding this->mutex: the simulation thread
	// takes the locks in the order EventLoop -> Controller, so taking them here
	// in the order Controller -> EventLoop would deadlock (AB-BA).
	auto game_event = bind.transform(ev_args, controller);

	switch (bind.action_type) {
	case forward_action_t::SEND:
	{
		std::unique_lock lock{this->mutex};
		this->outqueue.push_back(game_event);
		for (auto event : this->outqueue) {
			// TODO: Send gamestate event
		}
	}
		break;

	case forward_action_t::QUEUE:
	{
		std::unique_lock lock{this->mutex};
		this->outqueue.push_back(game_event);
	}
		break;

	case forward_action_t::CLEAR:
	{
		std::unique_lock lock{this->mutex};
		this->outqueue.clear();
	}
		break;

	default:
		throw Error{MSG(err) << "Unrecognized action type."};
	}

	return true;
}

void Controller::set_drag_select_start(const coord::input &start) {
	std::unique_lock lock{this->mutex};

	log::log(DBG << "Drag select start at " << start);
	this->drag_select_start = start;
}

const coord::input Controller::get_drag_select_start() const {
	std::unique_lock lock{this->mutex};

	if (not this->drag_select_start.has_value()) {
		throw Error{ERR << "Drag select start not set."};
	}

	return this->drag_select_start.value();
}

bool Controller::has_drag_select_start() const {
	std::unique_lock lock{this->mutex};

	return this->drag_select_start.has_value();
}

void Controller::reset_drag_select() {
	std::unique_lock lock{this->mutex};

	log::log(DBG << "Drag select start reset");
	this->drag_select_start = std::nullopt;
}

void setup_defaults(const std::shared_ptr<BindingContext> &ctx,
                    const std::shared_ptr<time::TimeLoop> &time_loop,
                    const std::shared_ptr<openage::gamestate::GameSimulation> &simulation,
                    const std::shared_ptr<renderer::camera::Camera> &camera,
                    std::function<std::optional<uint32_t>(coord::input)> sprite_picker, int bottom_hud_height) {
	binding_func_t create_entity_event{[time_loop, simulation, camera](const event_arguments &args,
	                                       const std::shared_ptr<Controller> controller) {
		auto mouse_pos = args.mouse.to_phys3(camera);
		event::EventHandler::param_map::map_t params{
			{"position", mouse_pos},
			{"owner", controller->get_controlled()},
		};

		auto event = simulation->get_event_loop()->create_event(
			"game.spawn_entity",
			simulation->get_spawner(),
			simulation->get_game()->get_state(),
			time_loop->get_clock()->get_time(),
			params);
		return event;
	}};

	binding_action create_entity_action{forward_action_t::SEND, create_entity_event};
	Event ev_mouse_lmb_ctrl{
		event_class::MOUSE_BUTTON,
		Qt::MouseButton::LeftButton,
		Qt::AltModifier | Qt::ControlModifier,
		QEvent::MouseButtonRelease};

	ctx->bind(ev_mouse_lmb_ctrl, create_entity_action);

	binding_func_t context_action{[time_loop, simulation, camera, sprite_picker](const event_arguments &args,
	                                  const std::shared_ptr<Controller> controller) {
		auto state = simulation->get_game()->get_state();
		auto now = time_loop->get_clock()->get_time();
		auto mouse_pos = args.mouse.to_phys3(camera);
		auto selected_ids = controller->get_selected();
		auto selection = make_selection_snapshot(state, selected_ids, now);
		const auto viewer = controller->get_controlled();
		const auto pick = context_pick(state, mouse_pos, sprite_picker ? sprite_picker(args.mouse) : std::nullopt,
		                               viewer, now);
		const auto resource_id = pick.entity;
		if (pick.has_entity) {
			const auto resource = state->get_game_entity(resource_id);
			if (pick.entity_owner == viewer) {
				const auto build = resource->try_get_component_as<gamestate::component::Build>(
					gamestate::component::component_t::BUILD);
				if (build and build->ordered_by_player and build->is_under_construction()) {
					std::shared_ptr<event::Event> last;
					for (auto id : selected_ids) {
						if (state->has_game_entity(id) and state->get_game_entity(id)->construct_ability) {
							last = simulation->get_event_loop()->create_event(
								"game.economy_action", simulation->get_commander(), state, now, {
									{"actor", id}, {"site", resource_id},
									{"issuer", gamestate::player_id_t{viewer}},
								});
						}
					}
					if (last) {
						return last;
					}
				}
				if (not build and not resource->is_dead()) {
					const auto live = resource->try_get_component_as<gamestate::component::Live>(
						gamestate::component::component_t::LIVE);
					const nyan::fqon_t health = "de2_base.data.util.attribute.types.Health";
					const auto hp = live ? live->get_attribute_value(now, health) : std::nullopt;
					const auto max_hp = live ? live->get_attribute_max(health) : std::nullopt;
					if (hp and max_hp and *hp < *max_hp) {
						std::vector<gamestate::entity_id_t> healers;
						for (auto id : selected_ids) {
							if (state->has_game_entity(id) and state->get_game_entity(id)->heal_ability) {
								healers.push_back(id);
							}
						}
						if (not healers.empty()) {
							return simulation->get_event_loop()->create_event(
								"game.send_command", simulation->get_commander(), state, now, {
									{"type", gamestate::component::command::command_t::HEAL},
									{"target_id", resource_id},
									{"entity_ids", healers},
									{"issuer", gamestate::player_id_t{viewer}},
								});
						}
					}
				}
			}
			if (resource->resource_spot.has_value()) {
				std::vector<gamestate::entity_id_t> workers;
				for (auto id : selected_ids) {
					if (not state->has_game_entity(id)) {
						continue;
					}
					auto unit = state->get_game_entity(id);
					auto gather = unit->try_get_component_as<gamestate::component::Gather>(
						gamestate::component::component_t::GATHER);
					auto owner = unit->try_get_component_as<gamestate::component::Ownership>(
						gamestate::component::component_t::OWNERSHIP);
					if (gather and owner and not unit->is_dead() and owner->get_owners().get(now) == viewer
					    and gather->find_ability(resource->resource_spot->definition)) {
						workers.push_back(id);
					}
				}
				if (not workers.empty()) {
					event::EventHandler::param_map::map_t params{
						{"type", gamestate::component::command::command_t::GATHER},
						{"target_id", resource_id}, {"entity_ids", workers}, {"issuer", gamestate::player_id_t{viewer}},
					};
					return simulation->get_event_loop()->create_event(
						"game.send_command", simulation->get_commander(), state, now, params);
				}
			}
		}
		auto action = resolve_context_action(state, selection, pick, viewer, now);
		if (action.kind == SemanticActionKind::MOVE and action.actors.size() > 1) {
			event::EventHandler::param_map::map_t formation{
				{"entity_ids", action.actors}, {"center_x", pick.world_x}, {"center_y", pick.world_y},
				{"issuer", gamestate::player_id_t{viewer}},
			};
			return simulation->get_event_loop()->create_event(
				"game.group_move", simulation->get_commander(), state, now, formation);
		}

		event::EventHandler::param_map::map_t params;
		switch (action.kind) {
		case SemanticActionKind::MOVE:
			params = {
				{"type", gamestate::component::command::command_t::MOVE},
				{"target", mouse_pos},
				{"entity_ids", action.actors},
			};
			break;
		case SemanticActionKind::ATTACK:
			params = {
				{"type", gamestate::component::command::command_t::ATTACK},
				{"target_id", action.target_entity.value()},
				{"entity_ids", action.actors},
			};
			break;
		case SemanticActionKind::NO_OP:
		case SemanticActionKind::REJECTED:
		case SemanticActionKind::UNSUPPORTED:
			log::log(DBG << "RMB context action " << action.kind_string()
			             << ": " << action.reason);
			return std::shared_ptr<event::Event>{nullptr};
		}

		params.emplace("issuer", gamestate::player_id_t{viewer});
		return simulation->get_event_loop()->create_event(
			"game.send_command",
			simulation->get_commander(),
			state,
			now,
			params);
	}};

	binding_action move_entity_action{forward_action_t::SEND, context_action};

	binding_func_t init_drag_selection{[](const event_arguments &args,
	                                       const std::shared_ptr<Controller> controller) {
		controller->set_drag_select_start(args.mouse);
		return nullptr;
	}};

	binding_action init_drag_selection_action{forward_action_t::CLEAR, init_drag_selection};

	binding_func_t drag_selection{
		[time_loop, simulation, camera, bottom_hud_height, sprite_picker = std::move(sprite_picker)](const event_arguments &args,
	        const std::shared_ptr<Controller> controller) {
			// A drag-select release without a registered press start must be a
			// no-op: this happens when the left button press was consumed by
			// the window manager (e.g. during a window resize) so the start
			// was never set. Skip instead of throwing.
			if (not controller->has_drag_select_start()) {
				return std::shared_ptr<event::Event>{nullptr};
			}

			const auto start = controller->get_drag_select_start();
			const auto distance = args.mouse - start;
			const bool point_select = std::abs(distance.x) <= 3 and std::abs(distance.y) <= 3;
			Eigen::Matrix4f cam_matrix = camera->get_projection_matrix() * camera->get_view_matrix();
			event::EventHandler::param_map::map_t params{
				{"controlled", gamestate::player_id_t{controller->get_controlled()}},
				{"same_type", bool(args.e.mod_code & Qt::ShiftModifier)},
				{"screen_bottom", 2.0f * bottom_hud_height / camera->get_viewport_size()[1] - 1.0f},
				{"additive", bool(args.e.mod_code & (Qt::ControlModifier
#ifdef __APPLE__
				                                            | Qt::MetaModifier
#endif
				                                            ))},
				{"previous_selection", std::function{[controller] { return controller->get_selected(); }}},
				{"point_select", point_select},
				{"drag_start", start.to_viewport(camera).to_ndc_space(camera)},
				{"drag_end", args.mouse.to_viewport(camera).to_ndc_space(camera)},
				{"camera_matrix", cam_matrix},
				{"select_cb",
		         std::function<void(const std::vector<gamestate::entity_id_t> ids)>{
					 [controller](
						 const std::vector<gamestate::entity_id_t> ids) {
						 controller->set_selected(ids);
					 }}},
			};

			if (point_select and sprite_picker) {
				params.emplace("sprite_pick", sprite_picker(args.mouse));
			}
			auto event = simulation->get_event_loop()->create_event(
				"game.drag_select",
				simulation->get_commander(),
				simulation->get_game()->get_state(),
				time_loop->get_clock()->get_time(),
				params);

			// Reset drag selection start
			controller->reset_drag_select();

			return event;
		}};

	binding_action drag_selection_action{forward_action_t::CLEAR, drag_selection};
	auto bind_mouse = [&](Qt::KeyboardModifiers modifiers) {
		ctx->bind({event_class::MOUSE_BUTTON, Qt::LeftButton, static_cast<modset_t>(modifiers), QEvent::MouseButtonPress}, init_drag_selection_action);
		ctx->bind({event_class::MOUSE_BUTTON, Qt::LeftButton, static_cast<modset_t>(modifiers), QEvent::MouseButtonRelease}, drag_selection_action);
		ctx->bind({event_class::MOUSE_BUTTON_DBL, Qt::LeftButton, static_cast<modset_t>(modifiers), QEvent::MouseButtonDblClick}, init_drag_selection_action);
		ctx->bind({event_class::MOUSE_BUTTON, Qt::RightButton, static_cast<modset_t>(modifiers), QEvent::MouseButtonRelease}, move_entity_action);
	};
	for (auto modifiers : {Qt::KeyboardModifiers{}, Qt::KeyboardModifiers{Qt::ControlModifier},
	                       Qt::KeyboardModifiers{Qt::ShiftModifier}, Qt::ControlModifier | Qt::ShiftModifier}) {
		bind_mouse(modifiers);
	}
#ifdef __APPLE__
	bind_mouse(Qt::MetaModifier);
	bind_mouse(Qt::MetaModifier | Qt::ShiftModifier);
#endif
}


} // namespace openage::input::game
