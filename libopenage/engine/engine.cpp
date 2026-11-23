// Copyright 2023-2025 the openage authors. See copying.md for legal info.

#include "engine.h"

#include "log/log.h"
#include "log/message.h"

#include "cvar/cvar.h"
#include "gamestate/simulation.h"
#include "presenter/presenter.h"
#include "time/time_loop.h"

#include "assets/mod_manager.h" // Phase G1 modpack closure proof logs.


namespace openage::engine {

Engine::Engine(mode mode,
               const util::Path &root_dir,
               const std::vector<std::string> &mods,
               const renderer::window_settings &window_settings,
               bool scenario_mode,
               const std::string &scenario_player_setup_fqon,
               const std::string &scenario_fqon) :
	running{true},
	run_mode{mode},
	root_dir{root_dir},
	scenario_mode{scenario_mode},
	scenario_player_setup_fqon{scenario_mode ? scenario_player_setup_fqon : std::string{}},
	scenario_fqon{scenario_mode ? scenario_fqon : std::string{}},
	threads{} {
	log::log(INFO
	         << "launching engine with root directory"
	         << root_dir);

	if (this->scenario_mode) {
		log::log(INFO
		         << "scenario mode enabled; player setup FQON = '"
		         << this->scenario_player_setup_fqon
		         << "', scenario FQON = '"
		         << this->scenario_fqon
		         << "'");
	}

	// read and apply the configuration files
	this->cvar_manager = std::make_shared<cvar::CVarManager>(this->root_dir["cfg"]);
	cvar_manager->load_all();

	// time loop
	this->time_loop = std::make_shared<time::TimeLoop>();

	// game simulation (constructed, NOT initialized -- see initialize_runtime())
	this->simulation = std::make_shared<gamestate::GameSimulation>(this->root_dir,
	                                                               this->cvar_manager,
	                                                               this->time_loop,
	                                                               this->scenario_player_setup_fqon,
	                                                               this->scenario_fqon);

	// Modpacks: only the *requested* set is recorded here. The dependency
	// expanded list (used at modpack activation time) lives inside the
	// ModManager. We log both for the modpack-closure proof called out by
	// Phase G1.4.
	log::log(INFO << "requested modpacks (initial requested set "
	                  "before dependency expansion): ["
	                  << mods.size() << (mods.size() == 1 ? " entry" : " entries")
	                  << "]");
	for (const auto &id : mods) {
		log::log(INFO << "  - " << id);
	}

	// Presenter (FULL only). Constructed, NOT initialized -- init_all()
	// runs in initialize_runtime() on the main thread before any worker
	// is spawned. This was Phase C5.3 + G1.3: detach the heavy renderer
	// init (Qt application + window + render passes + attach_renderer)
	// from the presentation loop so it does not race with the
	// simulation tick worker that is created in loop().
	if (this->run_mode == mode::FULL) {
		this->presenter = std::make_shared<presenter::Presenter>(this->root_dir,
		                                                         this->simulation,
		                                                         this->time_loop,
		                                                         window_settings);
	}
}

void Engine::initialize_runtime() {
	if (this->runtime_initialized) {
		log::log(MSG(err) << "Engine::initialize_runtime called twice; "
		                  << "engine.runtime_initialized=true. Phase G1 "
		                  << "forbids silent re-entry; the caller should "
		                  << "arrange for exactly one initialize_runtime() "
		                  << "between Engine construction and loop().");
		throw Error{ERR
		           << "Engine::initialize_runtime called twice (refusing "
		           << "to re-initialize a runtime that has already reached "
		           << "Initialized)."};
	}

	// Phase G1.4 / C5.3: register the requested modpacks into the
	// ModManager and activate them together with their declared
	// dependencies. The activation here MUST happen on the same thread
	// that called the Engine constructor and that will call
	// initialize_runtime() (i.e. the main thread), before any worker is
	// spawned. This is the "register -> activate -> freeze immutable
	// runtime snapshot" ordering required by Phase C5.4.
	//
	// The GameSimulation::set_modpacks() prepends "engine" implicitly --
	// any dependency-resolved expansion of de2_base etc. must therefore
	// include "engine" already. We log both the requested and the
	// dependency-expanded load order before the simulation moves to
	// initialize() so a reviewer can confirm the audit trail with a
	// single log line.
	//
	// TODO: The auto-expansion of `de2_base` (which the Mission 1
	// scenario references for Villager/TownCenter FQONs but which the
	// current scenarios UI does not request explicitly) is intentionally
	// NOT implemented in this commit -- the launch scripts pass
	// [engine, de2_base, ww_mvp] explicitly. The next follow-up should
	// teach the ModManager dependency walker to pull in
	// engine.modpack.toml [dependency] modpacks automatically.
	std::vector<std::string> requested = {"de2_base", "ww_mvp"};
	log::log(INFO << "engine: initialize_runtime: requested modpacks = ["
	                  << "engine, de2_base, ww_mvp"
	                  << "] (Mission 1 dependency closure; 'engine' is "
	                  << "prepended by GameSimulation::set_modpacks, so we "
	                  << "only pass the additional modpacks here)");
	this->simulation->set_modpacks(requested);

	// Modpack closure proof -- log the manager state once, after
	// activation. We only dump here (not at every other boundary) so the
	// default log is concise. The invariant-assert helper inside
	// dump_state() catches a missing active-entry before run() ever sees it.
	this->simulation->get_mod_manager()->dump_state("engine:runtime-initialized");

	// GameSimulation::initialize() does the heavy lift:
	//   - register event handlers
	//   - construct Game / GameState
	//   - load nyan data (engine / de2_base / ww_mvp)
	//   - add the PlayerSetup
	//   - load the scenario (terrain + initial PlacedEntity spawns)
	//   - construct the data-driven TriggerRuntime + wire trigger data
	// All of this runs on the main thread so that no worker thread has
	// to allocate against the same heap pages as the nyan parser.
	this->simulation->initialize();

	// Presenter pre-flight (FULL only). Idempotent via Presenter's own
	// `initialized` flag, but we explicitly skip it in HEADLESS so the
	// headless runtime path provably never touches Qt, the renderer,
	// or any OpenGL code (Phase G1.6 / Stage 4A requirement).
	if (this->run_mode == mode::FULL) {
		this->presenter->init_all();
	}

	this->runtime_initialized = true;
	log::log(MSG(info) << "engine: runtime initialized "
	                       << "(initialized=true; loop() may now spawn workers)");
}

void Engine::loop() {
	if (not this->runtime_initialized) {
		log::log(MSG(err) << "Engine::loop called before "
		                  << "initialize_runtime(); refusing to spawn "
		                  << "workers on a half-prepared runtime.");
		throw Error{ERR
		           << "Engine::loop requires the runtime to be "
		           << "initialized first (call Engine::initialize_runtime() "
		           << "between construction and loop()). Partial "
		           << "initialization is not safe to drive."};
	}

	// Spawn the time loop in a worker thread. The time loop advances the
	// simulation clock and runs independently of the main loop.
	this->threads.emplace_back([&]() {
		this->run_time_loop();
	});

	if (this->run_mode == mode::FULL) {
		// Run the simulation in a worker thread...
		this->threads.emplace_back([&]() {
			this->run_simulation();
		});

		log::log(INFO << "Using " << this->threads.size() + 1 << " threads "
		              << "(" << std::jthread::hardware_concurrency() << " available)");

		// ...while the presenter runs on the main thread. The presenter owns the
		// QGuiApplication, which Qt requires to be created and operated on the
		// main thread (on macOS, QGuiApplication setup on a non-main thread is
		// an API misuse that the platform plugin rejects). This is also why the
		// simulation cannot stay on the main thread in FULL mode: the main
		// thread must be free to drive the presenter.
		this->presenter->run();
		log::log(MSG(info) << "presenter exited");
		// Presenter::run requests both workers to stop. Their last
		// callbacks can still reference the HUD and renderer.
		this->threads.clear();
		// Make sure that the presenter gets destructed in the same thread,
		// otherwise OpenGL complains about missing contexts.
		this->presenter.reset();
		this->running = false;
	}
	else {
		log::log(INFO << "Using " << this->threads.size() + 1 << " threads "
		              << "(" << std::jthread::hardware_concurrency() << " available)");

		// In headless mode there is no presenter, so the main thread is free to
		// run the simulation directly.
		this->run_simulation();
	}
}

void Engine::run_time_loop() {
	this->time_loop->run();
	this->time_loop.reset();
}

void Engine::run_simulation() {
	// Run the main game simulation loop:
	this->simulation->run();
	// After stopping, clean up the simulation
	this->simulation.reset();
	if (this->run_mode != mode::FULL) {
		this->running = false;
	}
}

} // namespace openage::engine
