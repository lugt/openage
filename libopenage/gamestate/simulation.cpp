// Copyright 2013-2024 the openage authors. See copying.md for legal info.

#include "simulation.h"

#include "assets/mod_manager.h"
#include "event/event_loop.h"
#include "gamestate/entity_factory.h"
#include "gamestate/event/drag_select.h"
#include "gamestate/event/attack.h"
#include "gamestate/event/apply_discrete_effect.h"
#include "gamestate/event/death.h"
#include "gamestate/event/movement.h"
#include "gamestate/event/removal.h"
#include "gamestate/event/process_command.h"
#include "gamestate/event/send_command.h"
#include "gamestate/event/spawn_entity.h"
#include "gamestate/event/wait.h"
#include "gamestate/terrain_factory.h"
#include "pathfinding/group_move_handler.h"
#include "time/clock.h"
#include "time/time_loop.h"

// TODO
#include "gamestate/game.h"
#include "gamestate/game_state.h"
#include "gamestate/map.h"
#include "scenario/mission_pack.h"
#include "scenario/trigger/trigger_runtime.h"

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

// P4.3: synthesize the data-driven defeat_enemy trigger IR from the
// Mission Pack's VictoryDefinition.required_removals. The committed
// dump (docs/mvp/reports/mission1-scenario-dump/) is contractually
// zero-trigger -- test_p1_dump.py pins trigger_count==0 because the
// original scenario source is BLOCKED (Path D) -- so the trigger
// cannot live in scenario.json yet. Instead the production wiring
// translates required_removals (the committed data signal) into a
// MissionTriggerDefinition that the TriggerRuntime executes. When
// Path C unblocks the original scenario data and the dump carries
// TriggerDefinition records, pack.triggers is fed directly and this
// synthesis becomes a fallback.
//
// Mission 1 has a single required_removal ("EnemyVillager"), so one
// trigger declares victory on its destruction. A multi-removal
// victory condition is future work (would need an AND-aggregator).
gamestate::MissionTriggerDefinition make_defeat_enemy_trigger_ir(
	const std::string &required_removal) {

	gamestate::MissionTriggerDefinition def;
	def.trigger_id = "mission_01.defeat_" + required_removal;
	def.initially_enabled = true;
	def.looping = false;
	def.delay = 0.0;

	// Condition: the required-removal entity is destroyed. The
	// loader registers named entities under BOTH the full instance
	// FQON and the short scenario_object_id, so the short name from
	// required_removals resolves directly via
	// GameState::get_entity_id_by_instance.
	gamestate::MissionTriggerCondition cond;
	cond.kind = "EntityDestroyed";
	cond.target_scenario_object_id = required_removal;
	def.conditions.push_back(cond);

	// Effects (stable effect_index order):
	//   0: SetObjective(ACTIVE)  -- mark the objective active
	//   1: CompleteObjective     -- mark the objective completed
	//   2: DeclareVictory        -- MissionState -> VICTORY
	const std::string objective_id = "mission_01.defeat_enemy";
	gamestate::MissionTriggerEffect eff_active;
	eff_active.kind = "SetObjective";
	eff_active.target_scenario_object_id = objective_id;
	eff_active.target_area_id = "ACTIVE";
	def.effects.push_back(eff_active);

	gamestate::MissionTriggerEffect eff_complete;
	eff_complete.kind = "CompleteObjective";
	eff_complete.target_scenario_object_id = objective_id;
	def.effects.push_back(eff_complete);

	gamestate::MissionTriggerEffect eff_victory;
	eff_victory.kind = "DeclareVictory";
	eff_victory.target_scenario_object_id = "enemy_removed";
	def.effects.push_back(eff_victory);

	return def;
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
			// P4.3: construct the data-driven TriggerRuntime. The
			// runtime replaces the hand-rolled
			// WilliamWallaceMission01ObjectiveController for Mission 1:
			// the defeat_enemy trigger (EntityDestroyed +
			// SetObjective + CompleteObjective + DeclareVictory) is
			// synthesized from the Mission Pack's
			// VictoryDefinition.required_removals (the committed data
			// signal) and executed by the runtime. The runtime owns
			// the MissionState; is_objective_finished() reads its
			// victory state.
			this->trigger_runtime = std::make_shared<
				::openage::scenario::trigger::TriggerRuntime>(
				this->event_loop, this->game->get_state());
			this->trigger_runtime->register_handlers();
			this->wire_trigger_data(mission_pack_dir);
			this->register_trigger_handler();
		}

		// P4.4F Task 3: register the simulation-level removal
		// observer that clears DynamicOccupancy on entity death,
		// INDEPENDENT of SelectionModel. This runs unconditionally
		// (even in headless / non-scenario mode) so dead entities
		// never leave stale blockers in the occupancy table. The
		// observer closure looks up the map lazily at fire time;
		// if there is no map (defensive), it no-ops.
		this->register_dynamic_occupancy_removal_observer();
	}
	catch (...) {
		this->lifecycle = SimulationLifecycle::Failed;
		log::log(MSG(err) << "GameSimulation::initialize: phase threw; "
		                  << "lifecycle=Failed; runtime must NOT be used");
		throw;
	}

	log::log(MSG(info) << "GameSimulation::initialize: phase complete "
	                       "(game + trigger runtime ready; lifecycle=Initialized)");
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

		// P4.3: no per-tick objective work. Mission 1 victory is
		// event-driven: DeathEventHandler -> game.removal ->
		// RemovalEventHandler fires the TriggerRuntime observer ->
		// trigger.evaluate -> defeat_enemy trigger fires ->
		// DeclareVictory -> MissionState::VICTORY. reach_time() alone
		// dispatches the whole chain within the current batch.

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
	auto waypoint_handler = std::make_shared<gamestate::event::WaypointReachedHandler>();
	auto movement_arrived_handler = std::make_shared<gamestate::event::MovementArrivedHandler>();
	auto group_move_handler = std::make_shared<
		openage::gamestate::event::GroupMoveHandler>(
			std::shared_ptr<openage::gamestate::GameSimulation>{});
	this->event_loop->add_event_handler(drag_select_handler);
	this->event_loop->add_event_handler(spawn_handler);
	this->event_loop->add_event_handler(command_handler);
	this->event_loop->add_event_handler(manager_handler);
	this->event_loop->add_event_handler(attack_handler);
	this->event_loop->add_event_handler(apply_discrete_effect_handler);
	this->event_loop->add_event_handler(death_handler);
	this->event_loop->add_event_handler(this->removal_handler);
	this->event_loop->add_event_handler(wait_handler);
	this->event_loop->add_event_handler(waypoint_handler);
	this->event_loop->add_event_handler(movement_arrived_handler);
	this->event_loop->add_event_handler(group_move_handler);
}

