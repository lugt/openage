// Copyright 2023-2025 the openage authors. See copying.md for legal info.

#pragma once

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "renderer/window.h"
#include "util/path.h"


// TODO: Remove custom jthread definition when clang/libc++ finally supports it
#if __llvm__
	#if !__cpp_lib_jthread
namespace std {
class jthread : public thread {
public:
	using thread::thread; // needed constructors
	jthread(const jthread &) = delete;
	jthread &operator=(const jthread &) = delete;
	jthread(jthread &&) = default;
	jthread &operator=(jthread &&) = default;
	~jthread() {
		if (this->joinable()) {
			this->join();
		}
	}
};
} // namespace std
	#endif
#else
	#include <stop_token>
#endif


namespace openage {

namespace cvar {
class CVarManager;
} // namespace cvar

namespace gamestate {
class GameSimulation;
} // namespace gamestate

namespace presenter {
class Presenter;
} // namespace presenter

namespace time {
class TimeLoop;
} // namespace time


namespace engine {

/**
 * Encapsulates all subcomponents needed for a run.
 */
class Engine {
public:
	enum class mode {
		LEGACY,
		HEADLESS,
		FULL,
	};

	/**
	 * Create the engine instance for this run.
	 *
	 * @param mode The run mode to use.
	 * @param root_dir openage root directory.
	 * @param mods The mods to load.
	 * @param window_settings The settings to customize the display window (e.g. size, display mode, vsync).
	 * @param tutorial When true, the engine launches in tutorial mode: a
	 *                 tutorial PlayerSetup FQON is forwarded to the
	 *                 simulation so a single tutorial slot is created.
	 *                 Empty FQON strings disable the tutorial overlay even
	 *                 when this is true (allows forcing a debug/observer
	 *                 slot via CLI).
	 * @param tutorial_player_setup_fqon PlayerSetup FQON the tutorial
	 *                                    player slot should use. Empty
	 *                                    means observer/debug slot.
	 * @param tutorial_scenario_fqon Scenario FQON to load when the
	 *                               simulation starts. Empty falls back to
	 *                               the simulation's default procedural
	 *                               terrain.
	 */
	Engine(mode mode,
	       const util::Path &root_dir,
	       const std::vector<std::string> &mods,
	       const renderer::window_settings &window_settings = {},
	       bool tutorial = false,
	       const std::string &tutorial_player_setup_fqon = "",
	       const std::string &tutorial_scenario_fqon = "");

	// engine should not be copied or moved
	Engine(const Engine &) = delete;
	Engine &operator=(const Engine &) = delete;
	Engine(Engine &&) = delete;
	Engine &operator=(Engine &&) = delete;
	~Engine() = default;


	/**
	 * Run the main loop.
	 */
	void loop();

	/**
	 * current simulation state variable.
	 * to be set to false to stop the simulation loop.
	 */
	bool running;

private:

	/**
	 * Run the time loop. Executed in a worker thread.
	 */
	void run_time_loop();

	/**
	 * Run the simulation loop. Executed in a worker thread in FULL mode and on
	 * the main thread in headless mode.
	 */
	void run_simulation();

	/**
	 * The run mode to use.
	 */
	mode run_mode;

	/**
	 * openage root directory.
	 */
	util::Path root_dir;

	/**
	 * Whether the engine was started in tutorial mode.
	 *
	 * Propagated to the GameSimulation at construction so the simulation
	 * can choose the tutorial PlayerSetup / scenario instead of the
	 * hardcoded empty / debug defaults.
	 */
	bool tutorial;

	/**
	 * PlayerSetup FQON for the tutorial player slot. Empty = observer/debug.
	 */
	std::string tutorial_player_setup_fqon;

	/**
	 * Scenario FQON to load for the tutorial. Empty = default procedural terrain.
	 */
	std::string tutorial_scenario_fqon;

	/**
	 * The threads used by the engine.
	 */
	std::vector<std::jthread> threads;

	/**
	 * Environment variables.
	 */
	std::shared_ptr<cvar::CVarManager> cvar_manager;

	/**
	 * Controls and update the clock for time-based measurements.
	 */
	std::shared_ptr<time::TimeLoop> time_loop;

	/**
	 * Gameplay simulation.
	 */
	std::shared_ptr<gamestate::GameSimulation> simulation;

	/**
	 * Video/audio/input management. Can be nullptr in headless mode.
	 */
	std::shared_ptr<presenter::Presenter> presenter;
};

} // namespace engine
} // namespace openage
