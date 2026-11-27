// Copyright 2019-2024 the openage authors. See copying.md for legal info.

#include "presenter.h"

#include <eigen3/Eigen/Dense>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

#include <QGuiApplication>
#include <QImage>
#include <QCursor>
#include <QPixmap>
#include <QWindow>
#include <QRegularExpression>
#include <QPainter>
#include <QPolygonF>
#include <QMouseEvent>
#include <QTimer>
#include <QUrl>

#include "error/error.h"
#include "gamestate/game.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/build.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/command_queue.h"
#include "gamestate/component/internal/commands/types.h"
#include "time/clock.h"
#include "gamestate/game_state.h"
#include "gamestate/player.h"
#include "scenario/trigger/original_scenario.h"
#include "gamestate/map.h"
#include "gamestate/terrain.h"
#include "gamestate/terrain_chunk.h"
#include "coord/tile.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/event/economy_action.h"
#include "gamestate/event/send_command.h"
#include "gamestate/simulation.h"
#include "gamestate/save_load.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/game_entity.h"
#include "coord/scene.h"
#include "input/controller/camera/binding_context.h"
#include "input/controller/camera/controller.h"
#include "input/controller/game/binding_context.h"
#include "input/controller/game/controller.h"
#include "input/controller/hud/binding_context.h"
#include "input/controller/hud/controller.h"
#include "input/input_context.h"
#include "input/input_manager.h"
#include "log/log.h"
#include "presenter/hud_qml_adapter.h"
#include "renderer/camera/camera.h"
#include "renderer/gui/gui.h"
#include "renderer/gui/integration/public/gui_application_with_logger.h"
#include "renderer/render_factory.h"
#include "renderer/render_pass.h"
#include "renderer/render_target.h"
#include "renderer/resources/assets/asset_manager.h"
#include "renderer/resources/shader_source.h"
#include "renderer/resources/texture_data.h"
#include "renderer/resources/texture_info.h"
#include "renderer/stages/camera/manager.h"
#include "renderer/stages/hud/render_stage.h"
#include "renderer/stages/screen/render_stage.h"
#include "renderer/stages/skybox/render_stage.h"
#include "renderer/stages/terrain/render_stage.h"
#include "renderer/stages/world/render_stage.h"
#include "scenario/trigger/trigger_runtime.h"
#include "time/time_loop.h"
#include "time/clock.h"
#include "util/path.h"
#include "presenter/viewmodel/selection_model.h"

import openage.util.startup_trace;


