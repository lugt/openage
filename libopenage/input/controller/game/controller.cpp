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
#include "gamestate/component/api/move.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/event/send_command.h"
#include "gamestate/event/spawn_entity.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
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
	const time::time_t &now) {
	PickResult pick;
	pick.world_x = world_pos.ne.to_double();
	pick.world_y = world_pos.se.to_double();

	double closest_distance = std::numeric_limits<double>::infinity();
	for (const auto &[id, entity] : state->get_game_entities()) {
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


} // namespace


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

const std::vector<gamestate::entity_id_t> &Controller::get_selected() const {
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
                    const std::shared_ptr<renderer::camera::Camera> &camera) {
	binding_func_t create_entity_event{[&](const event_arguments &args,
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
		Qt::KeyboardModifier::ControlModifier,
		QEvent::MouseButtonRelease};

	ctx->bind(ev_mouse_lmb_ctrl, create_entity_action);

	binding_func_t context_action{[&](const event_arguments &args,
	                                  const std::shared_ptr<Controller> controller) {
		auto state = simulation->get_game()->get_state();
		auto now = time_loop->get_clock()->get_time();
		auto mouse_pos = args.mouse.to_phys3(camera);
		auto selection = make_selection_snapshot(
			state, controller->get_selected(), now);
		auto pick = pick_entity_at(state, mouse_pos, now);
		auto action = resolve_context_action(
			state, selection, pick, controller->get_controlled());

		event::EventHandler::param_map::map_t params;
		switch (action.kind) {
		case SemanticActionKind::MOVE:
			params = {
				{"type", gamestate::component::command::command_t::MOVE},
				{"target", mouse_pos},
				{"entity_ids", controller->get_selected()},
			};
			break;
		case SemanticActionKind::ATTACK:
			params = {
				{"type", gamestate::component::command::command_t::ATTACK},
				{"target_id", action.target_entity.value()},
				{"entity_ids", controller->get_selected()},
			};
			break;
		case SemanticActionKind::NO_OP:
		case SemanticActionKind::REJECTED:
		case SemanticActionKind::UNSUPPORTED:
			log::log(DBG << "RMB context action " << action.kind_string()
			             << ": " << action.reason);
			return std::shared_ptr<event::Event>{nullptr};
		}

		return simulation->get_event_loop()->create_event(
			"game.send_command",
			simulation->get_commander(),
			state,
			now,
			params);
	}};

	binding_action move_entity_action{forward_action_t::SEND, context_action};
	Event ev_mouse_rmb{
		event_class::MOUSE_BUTTON,
		Qt::MouseButton::RightButton,
		Qt::KeyboardModifier::NoModifier,
		QEvent::MouseButtonRelease};

	ctx->bind(ev_mouse_rmb, move_entity_action);

	binding_func_t init_drag_selection{[&](const event_arguments &args,
	                                       const std::shared_ptr<Controller> controller) {
		controller->set_drag_select_start(args.mouse);
		return nullptr;
	}};

	binding_action init_drag_selection_action{forward_action_t::CLEAR, init_drag_selection};
	Event ev_mouse_lmb_press{
		event_class::MOUSE_BUTTON,
		Qt::MouseButton::LeftButton,
		Qt::KeyboardModifier::NoModifier,
		QEvent::MouseButtonPress};

	ctx->bind(ev_mouse_lmb_press, init_drag_selection_action);

	binding_func_t drag_selection{
		[&](const event_arguments &args,
	        const std::shared_ptr<Controller> controller) {
			// A drag-select release without a registered press start must be a
			// no-op: this happens when the left button press was consumed by
			// the window manager (e.g. during a window resize) so the start
			// was never set. Skip instead of throwing.
			if (not controller->has_drag_select_start()) {
				return std::shared_ptr<event::Event>{nullptr};
			}

			Eigen::Matrix4f cam_matrix = camera->get_projection_matrix() * camera->get_view_matrix();
			event::EventHandler::param_map::map_t params{
				{"controlled", controller->get_controlled()},
				{"drag_start", controller->get_drag_select_start().to_viewport(camera).to_ndc_space(camera)},
				{"drag_end", args.mouse.to_viewport(camera).to_ndc_space(camera)},
				{"camera_matrix", cam_matrix},
				{"select_cb",
		         std::function<void(const std::vector<gamestate::entity_id_t> ids)>{
					 [controller](
						 const std::vector<gamestate::entity_id_t> ids) {
						 controller->set_selected(ids);
					 }}},
			};

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
	Event ev_mouse_lmb_release{
		event_class::MOUSE_BUTTON,
		Qt::MouseButton::LeftButton,
		Qt::KeyboardModifier::NoModifier,
		QEvent::MouseButtonRelease};

	ctx->bind(ev_mouse_lmb_release, drag_selection_action);
}


} // namespace openage::input::game
