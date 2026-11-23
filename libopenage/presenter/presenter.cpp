// Copyright 2019-2024 the openage authors. See copying.md for legal info.

#include "presenter.h"

#include <eigen3/Eigen/Dense>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_set>
#include <vector>

#include <QImage>
#include <QCursor>
#include <QPixmap>
#include <QWindow>
#include <QRegularExpression>
#include <QPainter>
#include <QPolygonF>
#include <QMouseEvent>
#include <QTimer>

#include "gamestate/game.h"
#include "gamestate/component/api/build.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/game_state.h"
#include "gamestate/player.h"
#include "scenario/trigger/original_scenario.h"
#include "gamestate/map.h"
#include "gamestate/terrain.h"
#include "gamestate/terrain_chunk.h"
#include "coord/tile.h"
#include "renderer/gui/guisys/public/gui_input.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/event/send_command.h"
#include "gamestate/simulation.h"
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

Presenter::Presenter(const util::Path &root_dir,
                     const std::shared_ptr<gamestate::GameSimulation> &simulation,
                     const std::shared_ptr<time::TimeLoop> &time_loop,
                     const renderer::window_settings &window_settings) :
	root_dir{root_dir},
	window_settings{window_settings},
	render_passes{},
	simulation{simulation},
	time_loop{time_loop} {}


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
		this->gui_app->process_events();
		// TODO: pass button presses and events from GUI to controller

		this->render();

		this->renderer->check_error();

		this->window->update();
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
	this->gui_app = this->init_window_system();

	// Window and renderer
	this->window = renderer::Window::create("openage presenter test", this->window_settings);
	this->renderer = this->window->make_renderer();

	// Asset mangement
	this->asset_manager = std::make_shared<renderer::resources::AssetManager>(
		this->renderer,
		this->root_dir / "assets" / "converted");
	auto missing_tex = this->root_dir / "assets" / "test" / "textures" / "test_missing.sprite";
	this->asset_manager->set_placeholder_animation(missing_tex);
	auto fallback_terrain = this->root_dir / "assets" / "test" / "textures" / "test_terrain.terrain";
	this->asset_manager->set_placeholder_terrain(fallback_terrain);

	// Camera
	this->camera = std::make_shared<renderer::camera::Camera>(this->renderer, this->window->get_size());
	this->camera->look_at_coord(coord::scene3{10.0, 10.0, 0}); // Center camera on the map
	this->window->add_resize_callback([this](size_t w, size_t h, double /*scale*/) {
		this->camera->resize(w, h);
	});

	// Camera manager
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

	// Units/buildings
	this->world_renderer = std::make_shared<renderer::world::WorldRenderStage>(
		this->window,
		this->renderer,
		this->camera,
		this->root_dir["assets"]["shaders"],
		this->asset_manager,
		this->time_loop->get_clock());
	this->render_passes.push_back(this->world_renderer->get_render_pass());

	// HUD
	this->hud_renderer = std::make_shared<renderer::hud::HudRenderStage>(
		this->window,
		this->renderer,
		this->camera,
		this->root_dir["assets"]["shaders"],
		this->asset_manager,
		this->time_loop->get_clock());
	this->render_passes.push_back(this->hud_renderer->get_render_pass());

	// World-space selection overlay pass. Composites on top of
	// terrain/units/HUD but under the GUI. Owns its own FBO so the
	// drag-rect pass lifecycle is undisturbed.
	this->render_passes.push_back(this->hud_renderer->get_selection_render_pass());

	this->init_gui();
	this->init_final_render_pass();

	if (this->simulation) {
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
                if (position and (not runtime->original_scenario() or (ownership and ownership->get_owners().get(time::time_t{0}) == 0))) {
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
		this->window,  // window for the gui
		qml_root_file, // entry qml file, absolute path.
		qml_root,      // directory to watch for qml file changes
		qml_assets,    // qml data: Engine *, the data directory, ...
		this->renderer,
		"missionHud",
		this->mission_hud.get()
	);

	auto gui_pass = this->gui->get_render_pass();
	this->render_passes.push_back(gui_pass);
}