namespace openage::presenter {

namespace {

QCursor attack_cursor() {
	QPixmap image(32, 32);
	image.fill(Qt::transparent);
	QPainter painter(&image);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(QPen(QColor("#171417"), 1.5));
	painter.setBrush(QColor("#f4eded"));
	painter.drawPolygon(QPolygonF{{4, 3}, {10, 5}, {22, 17}, {18, 21}, {6, 9}});
	painter.setBrush(QColor("#cb673e"));
	painter.drawPolygon(QPolygonF{{14, 21}, {22, 13}, {26, 17}, {18, 25}});
	painter.drawPolygon(QPolygonF{{21, 22}, {23, 20}, {29, 26}, {26, 29}});
	return QCursor(image, 5, 4);
}

QString object_label(const nyan::fqon_t &fqon) {
	auto result = QString::fromStdString(fqon.substr(fqon.find_last_of('.') + 1));
	if (result.endsWith(QStringLiteral("Researchable"))) {
		result.chop(12);
	}
	static const QRegularExpression words{QStringLiteral("([a-z0-9])([A-Z])")};
	return result.replace(words, QStringLiteral("\\1 \\2"));
}

const nyan::fqon_t health_attribute = "de2_base.data.util.attribute.types.Health";

} // namespace

struct Presenter::HudCache {
	std::unordered_map<std::string, QString> icon_urls;
	bool has_selection_snapshot{false};
	QVariantList last_cards;
	QVariantList last_actions;
	QVariantList last_production_queue;
	int last_construction{-1};
	bool last_can_stop{false};
	QString last_action_status;
	uint64_t last_feedback_seq{0};
	int last_idle_villagers{-1};
	bool has_diplomacy_snapshot{false};
	QVariantList last_diplomacy;
	size_t minimap_width{0};
	size_t minimap_height{0};
	std::vector<QRgb> minimap_visible_colors;
	std::vector<QRgb> minimap_explored_colors;
};

Presenter::Presenter(const util::Path &root_dir,
                     const std::shared_ptr<gamestate::GameSimulation> &simulation,
                     const std::shared_ptr<time::TimeLoop> &time_loop,
                     const renderer::window_settings &window_settings) :
	root_dir{root_dir},
	window_settings{window_settings},
	render_passes{},
	simulation{simulation},
	time_loop{time_loop},
	hud_cache{std::make_unique<HudCache>()} {}

Presenter::~Presenter() = default;


void Presenter::run() {
	// Phase C5: if init_all() was already driven by Engine::Engine
	// during its pre-flight (before Engine::loop() spawned T1/T2), skip
	// the init phase here and proceed straight to the render loop. This
	// keeps init_graphics/init_input, which touch
	// simulation->attach_renderer(), off the racing path with the
	// simulation thread.
	if (not this->initialized) {
		this->init_all();
	}

	while (not this->window->should_close()) {
		this->frame_rendered = false;
		this->window->update();
		this->gui_app->process_events();
		if (not this->frame_rendered and not this->window->should_close()) {
			std::this_thread::sleep_for(std::chrono::milliseconds{1});
		}
	}

	log::log(MSG(info) << "Presenter: Draw loop exited");

	if (this->simulation) {
		this->simulation->stop();
	}

	if (this->time_loop) {
		this->time_loop->stop();
	}

	this->window->close();
}

void Presenter::init_all() {
	if (this->initialized) {
		return;
	}
	this->startup_origin_ns = timing::get_monotonic_time();
	util::StartupTraceScope total_trace{"presenter.init_all.total"};
	log::log(MSG(info) << "Presenter: pre-flight init_all() (on main thread, "
	                       "before Engine::loop spawns workers)");
	this->init_graphics();
	this->init_input();
	this->initialized = true;
	log::log(MSG(info) << "Presenter: pre-flight complete; render loop is safe "
	                       "to start concurrently with simulation tick");
}

void Presenter::set_simulation(const std::shared_ptr<gamestate::GameSimulation> &simulation) {
	this->simulation = simulation;
	auto render_factory = std::make_shared<renderer::RenderFactory>(this->terrain_renderer, this->world_renderer);
	this->simulation->attach_renderer(render_factory);
	this->setup_selection_viewmodel();
}

void Presenter::set_time_loop(const std::shared_ptr<time::TimeLoop> &time_loop) {
	this->time_loop = time_loop;
}

std::shared_ptr<qtgui::GuiApplication> Presenter::init_window_system() {
	return std::make_shared<renderer::gui::GuiApplicationWithLogger>();
}

void Presenter::init_graphics() {
	log::log(INFO << "Presenter: Initializing graphics subsystems...");

	// Start up rendering framework
	{
		util::StartupTraceScope trace{"presenter.qt_app"};
		this->gui_app = this->init_window_system();
		ENSURE(this->gui_app != nullptr, "Presenter::init_graphics: failed to create GuiApplication.");
	}

	// Window and renderer
	{
		util::StartupTraceScope trace{"presenter.window"};
		this->window = renderer::Window::create("openage presenter test", this->window_settings);
		ENSURE(this->window != nullptr, "Presenter::init_graphics: failed to create Window.");
	}
	{
		util::StartupTraceScope trace{"presenter.renderer"};
		this->renderer = this->window->make_renderer();
		ENSURE(this->renderer != nullptr, "Presenter::init_graphics: failed to create Renderer.");
	}

	// Asset management
	{
		util::StartupTraceScope trace{"presenter.asset_manager"};
		this->asset_manager = std::make_shared<renderer::resources::AssetManager>(
			this->renderer,
			this->root_dir / "assets" / "converted");
		auto missing_tex = this->root_dir / "assets" / "test" / "textures" / "test_missing.sprite";
		this->asset_manager->set_placeholder_animation(missing_tex);
		auto fallback_terrain = this->root_dir / "assets" / "test" / "textures" / "test_terrain.terrain";
		this->asset_manager->set_placeholder_terrain(fallback_terrain);
	}

	// Camera and camera manager
	{
		util::StartupTraceScope trace{"presenter.camera"};
		this->camera = std::make_shared<renderer::camera::Camera>(this->renderer, this->window->get_size());
		ENSURE(this->camera != nullptr, "Presenter::init_graphics: failed to create Camera.");
		this->camera->look_at_coord(coord::scene3{10.0, 10.0, 0}); // Center camera on the map
		this->window->add_resize_callback([this](size_t w, size_t h, double /*scale*/) {
			this->camera->resize(w, h);
		});

		this->camera_manager = std::make_shared<renderer::camera::CameraManager>(this->camera);
		// TODO: Make boundaries dynamic based on map size.
		this->camera_manager->set_camera_boundaries(
			renderer::camera::CameraBoundaries{
				renderer::camera::X_BOUND_MIN,
				renderer::camera::X_BOUND_MAX,
				renderer::camera::Y_BOUND_MIN,
				renderer::camera::Y_BOUND_MAX,
				renderer::camera::Z_BOUND_MIN,
				renderer::camera::Z_BOUND_MAX});
	}

	// Render stages
	{
		util::StartupTraceScope trace{"presenter.render_stages"};
		ENSURE(this->time_loop != nullptr, "Presenter::init_graphics: TimeLoop must not be null.");

		// Skybox
		this->skybox_renderer = std::make_shared<renderer::skybox::SkyboxRenderStage>(
			this->window,
			this->renderer,
			this->root_dir["assets"]["shaders"]);
		this->skybox_renderer->set_color(0.035f, 0.045f, 0.05f, 1.0f);
		this->render_passes.push_back(this->skybox_renderer->get_render_pass());

		// Terrain
		this->terrain_renderer = std::make_shared<renderer::terrain::TerrainRenderStage>(
			this->window,
			this->renderer,
			this->camera,
			this->root_dir["assets"]["shaders"],
			this->asset_manager,
			this->time_loop->get_clock());
		this->render_passes.push_back(this->terrain_renderer->get_render_pass());

		// HUD (initialized before world pass so ground selection rings composite beneath units/buildings)
		this->hud_renderer = std::make_shared<renderer::hud::HudRenderStage>(
			this->window,
			this->renderer,
			this->camera,
			this->root_dir["assets"]["shaders"],
			this->asset_manager,
			this->time_loop->get_clock());
		this->render_passes.push_back(this->hud_renderer->get_selection_ring_render_pass());

		// Units/buildings
		this->world_renderer = std::make_shared<renderer::world::WorldRenderStage>(
			this->window,
			this->renderer,
			this->camera,
			this->root_dir["assets"]["shaders"],
			this->asset_manager,
			this->time_loop->get_clock());
		this->render_passes.push_back(this->world_renderer->get_render_pass());

		// Drag selection box and health bar overlays (composited above units/buildings, under GUI)
		this->render_passes.push_back(this->hud_renderer->get_render_pass());
		this->render_passes.push_back(this->hud_renderer->get_selection_render_pass());
	}

	{
		util::StartupTraceScope trace{"presenter.gui"};
		this->init_gui();
	}
	{
		util::StartupTraceScope trace{"presenter.final_pass"};
		this->init_final_render_pass();
		ENSURE(this->screen_renderer != nullptr, "Presenter::init_graphics: failed to create ScreenRenderStage.");
	}

	if (this->simulation) {
		util::StartupTraceScope trace{"presenter.attach_renderer"};
		auto render_factory = std::make_shared<renderer::RenderFactory>(this->terrain_renderer, this->world_renderer);
		this->simulation->attach_renderer(render_factory);
	}

	log::log(INFO << "Presenter: Graphics subsystems initialized");
}

void Presenter::init_gui() {
	log::log(INFO << "Presenter: Initializing GUI with Qt backend");

	auto runtime = this->simulation ? this->simulation->get_trigger_runtime() : nullptr;
	if (runtime) {
		auto objectives = runtime->mission_state().registered_objectives();
		const bool stockpile = std::find(objectives.begin(), objectives.end(), "mission_02.stockpile") != objectives.end();
		const bool route = std::find(objectives.begin(), objectives.end(), "mission_01.march") != objectives.end();
		if (std::find(objectives.begin(), objectives.end(), "mission_01.defeat_enemy") != objectives.end() or stockpile or runtime->original_scenario()) {
			if (stockpile or route or runtime->original_scenario()) {
				this->camera->set_zoom(1.5f);
			}
			this->setup_selection_viewmodel();
			this->mission_hud = std::make_shared<HudQmlAdapter>();
			this->mission_hud->attach(*this->selection_model, *this->simulation);

			// Center on the placed entities.
			const coord::scene3 origin{0, 0, 0};
			coord::scene3_delta center{0, 0, 0};
			size_t positioned = 0;
			for (const auto &[id, entity] : this->simulation->get_game()->get_state()->get_game_entities()) {
				auto position = entity->try_get_component_as<gamestate::component::Position>(
					gamestate::component::component_t::POSITION);
				auto ownership = entity->try_get_component_as<gamestate::component::Ownership>(gamestate::component::component_t::OWNERSHIP);
                if (position and (not runtime->original_scenario()
                                  or (ownership and ownership->get_owners().get(time::time_t{0}) == 0
                                      and (entity->has_component(gamestate::component::component_t::MOVE)
                                           or entity->has_component(gamestate::component::component_t::BUILD))))) {
					center = center + (position->get_positions().get(time::time_t{0}).to_scene3() - origin);
					++positioned;
				}
			}
			if (positioned > 0) {
				this->camera->look_at_coord(origin + center * (1.0 / positioned));
			}
		}
		else {
			const auto &state = this->simulation->get_game()->get_state();
			if (state->get_game_entities().empty() and state->get_map()) {
				const auto &size = state->get_map()->get_size();
				this->camera->look_at_coord(coord::scene3{size[0] / 2.0, size[1] / 2.0, 0});
			}
		}
	}

	util::Path qml_assets = this->root_dir / "assets";
	util::Path qml_root = this->mission_hud ? qml_assets / "qml" : qml_assets / "test" / "qml";
	log::log(INFO << "Presenter: Setting QML root to " << qml_root.resolve_native_path());
	if (not qml_root.is_dir()) {
		throw Error{ERR << "could not find qml root folder " << qml_root};
	}

	log::log(INFO << "Presenter: Setting QML asset path to " << qml_assets.resolve_native_path());
	if (not qml_assets.is_dir()) {
		throw Error{ERR << "could not find asset root folder " << qml_assets};
	}

	util::Path qml_root_file = qml_root / (this->mission_hud ? "MissionHud.qml" : "main.qml");
	log::log(INFO << "Presenter: Setting QML root file to " << qml_root_file.resolve_native_path());
	if (not qml_root_file.is_file()) {
		throw Error{ERR << "could not find QML entry file " << qml_root_file};
	}

	// TODO: in order to support qml-mods, the fslike and filelike
	//       library has to be integrated into qt. For now,
	//       figure out the absolute paths here and pass them in.

	this->gui = std::make_shared<renderer::gui::GUI>(
		this->gui_app, // Qt application wrapper
		this->window,  // host QQuickWindow for the gui
		qml_root_file, // entry qml file, absolute path.
		qml_root,      // directory to watch for qml file changes
		"missionHud",
		this->mission_hud.get()
	);
}

void Presenter::init_input() {
	util::StartupTraceScope trace{"presenter.init_input"};
	log::log(INFO << "Presenter: Initializing input subsystem...");
	Is_True(this->window != nullptr, ("Presenter::init_input: window is null"));

	this->input_manager = std::make_shared<input::InputManager>();

	this->window->add_key_callback([&](const QKeyEvent &ev) {
		if (this->mission_hud) {
			if (this->mission_hud->chat_open()) {
				if (ev.type() == QEvent::KeyPress) {
					if (ev.key() == Qt::Key_Escape) {
						this->mission_hud->closeChat();
						return;
					}
					if ((ev.modifiers() & (Qt::ControlModifier | Qt::MetaModifier)) != 0
					    and ev.key() == Qt::Key_V) {
						this->mission_hud->pasteToChat();
						return;
					}
				}
				return;
			}
			if (this->mission_hud->menu_open()) {
				if (ev.type() == QEvent::KeyPress and ev.key() == Qt::Key_Escape) {
					this->mission_hud->closeMenu();
					return;
				}
				return;
			}
			if (ev.type() == QEvent::KeyPress and ev.key() == Qt::Key_Escape) {
				if (this->pending_repair) {
					this->pending_repair.reset();
				}
				else if (this->pending_build) {
					this->pending_build.reset();
				}
				else if (this->mission_hud->backCommands()) {
					return;
				}
				else {
					if (this->camera_manager) {
						this->camera_manager->set_move_motion_dirs(0);
					}
					this->mission_hud->toggleMenu();
				}
				return;
			}
			if (ev.type() == QEvent::KeyPress
			    and (ev.key() == Qt::Key_Return or ev.key() == Qt::Key_Enter)) {
				if (this->camera_manager) {
					this->camera_manager->set_move_motion_dirs(0);
				}
				this->mission_hud->openChat();
				return;
			}
		}
		if (ev.type() == QEvent::KeyPress and this->handle_gameplay_hotkey(ev)) {
			return;
		}
		this->input_manager->process(ev);
	});
	this->window->add_mouse_button_callback([this](const QMouseEvent &ev) {
		const auto point = ev.position();
		const auto size = this->window->get_size();
		const bool over_hud = this->mission_hud
		                   and this->mission_hud->is_interactive(point.x(), point.y(), size[0], size[1]);
		if (ev.button() == Qt::LeftButton and ev.type() == QEvent::MouseButtonRelease
		    and std::exchange(this->placement_pressed, false)) {
			return;
		}
		if (this->pending_repair and not over_hud and ev.type() == QEvent::MouseButtonPress) {
			if (ev.button() == Qt::RightButton) {
				this->pending_repair.reset();
				return;
			}
			if (ev.button() == Qt::LeftButton) {
				const coord::input mouse{static_cast<int>(point.x()), static_cast<int>(point.y())};
				const auto state = this->simulation->get_game()->get_state();
				const auto now = this->time_loop->get_clock()->get_time();
				const auto viewer = this->game_controller->get_controlled();
				const auto selected = this->game_controller->get_selected();
				const auto target = input::game::command_target(state, mouse.to_phys3(this->camera),
				                                                 this->world_renderer->pick(mouse.x, mouse.y), viewer, now);
				if (target and std::ranges::find(selected, *this->pending_repair) != selected.end()) {
					for (auto actor : selected) {
						this->simulation->get_event_loop()->create_event("game.economy_action", this->simulation->get_commander(), state, now, {
							{"actor", actor}, {"issuer", gamestate::player_id_t{viewer}}, {"repair_target", *target},
						});
					}
					this->pending_repair.reset();
				}
				this->placement_pressed = true;
				return;
			}
		}
		if (this->pending_build and not over_hud and ev.type() == QEvent::MouseButtonPress) {
			if (ev.button() == Qt::RightButton) {
				this->pending_build.reset();
				return;
			}
			if (ev.button() == Qt::LeftButton) {
				const auto [actor, option] = *this->pending_build;
				const auto selected = this->game_controller->get_selected();
				if (std::ranges::find(selected, actor) != selected.end()) {
					const coord::input mouse{static_cast<int>(point.x()), static_cast<int>(point.y())};
					this->send_economy_action(actor, option, mouse.to_phys3(this->camera));
				}
				this->pending_build.reset();
				this->placement_pressed = true;
				return;
			}
		}
		const bool active_drag = ev.button() == Qt::LeftButton
		                       and ev.type() == QEvent::MouseButtonRelease
		                       and this->game_controller
		                       and this->game_controller->has_drag_select_start();
		if (over_hud and not active_drag) {
			return;
		}
		this->input_manager->set_mouse(point.x(), point.y());
		this->input_manager->process(ev);
	});
	this->window->add_mouse_move_callback([this](const QMouseEvent &ev) {
		const auto point = ev.position();
		const auto size = this->window->get_size();
		const bool over_hud = this->mission_hud
		                   and this->mission_hud->is_interactive(point.x(), point.y(), size[0], size[1]);
		const coord::input mouse{static_cast<int>(point.x()), static_cast<int>(point.y())};
		bool attacking = false;
		if (not over_hud and ev.buttons() == Qt::NoButton and this->game_controller
		    and this->simulation and this->simulation->get_game()
		    and this->world_renderer and this->time_loop and this->camera) {
			const auto state = this->simulation->get_game()->get_state();
			Is_True(state != nullptr, ("Presenter::mouse_move: game state is null"));
			attacking = input::game::is_attack_target(state, mouse.to_phys3(this->camera),
			                                        this->world_renderer->pick(mouse.x, mouse.y),
			                                        this->game_controller->get_controlled(),
			                                        this->time_loop->get_clock()->get_time());
		}
		if (attacking != this->attack_pointer) {
			this->attack_pointer = attacking;
			static const auto sword = attack_cursor();
			this->window->get_qt_window()->setCursor(attacking ? sword : QCursor(Qt::ArrowCursor));
		}
		if (this->mission_hud and this->mission_hud->menu_open()) {
			if (this->camera_manager) {
				this->camera_manager->set_move_motion_dirs(0);
			}
			return;
		}
		this->input_manager->set_mouse(mouse.x, mouse.y);
		this->input_manager->process(ev);
	});
	this->window->add_mouse_wheel_callback([this](const QWheelEvent &ev) {
		const auto size = this->window->get_size();
		if (this->mission_hud and this->mission_hud->is_interactive(ev.position().x(), ev.position().y(), size[0], size[1])) {
			return;
		}
		this->input_manager->process(ev);
	});
	this->window->add_focus_lost_callback([this](bool deactivated) {
		if (this->camera_manager) {
			this->camera_manager->set_move_motion_dirs(0);
		}
		if (deactivated) {
			this->placement_pressed = false;
			if (this->hud_renderer) {
				this->hud_renderer->remove_drag_entity();
			}
			if (this->game_controller) {
				this->game_controller->reset_drag_select();
			}
		}
	});

	auto input_ctx = this->input_manager->get_global_context();
	input::setup_defaults(input_ctx);

	// setup simulation controls
	if (this->simulation) {
		log::log(INFO << "Loading game simulation controls");

		// Build the controlled-factions set from the players that Game
		// actually created (one entry per PlayerDescriptor, in order).
		// Defaults to {0} when the simulation has no game yet (very
		// early presenter init race) so the controller is never empty.
		std::unordered_set<size_t> controlled_factions{0};
		if (auto game = this->simulation->get_game()) {
			if (auto state = game->get_state()) {
				controlled_factions.clear();
				for (const auto &kv : state->get_players()) {
					controlled_factions.insert(static_cast<size_t>(kv.first));
				}
				if (controlled_factions.empty()) {
					controlled_factions.insert(0);
				}
			}
		}
		auto game_controller = std::make_shared<input::game::Controller>(
			controlled_factions, 0);
		this->game_controller = game_controller;
		auto engine_context = std::make_shared<input::game::BindingContext>();
		input::game::setup_defaults(engine_context, this->time_loop, this->simulation, this->camera,
		                           [this](coord::input mouse) {
			// The world framebuffer uses logical window dimensions.
			return this->world_renderer->pick(mouse.x, mouse.y);
		}, this->mission_hud ? this->mission_hud->command_bar_height() : 0);
		this->input_manager->set_game_controller(game_controller);
		input_ctx->set_game_bindings(engine_context);

		// Selection viewmodel: mirror the Controller's selection
		// into SelectionModel (+SelectionOverlay) for the HUD and
		// the overlay renderer.
		this->setup_selection_viewmodel();
		if (this->mission_hud) {
			this->mission_hud->set_saves_dir(gamestate::GameSaveManager::directory(this->root_dir));
			QObject::connect(this->mission_hud.get(), &HudQmlAdapter::game_loaded, [this] {
				this->pending_build.reset();
				this->pending_repair.reset();
				this->game_controller->set_selected({});
			});
			QObject::connect(this->mission_hud.get(), &HudQmlAdapter::quit_requested, [this] {
				if (this->simulation) {
					this->simulation->stop();
				}
				if (this->window) {
					this->window->close();
				}
			});
			QObject::connect(this->mission_hud.get(), &HudQmlAdapter::action_requested,
			                 [this](const QString &option, bool build) {
				const auto selected = this->game_controller->get_selected();
				if (not selected.empty()) {
					this->pending_repair.reset();
					if (option == QStringLiteral("command.repair")) {
						this->pending_build.reset();
						this->pending_repair = selected.front();
						return;
					}
					if (build) {
						bool available = true;
						bool affordable = true;
						for (const auto &item : this->mission_hud->action_options()) {
							const auto map = item.toMap();
							if (map.value(QStringLiteral("id")).toString() == option) {
								available = map.value(QStringLiteral("available"), true).toBool();
								affordable = map.value(QStringLiteral("affordable"), true).toBool();
								break;
							}
						}
						if (not available) {
							this->mission_hud->show_toast(QObject::tr("Prerequisites are missing."));
							return;
						}
						if (not affordable) {
							this->mission_hud->show_toast(QObject::tr("Not enough resources."));
							return;
						}
						this->pending_build = {selected.front(), option.toStdString()};
					}
					else if (selected.size() == 1) {
						this->pending_build.reset();
						const bool shift_held = (QGuiApplication::keyboardModifiers() & Qt::ShiftModifier) != 0;
						bool is_train = false;
						for (const auto &item : this->mission_hud->action_options()) {
							const auto map = item.toMap();
							if (map.value(QStringLiteral("id")).toString() == option) {
								is_train = not map.value(QStringLiteral("research")).toBool()
								       and not map.value(QStringLiteral("transform")).toBool();
								break;
							}
						}
						const int count = (shift_held and is_train) ? 5 : 1;
						for (int i = 0; i < count; ++i) {
							this->send_economy_action(selected.front(), option.toStdString());
						}
					}
				}
			});
			QObject::connect(this->mission_hud.get(), &HudQmlAdapter::cancel_queue_requested, [this](int index) {
				const auto selected = this->game_controller->get_selected();
				if (selected.size() == 1 and index >= 0) {
					this->simulation->get_event_loop()->create_event(
						"game.economy_action", this->simulation->get_commander(),
						this->simulation->get_game()->get_state(),
						this->time_loop->get_clock()->get_time(),
						{
							{"actor", selected.front()},
							{"issuer", gamestate::player_id_t{this->game_controller->get_controlled()}},
							{"cancel_index", static_cast<int64_t>(index)},
						});
				}
			});
			QObject::connect(this->mission_hud.get(), &HudQmlAdapter::card_requested, [this](int index) {
				const auto cards = this->mission_hud->selection_cards();
				if (index >= 0 and index < cards.size()) {
					auto id = cards[index].toMap().value(QStringLiteral("id")).toULongLong();
					const auto state = this->simulation->get_game()->get_state();
					if (state->has_game_entity(id) and state->can_see(this->game_controller->get_controlled(), id)) {
						this->game_controller->set_selected({id});
					}
				}
			});
			QObject::connect(this->mission_hud.get(), &HudQmlAdapter::clear_requested, [this] {
				this->pending_repair.reset();
				this->pending_build.reset();
				this->game_controller->set_selected({});
			});
			QObject::connect(this->mission_hud.get(), &HudQmlAdapter::idle_villager_requested, [this] {
				this->jump_to_next_matching("idle_villager", [](gamestate::GameEntity &e, time::time_t t) {
					if (e.definition.find("villager") == std::string::npos) {
						return false;
					}
					if (e.construction_order != 0 or e.heal_order != 0) {
						return false;
					}
					auto cmdq = e.try_get_component_as<gamestate::component::CommandQueue>(
						gamestate::component::component_t::COMMANDQUEUE);
					return not cmdq or cmdq->get_queue().empty(t);
				});
			});
			QObject::connect(this->mission_hud.get(), &HudQmlAdapter::stop_requested, [this] {
				this->pending_repair.reset();
				this->pending_build.reset();
				event::EventHandler::param_map::map_t params{
					{"type", gamestate::component::command::command_t::IDLE},
					{"issuer", gamestate::player_id_t{this->game_controller->get_controlled()}},
					{"entity_ids", this->game_controller->get_selected()},
				};
				this->simulation->get_event_loop()->create_event(
					"game.send_command", this->simulation->get_commander(), this->simulation->get_game()->get_state(),
					this->time_loop->get_clock()->get_time(), params);
			});
			QObject::connect(this->mission_hud.get(), &HudQmlAdapter::camera_requested, [this](qreal x, qreal y) {
				if (x >= 0 and y >= 0 and x <= 1 and y <= 1) {
					const auto &size = this->simulation->get_game()->get_state()->get_map()->get_size();
					this->camera->look_at_coord(coord::scene3{x * size[0], y * size[1], 0});
				}
			});
			QObject::connect(this->mission_hud.get(), &HudQmlAdapter::diplomacy_requested, [this](int player, int stance) {
				if (player < 0 or stance < 1 or stance > 3) {
					return;
				}
				event::EventHandler::param_map::map_t params{
					{"source", static_cast<gamestate::player_id_t>(this->game_controller->get_controlled())},
					{"target", static_cast<gamestate::player_id_t>(player)},
					{"stance", static_cast<gamestate::DiplomaticStance>(stance)},
				};
				this->simulation->get_event_loop()->create_event(
					"game.diplomacy", this->simulation->get_commander(), this->simulation->get_game()->get_state(),
					this->time_loop->get_clock()->get_time(), params);
			});
		}
	}

	// setup camera controls
	if (this->camera) {
		log::log(INFO << "Loading camera controls");
		auto camera_controller = std::make_shared<input::camera::Controller>();
		auto camera_context = std::make_shared<input::camera::BindingContext>();
		input::camera::setup_defaults(camera_context, this->camera, this->camera_manager);
		this->input_manager->set_camera_controller(camera_controller);
		input_ctx->set_camera_bindings(camera_context);
	}

	// setup HUD controls
	if (this->hud_renderer) {
		log::log(INFO << "Loading HUD controls");
		auto hud_controller = std::make_shared<input::hud::Controller>();
		auto hud_context = std::make_shared<input::hud::BindingContext>();
		input::hud::setup_defaults(hud_context, this->hud_renderer);
		this->input_manager->set_hud_controller(hud_controller);
		input_ctx->set_hud_bindings(hud_context);
	}

	log::log(INFO << "Presenter: Input subsystem initialized");

	// TODO: DEBUG-ONLY auto-spawn. Synthesizes Ctrl+Alt+LeftClick events to trigger
	// the "game.spawn_entity" binding (see input/controller/game/controller.cpp),
	// which spawns trial_base test entities (EagleWarrior, JaguarWarrior,
	// PlumedArcher, JungleTree, Barracks) in sequence. This lets us verify that
	// buildings/units render without needing a real mouse. Remove before merge.
	//
	// Gated behind the OPENAGE_DEBUG_AUTOSPAWN env var so it is inert by
	// default and only runs when explicitly requested. The per-click coordinate
	// logging is at DBG level (use -v / VERBOSE to see it); a single INFO line
	// announces that auto-spawn is enabled so a normal run is not noisy.
	const char *autospawn_env = std::getenv("OPENAGE_DEBUG_AUTOSPAWN");
	if (autospawn_env != nullptr and autospawn_env[0] != '\0' and autospawn_env[0] != '0') {
		log::log(MSG(info) << "DEBUG: OPENAGE_DEBUG_AUTOSPAWN set; auto-spawning entities "
		                       "via synthesized Ctrl+Alt+LMB in 2s");
		auto win_size = this->window->get_size();
		QTimer::singleShot(2000, [this, win_size]() {
			// NOTE: the spawn binding uses InputManager::mouse_position (updated by
			// MouseMove events), NOT the click's own coordinates. So we must move the
			// recorded mouse position before each click, otherwise every entity
			// spawns at (0,0) and stacks on top of each other.
			std::vector<QPoint> points = {
				{int(win_size[0]) / 3, int(win_size[1]) / 3},
				{int(win_size[0]) * 2 / 3, int(win_size[1]) / 3},
				{int(win_size[0]) / 2, int(win_size[1]) / 2},
				{int(win_size[0]) / 3, int(win_size[1]) * 2 / 3},
				{int(win_size[0]) * 2 / 3, int(win_size[1]) * 2 / 3},
			};
			for (const auto &pt : points) {
				// Move the recorded mouse position (used by the spawn binding's
				// to_phys3) without dispatching a MouseMove event - dispatching one
				// changes the input context stack and the subsequent click no longer
				// reaches the game controller.
				this->input_manager->set_mouse(pt.x(), pt.y());
				log::log(DBG << "DEBUG: Ctrl+Alt+LMB at window (" << pt.x() << "," << pt.y() << ")");
				QMouseEvent press{QEvent::MouseButtonRelease, QPointF(pt), QPointF(pt), Qt::LeftButton,
				                  Qt::LeftButton, Qt::ControlModifier | Qt::AltModifier};
				this->input_manager->process(press);
			}
			// report how many entities actually exist after spawning
			if (this->simulation) {
				auto gstate = this->simulation->get_game()->get_state();
				log::log(MSG(info) << "DEBUG: game now has "
				                   << gstate->get_game_entities().size()
				                   << " game entities");
			}
			log::log(MSG(info) << "DEBUG: auto-spawn done");
		});
	}
}

void Presenter::setup_selection_viewmodel() {
	if (not this->simulation) {
		return;
	}
	// Create the model + overlay exactly once. The model
	// registers a removal observer on the simulation in its
	// constructor, so it tracks entity death for as long as it
	// lives (presenter-owned).
	if (not this->selection_model) {
		this->selection_model = std::make_shared<viewmodel::SelectionModel>(this->simulation);
	}
	// Attach the Controller -> SelectionModel mirror. The
	// Controller is the master; the model is the presentation
	// view. Safe to call repeatedly: the callback just overwrites.
	if (this->game_controller and this->selection_model) {
		auto model = this->selection_model;
		this->game_controller->set_selection_callback(
			[this, model](const std::vector<gamestate::entity_id_t> &ids) {
				model->set_multi_selection(ids);
				const auto &runtime = this->simulation->get_trigger_runtime();
				if (runtime and runtime->original_scenario() and this->time_loop) {
					this->simulation->get_event_loop()->create_event(
						"scenario.original_selection", this->simulation->get_commander(),
						this->simulation->get_game()->get_state(),
						this->time_loop->get_clock()->get_time(), {{"selection", ids}});
				}
			});
	}
}

void Presenter::init_final_render_pass() {
	// Final output to window
	this->screen_renderer = std::make_shared<renderer::screen::ScreenRenderStage>(
		this->window,
		this->renderer,
		this->root_dir["assets"]["shaders"]);
	std::vector<std::shared_ptr<renderer::RenderTarget>> targets{};
	for (auto pass : this->render_passes) {
		targets.push_back(pass->get_target());
	}
	this->screen_renderer->set_render_targets(targets);
	this->render_passes.push_back(this->screen_renderer->get_render_pass());

	// Update final render pass if the textures are reassigned on resize
	// TODO: This REQUIRES that all other render passes have already been
	//       resized
	this->window->add_resize_callback([this](size_t, size_t, double /*scale*/) {
		// Acquire the render targets for all previous passes
		std::vector<std::shared_ptr<renderer::RenderTarget>> targets{};
		for (size_t i = 0; i < this->render_passes.size() - 1; ++i) {
			targets.push_back(this->render_passes[i]->get_target());
		}
		this->screen_renderer->set_render_targets(targets);
	});

	this->window->set_render_callback([this]() {
		this->render();
		this->renderer->check_error();
		this->frame_rendered = true;
		if (not this->first_frame_logged) {
			this->first_frame_logged = true;
			util::log_startup_milestone("presenter.first_frame", this->startup_origin_ns);
		}
	});

	this->window->set_post_render_callback([this]() {
		// Captures the full framebuffer (OpenGL underlay + QML overlay) once at
		// frame 60 when OPENAGE_DEBUG_SCREENSHOT is set.
		if (const char *shot_env = std::getenv("OPENAGE_DEBUG_SCREENSHOT")) {
			if (shot_env[0] != '\0') {
				static int m1_frame = 0;
				if (++m1_frame == 60) {
					std::string shot{shot_env};
					auto tex = this->render_passes.back()->get_target()->into_data().flip_y();
					auto size = tex.get_info().get_size();
					QImage qimg{tex.get_data(), size.first, size.second, QImage::Format_RGBA8888};
					qimg.save(shot.c_str());
					log::log(MSG(info) << "M1: screenshot saved to " << shot);
				}
			}
		}
	});
}

void Presenter::send_economy_action(gamestate::entity_id_t actor, const nyan::fqon_t &option,
                                    std::optional<coord::phys3> position) {
	event::EventHandler::param_map::map_t params{
		{"actor", actor}, {"option", option},
		{"issuer", gamestate::player_id_t{this->game_controller->get_controlled()}},
	};
	if (position) {
		params.emplace("position", *position);
		params.emplace("builders", this->game_controller->get_selected());
	}
	this->simulation->get_event_loop()->create_event(
		"game.economy_action", this->simulation->get_commander(), this->simulation->get_game()->get_state(),
		this->time_loop->get_clock()->get_time(), params);
}

void Presenter::update_campaign_map() {
	if (not this->simulation or not this->simulation->get_game() or not this->game_controller) {
		return;
	}
	const auto &state = this->simulation->get_game()->get_state();
	Is_True(state != nullptr, ("Presenter::update_campaign_map: game state is null"));
	if (not this->terrain_renderer or not this->world_renderer or not this->time_loop
	    or not this->window or not this->camera or not state->get_map() or not state->get_map()->get_terrain()) {
		return;
	}
	const auto viewer = this->game_controller->get_controlled();
	const auto view = state->visibility(viewer);
	if (view) {
		auto selected = this->game_controller->get_selected();
		if (std::erase_if(selected, [&](auto id) { return not state->can_see(viewer, id); }) > 0) {
			this->game_controller->set_selected(std::move(selected));
		}
	}
	this->terrain_renderer->set_visibility(view);
	this->world_renderer->set_visibility(view);
	if (not this->mission_hud) {
		return;
	}
	const auto now = this->time_loop->get_clock()->get_real_time();
	if (now < this->next_minimap_update) {
		return;
	}
	this->next_minimap_update = now + time::time_t::from_double(0.15);
	this->mission_hud->refresh_economy(viewer);
	const auto sim_now = this->time_loop->get_clock()->get_time();
	int idle_villager_count = 0;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (not entity or entity->is_dead() or entity->definition.find("villager") == std::string::npos) {
			continue;
		}
		auto ownership = entity->try_get_component_as<gamestate::component::Ownership>(
			gamestate::component::component_t::OWNERSHIP);
		if (not ownership or ownership->get_owners().get(sim_now) != viewer) {
			continue;
		}
		if (entity->construction_order != 0 or entity->heal_order != 0) {
			continue;
		}
		auto cmdq = entity->try_get_component_as<gamestate::component::CommandQueue>(
			gamestate::component::component_t::COMMANDQUEUE);
		if (not cmdq or cmdq->get_queue().empty(sim_now)) {
			++idle_villager_count;
		}
	}
	if (idle_villager_count != this->hud_cache->last_idle_villagers) {
		this->hud_cache->last_idle_villagers = idle_villager_count;
		this->mission_hud->set_idle_villagers(idle_villager_count);
	}
	const auto feedback_event = state->action_feedback_event(viewer);
	const QString action_status = this->pending_repair
	                                ? QObject::tr("Left-click a damaged building to repair. Right-click to cancel.")
	                                : this->pending_build
	                                ? QObject::tr("Left-click to place the building. Right-click to cancel.")
	                                : QString::fromStdString(feedback_event.message);
	if (action_status != this->hud_cache->last_action_status) {
		this->hud_cache->last_action_status = action_status;
		this->mission_hud->set_action_status(action_status);
	}
	if (feedback_event.seq != 0 and feedback_event.seq != this->hud_cache->last_feedback_seq) {
		this->hud_cache->last_feedback_seq = feedback_event.seq;
		if (feedback_event.is_error and not feedback_event.message.empty()) {
			this->mission_hud->show_toast(QString::fromStdString(feedback_event.message));
		}
	}

