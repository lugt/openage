// Copyright 2026 the openage authors. See copying.md for legal info.

#include "gamestate/demo/hud_adapter_demo.h"

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
#include "presenter/hud_qml_adapter.h"
#include "presenter/viewmodel/selection_model.h"
#include "time/time.h"
#include "time/time_loop.h"
#include "util/path.h"

#include <memory>
#include <stdexcept>
#include <string>


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


void simulation_demo_24(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 24: P4.4A HudQmlAdapter headless contract ===");

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
		throw std::runtime_error{"demo 24: missing placed entity"};
	}

	presenter::viewmodel::SelectionModel model{simulation};
	presenter::HudQmlAdapter adapter;
	adapter.attach(model, *simulation);
	adapter.refresh_economy(0);
	if (adapter.population() != 1 or adapter.population_capacity() != 5
	    or not adapter.is_interactive(25, 25, 1280, 720)) {
		throw std::runtime_error{"demo 24: economy bar did not expose the starting population or block map input"};
	}

	// ---- Pin 1: initial snapshot -- nothing selected ----
	if (adapter.selected_entity_id() != -1) {
		throw std::runtime_error{
			"demo 24 pin 1 FAILED: initial selectedEntityId="
			+ std::to_string(adapter.selected_entity_id())
		};
	}
	if (adapter.mission_lifecycle() != "RUNNING") {
		throw std::runtime_error{
			"demo 24 pin 1 FAILED: initial mission lifecycle="
			+ adapter.mission_lifecycle().toStdString()
		};
	}
	log::log(MSG(info) << "demo 24: pin 1 PASS (initial: no selection, mission RUNNING)");

	// ---- Pin 2: set_selected -> adapter mirrors selection ----
	model.set_selected(pv_id);
	if (adapter.selected_entity_id() != static_cast<qint64>(pv_id)) {
		throw std::runtime_error{
			"demo 24 pin 2 FAILED: selectedEntityId="
			+ std::to_string(adapter.selected_entity_id())
		};
	}
	if (adapter.selected_name() != "PlayerVillager") {
		throw std::runtime_error{
			"demo 24 pin 2 FAILED: selectedName="
			+ adapter.selected_name().toStdString()
		};
	}
	if (adapter.selected_hp_current() != 25 || adapter.selected_hp_max() != 25) {
		throw std::runtime_error{
			"demo 24 pin 2 FAILED: hp_cur=" + std::to_string(adapter.selected_hp_current())
			+ " hp_max=" + std::to_string(adapter.selected_hp_max())
		};
	}
	if (adapter.selected_attack_damage() != 3 || adapter.selected_attack_range() < 0.99
	    || adapter.selected_attack_range() > 1.01) {
		throw std::runtime_error{
			"demo 24 pin 2 FAILED: attack damage/range mismatch"
		};
	}
	log::log(MSG(info) << "demo 24: pin 2 PASS (selected mirrored: name=PlayerVillager "
	                   << "hp=25/25 atk=3 rng=1.0)");

	// ---- Pin 3: damage -> HealthChanged updates hp_current ----
	auto live = std::dynamic_pointer_cast<component::Live>(
		state->get_game_entity(pv_id)->get_component(
			component::component_t::LIVE));
	auto attack = std::dynamic_pointer_cast<component::Attack>(
		state->get_game_entity(pv_id)->get_component(
			component::component_t::ATTACK));
	live->set_attribute(openage::time::time_t{5},
		attack->get_health_attribute_fqon(), 15);
	if (adapter.selected_hp_current() != 15) {
		throw std::runtime_error{
			"demo 24 pin 3 FAILED: hp_current="
			+ std::to_string(adapter.selected_hp_current()) + " (expected 15)"
		};
	}
	log::log(MSG(info) << "demo 24: pin 3 PASS (HealthChanged mirrored, hp=15)");

	// ---- Pin 4: remove villager -> selection cleared ----
	remove_via_event(loop, state, commander, pv_id, openage::time::time_t{10});
	loop->reach_time(openage::time::time_t{10}, state);
	state->update_player_economies(openage::time::time_t{10});
	adapter.refresh_economy(0);
	if (adapter.population() != 0 or adapter.population_capacity() != 5) {
		throw std::runtime_error{"demo 24: population did not decrease after unit removal"};
	}
	if (adapter.selected_entity_id() != -1) {
		throw std::runtime_error{
			"demo 24 pin 4 FAILED: selectedEntityId not cleared after death"
		};
	}
	if (adapter.selected_is_alive()) {
		throw std::runtime_error{
			"demo 24 pin 4 FAILED: selectedIsAlive still true after death"
		};
	}
	log::log(MSG(info) << "demo 24: pin 4 PASS (selection cleared on death; isAlive=false)");

	// ---- Pin 5: remove enemy -> mission victory ----
	remove_via_event(loop, state, commander, ev_id, openage::time::time_t{20});
	loop->reach_time(openage::time::time_t{20}, state);
	loop->reach_time(openage::time::time_t{20}, state);
	if (!adapter.mission_is_victory()) {
		throw std::runtime_error{
			"demo 24 pin 5 FAILED: missionIsVictory=false after enemy removal"
		};
	}
	if (adapter.mission_lifecycle() != "VICTORY") {
		throw std::runtime_error{
			"demo 24 pin 5 FAILED: missionLifecycle="
			+ adapter.mission_lifecycle().toStdString()
		};
	}
	if (adapter.mission_defeat_enemy_state() != "COMPLETED") {
		throw std::runtime_error{"demo 24 pin 5 FAILED: objective not complete after enemy removal"};
	}
	if (adapter.mission_victory_reason() != "enemy_removed") {
		throw std::runtime_error{
			"demo 24 pin 5 FAILED: missionVictoryReason="
			+ adapter.mission_victory_reason().toStdString()
		};
	}
	log::log(MSG(info) << "demo 24: pin 5 PASS (mission VICTORY reason='"
	                   << adapter.mission_victory_reason().toStdString() << "')");

	log::log(MSG(info) << "DEMO 24: PASS (HudQmlAdapter Q_PROPERTY contract)");
}


} // namespace openage::gamestate::tests
