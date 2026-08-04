// Copyright 2026 the openage authors. See copying.md for legal info.

#include "gamestate/demo/selection_events_demo.h"

#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "event/evententity.h"
#include "event/eventhandler.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/types.h"
#include "gamestate/event/send_command.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "presenter/viewmodel/selection_model.h"
#include "time/time.h"
#include "time/time_loop.h"
#include "util/path.h"

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>


namespace openage::gamestate::tests {


namespace {

constexpr const char *PLAYER_VILLAGER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.PlayerVillager";
constexpr const char *ENEMY_VILLAGER_FQON =
	"ww_mvp.campaign.william_wallace.mission_01.EnemyVillager";


entity_id_t find_named(
	const std::shared_ptr<GameState> &state, const std::string &fqon) {
	auto it = state->get_named_entities().find(fqon);
	if (it == state->get_named_entities().end()) {
		return static_cast<entity_id_t>(-1);
	}
	return it->second;
}


// Drive the production removal chain: create a game.removal event
// for the given entity. Mirrors demo 20's remove_via_event.
void remove_via_event(
	const std::shared_ptr<openage::event::EventLoop> &loop,
	const std::shared_ptr<GameState> &state,
	const std::shared_ptr<openage::event::EventEntity> &commander,
	entity_id_t id, openage::time::time_t at) {
	openage::event::EventHandler::param_map::map_t params{
		{"target_id", id},
		{"time", at},
	};
	loop->create_event("game.removal", commander, state, at, params);
}


}  // namespace


void simulation_demo_23(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 23: P4.4A SelectionModel event wiring ===");

	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<time::TimeLoop>();
	auto simulation = std::make_shared<GameSimulation>(
		path, cvar, time_loop,
		"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup",
		"ww_mvp.campaign.william_wallace.mission_01.Mission");
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();

	auto game = simulation->get_game();
	auto state = game->get_state();
	auto loop = simulation->get_event_loop();
	auto commander = simulation->get_commander();

	entity_id_t pv_id = find_named(state, PLAYER_VILLAGER_FQON);
	entity_id_t ev_id = find_named(state, ENEMY_VILLAGER_FQON);
	if (pv_id == static_cast<entity_id_t>(-1)
	    || ev_id == static_cast<entity_id_t>(-1)) {
		throw std::runtime_error{"demo 23: missing placed entity"};
	}

	// --- Counters per event ---
	int sel_changes = 0;
	std::optional<entity_id_t> last_sel_new;
	std::optional<entity_id_t> last_sel_old;
	int health_changes = 0;
	int64_t last_health_value = -1;
	entity_id_t last_health_id = 0;
	int removals = 0;
	std::vector<entity_id_t> removed_ids;
	int mission_changes = 0;
	std::string last_mission_reason;

	presenter::viewmodel::SelectionModel model{simulation};
	model.subscribe_selection_changed(
		[&](presenter::viewmodel::SelectionSnapshot n,
		    presenter::viewmodel::SelectionSnapshot o) {
			sel_changes += 1;
			last_sel_new = n.primary;
			last_sel_old = o.primary;
		});
	model.subscribe_health_changed(
		[&](entity_id_t id, int64_t v) {
			health_changes += 1;
			last_health_value = v;
			last_health_id = id;
		});
	model.subscribe_entity_removed(
		[&](entity_id_t id) {
			removals += 1;
			removed_ids.push_back(id);
		});
	model.subscribe_mission_state_changed(
		[&](const std::string &reason) {
			mission_changes += 1;
			last_mission_reason = reason;
		});

	// ---- Pin 1: set_selected fires SelectionChanged ----------------
	model.set_selected(pv_id);
	if (sel_changes != 1) {
		throw std::runtime_error{
			"demo 23 pin 1 FAILED: sel_changes="
			+ std::to_string(sel_changes) + " (expected 1)"
		};
	}
	if (last_sel_new != pv_id || last_sel_old != std::nullopt) {
		throw std::runtime_error{
			"demo 23 pin 1 FAILED: new/old mismatch"
		};
	}
	log::log(MSG(info) << "demo 23: pin 1 PASS (SelectionChanged fired on set_selected)");

	// ---- Pin 2: damaging the selected entity fires HealthChanged -----
	auto live = std::dynamic_pointer_cast<component::Live>(
		state->get_game_entity(pv_id)->get_component(
			component::component_t::LIVE));
	auto attack = std::dynamic_pointer_cast<component::Attack>(
		state->get_game_entity(pv_id)->get_component(
			component::component_t::ATTACK));
	if (not live || not attack) {
		throw std::runtime_error{
			"demo 23 pin 2 FAILED: villager missing Live/Attack"
		};
	}
	auto health_fqon = attack->get_health_attribute_fqon();
	live->set_attribute(openage::time::time_t{5}, health_fqon, 20);
	if (health_changes != 1 || last_health_value != 20
	    || last_health_id != pv_id) {
		throw std::runtime_error{
			"demo 23 pin 2 FAILED: health_changes="
			+ std::to_string(health_changes)
			+ " last_value=" + std::to_string(last_health_value)
			+ " last_id=" + std::to_string(last_health_id)
		};
	}
	log::log(MSG(info) << "demo 23: pin 2 PASS (HealthChanged fired with new value=20)");

	// ---- Pin 3: removing the selected entity clears selection --------
	remove_via_event(loop, state, commander, pv_id, openage::time::time_t{10});
	loop->reach_time(openage::time::time_t{10}, state);
	if (sel_changes != 2) {
		throw std::runtime_error{
			"demo 23 pin 3 FAILED: sel_changes="
			+ std::to_string(sel_changes) + " (expected 2 after removal clear)"
		};
	}
	if (last_sel_new != std::nullopt || last_sel_old != pv_id) {
		throw std::runtime_error{
			"demo 23 pin 3 FAILED: removal did not clear selection "
			"(new=" + std::to_string(last_sel_new.value_or(-1))
			+ " old=" + std::to_string(last_sel_old.value_or(-1)) + ")"
		};
	}
	if (model.selected_entity_id().has_value()) {
		throw std::runtime_error{
			"demo 23 pin 3 FAILED: selected_entity_id still set after removal"
		};
	}
	log::log(MSG(info) << "demo 23: pin 3 PASS (selection cleared on death; "
	                   << "SelectionChanged(new=nullopt, old=" << pv_id << "))");

	// ---- Pin 4: EntityRemoved fired for every removal ----------------
	if (removals < 1) {
		throw std::runtime_error{
			"demo 23 pin 4 FAILED: removals="
			+ std::to_string(removals) + " (expected >= 1)"
		};
	}
	bool found_pv = false;
	for (auto id : removed_ids) {
		if (id == pv_id) found_pv = true;
	}
	if (not found_pv) {
		throw std::runtime_error{
			"demo 23 pin 4 FAILED: villager id not in removed_ids"
		};
	}
	log::log(MSG(info) << "demo 23: pin 4 PASS (EntityRemoved fired for villager, "
	                   << "total removals=" << removals << ")");

	// ---- Pin 5: removing the Enemy fires MissionStateChanged ---------
	// The defeat_enemy trigger's notify_entity_removed on Enemy
	// schedules trigger.evaluate; the trigger fires DeclareVictory;
	// the runtime's declare_victory handler invokes the
	// MissionState callback -> SelectionModel fires
	// MissionStateChanged. Wait long enough for the chain to settle.
	remove_via_event(loop, state, commander, ev_id, openage::time::time_t{20});
	loop->reach_time(openage::time::time_t{20}, state);
	// A second reach_time may be needed for the trigger.evaluate
	// event scheduled by notify_entity_removed (do-while loop in
	// the event loop) to settle the runtime's declare_victory.
	loop->reach_time(openage::time::time_t{20}, state);
	if (mission_changes != 1) {
		throw std::runtime_error{
			"demo 23 pin 5 FAILED: mission_changes="
			+ std::to_string(mission_changes) + " (expected 1)"
		};
	}
	if (last_mission_reason.empty()) {
		throw std::runtime_error{
			"demo 23 pin 5 FAILED: empty mission reason"
		};
	}
	log::log(MSG(info) << "demo 23: pin 5 PASS (MissionStateChanged fired, "
	                   << "reason='" << last_mission_reason << "')");

	log::log(MSG(info) << "DEMO 23: PASS (SelectionModel event wiring)");
}


} // namespace openage::gamestate::tests