	QVariantList relations;
	std::vector<gamestate::player_id_t> players;
	for (const auto &[id, player] : state->get_players()) {
		if (id != viewer) {
			players.push_back(id);
		}
	}
	std::ranges::sort(players);
	for (auto id : players) {
		relations.append(QVariantMap{
			{"id", static_cast<int>(id)},
			{"name", QString::fromStdString(state->get_player_name(id))},
			{"stance", static_cast<int>(state->get_stance(viewer, id))},
		});
	}
	if (not this->hud_cache->has_diplomacy_snapshot or relations != this->hud_cache->last_diplomacy) {
		this->hud_cache->last_diplomacy = relations;
		this->hud_cache->has_diplomacy_snapshot = true;
		this->mission_hud->set_diplomacy(std::move(relations));
	}
	if (not view) {
		return;
	}

	if (this->hud_cache->minimap_width != view->width or this->hud_cache->minimap_height != view->height) {
		this->hud_cache->minimap_width = view->width;
		this->hud_cache->minimap_height = view->height;
		const size_t total_tiles = view->width * view->height;
		const QColor water_color("#356f8f");
		const QColor road_color("#aa946a");
		const QColor grass_color("#6b894d");
		this->hud_cache->minimap_visible_colors.assign(total_tiles, grass_color.rgb());
		this->hud_cache->minimap_explored_colors.assign(total_tiles, grass_color.darker(240).rgb());
		for (const auto &chunk : state->get_map()->get_terrain()->get_chunks()) {
			const auto offset = chunk->get_offset();
			const auto size = chunk->get_size();
			const auto &tiles = chunk->get_tiles();
			for (size_t local_y = 0; local_y < size[1]; ++local_y) {
				for (size_t local_x = 0; local_x < size[0]; ++local_x) {
					const auto x = offset.ne + local_x;
					const auto y = offset.se + local_y;
					if (x >= view->width or y >= view->height) {
						continue;
					}
					const auto &name = tiles[local_y * size[0] + local_x].terrain.get_name();
					const QColor color = name.find("water") != std::string::npos ? water_color
					                   : name.find("road") != std::string::npos  ? road_color
					                                                             : grass_color;
					const size_t idx = y * view->width + x;
					this->hud_cache->minimap_visible_colors[idx] = color.rgb();
					this->hud_cache->minimap_explored_colors[idx] = color.darker(240).rgb();
				}
			}
		}
	}

