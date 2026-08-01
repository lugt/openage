// Copyright 2013-2024 the openage authors. See copying.md for legal info.

#include "simulation.h"

#include "assets/mod_manager.h"
#include "event/event_loop.h"
#include "gamestate/entity_factory.h"
#include "gamestate/event/drag_select.h"
#include "gamestate/event/attack.h"
#include "gamestate/event/apply_discrete_effect.h"
#include "gamestate/event/death.h"
#include "gamestate/event/removal.h"
#include "gamestate/event/process_command.h"
#include "gamestate/event/send_command.h"
#include "gamestate/event/spawn_entity.h"
#include "gamestate/event/wait.h"
#include "gamestate/terrain_factory.h"
#include "time/clock.h"
#include "time/time_loop.h"

// TODO
#include "gamestate/game.h"
#include "gamestate/game_state.h"
#include "gamestate/objective/objective_controller.h"

namespace openage::gamestate {

GameSimulation::GameSimulation(const util::Path &root_dir,
                               const std::shared_ptr<cvar::CVarManager> &cvar_manager,
                               const std::shared_ptr<openage::time::TimeLoop> time_loop,
                               const std::string &scenario_player_setup_fqon,
                               const std::string &scenario_fqon) :
	running{false},
	root_dir{root_dir},
	cvar_manager{cvar_manager},
	time_loop{time_loop},
	event_loop{std::make_shared<openage::event::EventLoop>()},
	entity_factory{std::make_shared<gamestate::EntityFactory>()},
	terrain_factory{std::make_shared<gamestate::TerrainFactory>()},
	mod_manager{std::make_shared<assets::ModManager>(this->root_dir / "assets" / "converted")},
	spawner{std::make_shared<gamestate::event::Spawner>(this->event_loop)},
	commander{std::make_shared<gamestate::event::Commander>(this->event_loop)},
	scenario_player_setup_fqon{scenario_player_setup_fqon},
	scenario_fqon{scenario_fqon} {
	auto mods = mod_manager->enumerate_modpacks(root_dir / "assets" / "converted");
	for (const auto &mod : mods) {
		this->mod_manager->register_modpack(mod);
	}

	log::log(MSG(info) << "Created game simulation");
}


// GameSimulation::run() is defined later in this file (Phase G1: the
// loop driver sits below the deprecated start()/stop() pair so the
// initialization lifecycle is visible at a glance from the top of the
// file).

std::shared_ptr<WilliamWallaceMission01ObjectiveController> GameSimulation::create_objective_controller() {
	const auto &state = this->game->get_state();

	// Suffix-match the scenario FQON against the 5 known mission names.
	// This way the simulation doesn't need a separate "mission id" CLI
	// flag - the scenario's name encodes the mission.
	auto pick = [&](const std::string &suffix, auto factory) -> std::shared_ptr<WilliamWallaceMission01ObjectiveController> {
		if (this->scenario_fqon.find(suffix) != std::string::npos) {
			return factory(state);
		}
		return nullptr;
	};

	if (auto t = pick(".mission_01.Mission", &create_mission_1_trigger)) {
		return t;
	}
	if (auto t = pick(".mission_02.Mission", &create_mission_2_trigger)) {
		return t;
	}
	if (auto t = pick(".mission_03.Mission", &create_mission_3_trigger)) {
		return t;
	}
	if (auto t = pick(".mission_04.Mission", &create_mission_4_trigger)) {
		return t;
	}
	if (auto t = pick(".mission_05.Mission", &create_mission_5_trigger)) {
		return t;
	}

	// Default: William Wallace Mission 1 MVP. Lets the user run `--scenario` with whatever
	// scenario FQON they have and still get a working (if simplistic)
	// trigger, instead of a silent no-op.
	log::log(MSG(info) << "create_objective_controller: no mission suffix matched; "
	                   << "falling back to Mission 1");
	return create_mission_1_trigger(state);
}


const char *to_string(SimulationLifecycle s) {
	switch (s) {
	case SimulationLifecycle::Uninitialized: return "Uninitialized";
	case SimulationLifecycle::Initialized:   return "Initialized";
	case SimulationLifecycle::Running:      return "Running";
	case SimulationLifecycle::Stopped:      return "Stopped";
	case SimulationLifecycle::Failed:       return "Failed";
	}
	return "<unknown SimulationLifecycle>";
}

bool GameSimulation::initialize() {
	std::unique_lock lock{this->mutex};

	if (this->lifecycle != SimulationLifecycle::Uninitialized) {
		auto label = to_string(this->lifecycle);
		log::log(MSG(err) << "GameSimulation::initialize refused: lifecycle="
		                  << label << " (expected Uninitialized)");
		throw Error{ERR
		           << "GameSimulation::initialize called twice (current='"
		           << label
		           << "'). Phase G1 forbids silent re-entry: a successful "
		           << "initialize sets the runtime to Initialized; any further "
		           << "initialize call is a hard lifecycle error."};
	}

	log::log(MSG(info) << "GameSimulation::initialize: phase begin "
	                       "(modpacks activated, runtime frozen before any "
	                       "worker is spawned)");

	this->lifecycle = SimulationLifecycle::Initialized; // optimistic
	try {
		this->init_event_handlers();

		// Build the initial player list. When scenario mode provides a
		// PlayerSetup FQON, use it as the slot's civ; otherwise fall back
		// to the debug/observer slot (empty FQON -> no civ patches, no
		// starting resources).
		nyan::fqon_t player_setup_fqon = this->scenario_player_setup_fqon.empty()
			? nyan::fqon_t{""}
			: nyan::fqon_t{this->scenario_player_setup_fqon};
		std::vector<gamestate::PlayerDescriptor> players{
			gamestate::PlayerDescriptor{player_setup_fqon},
		};

		nyan::fqon_t scenario_fqon = this->scenario_fqon.empty()
			? nyan::fqon_t{""}
			: nyan::fqon_t{this->scenario_fqon};

		// P2.7 production path: derive the Mission Pack directory from
		// the scenario FQON. The Mission Pack is written by
		// openage.scenario.writer.write_mission_pack alongside the
		// nyan mission. The mapping is mechanical:
		//   "ww_mvp.campaign.william_wallace.mission_01.Mission"
		//     -> <assets/converted>/ww_mvp/campaign/william_wallace/
		// i.e. everything before ".mission_NN.Mission" is the directory
		// suffix. The derivation is best-effort: if no mission.json
		// exists at the derived path, Game::load_scenario falls back
		// to the legacy nyan-only path (and warns).
		std::string mission_pack_dir;
		if (!this->scenario_fqon.empty()) {
			// Drop the trailing ".<suffix>.Mission" portion.
			auto mission_pos = this->scenario_fqon.find(".mission_");
			if (mission_pos != std::string::npos) {
				std::string sub = this->scenario_fqon.substr(0, mission_pos);
				// FQON uses '.' as a path separator; replace with '/'
				// so the modpack directory layout is honoured.
				for (auto &c : sub) {
					if (c == '.') c = '/';
				}
				// <root_dir>/assets/converted/<sub>/
				mission_pack_dir =
					(this->root_dir / "assets" / "converted" / sub).resolve_native_path();
			}
		}

		this->game = std::make_shared<gamestate::Game>(
			event_loop,
			this->mod_manager,
			this->entity_factory,
			this->terrain_factory,
			players,
			scenario_fqon,
			mission_pack_dir);

		if (not this->scenario_player_setup_fqon.empty() or
		    not this->scenario_fqon.empty()) {
			this->objective_controller = this->create_objective_controller();
			// P4.1: register the ObjectiveRemovalHandler after the
			// controller is created. The handler subscribes to
			// game.removal and forwards notifications to the
			// controller, replacing the per-batch polling that
			// lived here as H0.5 temporary debt.
			this->register_objective_handler();
		}
	}
	catch (...) {
		this->lifecycle = SimulationLifecycle::Failed;
		log::log(MSG(err) << "GameSimulation::initialize: phase threw; "
		                  << "lifecycle=Failed; runtime must NOT be used");
		throw;
	}

	log::log(MSG(info) << "GameSimulation::initialize: phase complete "
	                       "(game + objective ready; lifecycle=Initialized)");
	return true;
}

void GameSimulation::start() {
	std::unique_lock lock{this->mutex};

	// Phase G1: `start()` no longer performs initialization. Its single
	// responsibility is to transition Initialized -> Running. Callers
	// that previously relied on `start()` to do work must call
	// `initialize()` first (typically the Engine owns the order).
	if (this->lifecycle != SimulationLifecycle::Initialized) {
		auto label = to_string(this->lifecycle);
		log::log(MSG(err) << "GameSimulation::start refused: lifecycle="
		                  << label << " (expected Initialized)");
		throw Error{ERR
		           << "GameSimulation::start requires lifecycle='Initialized'; "
		           << "current='"
		           << label
		           << "'. Call initialize() first. Phase G1 explicitly "
		           << "disallows the old initialize-in-start() semantics so a "
		           << "double-start or a start-before-initialize regression "
		           << "surfaces as a hard error rather than a silent no-op."};
	}

	this->lifecycle = SimulationLifecycle::Running;
	this->running = true;
}

void GameSimulation::run() {
	if (this->lifecycle != SimulationLifecycle::Initialized) {
		auto label = to_string(this->lifecycle);
		log::log(MSG(err) << "GameSimulation::run refused: lifecycle="
		                  << label << " (expected Initialized before first "
		                  << "run, then a one-shot Running transition)");
		throw Error{ERR
		           << "GameSimulation::run requires lifecycle='Initialized'; "
		           << "current='"
		           << label
		           << "'. Phase G1 split initialize() and run() so a run() "
		           << "issued before initialize() surfaces as a hard error "
		           << "instead of trying to bootstrap internally."};
	}

	// One-shot Running transition. This is where the loop driver lives.
	this->start();

	time::time_t last_logged_at = time::time_t{0};
	while (this->lifecycle == SimulationLifecycle::Running and this->running) {
		time::time_t current_time = this->time_loop->get_clock()->get_time();
		this->event_loop->reach_time(current_time, this->game->get_state());

		// P4.1: removed tick_objective_controller(current_time).
		// The objective controller is now event-driven via
		// gamestate::event::ObjectiveRemovalHandler (subscribed to
		// game.removal). The on_update() path is retained for the
		// not-yet-event-driven missions (2-5) but does nothing for
		// the Mission 1 defeat_enemy step.

		if (current_time - last_logged_at >= time::time_t{5000}) {
			log::log(DBG << "GameSimulation::run: simulation time advanced to "
			              << current_time
			              << " (lifecycle=Running)");
			last_logged_at = current_time;
		}
	}

	this->lifecycle = SimulationLifecycle::Stopped;
	log::log(MSG(info) << "GameSimulation::run: loop exited "
	                       "(lifecycle=Stopped)");
}


void GameSimulation::stop() {
	std::unique_lock lock{this->mutex};

	this->running = false;

	// Best-effort lifecycle transition. The loop predicate is `running`,
	// so we set it false to break the loop; if the loop is no longer in
	// Running (e.g. we already finished or never initialized) we stay in
	// whatever state we were in rather than falsely stamping Stopped.
	if (this->lifecycle == SimulationLifecycle::Running) {
		this->lifecycle = SimulationLifecycle::Stopped;
	}

	log::log(MSG(info) << "GameSimulation::stop: running=false "
	                       "(lifecycle=" << to_string(this->lifecycle) << ")");
}


const util::Path &GameSimulation::get_root_dir() {
	std::shared_lock lock{this->mutex};

	return this->root_dir;
}

const std::shared_ptr<cvar::CVarManager> GameSimulation::get_cvar_manager() {
	std::shared_lock lock{this->mutex};

	return this->cvar_manager;
}

const std::shared_ptr<gamestate::Game> GameSimulation::get_game() {
	std::shared_lock lock{this->mutex};

	return this->game;
}

const std::shared_ptr<openage::event::EventLoop> GameSimulation::get_event_loop() {
	std::shared_lock lock{this->mutex};

	return this->event_loop;
}

const std::shared_ptr<gamestate::event::Spawner> GameSimulation::get_spawner() {
	std::shared_lock lock{this->mutex};

	return this->spawner;
}

const std::shared_ptr<gamestate::event::Commander> GameSimulation::get_commander() {
	std::shared_lock lock{this->mutex};

	return this->commander;
}

const std::shared_ptr<gamestate::EntityFactory> GameSimulation::get_entity_factory() {
	std::shared_lock lock{this->mutex};

	return this->entity_factory;
}

void GameSimulation::attach_renderer(const std::shared_ptr<renderer::RenderFactory> &render_factory) {
	std::unique_lock lock{this->mutex};

	this->game->attach_renderer(render_factory);
	this->entity_factory->attach_renderer(render_factory);
	this->terrain_factory->attach_renderer(render_factory);
}

void GameSimulation::set_modpacks(const std::vector<std::string> &modpacks) {
	std::unique_lock lock{this->mutex};

	std::vector<std::string> mods{"engine"};
	mods.insert(mods.end(), modpacks.begin(), modpacks.end());

	this->mod_manager->activate_modpacks(mods);

	// TODO: Prevent setting modpacks if a game is already running
}

void GameSimulation::init_event_handlers() {
	auto drag_select_handler = std::make_shared<gamestate::event::DragSelectHandler>();
	auto spawn_handler = std::make_shared<gamestate::event::SpawnEntityHandler>(this->event_loop,
	                                                                            this->entity_factory);
	auto command_handler = std::make_shared<gamestate::event::SendCommandHandler>();
	auto manager_handler = std::make_shared<gamestate::event::ProcessCommandHandler>();
	auto attack_handler = std::make_shared<gamestate::event::AttackHandler>();
	auto apply_discrete_effect_handler = std::make_shared<gamestate::event::ApplyDiscreteEffectHandler>();
	auto death_handler = std::make_shared<gamestate::event::DeathEventHandler>();
	this->removal_handler = std::make_shared<gamestate::event::RemovalEventHandler>();
	auto wait_handler = std::make_shared<gamestate::event::WaitHandler>();
	this->event_loop->add_event_handler(drag_select_handler);
	this->event_loop->add_event_handler(spawn_handler);
	this->event_loop->add_event_handler(command_handler);
	this->event_loop->add_event_handler(manager_handler);
	this->event_loop->add_event_handler(attack_handler);
	this->event_loop->add_event_handler(apply_discrete_effect_handler);
	this->event_loop->add_event_handler(death_handler);
	this->event_loop->add_event_handler(this->removal_handler);
	this->event_loop->add_event_handler(wait_handler);
}

void GameSimulation::register_objective_handler() {
	// P4.1: subscribe the mission-1 objective controller to
	// game.removal so completion is driven by the event handler chain
	// instead of per-batch polling. The handler is registered as an
	// observer on the production RemovalEventHandler (the openage
	// event loop supports one handler per event name, so the
	// observer pattern is the only sane extension point).
	//
	// This is split from init_event_handlers() because the
	// objective_controller is created later (after the scenario
	// FQON is resolved). A null controller is a no-op.
	if (not this->objective_controller) {
		return;
	}
	if (not this->removal_handler) {
		return;
	}
	this->removal_handler->add_observer(
		[this](openage::gamestate::entity_id_t target_id) {
			this->objective_controller->notify_entity_removed(target_id);
		});
}

} // namespace openage::gamestate
