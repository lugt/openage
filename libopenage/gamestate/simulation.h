// Copyright 2013-2024 the openage authors. See copying.md for legal info.

#pragma once

#include <shared_mutex>

#include "util/path.h"

#include "gamestate/event/removal.h"

namespace openage {

namespace assets {
class ModManager;
}

namespace cvar {
class CVarManager;
}

namespace event {
class EventLoop;
} // namespace event

namespace renderer {
class RenderFactory;
}

namespace time {
class TimeLoop;
} // namespace time

namespace scenario::trigger {
class TriggerRuntime;
}

namespace gamestate {
class EntityFactory;
class Game;
class TerrainFactory;

namespace event {
class Commander;
class Spawner;
} // namespace event

/**
 * Explicit lifecycle states for GameSimulation.
 *
 * Phase G1 split `start()` out into `initialize()` and `run()` so that
 * the "runtime is ready" boundary can be reached exactly once, on the
 * caller's thread, and explicitly fail-fast on any re-entry or
 * out-of-order calls. The state machine only progresses forward; a
 * duplicate `initialize()` or a `run()` issued before `initialize()` has
 * completed is reported as a hard error so the noise that the old
 * `if (game) return` guard silently swallowed can no longer hide a
 * lifecycle regression.
 */
enum class SimulationLifecycle {
	Uninitialized,   //!< Just constructed; nothing has been registered.
	Initialized,      //!< initialize() has finished; runtime is ready to step.
	Running,         //!< run() is the active loop driver.
	Stopped,         //!< stop() has been observed; the loop will exit on the next check.
	Failed,          //!< initialize() failed; no further calls allowed.
};

const char *to_string(SimulationLifecycle s);

/**
 * Gameplay subsystem of the engine.
 *
 * Manages the state of the game world and the event loop.
 */
class GameSimulation final {
public:
	/**
	 * Create the game simulation subsystems depending on the requested run mode.
	 *
	 * @param root_dir openage root directory.
	 * @param cvar_manager Environment variable manager.
	 * @param time_loop Time management loop.
	 * @param scenario_player_setup_fqon PlayerSetup FQON the tutorial slot
	 *                                   should use (empty = observer/debug).
	 * @param scenario_fqon Scenario FQON to load at start (empty
	 *                               = default procedural terrain).
	 */
	GameSimulation(const util::Path &root_dir,
	               const std::shared_ptr<cvar::CVarManager> &cvar_manager,
	               const std::shared_ptr<openage::time::TimeLoop> time_loop,
	               const std::string &scenario_player_setup_fqon = "",
	               const std::string &scenario_fqon = "");

	// game simulation should not be copied or moved
	GameSimulation(const GameSimulation &copy) = delete;
	GameSimulation &operator=(const GameSimulation &copy) = delete;
	GameSimulation(GameSimulation &&other) = delete;
	GameSimulation &operator=(GameSimulation &&other) = delete;
	~GameSimulation() = default;

	/**
	 * Initialize the simulation runtime synchronously on the caller's
	 * thread. Performs, in order:
	 *   - `init_event_handlers`
	 *   - `Game` / `GameState` construction
	 *   - `Game::load_data` (nyan / engine schema / scenario data load)
	 *   - `Game::add_player` for the configured PlayerSetup
	 *   - `Game::load_scenario` (terrain + initial PlacedEntity spawns)
	 *   - TriggerRuntime construction + trigger-data wiring (Mission 1 MVP)
	 *
	 * Idempotent? **No** — calling `initialize()` after a successful
	 * `initialize()` is a hard error. Re-entry attempts return false via
	 * the return value (for tests) AND log + throw, so neither path
	 * silently absorbs a state-machine regression.
	 *
	 * Threading: must be called on the thread that will own the runtime
	 * -- in openage, the main thread. Doing it on a worker after the
	 * GameSimulation::run() worker has spawned is precisely the
	 * anti-pattern this split was added to forbid.
	 *
	 * @return true on success.
	 * @throws Error if the runtime is not in Uninitialized state, or if
	 *         any phase fails (state moves to Failed and the simulation
	 *         must be discarded rather than used).
	 */
	bool initialize();

	/**
	 * Run the simulation loop. **Does not initialize** the runtime; a
	 * successful `initialize()` is a precondition. The loop iterates
	 * while `lifecycle == Running`; the loop exits cleanly when
	 * `stop()` is called.
	 *
	 * Threading: typically called from a worker thread spawned by
	 * `engine::Engine::loop()` after `initialize()` has fully returned.
	 */
	void run();

