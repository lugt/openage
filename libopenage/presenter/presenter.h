// Copyright 2019-2024 the openage authors. See copying.md for legal info.

#pragma once

#include "time/time.h"
#include "coord/phys.h"
#include "gamestate/types.h"

#include <array>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "renderer/window.h"
#include "util/path.h"
#include "util/timing.h"


namespace qtgui {
class GuiApplication;
}

namespace openage {

namespace gamestate {
class GameEntity;
class GameSimulation;
}

namespace input {
class InputManager;

namespace game {
class Controller;
}
}

namespace time {
class TimeLoop;
}

namespace renderer {
class RenderPass;
class Renderer;
class Texture2d;
class ShaderProgram;
class Window;

namespace camera {
class Camera;
class CameraManager;
} // namespace camera

namespace gui {
class GUI;
}

namespace hud {
class HudRenderStage;
}

namespace screen {
class ScreenRenderStage;
}

namespace skybox {
class SkyboxRenderStage;
}

namespace terrain {
class TerrainRenderStage;
}

namespace world {
class WorldRenderStage;
}

namespace resources {
class AssetManager;
}

} // namespace renderer

namespace presenter {

class HudQmlAdapter;

namespace viewmodel {
class SelectionModel;
class SelectionOverlay;
}

class Presenter {
public:
	/**
	 * Create a new presenter.
	 *
	 * @param path Root directory path.
	 * @param simulation Game simulation. Can be set later with \p set_engine()
	 * @param time_loop Time loop which controls simulation time. Can be set later with \p set_time_loop()
	 * @param window_settings The settings to customize the display window (e.g. size, display mode, vsync).
	 */
	Presenter(const util::Path &path,
	          const std::shared_ptr<gamestate::GameSimulation> &simulation = nullptr,
	          const std::shared_ptr<time::TimeLoop> &time_loop = nullptr,
	          const renderer::window_settings &window_settings = {});

	~Presenter();

	/**
	 * Start the presenter and initialize subsystems.
	 */
	void run();

	/**
	 * Initialize graphics + GUI + input subsystems up-front.
	 *
	 * Phase C5 split this off from `run()` so the Engine can drive the
	 * full init phase on the main thread (no presenter renderer init
	 * racing with the simulation's first tick). Idempotent: subsequent
	 * calls are no-ops; `run()` detects `is_initialized()` and skips
	 * re-init, proceeding straight to the render loop.
	 */
	void init_all();

	/**
	 * Whether `init_all()` has already completed.
	 */
	bool is_initialized() const {
		return this->initialized;
	}

	/**
	 * Set the game simulation controlled by this presenter.
	 *
	 * @param simulation Game simulation.
	 */
	void set_simulation(const std::shared_ptr<gamestate::GameSimulation> &simulation);

	/**
	 * Set the time loop controlled by this presenter.
	 *
	 * @param time_loop Time loop.
	 */
	void set_time_loop(const std::shared_ptr<time::TimeLoop> &time_loop);

	/**
	 * Initialize the Qt application managing the graphical views. Required
	 * for creating windows.
	 *
	 * @returns Pointer to openage's application wrapper,
	 */
	static std::shared_ptr<qtgui::GuiApplication> init_window_system();

protected:
	/**
	 * Initialize all graphics subsystems of the presenter, i.e.
	 *     - window creation
	 *     - main renderer
	 *     - component renderers (Terrain, Game Entities, GUI)
	 */
	void init_graphics();

	/**
	 * Initialize the GUI.
	 */
	void init_gui();

	/**
	 * Initialize the input management.
	 */
	void init_input();

	/**
	 * Initialize the final render pass that renders the results of all previous
	 * render passes to the window screen.
	 */
	void init_final_render_pass();

	/**
	 * Create the SelectionModel + SelectionOverlay and wire the
	 * Controller's selection callback into the model. Idempotent:
	 * safe to call from both \p set_simulation() (early sim
	 * attach) and \p init_input() (controller creation). The
	 * model is the single presentation-layer view of selection;
	 * the Controller remains the master.
	 */
	void setup_selection_viewmodel();

	// void init_audio();

	/**
	 * Render all configured render passes in sequence.
	 */
	void render();

	// TODO: remove and move into our config/settings system
	util::Path root_dir;