void GameSimulation::wire_trigger_data(const std::string &mission_pack_dir) {
	// P4.3: feed the Mission Pack's trigger data into the runtime.
	// The pack was already read once inside Game::load_scenario (and
	// discarded); re-reading here is a one-time init cost that keeps
	// the change localized to GameSimulation (no Game/GameState header
	// changes to retain the pack). A read failure is logged and
	// downgraded to a triggerless runtime -- the simulation still
	// runs, it just never declares victory.
	if (not this->trigger_runtime or mission_pack_dir.empty()) {
		return;
	}
	gamestate::MissionPack pack;
	try {
		pack = gamestate::read_mission_pack(mission_pack_dir);
	}
	catch (const std::exception &e) {
		log::log(MSG(err) << "GameSimulation::wire_trigger_data: "
		                   << "could not re-read mission pack from '"
		                   << mission_pack_dir << "' (" << e.what()
		                   << "); trigger runtime will have no triggers");
		return;
	}

	// Data-driven triggers (empty for the Mission 1 reconstruction
	// until Path C unblocks the original scenario data).
	for (const auto &td : pack.triggers) {
		this->trigger_runtime->add_trigger(td);
	}

	// Fallback synthesis: translate the pack's
	// VictoryDefinition.required_removals into defeat_enemy triggers.
	// This is the path Mission 1 takes today (the dump carries zero
	// TriggerDefinition records but a non-empty required_removals
	// list). Each required_removal becomes one EntityDestroyed ->
	// SetObjective + CompleteObjective + DeclareVictory trigger.
	for (const auto &removal : pack.victory.required_removals) {
		this->trigger_runtime->add_trigger(
			make_defeat_enemy_trigger_ir(removal));
	}

	log::log(MSG(info) << "GameSimulation::wire_trigger_data: "
	                   << "triggers=" << pack.triggers.size()
	                   << " (data) + " << pack.victory.required_removals.size()
	                   << " (synthesized from required_removals)"
	                   << "; runtime trigger_count="
	                   << this->trigger_runtime->trigger_count());
}


void GameSimulation::register_trigger_handler() {
	// P4.3: subscribe the TriggerRuntime to game.removal so trigger
	// evaluation is driven by the event handler chain instead of
	// per-batch polling. The runtime's notify_entity_removed takes a
	// simulation time; the observer obtains it from
	// time_loop->get_clock()->get_time() (the same source the run()
	// loop uses for reach_time). The runtime stays a pure policy
	// object and never consults the clock itself. A null runtime or
	// removal handler is a no-op.
	if (not this->trigger_runtime) {
		return;
	}
	if (not this->removal_handler) {
		return;
	}
	this->removal_handler->add_observer(
		[this](openage::gamestate::entity_id_t target_id) {
			auto at = this->time_loop->get_clock()->get_time();
			this->trigger_runtime->notify_entity_removed(target_id, at);
		});
}


void GameSimulation::register_dynamic_occupancy_removal_observer() {
	// P4.4F Task 3: subscribe the Map's shared DynamicOccupancy to
	// game.removal so dead entities are cleared from the occupancy
	// table at the simulation level, INDEPENDENT of SelectionModel.
	// The SelectionModel also calls remove_entity (presenter-gated),
	// but headless / demo paths never construct a SelectionModel, so
	// without this observer dead entities would leave stale blockers
	// and the Move runtime would never let a new unit enter that
	// cell. Double-remove is safe (DynamicOccupancy::remove_entity
	// is idempotent -- no-op if the entity is not registered).
	//
	// The closure captures `this` and looks up the map lazily at
	// fire time (the map may be null in degenerate test paths; the
	// observer no-ops in that case). This mirrors the SelectionModel
	// observer's null-checked lookup chain.
	if (not this->removal_handler) {
		return;
	}
	this->removal_handler->add_observer(
		[this](openage::gamestate::entity_id_t removed_id) {
			if (not this->game) {
				return;
			}
			auto st = this->game->get_state();
			if (not st) {
				return;
			}
			auto m = st->get_map();
			if (not m) {
				return;
			}
			auto occ = m->get_dynamic_occupancy();
			if (not occ) {
				return;
			}
			occ->remove_entity(removed_id);
		});
}


bool GameSimulation::is_objective_finished() const {
	// P4.3: the runtime's MissionState is the single source of truth.
	return this->trigger_runtime
	       and this->trigger_runtime->mission_state().is_victory();
}

} // namespace openage::gamestate
