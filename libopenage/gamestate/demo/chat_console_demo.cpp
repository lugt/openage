// Copyright 2026+ the openage authors. See copying.md for legal info.

#include "gamestate/demo/chat_console_demo.h"

#include <memory>
#include <stdexcept>
#include <string>

#include "cvar/cvar.h"
#include "error/error.h"
#include "gamestate/chat_console.h"
#include "gamestate/game.h"
#include "gamestate/game_state.h"
#include "gamestate/player.h"
#include "gamestate/save_load.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "scenario/trigger/mission_state.h"
#include "scenario/trigger/trigger_runtime.h"
#include "time/clock.h"
#include "time/time_loop.h"
#include "util/path.h"


namespace openage::gamestate::tests {

namespace {

void require(bool condition, const std::string &message) {
	if (not condition) {
		throw std::runtime_error("chat_console_demo failed: " + message);
	}
}

} // namespace

void simulation_demo_55(const util::Path &path) {
	auto cvar_manager = std::make_shared<cvar::CVarManager>(path["cfg"]);
	auto time_loop = std::make_shared<time::TimeLoop>();

	GameSimulation sim{
		path,
		cvar_manager,
		time_loop,
		"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup",
		"ww_mvp.campaign.william_wallace.mission_01.Mission",
	};
	sim.set_modpacks({"de2_base", "ww_mvp"});
	require(sim.initialize(), "GameSimulation::initialize failed");

	auto game = sim.get_game();
	auto state = game->get_state();
	auto player0 = state->get_player(0);
	require(player0 != nullptr, "player 0 missing");

	ChatConsole console{sim, 0};

	// 1. Plain chat (multiplayer-ready)
	auto chat_res = console.execute("glhf everyone!");
	require(not chat_res.is_command and chat_res.handled, "plain chat should be handled as non-command");

	// 2. Resource cheats
	const auto wood0 = player0->get_resource("de2_base.data.util.resource.types.Wood");
	const auto food0 = player0->get_resource("de2_base.data.util.resource.types.Food");
	const auto gold0 = player0->get_resource("de2_base.data.util.resource.types.Gold");
	const auto stone0 = player0->get_resource("de2_base.data.util.resource.types.Stone");

	require(console.execute("Lumberjack").handled, "lumberjack failed");
	require(player0->get_resource("de2_base.data.util.resource.types.Wood") == wood0 + 10000, "lumberjack wood mismatch");

	require(console.execute("Robin Hood").handled, "robin hood failed");
	require(player0->get_resource("de2_base.data.util.resource.types.Gold") == gold0 + 10000, "robin hood gold mismatch");

	require(console.execute("Rock On").handled, "rock on failed");
	require(player0->get_resource("de2_base.data.util.resource.types.Stone") == stone0 + 10000, "rock on stone mismatch");

	require(console.execute("Cheese Steak Jimmy's").handled, "cheese steak jimmy's failed");
	require(player0->get_resource("de2_base.data.util.resource.types.Food") == food0 + 10000, "food cheat mismatch");

	require(console.execute("ninjaconnor").handled, "ninjaconnor failed");
	require(player0->get_resource("de2_base.data.util.resource.types.Wood") == wood0 + 110000, "ninjaconnor wood mismatch");

	// 3. Map & Fog cheats (reversible)
	state->enable_fog();
	require(console.execute("marco").handled and state->is_reveal_map_cheat(), "marco did not enable reveal");
	require(console.execute("marco").handled and not state->is_reveal_map_cheat(), "marco did not toggle off");
	require(console.execute("polo").handled and state->is_no_fog_cheat(), "polo did not disable fog");
	require(console.execute("polo").handled and not state->is_no_fog_cheat(), "polo did not restore fog");

	// 4. Aegis & /speed
	require(console.execute("aegis").handled and state->fast_actions(), "aegis did not enable");
	require(console.execute("aegis").handled and not state->fast_actions(), "aegis did not toggle off");
	require(console.execute("/speed 2.5").handled, "/speed failed");
	require(time_loop->get_clock()->get_speed() == time::speed_t::from_double(2.5), "clock speed mismatch");

	// 5. Unit spawn cheat
	const size_t before_count = state->get_game_entities().size();
	auto spawn_res = console.execute("how do you turn this on");
	require(spawn_res.handled and not spawn_res.is_error, "cobra/knight spawn cheat failed: " + spawn_res.response);
	require(state->get_game_entities().size() == before_count + 1, "unit count did not increase after spawn cheat");

	// 6. Debug commands
	require(console.execute("/help").handled, "/help failed");
	require(console.execute("/status").handled, "/status failed");
	require(console.execute("/give gold 500").handled, "/give failed");
	require(player0->get_resource("de2_base.data.util.resource.types.Gold") == gold0 + 110500, "/give gold mismatch");

	// 7. Direct Python execution
	auto py1 = console.execute("py 21 * 2");
	require(py1.handled and not py1.is_error and py1.response == "42", "py eval failed: " + py1.response);
	auto py2 = console.execute("py import openage; print(openage.__name__)");
	require(py2.handled and not py2.is_error and py2.response == "openage", "py import failed: " + py2.response);

	// 8. Save & Load roundtrip (GameSaveManager::directory + serialize / deserialize)
	const std::string saves_dir = GameSaveManager::directory(path);
	Is_True(not saves_dir.empty(), ("GameSaveManager::directory returned empty path"));
	bool is_true_threw = false;
	try {
		Is_True(false, ("expected assertion failure %d", 1));
	}
	catch (const Error &) {
		is_true_threw = true;
	}
	require(is_true_threw, "Is_True(false, ...) must throw Error instead of crashing");
	const auto saved_wood = player0->get_resource("de2_base.data.util.resource.types.Wood");
	const auto saved_entities = state->get_game_entities().size();
	const std::string snapshot = GameSaveManager::serialize(sim);
	require(snapshot.rfind("OPENAGE_SAVE_V1|", 0) == 0, "serialize header missing");
	player0->set_resource("de2_base.data.util.resource.types.Wood", 0);
	std::string load_err;
	require(GameSaveManager::deserialize(sim, snapshot, &load_err), "deserialize failed: " + load_err);
	require(player0->get_resource("de2_base.data.util.resource.types.Wood") == saved_wood, "save/load wood mismatch");
	require(state->get_game_entities().size() == saved_entities, "save/load entity count mismatch");

	// 9. Black death & victory
	require(console.execute("black death").handled, "black death failed");
	require(console.execute("i r winner").handled, "i r winner failed");
	require(sim.get_trigger_runtime()->mission_state().is_victory(), "victory not declared by i r winner");

	log::log(INFO << "DEMO 55: PASS (chat console, AoE2 cheats, debug commands, Python REPL & save/load)");
}

} // namespace openage::gamestate::tests
