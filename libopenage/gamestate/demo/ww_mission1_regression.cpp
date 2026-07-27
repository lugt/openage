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
// Phase H0.5 event-batch budget. We track EVENT BATCHES, not actual
// dispatched events (the EventLoop reach_time() return value is
// currently void; the implemented dispatcher returns a count but it
// is private to the loop). Phases H1/H2+ are planned to expose a
// real executed_event counter; until then we use event_batches as
// the explicit counter and let a spike in batches fail the test.
constexpr size_t WW_MISSION1_MAX_EVENT_BATCHES = 4096;

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

	// Phase H0.5 manual-time loop. We advance by scheduled event time,
	// not by a fixed `+1`. Each loop iteration:
	//   1. Peek next pending event time (no pop, no reorder).
	//   2. If no event is pending or its time exceeds the deadline, exit.
	//   3. reach_time(next_event_time) so the kernel can dispatch the
	//      batch of events scheduled at that exact time.
	//   4. Poll the objective controller exactly once per event batch
	//      (Phase D temporary compatibility hook; will be removed when
	//      the controller consumes a dedicated event).
	//   5. Check the stop conditions (enemy removed, objective finished).
	//
	// Counters tracked explicitly so a future regression that spikes
	// e.g. on event-batches visibly fails. We name them precisely:
	//   time_advances        -- number of peek_next_event_time() queries
	//   event_batches        -- number of reach_time() calls
	//   objective_poll_count -- number of tick_objective_controller() calls
	size_t time_advances = 0;
	size_t event_batches = 0;
	size_t objective_poll_count = 0;
	time::time_t final_dispatched_time = time::time_t::from_int(0);

	bool enemy_removed = false;
	bool objective_finished = false;
	while (event_batches < WW_MISSION1_MAX_EVENT_BATCHES) {
		// Step 1: peek next event time without consuming it.
		auto next_opt = event_loop->peek_next_event_time();
		++time_advances;
		if (not next_opt.has_value()) {
			// Queue exhausted -- no more events to dispatch.
			break;
		}
		time::time_t next = *next_opt;

		// Step 2: stop if the next event time would exceed the deadline.
		if (next > WW_MISSION1_DEADLINE) {
			break;
		}

		// Step 3: dispatch the event(s) scheduled at `next`.
		event_loop->reach_time(next, state);
		++event_batches;
		final_dispatched_time = next;

		// Step 4: Phase D TEMPORARY DEBT. The objective controller
		// currently polls the game state once per simulation step
		// rather than consuming an explicit MissionCompletedEvent.
		// Until Phase D rewires this, we replicate run()'s per-step
		// on_update here so headless tests can observe Victory. Phase D
		// event-driven objective controller will allow deletion of
		// tick_objective_controller / objective_poll_count.
		simulation->tick_objective_controller(next);
		++objective_poll_count;

		enemy_removed = not state->has_game_entity(enemy_id);
		objective_finished = simulation->is_objective_finished();
		if (enemy_removed or objective_finished) {
			break;
		}
	}

	// ---- Phase 4: stop-condition diagnostics + assertions.
	WW1_ASSERT(enemy_removed,
	          "EnemyVillager not removed before deadline "
	              "(time_advances=" << time_advances
	              << " event_batches=" << event_batches
	              << " objective_poll_count=" << objective_poll_count
	              << " last_dispatched_t=" << final_dispatched_time << ")");
	WW1_ASSERT(objective_finished,
	          "Mission 1 objective not finished (Victory not reached; "
	              "time_advances=" << time_advances
	              << " event_batches=" << event_batches
	              << " objective_poll_count=" << objective_poll_count
	              << " last_dispatched_t=" << final_dispatched_time << ")");

	if (assertion_failed) {
		log::log(MSG(err) << snapshot_failure_state(state, simulation, final_dispatched_time));
		log::log(MSG(err) << "WW MISSION 1 HEADLESS REGRESSION: FAIL");
		return;
	}

	log::log(MSG(info) << "WW MISSION 1 HEADLESS REGRESSION: PASS"
	                       " (enemy_removed=" << enemy_removed
	                       << " objective_finished=" << objective_finished
	                       << " time_advances=" << time_advances
	                       << " event_batches=" << event_batches
	                       << " objective_poll_count=" << objective_poll_count
	                       << " last_dispatched_t=" << final_dispatched_time << ")");
}

} // namespace openage::gamestate::tests