	QImage terrain(view->width, view->height, QImage::Format_RGB32);
	const QRgb unexplored_rgb = QColor("#0d141b").rgb();
	for (size_t y = 0; y < view->height; ++y) {
		auto *row = reinterpret_cast<QRgb *>(terrain.scanLine(static_cast<int>(y)));
		const size_t row_offset = y * view->width;
		for (size_t x = 0; x < view->width; ++x) {
			const auto vis = view->tiles[row_offset + x];
			if (vis == gamestate::TileVisibility::Unexplored) {
				row[x] = unexplored_rgb;
			}
			else if (vis == gamestate::TileVisibility::Explored) {
				row[x] = this->hud_cache->minimap_explored_colors[row_offset + x];
			}
			else {
				row[x] = this->hud_cache->minimap_visible_colors[row_offset + x];
			}
		}
	}

	constexpr int map_size = 158;
	auto image = terrain.scaled(map_size, map_size, Qt::IgnoreAspectRatio, Qt::FastTransformation);
	{
		QPainter painter(&image);
		if (this->mission_hud->map_units()) {
			for (const auto &marker : view->entities) {
				const auto stance = state->get_stance(viewer, marker.owner);
				const QColor color = stance == gamestate::DiplomaticStance::Self ? QColor("#79b9ff")
				                   : stance == gamestate::DiplomaticStance::Ally ? QColor("#a3e08b")
				                   : stance == gamestate::DiplomaticStance::Enemy ? QColor("#ff6c59") : QColor("#ebcf80");
				painter.fillRect(static_cast<int>((std::floor(marker.x) + 0.5) * map_size / view->width) - 2,
				                 static_cast<int>((std::floor(marker.y) + 0.5) * map_size / view->height) - 2, 4, 4, color);
			}
		}
		const auto &size = this->window->get_size();
		QPolygonF camera_frame;
		for (const auto &point : {coord::input{0, 0}, coord::input{static_cast<int>(size[0]), 0},
		                          coord::input{static_cast<int>(size[0]), static_cast<int>(size[1]) - this->mission_hud->command_bar_height()},
		                          coord::input{0, static_cast<int>(size[1]) - this->mission_hud->command_bar_height()}}) {
			const auto world = point.to_phys3(this->camera);
			camera_frame.append(QPointF(world.ne.to_double() * map_size / view->width,
			                            world.se.to_double() * map_size / view->height));
		}
		painter.setPen(QColor("#f4ead2"));
		if (this->mission_hud->map_viewport()) {
			painter.drawPolygon(camera_frame);
		}
	}
	this->mission_hud->set_minimap(std::move(image));
}