void Presenter::init_input() {
	log::log(INFO << "Presenter: Initializing input subsystem...");

	this->input_manager = std::make_shared<input::InputManager>();

	this->window->add_key_callback([&](const QKeyEvent &ev) {
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
		if (this->pending_build and not over_hud and ev.type() == QEvent::MouseButtonPress) {
			if (ev.button() == Qt::RightButton) {
				this->pending_build.reset();
				return;
			}
			if (ev.button() == Qt::LeftButton) {
				const auto [actor, option] = *this->pending_build;
				const auto selected = this->game_controller->get_selected();
				if (selected.size() == 1 and selected.front() == actor) {
					const coord::input mouse{static_cast<int>(point.x()), static_cast<int>(point.y())};
					this->send_economy_action(actor, option, mouse.to_phys3(this->camera));
				}
				this->pending_build.reset();
				this->placement_pressed = true;
				return;
			}
		}
		if (ev.button() == Qt::LeftButton) {
			if (ev.type() == QEvent::MouseButtonPress or ev.type() == QEvent::MouseButtonDblClick) {
				this->mission_ui_pressed = over_hud;
			}
			if (this->mission_ui_pressed) {
				this->gui->get_input_handler()->process(std::shared_ptr<QEvent>(ev.clone()));
				if (ev.type() == QEvent::MouseButtonRelease) {
					this->mission_ui_pressed = false;
				}
				return;
			}
		}
		else if (over_hud) {
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
		if (not over_hud and ev.buttons() == Qt::NoButton and this->game_controller) {
			const auto state = this->simulation->get_game()->get_state();
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
		if (this->mission_ui_pressed) {
			this->gui->get_input_handler()->process(std::shared_ptr<QEvent>(ev.clone()));
			return;
		}
		this->input_manager->set_mouse(mouse.x, mouse.y);
		this->input_manager->process(ev);
	});
	this->window->add_mouse_wheel_callback([this](const QWheelEvent &ev) {
		const auto size = this->window->get_size();
		if (this->mission_hud and this->mission_hud->is_interactive(ev.position().x(), ev.position().y(), size[0], size[1])) {
			this->gui->get_input_handler()->process(std::shared_ptr<QEvent>(ev.clone()));
			return;
		}
		this->input_manager->process(ev);
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
			QObject::connect(this->mission_hud.get(), &HudQmlAdapter::action_requested,
			                 [this](const QString &option, bool build) {
				const auto selected = this->game_controller->get_selected();
				if (selected.size() == 1) {
					if (build) {
						this->pending_build = {selected.front(), option.toStdString()};
					}
					else {
						this->pending_build.reset();
						this->send_economy_action(selected.front(), option.toStdString());
					}
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
				this->game_controller->set_selected({});
			});
			QObject::connect(this->mission_hud.get(), &HudQmlAdapter::stop_requested, [this] {
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

	// attach GUI if it's initialized
	if (this->gui) {
		log::log(INFO << "Loading GUI controls");
		this->input_manager->set_gui(this->gui->get_input_handler());
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
                if (this->simulation->get_trigger_runtime()->original_scenario() and this->time_loop) {
                    this->simulation->get_event_loop()->create_event("scenario.original_selection", this->simulation->get_commander(),
                        this->simulation->get_game()->get_state(), this->time_loop->get_clock()->get_time(), {{"selection", ids}});
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
}

void Presenter::send_economy_action(gamestate::entity_id_t actor, const nyan::fqon_t &option,
                                    std::optional<coord::phys3> position) {
	event::EventHandler::param_map::map_t params{
		{"actor", actor}, {"option", option},
		{"issuer", gamestate::player_id_t{this->game_controller->get_controlled()}},
	};
	if (position) {
		params.emplace("position", *position);
	}
	this->simulation->get_event_loop()->create_event(
		"game.economy_action", this->simulation->get_commander(), this->simulation->get_game()->get_state(),
		this->time_loop->get_clock()->get_time(), params);
}

void Presenter::update_campaign_map() {
	if (not this->simulation or not this->game_controller) {
		return;
	}
	const auto &state = this->simulation->get_game()->get_state();
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
	this->mission_hud->set_action_status(this->pending_build
		? QObject::tr("Left-click to place the building. Right-click to cancel.")
		: QString::fromStdString(state->action_feedback(viewer)));

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
	this->mission_hud->set_diplomacy(std::move(relations));
	if (not view) {
		return;
	}

	QImage terrain(view->width, view->height, QImage::Format_RGB32);
	terrain.fill(QColor("#0d141b"));
	for (const auto &chunk : state->get_map()->get_terrain()->get_chunks()) {
		const auto offset = chunk->get_offset();
		const auto size = chunk->get_size();
		const auto &tiles = chunk->get_tiles();
		for (size_t local_y = 0; local_y < size[1]; ++local_y) {
			for (size_t local_x = 0; local_x < size[0]; ++local_x) {
				const auto x = offset.ne + local_x;
				const auto y = offset.se + local_y;
				auto visibility = view->at(x, y);
				if (visibility == gamestate::TileVisibility::Unexplored) {
					continue;
				}
				const auto &name = tiles[local_y * size[0] + local_x].terrain.get_name();
				QColor color = name.find("water") != std::string::npos ? QColor("#356f8f")
				             : name.find("road") != std::string::npos ? QColor("#aa946a") : QColor("#6b894d");
				terrain.setPixelColor(x, y, visibility == gamestate::TileVisibility::Explored ? color.darker(240) : color);
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
    if (this->simulation and this->camera) {
        if (auto original = this->simulation->get_trigger_runtime()->original_scenario()) {
            if (auto point = original->take_camera()) this->camera->look_at_coord(*point);
        }
    }
	// TODO: Pass current time to update() instead of fetching it in renderer
	this->camera_manager->update();
	this->update_campaign_map();
	this->terrain_renderer->update();
	this->world_renderer->update();

	std::vector<renderer::hud::SelectionOverlayQuad> quads;
	if (this->game_controller and this->simulation) {
		const auto state = this->simulation->get_game()->get_state();
		const auto simulation_time = this->time_loop->get_clock()->get_time();
		const auto render_time = this->time_loop->get_clock()->get_real_time();
		const auto player = this->game_controller->get_controlled();
		const auto selected = this->game_controller->get_selected();
		QVariantList cards;
		QVariantList actions;
		bool can_stop = false;
		int construction = -1;
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
			cards.emplaceBack(QVariantMap{
				{"id", QVariant::fromValue<qulonglong>(id)}, {"name", object_label(entity->definition)},
				{"owner", QString::fromStdString(state->get_player_name(owner))},
				{"hp", QVariant::fromValue<qlonglong>(hp)}, {"maxHp", QVariant::fromValue<qlonglong>(max_hp)},
				{"color", color}, {"building", bool(build)},
			});
			if (ownership and owner == player) {
				can_stop = can_stop or entity->has_component(gamestate::component::component_t::MOVE)
				                    or entity->has_component(gamestate::component::component_t::ATTACK);
				if (selected.size() == 1) {
					if (build and build->get_progress() < 100) {
						construction = static_cast<int>(build->get_progress());
					}
					if (not build or not build->is_under_construction()) {
						for (const auto &option : entity->action_options) {
                                if (not state->get_player(0)->allows_action(option.subject)) continue;
							QStringList cost;
							for (const auto &[resource, amount] : option.cost) {
								if (amount > 0) {
									cost.append(QStringLiteral("%1 %2").arg(amount).arg(object_label(resource)));
								}
							}
							actions.append(QVariantMap{
								{"id", QString::fromStdString(option.id)},
								{"name", object_label(option.subject)},
								{"build", option.kind == gamestate::ActionKind::Build},
								{"research", option.kind == gamestate::ActionKind::Research},
								{"detail", cost.join(QStringLiteral(" · "))},
							});
						}
					}
				}
			}
		}
		if (this->mission_hud) {
			this->mission_hud->set_selection_details(std::move(cards), std::move(actions), construction, can_stop);
		}
	}
	this->hud_renderer->set_selection_overlays(std::move(quads));

	this->hud_renderer->update();
	this->gui->render();

	for (auto &pass : this->render_passes) {
		this->renderer->render(pass);
	}

	// M1 visibility-spike screenshot hook (TEMPORARY; deletion condition: M6
	// Gate A signoff, replaced by the acceptance screenshot flow). Gated by
	// OPENAGE_DEBUG_SCREENSHOT (native path). Captures the framebuffer once at
	// frame 60 (~1 s) so scenario spawn/init has settled. We read the SCREEN
	// render target via into_data() (glReadPixels, available on macOS GL 4.1)
	// instead of renderer->display_into_data(): the latter calls glReadnPixels
	// which requires GL 4.5 and aborts on macOS (GL caps at 4.1). into_data()
	// is bottom-origin so flip_y(). The PNG is written via QImage::save with a
	// native path, bypassing util::Path (fslike::Directory::writable() has an
	// access()-return-value bug that blocks native writes).
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
}

} // namespace openage::presenter
