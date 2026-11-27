// Copyright 2023-2025 the openage authors. See copying.md for legal info.

#pragma once

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "renderer/window.h"
#include "util/path.h"
#include "util/timing.h"


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
	 * @param scenario_mode When true, the engine launches in scenario mode: a
	 *                 scenario PlayerSetup FQON is forwarded to the
	 *                 simulation so a single scenario slot is created.
	 *                 Empty FQON strings disable the scenario overlay even
	 *                 when this is true (allows forcing a debug/observer
	 *                 slot via CLI).
	 * @param scenario_player_setup_fqon PlayerSetup FQON the scenario
	 *                                    player slot should use. Empty
	 *                                    means observer/debug slot.
	 * @param scenario_fqon Scenario FQON to load when the
	 *                               simulation starts. Empty falls back to
	 *                               the simulation's default procedural
	 *                               terrain.
	 */
	Engine(mode mode,
	       const util::Path &root_dir,
	       const std::vector<std::string> &mods,
	       const renderer::window_settings &window_settings = {},
	       bool scenario_mode = false,
	       const std::string &scenario_player_setup_fqon = "",
	       const std::string &scenario_fqon = "");

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
	/** Enable explicitly configured computer players before starting worker threads. */
	void configure_ai(const std::string &json);

	/**
	 * Phase G1 explicit-runtime entry: synchronously prepare the runtime
	 * (modpacks, Game, scenario, objective controller, FULL-mode
	 * Presenter/Renderer) on the caller's thread **before** any worker
	 * thread is spawned. Idempotent? No -- calling it twice is a hard
	 * lifecycle error so the noise that the old "Engine constructor
	 * does it all and run() does not re-do it" pattern silently swallowed
	 * can no longer hide a regression.
	 *
	 * Threading: must be called on the same thread that will later call
	 * `loop()`. In openage, that is the main thread (so the FULL-mode
	 * Presenter/Renderer thread-affinity for Qt is satisfied).
	 */
	void initialize_runtime();

	/**
	 * True iff `initialize_runtime()` has run to completion and the
	 * runtime is fully prepared for `loop()`. `loop()` asserts this.
	 */
	bool is_runtime_initialized() const {
		return this->runtime_initialized;
	}

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
	 * Whether the engine was started in scenario mode.
	 *
	 * Propagated to the GameSimulation at construction so the simulation
	 * can choose the scenario PlayerSetup and the scenario FQON instead of the
	 * hardcoded empty / debug defaults.
	 */
	bool scenario_mode;

	/**
	 * PlayerSetup FQON for the scenario slot. Empty = observer/debug.
	 */
	std::string scenario_player_setup_fqon;

	/**
	 * Scenario FQON to load. Empty = default procedural terrain.
	 */
	std::string scenario_fqon;

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

	/**
	 * Monotonic timestamp captured at Engine construction for startup milestones.
	 */
	time_nsec_t startup_origin_ns{0};

	/**
	 * True iff `initialize_runtime()` has run to completion. Set true at
	 * the end of a successful initialize_runtime() call, never reset.
	 * Doubled initialize_runtime() calls fail-fast on this flag.
	 */
	bool runtime_initialized = false;
};

} // namespace engine
} // namespace openage
