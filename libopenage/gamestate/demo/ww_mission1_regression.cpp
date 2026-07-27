// Copyright 2026 the openage authors. See copying.md for legal info.
//
// William Wallace Mission 1 — Phase H0 minimal headless regression.
//
// Goal of this demo / regression harness:
//   1. Initialise the runtime exactly the way Engine::initialize_runtime()
//      does on the main thread, without spawning any worker.
//   2. Locate the three initial entities by their PlacedEntity instance
//      FQONs (PlayerVillager / PlayerTownCenter / EnemyVillager) --
//      "stable name" lookup, immune to spawn-order or std::unordered_map
//      iteration order.
//   3. Issue a single ATTACK via the existing send_command event path,
//      then advance the kernel explicitly through reach_time() calls
//      (NO fixed tick, NO sleep, NO wall-clock) until one of three
//      stop conditions is hit:
//          * enemy removed (HP drained to 0)
//          * objective controller reported finished
//          * deadline reached before either
//   4. Emit a stable, deterministically-ordered failure snapshot
//      whenever any of the three assertions fail. The snapshot never
//      contains pointers, thread IDs, or unordered-iteration artefacts.
//
// This is the minimum Phase D gate: prove the William Wallace
// Mission 1 scenario, once the runtime is initialised, can be
// driven to completion in headless mode using manual time.

#include "demo_0.h"  // for log::log convenience macros

#include <algorithm>
#include <sstream>
#include <vector>

#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/activity.h"
#include "gamestate/component/internal/commands/attack.h"
#include "gamestate/component/internal/commands/move.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/event/send_command.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/objective/objective_controller.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "time/clock.h"
#include "time/time.h"
#include "time/time_loop.h"