void Presenter::render() {
	Is_True(this->hud_renderer != nullptr, ("Presenter::render: hud_renderer is null"));
	Is_True(this->gui != nullptr, ("Presenter::render: gui is null"));
	if (this->simulation and this->camera) {
		const auto &runtime = this->simulation->get_trigger_runtime();
		if (runtime) {
			if (auto original = runtime->original_scenario()) {
				if (auto point = original->take_camera()) {
					this->camera->look_at_coord(*point);
				}
			}
		}
	}
	if (this->simulation and this->simulation->get_game() and this->camera_manager) {
		const auto state = this->simulation->get_game()->get_state();
		if (state and state->get_map()) {
			const auto map_size = state->get_map()->get_size();
			if (map_size[0] > 0 and map_size[1] > 0 and map_size != this->camera_map_size) {
				// Default camera height is 10.0 -> horizontal look-at offset is 10 * sqrt(3) / sqrt(2).
				constexpr float side_length = 12.2474487f;
				this->camera_manager->set_camera_boundaries(
					renderer::camera::CameraBoundaries{
						side_length,
						side_length + static_cast<float>(map_size[0]),
						renderer::camera::Y_BOUND_MIN,
						renderer::camera::Y_BOUND_MAX,
						side_length - static_cast<float>(map_size[1]),
						side_length});
				this->camera_map_size = map_size;
			}
		}
	}
	// TODO: Pass current time to update() instead of fetching it in renderer
	this->camera_manager->update();
	this->update_campaign_map();
	this->terrain_renderer->update();
	this->world_renderer->update();

	std::vector<renderer::hud::SelectionOverlayQuad> quads;
	if (this->game_controller and this->simulation and this->simulation->get_game()
	    and this->time_loop and this->world_renderer) {
		const auto state = this->simulation->get_game()->get_state();
		Is_True(state != nullptr, ("Presenter::render: game state is null"));
		const auto simulation_time = this->time_loop->get_clock()->get_time();
		const auto render_time = this->time_loop->get_clock()->get_real_time();
		const auto player = this->game_controller->get_controlled();
		const auto selected = this->game_controller->get_selected();
		auto icon_url = [this](const nyan::fqon_t &subject) -> const QString & {
			if (const auto it = this->hud_cache->icon_urls.find(subject); it != this->hud_cache->icon_urls.end()) {
				return it->second;
			}
			const auto path = this->root_dir / "assets" / "converted" / "ww_mvp" / "campaign"
			                  / "william_wallace" / "hud" / (subject + ".png");
			QString resolved = path.is_file()
			                       ? QUrl::fromLocalFile(QString::fromStdString(path.resolve_native_path())).toString()
			                       : QString{};
			return this->hud_cache->icon_urls.emplace(subject, std::move(resolved)).first->second;
		};
		QVariantList cards;
		QVariantList actions;
		QVariantList production_queue;
		bool can_stop = false;
		int construction = -1;
		const auto economy_handler = this->simulation->get_economy_handler();
		const bool builders_selected = not selected.empty() and std::ranges::all_of(selected, [&](gamestate::entity_id_t id) {
			if (not state->has_game_entity(id)) {
				return false;
			}
			const auto entity = state->get_game_entity(id);
			const auto ownership = entity->try_get_component_as<gamestate::component::Ownership>(gamestate::component::component_t::OWNERSHIP);
			return not entity->is_dead() and entity->construct_ability and ownership
			    and ownership->get_owners().get(simulation_time) == player;
		});
		for (auto id : selected) {
			if (not state->has_game_entity(id) or not state->can_see(player, id)) {
				continue;
			}
			const auto entity = state->get_game_entity(id);
			const auto position = entity->try_get_component_as<gamestate::component::Position>(
				gamestate::component::component_t::POSITION);
			if (not position or entity->is_dead()) {
				continue;
			}
			const auto ownership = entity->try_get_component_as<gamestate::component::Ownership>(
				gamestate::component::component_t::OWNERSHIP);
			const auto owner = ownership ? ownership->get_owners().get(simulation_time) : player;
			const auto stance = ownership ? state->get_stance(player, owner) : gamestate::DiplomaticStance::Neutral;
			const QColor color = stance == gamestate::DiplomaticStance::Enemy ? QColor("#f34c46")
			                   : stance == gamestate::DiplomaticStance::Ally ? QColor("#84c779")
			                   : stance == gamestate::DiplomaticStance::Self ? QColor("#629cff")
			                   : QColor("#d6c28c");
			const auto live = entity->try_get_component_as<gamestate::component::Live>(
				gamestate::component::component_t::LIVE);
			const auto hp = live ? live->get_attribute_value(simulation_time, health_attribute).value_or(0) : 0;
			const auto max_hp = live ? live->get_attribute_max(health_attribute).value_or(0) : 0;
			const float fraction = max_hp > 0 ? std::clamp(static_cast<float>(hp) / max_hp, 0.0f, 1.0f) : -1.0f;
			const auto center = position->get_positions().get(render_time).to_scene3().to_world_space();
			const float rx = entity->has_footprint() ? std::max(0.30f, float(entity->footprint().radius_x())) : 0.4f;
			const float ry = entity->has_footprint() ? std::max(0.30f, float(entity->footprint().radius_y())) : 0.4f;
			quads.push_back({center, ry, rx, {float(color.redF()), float(color.greenF()), float(color.blueF()), 1.0f},
			                 fraction, this->world_renderer->get_sprite_top(id)});
			if (not this->mission_hud) {
				continue;
			}
			const auto build = entity->try_get_component_as<gamestate::component::Build>(
				gamestate::component::component_t::BUILD);
			const QString civ_name = owner == 0 ? QStringLiteral("Celts")
			                       : owner == 2 ? QStringLiteral("Gaia")
			                       : QStringLiteral("Britons");
			cards.emplaceBack(QVariantMap{
				{"id", QVariant::fromValue<qulonglong>(id)}, {"name", object_label(entity->definition)},
				{"icon", icon_url(entity->definition)},
				{"owner", QString::fromStdString(state->get_player_name(owner))},
				{"civ", civ_name},
				{"hp", QVariant::fromValue<qlonglong>(hp)}, {"maxHp", QVariant::fromValue<qlonglong>(max_hp)},
				{"color", color}, {"building", bool(build)},
			});
			if (ownership and owner == player) {
				can_stop = can_stop or entity->has_component(gamestate::component::component_t::MOVE)
				                    or entity->has_component(gamestate::component::component_t::ATTACK);
				if (selected.size() == 1 or (builders_selected and id == selected.front())) {
					if (build and build->get_progress() < 100) {
						construction = static_cast<int>(build->get_progress());
					}
					if (not build or not build->is_under_construction()) {
						std::unordered_map<std::string, int> queued_counts;
						if (economy_handler) {
							const auto entries = economy_handler->queue_snapshot(id, simulation_time);
							for (const auto &entry : entries) {
								++queued_counts[entry.option.id];
								const int pct = static_cast<int>(std::round(entry.progress * 100.0));
								production_queue.append(QVariantMap{
									{"name", entry.option.kind == gamestate::ActionKind::Transform
										? (entity->unpacked ? QStringLiteral("Pack") : QStringLiteral("Unpack"))
										: object_label(entry.option.subject)},
									{"icon", icon_url(entry.option.kind == gamestate::ActionKind::Transform
										? (entity->unpacked ? "command.pack" : "command.unpack")
										: entry.option.subject)},
									{"research", entry.option.kind == gamestate::ActionKind::Research},
									{"transform", entry.option.kind == gamestate::ActionKind::Transform},
									{"active", entry.active},
									{"progress", pct},
								});
							}
						}
						const auto owner_player = state->get_player(owner);
						if (entity->repair_ability) {
							actions.append(QVariantMap{{"id", "command.repair"}, {"name", QObject::tr("Repair")},
							                           {"command", true}, {"enabled", true}});
						}
						for (const auto &option : entity->action_options) {
							if (selected.size() > 1 and option.kind != gamestate::ActionKind::Build) {
								continue;
							}
							if (owner_player and not owner_player->allows_action(option.subject)) {
								continue;
							}
							if (option.kind == gamestate::ActionKind::Research
							    and owner_player and owner_player->has_researched(option.subject)) {
								continue;
							}
							const bool research_queued = option.kind == gamestate::ActionKind::Research
							                          and economy_handler
							                          and economy_handler->is_research_queued(owner, option.subject);
							const bool available = not research_queued
							                    and (not owner_player
							                         or gamestate::event::action_available(option, *state, *owner_player, simulation_time));
							bool affordable = true;
							QStringList cost;
							QVariantList cost_items;
							for (const auto &[resource, amount] : option.cost) {
								if (amount > 0) {
									const bool enough = not owner_player or owner_player->has_enough(resource, amount);
									affordable = affordable and enough;
									const QString res_label = object_label(resource);
									cost.append(QStringLiteral("%1 %2").arg(amount).arg(res_label));
									cost_items.append(QVariantMap{
										{"label", res_label},
										{"amount", static_cast<qlonglong>(amount)},
										{"enough", enough},
									});
								}
							}
							const auto qit = queued_counts.find(option.id);
							actions.append(QVariantMap{
								{"id", QString::fromStdString(option.id)},
								{"icon", icon_url(option.kind == gamestate::ActionKind::Transform
								                  ? (entity->unpacked ? "command.pack" : "command.unpack") : option.subject)},
								{"name", option.kind == gamestate::ActionKind::Transform
									? (entity->unpacked ? QStringLiteral("Pack") : QStringLiteral("Unpack"))
									: object_label(option.subject)},
								{"build", option.kind == gamestate::ActionKind::Build},
								{"category", state->command_categories.contains(option.subject)
								                 ? QString::fromStdString(state->command_categories.at(option.subject)) : QString{}},
								{"research", option.kind == gamestate::ActionKind::Research},
								{"transform", option.kind == gamestate::ActionKind::Transform},
								{"detail", cost.join(QStringLiteral(" · "))},
								{"costs", cost_items},
								{"available", available},
								{"affordable", affordable},
								{"queued", research_queued},
								{"queuedCount", qit != queued_counts.end() ? qit->second : 0},
								{"enabled", available and affordable},
							});
						}
					}
				}
			}
		}
		if (this->mission_hud) {
			if (not this->hud_cache->has_selection_snapshot
			    or cards != this->hud_cache->last_cards
			    or actions != this->hud_cache->last_actions
			    or construction != this->hud_cache->last_construction
			    or can_stop != this->hud_cache->last_can_stop) {
				this->hud_cache->last_cards = cards;
				this->hud_cache->last_actions = actions;
				this->hud_cache->last_construction = construction;
				this->hud_cache->last_can_stop = can_stop;
				this->hud_cache->has_selection_snapshot = true;
				this->mission_hud->set_selection_details(std::move(cards), std::move(actions), construction, can_stop,
					{{"stop", icon_url("command.stop")}, {"cancel", icon_url("command.cancel")}});
			}
			if (production_queue != this->hud_cache->last_production_queue) {
				this->hud_cache->last_production_queue = production_queue;
				this->mission_hud->set_production_queue(std::move(production_queue));
			}
		}
	}
	this->hud_renderer->set_selection_overlays(std::move(quads));

	this->hud_renderer->update();

	for (auto &pass : this->render_passes) {
		this->renderer->render(pass);
	}
}