	/**
	 * The settings to customize the display window (e.g. size, display mode, vsync).
	 * Stored in the presenter because the presenter owns the window.
	 */
	const renderer::window_settings window_settings;

	// graphis components
	/**
	 * Windowing GUI Application wrapper.
	 */
	std::shared_ptr<qtgui::GuiApplication> gui_app;

	/**
	 * Display window.
	 */
	std::shared_ptr<renderer::Window> window;

	/**
	 * openage's graphics renderer.
	 */
	std::shared_ptr<renderer::Renderer> renderer;

	/**
	 * Camera for viewing things.
	 */
	std::shared_ptr<renderer::camera::Camera> camera;

	/**
	 * State exposed to the mission QML. The adapter outlives the QML engine.
	 */
	std::shared_ptr<HudQmlAdapter> mission_hud;
	void send_economy_action(gamestate::entity_id_t actor, const nyan::fqon_t &option,
	                         std::optional<coord::phys3> position = std::nullopt);
	bool handle_gameplay_hotkey(const QKeyEvent &ev);
	bool jump_to_next_matching(
		const std::string &cycle_key,
		const std::function<bool(gamestate::GameEntity &, time::time_t)> &pred);
	std::array<std::vector<gamestate::entity_id_t>, 10> control_groups_;
	std::unordered_map<std::string, size_t> hotkey_cycle_index_;
	std::optional<std::pair<gamestate::entity_id_t, nyan::fqon_t>> pending_build;
	std::optional<gamestate::entity_id_t> pending_repair;
	bool placement_pressed = false;
	bool attack_pointer = false;
	bool frame_rendered = false;
	bool first_frame_logged = false;
	time_nsec_t startup_origin_ns{0};
	time::time_t next_minimap_update{0};
	void update_campaign_map();

	/**
	 * Qt-based GUI for interface.
	 */
	std::shared_ptr<renderer::gui::GUI> gui;

	/**
	 * Camera manager for camera controls.
	 */
	std::shared_ptr<renderer::camera::CameraManager> camera_manager;

	/**
	 * Last map size used to configure dynamic camera boundaries.
	 */
	util::Vector2s camera_map_size{0, 0};

	/**
	 * Graphics output for the map background.
	 */
	std::shared_ptr<renderer::skybox::SkyboxRenderStage> skybox_renderer;

	/**
	 * Graphics output for terrain.
	 */
	std::shared_ptr<renderer::terrain::TerrainRenderStage> terrain_renderer;

	/**
	 * Graphics output for units/buildings.
	 */
	std::shared_ptr<renderer::world::WorldRenderStage> world_renderer;

	/**
	 * Graphics output for the HUD.
	 */
	std::shared_ptr<renderer::hud::HudRenderStage> hud_renderer;

	/**
	 * Final graphics output to the window screen.
	 */
	std::shared_ptr<renderer::screen::ScreenRenderStage> screen_renderer;

	/**
	 * Manager for loading/storing asset resources.
	 */
	std::shared_ptr<renderer::resources::AssetManager> asset_manager;

	/**
	 * Render passes in the openage renderer.
	 */
	std::vector<std::shared_ptr<renderer::RenderPass>> render_passes;

	/**
	 * Game simulation.
	 */
	std::shared_ptr<gamestate::GameSimulation> simulation;

	/**
	 * Time loop.
	 */
	std::shared_ptr<time::TimeLoop> time_loop;

	/**
	 * Input manager.
	 */
	std::shared_ptr<input::InputManager> input_manager;

	/**
	 * Game input controller (selection + command dispatch).
	 */
	std::shared_ptr<input::game::Controller> game_controller;

	/**
	 * Presentation-layer view of the current selection. Mirrored
	 * from the Controller via a selection callback; read by the
	 * HUD and the selection overlay renderer.
	 */
	std::shared_ptr<viewmodel::SelectionModel> selection_model;

	/**
	 * Renderer-facing overlay state (entity id -> colour). Subscribes
	 * to SelectionModel and rebuilds on every change.
	 */

	/**
	 * Whether `init_all()` (graphics + GUI + input) has been driven.
	 * Phase C5: the Engine constructor may pre-flight this on the main
	 * thread so that `run()` is allowed to start its render loop
	 * concurrently with the simulation worker threads.
	 */
	bool initialized = false;

	struct HudCache;
	std::unique_ptr<HudCache> hud_cache;
};

} // namespace presenter
} // namespace openage