namespace openage::gamestate::tests {

namespace {

// Stable FQONs declared by mission_01.nyan
constexpr const char *MISSION1_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.Mission";
constexpr const char *PLAYERSETUP_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup";
constexpr const char *PLAYERVILLAGER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerVillager";
constexpr const char *PLAYERTOWNCENTER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerTownCenter";
constexpr const char *ENEMYVILLAGER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.EnemyVillager";

// Phase H0 deadline: if Mission 1 cannot be won within 5 simulated
// minutes we have a regression against the legacy attack pipeline.
constexpr time::time_t WW_MISSION1_DEADLINE = time::time_t::from_int(300);
// Phase H0 max-event budget: 4096 events catches anything that
// would otherwise busy-loop in the kernel (e.g. when the legacy
// AttackHandler re-schedules itself faster than the resolution of
// the simulation time unit).
constexpr size_t WW_MISSION1_EVENT_BUDGET = 4096;

bool assertion_failed = false;
std::ostringstream failure_log;

#define WW1_ASSERT(cond, msg)                              \
	do {                                                   \
		if (not(cond)) {                                   \
			assertion_failed = true;                       \
			failure_log << "ASSERT FAIL: " << msg          \
			            << " (at " << __FILE__ << ":"     \
			            << __LINE__ << ")\n";              \
			log::log(MSG(err) << "WW1: " << msg);          \
		}                                                  \
	} while (0)

// Phase H0 minimal snapshot: stable, sorted, pointer-free. We avoid
// pulling component internals into the test until the
// component-level accessors (Live::get_attribute_value /
// Activity::get_current_state) stabilize. The minimum a reviewer
// needs to triage a regression is: time, objective state, the set
// of registered entity stable names, and the failure reasons.
std::string snapshot_failure_state(
	const std::shared_ptr<GameState> &state,
	const std::shared_ptr<GameSimulation> &simulation,
	time::time_t now) {
	std::ostringstream out;
	out << "snapshot_failure_state {\n";
	out << "  simulation_time=" << now << "\n";
	out << "  objective_finished=" << simulation->is_objective_finished() << "\n";

	std::vector<std::string> names;
	for (const auto &kv : state->get_named_entities()) {
		names.push_back(kv.first);
	}
	std::sort(names.begin(), names.end());
	out << "  registered_entities=" << names.size() << " (sorted): ";
	for (size_t i = 0; i < names.size(); ++i) {
		if (i) out << ",";
		out << names[i];
	}
	out << "\n";
	out << "  failure_reasons=" << failure_log.str();
	out << "}\n";
	return out.str();
}

} // namespace

void simulation_demo_7(const util::Path &path) {
	log::log(MSG(info) << "========== WW MISSION 1 HEADLESS REGRESSION (demo 7) ==========");
	assertion_failed = false;
	failure_log.str(std::string{});
	failure_log.clear();

	// ---- Phase 1: initialise the runtime on the main thread, mirroring
	// Engine::initialize_runtime() so the test and the real launch
	// path share the same boundary. NO worker is spawned here.
	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<time::TimeLoop>();
	auto simulation = std::make_shared<GameSimulation>(
		path, cvar, time_loop, PLAYERSETUP_FQON, MISSION1_FQON);
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();
	simulation->start();

	auto game = simulation->get_game();
	auto state = game->get_state();
	auto event_loop = simulation->get_event_loop();
	auto commander = simulation->get_commander();

	// ---- Phase 2: assert the three initial entities are present and
	// resolve them by their PlacedEntity instance FQON.
	WW1_ASSERT(state->get_game_entities().size() == 3,
	          "Mission 1 must spawn exactly 3 entities (got "
	              << state->get_game_entities().size() << ")");

	auto opt_villager = state->get_entity_id_by_instance(PLAYERVILLAGER_FQON);
	auto opt_tc = state->get_entity_id_by_instance(PLAYERTOWNCENTER_FQON);
	auto opt_enemy = state->get_entity_id_by_instance(ENEMYVILLAGER_FQON);
	WW1_ASSERT(opt_villager.has_value(), "PlayerVillager instance not registered");
	WW1_ASSERT(opt_tc.has_value(), "PlayerTownCenter instance not registered");
	WW1_ASSERT(opt_enemy.has_value(), "EnemyVillager instance not registered");

	if (assertion_failed or not opt_villager.has_value()
	    or not opt_enemy.has_value()) {
		log::log(MSG(err) << snapshot_failure_state(state, simulation,
		                                              time_loop->get_clock()->get_time()));
		log::log(MSG(err) << "WW MISSION 1 HEADLESS REGRESSION: FAIL (phase 2)");
		return;
	}
	auto player_villager_id = *opt_villager;
	auto enemy_id = *opt_enemy;

	// ---- Phase 3: drive the manual-time loop. We issue one ATTACK
	// on PlayerVillager at t=0, then advance the simulation tick by
	// tick, dispatching any events whose scheduled time falls within
	// the tick.
	openage::event::EventHandler::param_map::map_t attack_params{
		{"type", component::command::command_t::ATTACK},
		{"target_id", enemy_id},
		{"entity_ids", std::vector<entity_id_t>{player_villager_id}},
	};
	event_loop->create_event("game.send_command", commander, state,
	                         time::time_t::from_int(0), attack_params);

	// Phase H0 manual-time loop. We step `current` one time-unit at a
	// time and call reach_time() each step. The Clock does not need a
	// public set_time -- EventLoop::reach_time itself drives event
	// dispatch relative to its schedule.
	time::time_t current = time::time_t::from_int(0);
	size_t events_used = 0;
	time::time_t stop_at = WW_MISSION1_DEADLINE;

	bool enemy_removed = false;
	bool objective_finished = false;
	while (events_used < WW_MISSION1_EVENT_BUDGET and current <= stop_at) {
		event_loop->reach_time(current, state);
		events_used += 1;

		enemy_removed = not state->has_game_entity(enemy_id);
		simulation->tick_objective_controller(current);
		objective_finished = simulation->is_objective_finished();
		if (enemy_removed or objective_finished) {
			break;
		}
		current = current + time::time_t::from_int(1);
	}

	// ---- Phase 4: stop-condition diagnostics + assertions.
	WW1_ASSERT(enemy_removed,
	          "EnemyVillager not removed before deadline (events_used="
	              << events_used << " t=" << current << ")");
	WW1_ASSERT(objective_finished,
	          "Mission 1 objective not finished (Victory not reached; events_used="
	              << events_used << " t=" << current << ")");

	if (assertion_failed) {
		log::log(MSG(err) << snapshot_failure_state(state, simulation, current));
		log::log(MSG(err) << "WW MISSION 1 HEADLESS REGRESSION: FAIL");
		return;
	}

	log::log(MSG(info) << "WW MISSION 1 HEADLESS REGRESSION: PASS"
	                       " (enemy_removed=" << enemy_removed
	                       << " objective_finished=" << objective_finished
	                       << " events_used=" << events_used
	                       << " t=" << current << ")");
}

} // namespace openage::gamestate::tests