bool Presenter::jump_to_next_matching(
	const std::string &cycle_key,
	const std::function<bool(gamestate::GameEntity &, time::time_t)> &pred) {
	if (not this->simulation or not this->simulation->get_game() or not this->game_controller) {
		return false;
	}
	const auto state = this->simulation->get_game()->get_state();
	if (not state) {
		return false;
	}
	const time::time_t now = this->time_loop ? this->time_loop->get_clock()->get_time() : time::time_t{0};
	const auto controlled = static_cast<gamestate::player_id_t>(this->game_controller->get_controlled());

	std::vector<gamestate::entity_id_t> matches;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (not entity or entity->is_dead()) {
			continue;
		}
		auto ownership = entity->try_get_component_as<gamestate::component::Ownership>(
			gamestate::component::component_t::OWNERSHIP);
		if (not ownership or ownership->get_owners().get(now) != controlled) {
			continue;
		}
		if (pred(*entity, now)) {
			matches.push_back(id);
		}
	}
	if (matches.empty()) {
		if (this->mission_hud) {
			this->mission_hud->set_action_status(QStringLiteral("No matching unit or building found."));
		}
		return true;
	}
	std::ranges::sort(matches);
	auto &idx = this->hotkey_cycle_index_[cycle_key];
	const auto target_id = matches[idx % matches.size()];
	idx = (idx + 1) % matches.size();

	this->game_controller->set_selected({target_id});
	const auto &target = state->get_game_entity(target_id);
	if (this->camera) {
		if (auto pos = target->try_get_component_as<gamestate::component::Position>(
			    gamestate::component::component_t::POSITION)) {
			const auto p = pos->get_positions().get(now);
			this->camera->look_at_coord(coord::scene3{p.ne.to_double(), p.se.to_double(), p.up.to_double()});
		}
	}
	return true;
}


