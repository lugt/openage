// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Demo 51: exercise the real production RMB binding. Unlike demos 36/38,
// this test does not construct a PickResult or call the semantic resolver
// itself: it projects an actual Mission 1 entity under the mouse and invokes
// the exact BindingContext transform used by the Qt controller.

#include "gamestate/demo/production_context_input_demo.h"

#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "coord/pixel.h"
#include "coord/scene.h"
#include "cvar/cvar.h"
#include "event/event.h"
#include "event/event_loop.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "input/controller/game/binding_context.h"
#include "input/controller/game/controller.h"
#include "log/log.h"
#include "log/message.h"
#include "renderer/camera/camera.h"
#include "renderer/renderer.h"
#include "renderer/resources/texture_data.h"
#include "renderer/resources/texture_info.h"
#include "time/time.h"
#include "time/time_loop.h"


namespace openage::gamestate::tests {
namespace {

constexpr const char *MISSION1_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.Mission";
constexpr const char *PLAYERSETUP_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup";


class NullRenderer final : public renderer::Renderer {
public:
	std::shared_ptr<renderer::Texture2d> add_texture(
		const renderer::resources::Texture2dData &) override {
		return nullptr;
	}
	std::shared_ptr<renderer::Texture2d> add_texture(
		const renderer::resources::Texture2dInfo &) override {
		return nullptr;
	}
	std::shared_ptr<renderer::ShaderProgram> add_shader(
		const std::vector<renderer::resources::ShaderSource> &) override {
		return nullptr;
	}
	std::shared_ptr<renderer::Geometry> add_mesh_geometry(
		const renderer::resources::MeshData &) override {
		return nullptr;
	}
	std::shared_ptr<renderer::Geometry> add_bufferless_quad() override {
		return nullptr;
	}
	std::shared_ptr<renderer::RenderPass> add_render_pass(
		std::vector<renderer::Renderable>,
		const std::shared_ptr<renderer::RenderTarget> &) override {
		return nullptr;
	}
	std::shared_ptr<renderer::RenderTarget> create_texture_target(
		const std::vector<std::shared_ptr<renderer::Texture2d>> &) override {
		return nullptr;
	}
	std::shared_ptr<renderer::RenderTarget> get_display_target() override {
		return nullptr;
	}
	std::shared_ptr<renderer::UniformBuffer> add_uniform_buffer(
		const renderer::resources::UniformBufferInfo &) override {
		return nullptr;
	}
	std::shared_ptr<renderer::UniformBuffer> add_uniform_buffer(
		const std::shared_ptr<renderer::ShaderProgram> &,
		const std::string &) override {
		return nullptr;
	}
	renderer::resources::Texture2dData display_into_data() override {
		return renderer::resources::Texture2dData{
			renderer::resources::Texture2dInfo{}};
	}
	void check_error() override {}
	void render(const std::shared_ptr<renderer::RenderPass> &) override {}
};


entity_id_t find_entity_by_key(
	const std::shared_ptr<GameState> &state,
	const std::string &key_substr) {
	for (const auto &kv : state->get_named_entities()) {
		if (kv.first.find(key_substr) != std::string::npos) {
			return kv.second;
		}
	}
	return 0;
}


coord::phys3 position_at_zero(
	const std::shared_ptr<GameState> &state,
	entity_id_t id) {
	auto position = state->get_game_entity(id)->try_get_component_as<component::Position>(
		component::component_t::POSITION);
	if (position == nullptr) {
		throw std::runtime_error{"demo 51 FAILED: entity has no Position"};
	}
	return position->get_positions().get(time::time_t{0});
}


} // namespace


void simulation_demo_51(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 51: production RMB context input ===");

	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<time::TimeLoop>();
	auto simulation = std::make_shared<GameSimulation>(
		path, cvar, time_loop, PLAYERSETUP_FQON, MISSION1_FQON);
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();
	simulation->start();

	auto state = simulation->get_game()->get_state();
	auto villager_id = find_entity_by_key(state, "PlayerVillager");
	auto enemy_id = find_entity_by_key(state, "EnemyVillager");
	if (not state->has_game_entity(villager_id)
	    or not state->has_game_entity(enemy_id)) {
		throw std::runtime_error{"demo 51 FAILED: missing Mission 1 entities"};
	}

	auto renderer = std::make_shared<NullRenderer>();
	auto camera = std::make_shared<renderer::camera::Camera>(
		renderer, util::Vector2s{1280, 720});
	camera->look_at_coord(position_at_zero(state, enemy_id).to_scene3());

	auto controller = std::make_shared<input::game::Controller>(
		std::unordered_set<size_t>{0}, 0);
	controller->set_selected({villager_id});
	auto bindings = std::make_shared<input::game::BindingContext>();
	input::game::setup_defaults(bindings, time_loop, simulation, camera);

	input::Event rmb{
		input::event_class::MOUSE_BUTTON,
		Qt::MouseButton::RightButton,
		Qt::KeyboardModifier::NoModifier,
		QEvent::MouseButtonRelease};
	input::event_arguments args{rmb, coord::input{640, 360}, coord::input_delta{0, 0}, {}};

	auto game_event = bindings->lookup(rmb).transform(args, controller);
	if (game_event == nullptr) {
		throw std::runtime_error{"demo 51 pin 1 FAILED: enemy RMB produced no event"};
	}

	const auto &params = game_event->get_params();
	auto type = params.get(
		"type", component::command::command_t::NONE);
	auto target_id = params.get("target_id", entity_id_t{0});
	if (type != component::command::command_t::ATTACK
	    or target_id != enemy_id) {
		std::string actual = type == component::command::command_t::MOVE
			                     ? "MOVE"
			                     : "not ATTACK";
		throw std::runtime_error{
			"demo 51 pin 1 FAILED: enemy RMB expected ATTACK target_id="
			+ std::to_string(enemy_id) + ", actual=" + actual
			+ " target_id=" + std::to_string(target_id)};
	}
	log::log(MSG(info) << "demo 51: pin 1 PASS (production enemy RMB -> ATTACK)");

	// Pin 2: the same production binding still sends MOVE for empty ground.
	camera->look_at_coord(coord::scene3{4.0, 2.0, 0.0});
	auto ground_event = bindings->lookup(rmb).transform(args, controller);
	if (ground_event == nullptr) {
		throw std::runtime_error{"demo 51 pin 2 FAILED: ground RMB produced no event"};
	}
	const auto &ground_params = ground_event->get_params();
	auto ground_type = ground_params.get(
		"type", component::command::command_t::NONE);
	auto target = ground_params.get("target", coord::phys3{0, 0, 0});
	if (ground_type != component::command::command_t::MOVE
	    or std::abs(target.ne.to_double() - 4.0) > 0.001
	    or std::abs(target.se.to_double() - 2.0) > 0.001) {
		throw std::runtime_error{
			"demo 51 pin 2 FAILED: ground RMB expected MOVE target=(4,2)"};
	}
	log::log(MSG(info) << "demo 51: pin 2 PASS (production ground RMB -> MOVE)");

	// Pin 3: right-clicking a friendly entity is unsupported and must not
	// fall back to a MOVE command.
	camera->look_at_coord(position_at_zero(state, villager_id).to_scene3());
	auto friendly_event = bindings->lookup(rmb).transform(args, controller);
	if (friendly_event != nullptr) {
		throw std::runtime_error{
			"demo 51 pin 3 FAILED: friendly RMB unexpectedly produced a command"};
	}
	log::log(MSG(info) << "demo 51: pin 3 PASS (friendly RMB -> no command)");

	// Pin 4: a plain left-click is a zero-area drag. It must still select the
	// entity under the pointer through the real press/release bindings.
	controller->set_selected({});
	input::Event lmb_press{
		input::event_class::MOUSE_BUTTON,
		Qt::MouseButton::LeftButton,
		Qt::KeyboardModifier::NoModifier,
		QEvent::MouseButtonPress};
	input::Event lmb_release{
		input::event_class::MOUSE_BUTTON,
		Qt::MouseButton::LeftButton,
		Qt::KeyboardModifier::NoModifier,
		QEvent::MouseButtonRelease};
	input::event_arguments press_args{
		lmb_press, coord::input{640, 360}, coord::input_delta{0, 0}, {}};
	input::event_arguments release_args{
		lmb_release, coord::input{640, 360}, coord::input_delta{0, 0}, {}};
	bindings->lookup(lmb_press).transform(press_args, controller);
	auto select_event = bindings->lookup(lmb_release).transform(release_args, controller);
	if (select_event == nullptr) {
		throw std::runtime_error{"demo 51 pin 4 FAILED: click produced no selection event"};
	}
	simulation->get_event_loop()->reach_time(time::time_t{0}, state);
	const auto &clicked_selection = controller->get_selected();
	if (clicked_selection.size() != 1
	    or clicked_selection.front() != villager_id) {
		throw std::runtime_error{
			"demo 51 pin 4 FAILED: zero-area click selected "
			+ std::to_string(clicked_selection.size())
			+ " entities instead of PlayerVillager"};
	}
	log::log(MSG(info) << "demo 51: pin 4 PASS (production LMB click -> selection)");

	log::log(MSG(info) << "DEMO 51: PASS (production RMB context input)");
}


} // namespace openage::gamestate::tests