	/**
	 * Start the simulation loop. **Deprecated** as a public verb -- the
	 * lifecycle intent is now `initialize()` + `run()` so the call site
	 * that owns initialization is unmistakable in code review.
	 *
	 * Kept as a thin wrapper to avoid breaking existing call sites
	 * (demos, interactive debugger, libopenage unit tests). On call it
	 * asserts that the simulation is Initialized, then transitions to
	 * Running. Calling `start()` before `initialize()` is a hard error.
	 *
	 * Removing this alias is recorded as a technical-debt item -- the
	 * demos, headless smoke driver, and WW Mission 1 regression depend
	 * on having a single unambiguous verb to schedule the loop.
	 */
	void start();

	/**
	 * Stop of the simulation loop.
	 */
	void stop();

	/**
	 * Current lifecycle state. Primarily for diagnostic / testing.
	 */
	SimulationLifecycle get_lifecycle() const {
		return this->lifecycle;
	}

	/**
	 * P4.3: True iff the data-driven TriggerRuntime has declared
	 * victory. Replaces the P4.1 hand-rolled objective_controller
	 * query; the runtime's MissionState is now the single source of
	 * truth for mission completion. Defined out-of-line so the header
	 * only needs a forward-declare of TriggerRuntime. Returns false
	 * when no runtime is registered (non-scenario runs).
	 */
	bool is_objective_finished() const;

	/**
	 * P4.3: no per-tick objective call. Mission 1 victory is fully
	 * event-driven through the TriggerRuntime (DeathEventHandler ->
	 * game.removal -> TriggerRuntime observer -> trigger.evaluate ->
	 * DeclareVictory). The manual-time driver only needs
	 * reach_time(); the chain dispatches within a single batch.
	 */

	/**
	 * Get the root directory of the simulation executable.
	 */
	const util::Path &get_root_dir();

	/**
	 * Get this simulation's cvar manager.
	 *
	 * @return CVarManager instance.
	 */
	const std::shared_ptr<cvar::CVarManager> get_cvar_manager();

	/**
	 * Get the game running in the simulation.
	 *
	 * @return Game instance.
	 */
	const std::shared_ptr<gamestate::Game> get_game();

	/**
	 * Get the event loop for the gamestate.
	 *
	 * @return Event loop.
	 */
	const std::shared_ptr<openage::event::EventLoop> get_event_loop();

	/**
	 * Get the event entity for spawing game entities.
	 *
	 * TODO: Move somewhere else or remove.
	 *
	 * @return Spawner for entity creation.
	 */
	const std::shared_ptr<gamestate::event::Spawner> get_spawner();

	/**
	 * Get the event entity for sending commands.
	 *
	 * TODO: Move somewhere else or remove.
	 *
	 * @return Commander for sending commands.
	 */
	const std::shared_ptr<gamestate::event::Commander> get_commander();

	/**
	 * P4.4A: get the data-driven trigger runtime (MissionState +
	 * trigger instances). Used by the ViewModel layer
	 * (MissionView, future SelectionView) to read the single
	 * source of truth for objective lifecycle and victory.
	 */
	const std::shared_ptr<scenario::trigger::TriggerRuntime> &
	get_trigger_runtime() const {
		return this->trigger_runtime;
	}

	/**
	 * P4.4A: register an observer on the production
	 * RemovalEventHandler. The SelectionModel uses this to
	 * fire EntityRemoved and clear selection on death. The
	 * observer receives the removed entity id after the
	 * GameState has removed it.
	 */
	void add_removal_observer(gamestate::event::RemovalEventHandler::observer_t cb) {
		if (this->removal_handler) {
			this->removal_handler->add_observer(std::move(cb));
		}
	}

	/**
	 * Get the entity factory. Exposed so tests can spawn additional
	 * entities (e.g. a bystander movable for victory-semantics coverage)
	 * through the same init_components path as the scenario loader.
	 *
	 * @return Entity factory.
	 */
	const std::shared_ptr<gamestate::EntityFactory> get_entity_factory();

	/**
	 * Attach a renderer to the simulation.
	 *
	 * @param factory Factory for creating render entities.
	 */
	void attach_renderer(const std::shared_ptr<renderer::RenderFactory> &render_factory);

	/**
	 * Set the modpacks to load for a game.
	 *
	 * @param modpacks IDs of the modpacks to load.
	 */
	void set_modpacks(const std::vector<std::string> &modpacks);

	/**
	 * current simulation state variable.
	 * to be set to false to stop the simulation loop.
	 *
	 * @deprecated Replaced by the `lifecycle` enum above. Kept as a
	 * plain bool for the `while(this->running)` loop predicate; flips
	 * to false once `stop()` is observed.
	 */
	bool running;

private:
	/**
	 * Initialize event handlers.
	 */
	void init_event_handlers();