bool Presenter::handle_gameplay_hotkey(const QKeyEvent &ev) {
	if (not this->simulation or not this->simulation->get_game() or not this->game_controller) {
		return false;
	}
	const auto mods = ev.modifiers();
	const bool ctrl = (mods & (Qt::ControlModifier
#ifdef __APPLE__
	                           | Qt::MetaModifier
#endif
	                           )) != 0;
	const bool shift = (mods & Qt::ShiftModifier) != 0;
	const bool alt = (mods & Qt::AltModifier) != 0;
	if (alt) {
		return false;
	}
	const int key = ev.key();
	const auto state = this->simulation->get_game()->get_state();
	const time::time_t now = this->time_loop ? this->time_loop->get_clock()->get_time() : time::time_t{0};

	// 1. Control groups: Ctrl+1..9 assign, Shift+1..9 append, 1..9 recall (and center if already selected)
	if (key >= Qt::Key_1 and key <= Qt::Key_9) {
		const size_t group_idx = static_cast<size_t>(key - Qt::Key_0);
		if (ctrl) {
			this->control_groups_[group_idx] = this->game_controller->get_selected();
			if (this->mission_hud) {
				this->mission_hud->set_action_status(
					QStringLiteral("Assigned group %1 (%2 units).")
						.arg(group_idx)
						.arg(this->control_groups_[group_idx].size()));
			}
			return true;
		}
		std::vector<gamestate::entity_id_t> alive;
		for (auto id : this->control_groups_[group_idx]) {
			if (state and state->has_game_entity(id) and not state->get_game_entity(id)->is_dead()) {
				alive.push_back(id);
			}
		}
		if (shift) {
			auto current = this->game_controller->get_selected();
			for (auto id : alive) {
				if (std::ranges::find(current, id) == current.end()) {
					current.push_back(id);
				}
			}
			this->game_controller->set_selected(current);
			return true;
		}
		const auto prev = this->game_controller->get_selected();
		this->game_controller->set_selected(alive);
		if (not alive.empty() and prev == alive and this->camera) {
			const auto &e = state->get_game_entity(alive.front());
			if (auto pos = e->try_get_component_as<gamestate::component::Position>(
				    gamestate::component::component_t::POSITION)) {
				const auto p = pos->get_positions().get(now);
				this->camera->look_at_coord(coord::scene3{p.ne.to_double(), p.se.to_double(), p.up.to_double()});
			}
		}
		return true;
	}

	// 2. Building jump hotkeys: Ctrl+<Letter> (and bare H for Town Center)
	if (ctrl) {
		static const std::unordered_map<int, std::pair<const char *, const char *>> building_keys{
			{Qt::Key_H, {"town_center", "Town Center"}},
			{Qt::Key_B, {"barracks", "Barracks"}},
			{Qt::Key_A, {"archery_range", "Archery Range"}},
			{Qt::Key_S, {"stable", "Stable"}},
			{Qt::Key_K, {"siege_workshop", "Siege Workshop"}},
			{Qt::Key_V, {"castle", "Castle"}},
			{Qt::Key_M, {"market", "Market"}},
			{Qt::Key_U, {"university", "University"}},
			{Qt::Key_F, {"blacksmith", "Blacksmith"}},
			{Qt::Key_I, {"mill", "Mill"}},
			{Qt::Key_L, {"lumber_camp", "Lumber Camp"}},
			{Qt::Key_G, {"mining_camp", "Mining Camp"}},
			{Qt::Key_Y, {"monastery", "Monastery"}},
			{Qt::Key_D, {"dock", "Dock"}},
		};
		if (const auto it = building_keys.find(key); it != building_keys.end()) {
			const std::string sub = it->second.first;
			return this->jump_to_next_matching("bld_" + sub, [sub](gamestate::GameEntity &e, time::time_t) {
				auto build = e.try_get_component_as<gamestate::component::Build>(
					gamestate::component::component_t::BUILD);
				const bool finished = not build or not build->is_under_construction();
				return finished and e.definition.find(sub) != std::string::npos;
			});
		}
		return false;
	}

	// 3. Global navigation & speed hotkeys
	if (key == Qt::Key_Period) {
		return this->jump_to_next_matching("idle_villager", [](gamestate::GameEntity &e, time::time_t t) {
			if (e.definition.find("villager") == std::string::npos) {
				return false;
			}
			if (e.construction_order != 0 or e.heal_order != 0) {
				return false;
			}
			auto cmdq = e.try_get_component_as<gamestate::component::CommandQueue>(
				gamestate::component::component_t::COMMANDQUEUE);
			return not cmdq or cmdq->get_queue().empty(t);
		});
	}
	if (key == Qt::Key_Comma) {
		return this->jump_to_next_matching("idle_military", [](gamestate::GameEntity &e, time::time_t t) {
			if (e.definition.find("villager") != std::string::npos) {
				return false;
			}
			if (not e.has_component(gamestate::component::component_t::MOVE)
			    or not e.has_component(gamestate::component::component_t::ATTACK)) {
				return false;
			}
			auto cmdq = e.try_get_component_as<gamestate::component::CommandQueue>(
				gamestate::component::component_t::COMMANDQUEUE);
			auto atk = e.try_get_component_as<gamestate::component::Attack>(
				gamestate::component::component_t::ATTACK);
			const bool atk_active = atk and atk->is_active();
			return (not cmdq or cmdq->get_queue().empty(t)) and not atk_active;
		});
	}
	if (key == Qt::Key_H) {
		return this->jump_to_next_matching("bld_town_center", [](gamestate::GameEntity &e, time::time_t) {
			auto build = e.try_get_component_as<gamestate::component::Build>(
				gamestate::component::component_t::BUILD);
			const bool finished = not build or not build->is_under_construction();
			return finished and e.definition.find("town_center") != std::string::npos;
		});
	}
	if (key == Qt::Key_Space and this->camera and state) {
		const auto selected = this->game_controller->get_selected();
		if (not selected.empty() and state->has_game_entity(selected.front())) {
			const auto &e = state->get_game_entity(selected.front());
			if (auto pos = e->try_get_component_as<gamestate::component::Position>(
				    gamestate::component::component_t::POSITION)) {
				const auto p = pos->get_positions().get(now);
				this->camera->look_at_coord(coord::scene3{p.ne.to_double(), p.se.to_double(), p.up.to_double()});
				return true;
			}
		}
		return false;
	}
	if ((key == Qt::Key_Plus or key == Qt::Key_Equal or key == Qt::Key_Minus) and this->time_loop) {
		auto clock = this->time_loop->get_clock();
		const double cur = clock->get_speed().to_double();
		const double next = std::clamp(cur + (key == Qt::Key_Minus ? -0.5 : 0.5), 0.5, 4.0);
		clock->set_speed(time::speed_t::from_double(next));
		if (this->mission_hud) {
			this->mission_hud->set_action_status(QStringLiteral("Speed: %1x").arg(next, 0, 'f', 1));
		}
		return true;
	}

	// 4. 5x3 Grid Hotkeys for selected unit/building actions
	static const std::array<int, 15> grid_keys{
		Qt::Key_Q, Qt::Key_W, Qt::Key_E, Qt::Key_R, Qt::Key_T,
		Qt::Key_A, Qt::Key_S, Qt::Key_D, Qt::Key_F, Qt::Key_G,
		Qt::Key_Z, Qt::Key_X, Qt::Key_C, Qt::Key_V, Qt::Key_B,
	};
	for (size_t idx = 0; idx < grid_keys.size(); ++idx) {
		if (key != grid_keys[idx]) {
			continue;
		}
		if (this->mission_hud) {
			if (this->mission_hud->activateCommand(static_cast<int>(idx))) {
				return true;
			}
			if (key == Qt::Key_S and this->mission_hud->can_stop()) {
				this->mission_hud->stopSelected();
				return true;
			}
		}
		return false;
	}

	return false;
}

} // namespace openage::presenter
