// Copyright 2026 the openage authors. See copying.md for legal info.

#include "gamestate/demo/selection_optional_component_demo.h"

#include "cvar/cvar.h"
#include "error/error.h"
#include "event/event_loop.h"
#include "gamestate/component/api/attack.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/types.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "log/message.h"
#include "presenter/viewmodel/selected_entity_view.h"
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

entity_id_t find_entity_by_key(
	const std::shared_ptr<GameState> &state,
	const std::string &key_substr) {
	for (const auto &kv : state->get_named_entities()) {
		if (kv.first.find(key_substr) != std::string::npos) {
			return kv.second;
		}
	}
	return 0;
}


}  // namespace


void simulation_demo_50(const util::Path &path) {
	log::log(MSG(info) << "=== DEMO 50: selection of an entity without optional components ===");

	auto cvar = std::make_shared<cvar::CVarManager>(path);
	auto time_loop = std::make_shared<time::TimeLoop>();
	auto simulation = std::make_shared<GameSimulation>(
		path, cvar, time_loop,
		"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup",
		"ww_mvp.campaign.william_wallace.mission_01.Mission");
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();

	auto state = simulation->get_game()->get_state();

	entity_id_t tc_id = find_entity_by_key(state, "PlayerTownCenter");
	entity_id_t villager_id = find_entity_by_key(state, "PlayerVillager");
	if (not state->has_game_entity(tc_id)
	    or not state->has_game_entity(villager_id)) {
		throw std::runtime_error{
			"demo 50: missing placed entity (PlayerTownCenter / PlayerVillager)"};
	}

	// ---- Pin 1: premise -- the TownCenter has no Attack component ---
	auto tc = state->get_game_entity(tc_id);
	if (tc->has_component(component::component_t::ATTACK)) {
		throw std::runtime_error{
			"demo 50 pin 1 FAILED: the TownCenter unexpectedly HAS an "
			"Attack component -- this demo's premise no longer holds "
			"(check entity_factory.cpp: the Attack component is only "
			"mounted for ApplyDiscreteEffect / RangedDiscreteEffect "
			"abilities)"};
	}
	log::log(MSG(info) << "demo 50: pin 1 PASS (TownCenter has no Attack component)");

	// ---- Pin 2: selecting it must not throw ---------------------------
	presenter::viewmodel::SelectionModel model{simulation};
	model.set_multi_selection({tc_id});
	auto selected = model.selected();
	if (selected.size() != 1 or selected.front() != tc_id) {
		throw std::runtime_error{
			"demo 50 pin 2 FAILED: selection after set_multi_selection "
			"does not contain the TownCenter (size="
			+ std::to_string(selected.size()) + ")"};
	}
	log::log(MSG(info) << "demo 50: pin 2 PASS (set_multi_selection on an "
	                     "Attack-less entity did not throw)");

	// ---- Pin 3: the entity view must not throw either ----------------
	presenter::viewmodel::SelectedEntityView view{state, tc_id};
	if (view.entity_id() != tc_id) {
		throw std::runtime_error{
			"demo 50 pin 3 FAILED: SelectedEntityView reports the wrong "
			"entity id"};
	}
	// The documented contract: "If the entity is unknown or has no Live
	// component, the view's fields are nullopt / 0." A TownCenter has no
	// Attack component, so the attack fields must be empty rather than
	// a crash.
	if (view.attack_damage().has_value()) {
		throw std::runtime_error{
			"demo 50 pin 3 FAILED: attack_damage is set for an entity "
			"without an Attack component"};
	}
	log::log(MSG(info) << "demo 50: pin 3 PASS (SelectedEntityView on an "
	                     "Attack-less entity did not throw)");

	// ---- Pin 4: health wiring still works for entities that do -------
	int health_changes = 0;
	model.subscribe_health_changed(
		[&](entity_id_t, int64_t) { health_changes += 1; });

	model.set_multi_selection({tc_id, villager_id});

	auto villager = state->get_game_entity(villager_id);
	auto live = std::dynamic_pointer_cast<component::Live>(
		villager->get_component(component::component_t::LIVE));
	auto attack = std::dynamic_pointer_cast<component::Attack>(
		villager->get_component(component::component_t::ATTACK));
	if (not live or not attack) {
		throw std::runtime_error{
			"demo 50 pin 4 FAILED: villager missing Live/Attack -- the "
			"fixture changed, the premise of pin 4 no longer holds"};
	}
	auto health_fqon = attack->get_health_attribute_fqon();
	live->set_attribute(openage::time::time_t{5}, health_fqon, 20);
	if (health_changes != 1) {
		throw std::runtime_error{
			"demo 50 pin 4 FAILED: health_changes="
			+ std::to_string(health_changes)
			+ " (expected 1) -- the fix must not disable health wiring "
			  "for entities that DO have an Attack component"};
	}
	log::log(MSG(info) << "demo 50: pin 4 PASS (health wiring still active for "
	                     "the villager in a mixed multi-selection)");

	// ---- Pin 5: the typed accessor distinguishes "absent" from "wrong type" ----
	//
	// This is the guard against the SIGSEGV variant of the same bug. The
	// raw pattern
	//
	//     auto base = entity->try_get_component(component_t::ATTACK);
	//     if (base) { std::dynamic_pointer_cast<Attack>(base)->get_x(); }
	//
	// tests the BASE pointer, so a failed cast reaches the dereference
	// and segfaults. try_get_component_as<Attack>() must instead report
	// the mismatch as a diagnosable error.
	//
	// Asking for ATTACK as if it were a Live component is exactly that
	// mismatch: the villager HAS an Attack component, it is just not a
	// Live. A correct implementation must not return nullptr (that would
	// mean "absent", which is a lie) and must not dereference null.
	bool mismatch_reported = false;
	try {
		// The villager has an Attack component, so this must NOT return
		// nullptr -- it must raise instead.
		auto wrong = villager->try_get_component_as<component::Live>(
			component::component_t::ATTACK);
		if (wrong != nullptr) {
			throw std::runtime_error{
				"demo 50 pin 5 FAILED: try_get_component_as<Live>(ATTACK) "
				"returned a Live component for an Attack enum"};
		}
		// Falling through with nullptr is the dangerous case: it claims
		// the component is absent when it is actually a type mismatch.
		throw std::runtime_error{
			"demo 50 pin 5 FAILED: a type mismatch silently returned "
			"nullptr instead of being reported -- this is the SIGSEGV "
			"variant (a caller cannot tell 'absent' from 'mismatched')"};
	}
	catch (const openage::error::Error &err) {
		// Expected: the accessor reports the enum/class disagreement.
		mismatch_reported = true;
		log::log(MSG(info) << "demo 50: pin 5 caught the expected error: "
		                   << err.what());
	}

	if (not mismatch_reported) {
		throw std::runtime_error{
			"demo 50 pin 5 FAILED: the type mismatch was not reported"};
	}

	// ...while the correctly-typed call on the same entity still works,
	// proving pin 5 is not just "everything throws".
	auto live_ok = villager->try_get_component_as<component::Live>(
		component::component_t::LIVE);
	if (live_ok == nullptr) {
		throw std::runtime_error{
			"demo 50 pin 5 FAILED: a correctly-typed lookup returned "
			"nullptr -- the guard is rejecting valid requests"};
	}
	log::log(MSG(info) << "demo 50: pin 5 PASS (type mismatch is reported, "
	                     "absent returns nullptr, valid lookup works)");

	log::log(MSG(info) << "DEMO 50: PASS (selection of an entity without "
	                     "optional components)");
}


} // namespace openage::gamestate::tests
