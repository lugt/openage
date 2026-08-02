// Copyright 2026 the openage authors. See copying.md for legal info.

#include "gamestate/demo/selected_view_demo.h"

#include "cvar/cvar.h"
#include "gamestate/game.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "presenter/viewmodel/mission_view.h"
#include "presenter/viewmodel/selected_entity_view.h"
#include "scenario/trigger/trigger_runtime.h"
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
	const std::shared_ptr<GameState> &state,
	const std::string &fqon) {
	auto it = state->get_named_entities().find(fqon);
	if (it == state->get_named_entities().end()) {
		return static_cast<entity_id_t>(-1);
	}
	return it->second;
}


}  // namespace


void simulation_demo_22(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 22: P4.4A SelectedEntityView + MissionView ===");

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

	entity_id_t pv_id = find_named(state, PLAYER_VILLAGER_FQON);
	entity_id_t ev_id = find_named(state, ENEMY_VILLAGER_FQON);
	if (pv_id == static_cast<entity_id_t>(-1)
	    || ev_id == static_cast<entity_id_t>(-1)) {
		throw std::runtime_error{"demo 22: missing placed entity"};
	}

	// ---- SelectedEntityView (PlayerVillager) ----
	presenter::viewmodel::SelectedEntityView sel{state, pv_id};
	if (sel.entity_id() != pv_id) {
		throw std::runtime_error{"demo 22: entity_id mismatch"};
	}
	if (sel.scenario_object_id().value_or("") != "PlayerVillager") {
		throw std::runtime_error{
			"demo 22 pin 1 FAILED: scenario_object_id="
			+ sel.scenario_object_id().value_or("(none)")
		};
	}
	log::log(MSG(info) << "demo 22: pin 1 PASS (scenario_object_id=PlayerVillager)");

	if (sel.owner_player_id() != 0) {
		throw std::runtime_error{
			"demo 22 pin 2 FAILED: owner_player_id="
			+ std::to_string(sel.owner_player_id())
		};
	}
	log::log(MSG(info) << "demo 22: pin 2 PASS (owner_player_id=0)");

	if (not sel.hp_current().has_value() || not sel.hp_max().has_value()) {
		throw std::runtime_error{
			"demo 22 pin 3 FAILED: hp not present "
			"(cur=" + std::to_string(sel.hp_current().value_or(-1))
			+ " max=" + std::to_string(sel.hp_max().value_or(-1)) + ")"
		};
	}
	if (*sel.hp_current() <= 0 || *sel.hp_max() <= 0) {
		throw std::runtime_error{
			"demo 22 pin 3 FAILED: HP must be > 0 at spawn "
			"(cur=" + std::to_string(*sel.hp_current())
			+ " max=" + std::to_string(*sel.hp_max()) + ")"
		};
	}
	if (*sel.hp_current() != *sel.hp_max()) {
		throw std::runtime_error{
			"demo 22 pin 3 FAILED: at spawn, current must equal max "
			"(cur=" + std::to_string(*sel.hp_current())
			+ " max=" + std::to_string(*sel.hp_max()) + ")"
		};
	}
	log::log(MSG(info) << "demo 22: pin 3 PASS (hp current=max="
	                   << *sel.hp_current() << ")");

	if (not sel.attack_damage().has_value()
	    || *sel.attack_damage() <= 0) {
		throw std::runtime_error{
			"demo 22 pin 4 FAILED: attack_damage="
			+ std::to_string(sel.attack_damage().value_or(0))
		};
	}
	if (not sel.attack_range().has_value() || *sel.attack_range() <= 0) {
		throw std::runtime_error{
			"demo 22 pin 4 FAILED: attack_range="
			+ std::to_string(sel.attack_range().value_or(0))
		};
	}
	if (not sel.attack_cooldown().has_value() || *sel.attack_cooldown() <= 0) {
		throw std::runtime_error{
			"demo 22 pin 4 FAILED: attack_cooldown="
			+ std::to_string(sel.attack_cooldown().value_or(0))
		};
	}
	log::log(MSG(info) << "demo 22: pin 4 PASS (attack damage="
	                   << *sel.attack_damage() << " range=" << *sel.attack_range()
	                   << " cooldown=" << *sel.attack_cooldown() << ")");

	if (sel.attack_is_active()) {
		throw std::runtime_error{
			"demo 22 pin 5 FAILED: at spawn, attack must NOT be active"
		};
	}
	log::log(MSG(info) << "demo 22: pin 5 PASS (attack_is_active=false at spawn)");

	if (not sel.is_alive()) {
		throw std::runtime_error{"demo 22 pin 6 FAILED: is_alive=false at spawn"};
	}
	log::log(MSG(info) << "demo 22: pin 6 PASS (is_alive=true)");

	// ---- MissionView ----
	// Reach the runtime's MissionState via the simulation. The
	// TriggerRuntime is a private member of GameSimulation; the
	// headless test reads the same MissionState the runtime
	// mutates, so the view is authoritative.
	auto &ms = simulation->get_trigger_runtime()->mission_state();
	presenter::viewmodel::MissionView mission{ms};
	if (mission.lifecycle() != "RUNNING") {
		throw std::runtime_error{
			"demo 22 pin 7 FAILED: lifecycle="
			+ mission.lifecycle() + " (expected RUNNING)"
		};
	}
	if (mission.is_victory()) {
		throw std::runtime_error{
			"demo 22 pin 7 FAILED: is_victory=true at spawn"
		};
	}
	log::log(MSG(info) << "demo 22: pin 7 PASS (mission lifecycle=RUNNING)");

	if (mission.objectives().empty()) {
		throw std::runtime_error{
			"demo 22 pin 8 FAILED: no objectives reported"
		};
	}
	bool found_defeat_enemy = false;
	for (const auto &o : mission.objectives()) {
		if (o.id == "mission_01.defeat_enemy") {
			found_defeat_enemy = true;
			if (o.state != "INACTIVE" && o.state != "ACTIVE") {
				throw std::runtime_error{
					"demo 22 pin 8 FAILED: defeat_enemy state="
					+ o.state
				};
			}
		}
	}
	if (not found_defeat_enemy) {
		throw std::runtime_error{
			"demo 22 pin 8 FAILED: mission_01.defeat_enemy not in objectives"
		};
	}
	log::log(MSG(info) << "demo 22: pin 8 PASS (defeat_enemy objective present, "
	                   << "state=" << mission.objectives().front().state << ")");

	log::log(MSG(info) << "DEMO 22: PASS (SelectedEntityView + MissionView contract)");
}


} // namespace openage::gamestate::tests