	/**
	 * P4.3: register the TriggerRuntime as an observer on the
	 * production RemovalEventHandler so entity removals drive
	 * trigger evaluation. Split from init_event_handlers() because
	 * the runtime is created after the scenario FQON is resolved
	 * (it needs the GameState). A null runtime or removal handler
	 * is a no-op.
	 */
	void register_trigger_handler();

	/**
	 * P4.3: feed the Mission Pack's trigger data into the runtime.
	 * Reads pack.triggers (data-driven; empty for the Mission 1
	 * reconstruction until Path C unblocks) and synthesizes a
	 * defeat_enemy trigger from pack.victory.required_removals so
	 * Mission 1 victory is data-driven even though the dump carries
	 * zero TriggerDefinition records (pinned by test_p1_dump.py).
	 */
	void wire_trigger_data(const std::string &mission_pack_dir);

	/**
	 * Lifecycle state machine. Only modified under `this->mutex`; read
	 * from the controller / diagnostic code paths.
	 */
	SimulationLifecycle lifecycle = SimulationLifecycle::Uninitialized;

	/**
	 * The simulation root directory.
	 * Uses the openage fslike path abstraction that can mount paths into one.
	 *
	 * This means that this path does simultaneously lead to global assets,
	 * home-folder-assets, settings, and basically the whole filesystem access.
	 *
	 * TODO: move this to a settings class, which then also hosts cvar and the options system.
	 */
	util::Path root_dir;

	/**
	 * the simulation's cvar manager.
	 */
	std::shared_ptr<cvar::CVarManager> cvar_manager;

	/**
	 * Time loop for getting the current simulation time and changing speed.
	 */
	std::shared_ptr<openage::time::TimeLoop> time_loop;

	/**
	 * Event loop for processing events in the game.
	 */
	std::shared_ptr<openage::event::EventLoop> event_loop;

	/**
	 * Factory for creating game entities.
	 */
	std::shared_ptr<gamestate::EntityFactory> entity_factory;

	/**
	 * Factory for creating terrain.
	 */
	std::shared_ptr<gamestate::TerrainFactory> terrain_factory;

	/**
	 * Mod manager.
	 */
	std::shared_ptr<assets::ModManager> mod_manager;

public:
	/**
	 * Returns the mod manager. Used by Phase C5 diagnostic call sites
	 * (engine ctor / loop / start entry) to snapshot the manager's
	 * authoritative state without copying it. The accessor is intentional
	 * to avoid changing the manager's ownership story during diagnosis.
	 */
	const std::shared_ptr<assets::ModManager> &get_mod_manager() const {
		return this->mod_manager;
	}

private:

	// TODO: move somewhere sensible or remove
	std::shared_ptr<gamestate::event::Spawner> spawner;
	std::shared_ptr<gamestate::event::Commander> commander;

	// TODO: The game run by the engine
	std::shared_ptr<gamestate::Game> game;

	/**
	 * PlayerSetup FQON for the scenario slot. Empty means the simulation
	 * starts without any civ patches / starting resources (debug slot).
	 *
	 * Populated from the CLI / Engine; consumed by start() when
	 * constructing the initial PlayerDescriptor.
	 */
	std::string scenario_player_setup_fqon;

	/**
	 * Scenario FQON for the active scenario. Empty means the simulation falls back
	 * to the default procedural terrain (B3 will plumb this into Game).
	 */
	std::string scenario_fqon;

	/**
	 * P4.3: the data-driven trigger runtime. Owns the MissionState
	 * (objective lifecycle + victory state) and the per-load trigger
	 * instances. Constructed in initialize() after the Game (and its
	 * GameState) exist; the runtime registers its own event handlers
	 * (trigger.evaluate, set_objective_state, activate_trigger,
	 * declare_victory) on the event loop.
	 */
	std::shared_ptr<scenario::trigger::TriggerRuntime> trigger_runtime;

	/**
	 * P4.1/P4.3: handle to the production RemovalEventHandler so we
	 * can register the TriggerRuntime as an observer AFTER the
	 * runtime is created (the event loop supports one handler per
	 * event name, so the observer pattern is the only sane extension
	 * point). Set in init_event_handlers(); used by
	 * register_trigger_handler().
	 */
	std::shared_ptr<gamestate::event::RemovalEventHandler> removal_handler;

	/**
	 * Mutex for thread-safe access to the simulation.
	 */
	std::shared_mutex mutex;
};

} // namespace gamestate
} // namespace openage
