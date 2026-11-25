// Copyright 2026+ the openage authors. See copying.md for legal info.

#include "chat_console.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <sstream>
#include <utility>
#include <vector>

#include "coord/phys.h"
#include "event/event_loop.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/activity.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/component/types.h"
#include "gamestate/entity_factory.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/manager.h"
#include "gamestate/map.h"
#include "gamestate/player.h"
#include "gamestate/simulation.h"
#include "log/log.h"
#include "scenario/trigger/mission_state.h"
#include "scenario/trigger/trigger_runtime.h"
#include "time/clock.h"
#include "time/time_loop.h"

import nyan;


namespace openage::gamestate {

namespace {

const nyan::fqon_t RES_FOOD = "de2_base.data.util.resource.types.Food";
const nyan::fqon_t RES_WOOD = "de2_base.data.util.resource.types.Wood";
const nyan::fqon_t RES_GOLD = "de2_base.data.util.resource.types.Gold";
const nyan::fqon_t RES_STONE = "de2_base.data.util.resource.types.Stone";
const nyan::fqon_t ATTR_HEALTH = "de2_base.data.util.attribute.types.Health";

std::string trim_copy(std::string_view input) {
	size_t start = 0;
	while (start < input.size() and std::isspace(static_cast<unsigned char>(input[start]))) {
		++start;
	}
	size_t end = input.size();
	while (end > start and std::isspace(static_cast<unsigned char>(input[end - 1]))) {
		--end;
	}
	return std::string{input.substr(start, end - start)};
}

std::string to_lower_copy(std::string_view input) {
	std::string out{input};
	for (char &ch : out) {
		ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
	}
	return out;
}

std::vector<std::string> split_ws(std::string_view input) {
	std::vector<std::string> tokens;
	size_t i = 0;
	while (i < input.size()) {
		while (i < input.size() and std::isspace(static_cast<unsigned char>(input[i]))) {
			++i;
		}
		if (i >= input.size()) {
			break;
		}
		size_t j = i;
		while (j < input.size() and not std::isspace(static_cast<unsigned char>(input[j]))) {
			++j;
		}
		tokens.emplace_back(input.substr(i, j - i));
		i = j;
	}
	return tokens;
}

bool fqon_exists(const std::shared_ptr<GameState> &state, const std::string &fqon) {
	const auto &db = state->get_db_view();
	if (not db) {
		return false;
	}
	try {
		(void)db->get_object(fqon);
		return true;
	}
	catch (...) {
		return false;
	}
}

std::string resolve_unit_fqon(const std::shared_ptr<GameState> &state,
                              const std::vector<std::string> &candidates) {
	for (const auto &fqon : candidates) {
		if (fqon_exists(state, fqon)) {
			return fqon;
		}
	}
	return {};
}

std::string resolve_short_or_fqon(const std::shared_ptr<GameState> &state, const std::string &name) {
	if (name.find('.') != std::string::npos and fqon_exists(state, name)) {
		return name;
	}
	const std::string lower = to_lower_copy(name);
	std::string snake;
	snake.reserve(lower.size());
	for (char ch : lower) {
		if (std::isalnum(static_cast<unsigned char>(ch))) {
			snake.push_back(ch);
		}
		else if (ch == '_' or ch == '-') {
			snake.push_back('_');
		}
	}
	std::string pascal;
	bool cap = true;
	for (char ch : lower) {
		if (ch == '_' or ch == '-') {
			cap = true;
			continue;
		}
		if (cap) {
			pascal.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(ch))));
			cap = false;
		}
		else {
			pascal.push_back(ch);
		}
	}
	if (not snake.empty() and not pascal.empty()) {
		const std::string candidate = "de2_base.data.game_entity.generic." + snake + "." + snake + "." + pascal;
		if (fqon_exists(state, candidate)) {
			return candidate;
		}
	}
	if (lower == "villager") {
		return "de2_base.data.game_entity.generic.villager.villager.Villager";
	}
	if (lower == "militia") {
		return "de2_base.data.game_entity.generic.militia.militia.Militia";
	}
	if (lower == "knight") {
		return "de2_base.data.game_entity.generic.knight.knight.Knight";
	}
	if (lower == "archer") {
		return "de2_base.data.game_entity.generic.archer.archer.Archer";
	}
	if (lower == "sheep") {
		return "de2_base.data.game_entity.generic.sheep.sheep.Sheep";
	}
	if (lower == "teutonic_knight" or lower == "teutonicknight") {
		return "de2_base.data.game_entity.generic.teutonic_knight.teutonic_knight.TeutonicKnight";
	}
	if (lower == "flaming_camel" or lower == "flamingcamel") {
		return "de2_base.data.game_entity.generic.flaming_camel.flaming_camel.FlamingCamel";
	}
	if (lower == "mangudai") {
		return "de2_base.data.game_entity.generic.mangudai.mangudai.Mangudai";
	}
	return {};
}

coord::phys3 find_spawn_position(const std::shared_ptr<GameState> &state,
                                 player_id_t owner,
                                 const time::time_t &now) {
	std::optional<coord::phys3> fallback_owned;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->is_dead()) {
			continue;
		}
		auto ownership = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		auto position = entity->try_get_component_as<component::Position>(component::component_t::POSITION);
		if (not ownership or not position or ownership->get_owners().get(now) != owner) {
			continue;
		}
		auto pos = position->get_positions().get(now);
		if (entity->definition.find("town_center") != std::string::npos) {
			return coord::phys3{pos.ne + 2.5, pos.se + 2.5, pos.up};
		}
		if (not fallback_owned) {
			fallback_owned = coord::phys3{pos.ne + 1.5, pos.se + 1.5, pos.up};
		}
	}
	if (fallback_owned) {
		return *fallback_owned;
	}
	if (const auto &map = state->get_map()) {
		const auto size = map->get_size();
		return coord::phys3{static_cast<double>(size[0]) * 0.5, static_cast<double>(size[1]) * 0.5, 0.0};
	}
	return coord::phys3{4.0, 4.0, 0.0};
}

std::shared_ptr<GameEntity> spawn_unit_at(GameSimulation &sim,
                                          const std::shared_ptr<GameState> &state,
                                          player_id_t owner,
                                          const std::string &fqon,
                                          const coord::phys3 &pos,
                                          const time::time_t &now) {
	const auto &factory = sim.get_entity_factory();
	const auto &loop = sim.get_event_loop();
	if (not factory or not loop or fqon.empty()) {
		return nullptr;
	}
	auto entity = factory->add_game_entity(loop, state, owner, fqon, false);
	if (not entity) {
		return nullptr;
	}
	if (auto position = entity->try_get_component_as<component::Position>(component::component_t::POSITION)) {
		position->set_position(now, pos);
	}
	if (auto ownership = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP)) {
		ownership->set_owner(now, owner);
	}
	factory->attach_entity_renderer(entity);
	state->add_game_entity(entity);
	if (auto activity = entity->try_get_component_as<component::Activity>(component::component_t::ACTIVITY)) {
		activity->init(now);
		entity->get_manager()->run_activity_system(now);
	}
	return entity;
}

size_t defeat_player_entities(const std::shared_ptr<GameState> &state,
                              player_id_t target_player,
                              const time::time_t &now) {
	std::vector<std::pair<entity_id_t, std::shared_ptr<GameEntity>>> victims;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->is_dead()) {
			continue;
		}
		auto ownership = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		if (ownership and ownership->get_owners().get(now) == target_player) {
			victims.emplace_back(id, entity);
		}
	}
	for (auto &[id, entity] : victims) {
		if (auto live = entity->try_get_component_as<component::Live>(component::component_t::LIVE)) {
			live->set_attribute(now, ATTR_HEALTH, 0);
		}
		entity->mark_dead();
		state->remove_game_entity(id);
	}
	return victims.size();
}

size_t replace_player_units(GameSimulation &sim,
                            const std::shared_ptr<GameState> &state,
                            player_id_t owner,
                            const std::vector<std::string> &source_substrings,
                            const std::string &target_fqon,
                            const time::time_t &now) {
	if (target_fqon.empty()) {
		return 0;
	}
	struct Candidate {
		entity_id_t id;
		std::shared_ptr<GameEntity> entity;
		coord::phys3 pos;
	};
	std::vector<Candidate> matches;
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->is_dead()) {
			continue;
		}
		auto ownership = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		auto position = entity->try_get_component_as<component::Position>(component::component_t::POSITION);
		if (not ownership or not position or ownership->get_owners().get(now) != owner) {
			continue;
		}
		const bool matched = std::ranges::any_of(source_substrings, [&](const std::string &sub) {
			return entity->definition.find(sub) != std::string::npos;
		});
		if (matched) {
			matches.push_back({id, entity, position->get_positions().get(now)});
		}
	}
	for (auto &match : matches) {
		match.entity->mark_dead();
		state->remove_game_entity(match.id);
		spawn_unit_at(sim, state, owner, target_fqon, match.pos, now);
	}
	return matches.size();
}

} // namespace


ChatConsole::ChatConsole(GameSimulation &sim, player_id_t local_player) :
	sim{sim},
	local_player{local_player} {}


void ChatConsole::push_entry(player_id_t sender,
                             std::string sender_name,
                             std::string text,
                             ChatEntryKind kind) {
	std::lock_guard lock{this->mutex};
	this->history.push_back(ChatEntry{sender, std::move(sender_name), std::move(text), kind});
	if (this->history.size() > 64) {
		this->history.erase(this->history.begin(), this->history.begin() + (this->history.size() - 64));
	}
}


std::vector<ChatEntry> ChatConsole::get_history() const {
	std::lock_guard lock{this->mutex};
	return this->history;
}


void ChatConsole::clear_history() {
	std::lock_guard lock{this->mutex};
	this->history.clear();
}


void ChatConsole::ensure_python_env() {
	if (this->py_initialized) {
		return;
	}
	this->py_globals = py::dict();
	this->py_globals.exec(
		"import sys, io, traceback\n"
		"import openage\n"
		"import openage.log as log\n"
		"def _openage_console_run(code, ns):\n"
		"    buf = io.StringIO()\n"
		"    old_out, old_err = sys.stdout, sys.stderr\n"
		"    sys.stdout = buf\n"
		"    sys.stderr = buf\n"
		"    try:\n"
		"        try:\n"
		"            compiled = compile(code, '<chat>', 'eval')\n"
		"            val = eval(compiled, ns, ns)\n"
		"            out = buf.getvalue().rstrip()\n"
		"            if val is not None:\n"
		"                rep = repr(val)\n"
		"                return (out + '\\n' + rep).strip() if out else rep\n"
		"            return out if out else 'OK'\n"
		"        except SyntaxError:\n"
		"            compiled = compile(code, '<chat>', 'exec')\n"
		"            exec(compiled, ns, ns)\n"
		"            out = buf.getvalue().rstrip()\n"
		"            return out if out else 'OK'\n"
		"    except Exception as exc:\n"
		"        return 'Python error: ' + str(exc)\n"
		"    finally:\n"
		"        sys.stdout = old_out\n"
		"        sys.stderr = old_err\n"
	);
	this->py_initialized = true;
}


ChatCommandResult ChatConsole::try_execute_python(const std::string &code) {
	if (code.empty()) {
		return {true, true, true, "Usage: py <python expression or statement>"};
	}
	try {
		this->ensure_python_env();
		auto runner = this->py_globals.get("_openage_console_run");
		auto py_code = py::str(code);
		auto res = runner.call(py_code, this->py_globals);
		return {true, true, false, res.str()};
	}
	catch (const std::exception &ex) {
		return {true, true, true, std::string{"Python error: "} + ex.what()};
	}
}


ChatCommandResult ChatConsole::try_execute_debug_command(const std::string &trimmed, player_id_t sender) {
	const auto tokens = split_ws(trimmed);
	if (tokens.empty()) {
		return {};
	}
	const std::string cmd = to_lower_copy(tokens[0]);
	const auto game = this->sim.get_game();
	const auto state = game ? game->get_state() : nullptr;
	const auto clock = this->sim.get_time_loop() ? this->sim.get_time_loop()->get_clock() : nullptr;
	const time::time_t now = clock ? clock->get_time() : time::time_t{0};

	if (cmd == "/help") {
		return {
			true, true, false,
			"Commands: /status, /speed <x>, /give <food|wood|gold|stone|all> <n>, "
			"/spawn <unit> [x y] [player], /kill <id>, /entities [player], /fog <on|off|reveal>, "
			"/victory, /cheats <on|off>, /clear | Python: py <code> | AoE2 Cheats: marco, polo, aegis, "
			"lumberjack, robin hood, rock on, cheese steak jimmy's, ninjaconnor, how do you turn this on, i r winner, black death"};
	}
	if (cmd == "/clear") {
		this->clear_history();
		return {true, true, false, "Console cleared."};
	}
	if (cmd == "/cheats" and tokens.size() >= 2) {
		const auto arg = to_lower_copy(tokens[1]);
		this->set_cheats_enabled(arg == "on" or arg == "1" or arg == "true");
		return {true, true, false, std::string{"Cheats "} + (this->cheats_enabled ? "enabled." : "disabled.")};
	}
	if (not state) {
		return {true, true, true, "GameState is not initialized."};
	}
	if (cmd == "/status") {
		const auto eco = state->player_economy(sender);
		std::ostringstream os;
		os << "t=" << now.to_double()
		   << " speed=" << (clock ? clock->get_speed().to_double() : 1.0)
		   << " entities=" << state->get_game_entities().size()
		   << " wood=" << eco.wood << " food=" << eco.food
		   << " gold=" << eco.gold << " stone=" << eco.stone
		   << " pop=" << eco.population << "/" << eco.population_capacity;
		return {true, true, false, os.str()};
	}
	if (cmd == "/speed") {
		if (tokens.size() < 2 or not clock) {
			return {true, true, true, "Usage: /speed <factor>"};
		}
		try {
			const double factor = std::stod(tokens[1]);
			if (factor < 0.0) {
				return {true, true, true, "Speed must be >= 0."};
			}
			clock->set_speed(time::speed_t::from_double(factor));
			return {true, true, false, "Simulation speed set to " + tokens[1] + "x."};
		}
		catch (...) {
			return {true, true, true, "Invalid speed value: " + tokens[1]};
		}
	}
	if (cmd == "/give") {
		if (tokens.size() < 3) {
			return {true, true, true, "Usage: /give <food|wood|gold|stone|all> <amount>"};
		}
		const auto res = to_lower_copy(tokens[1]);
		int64_t amount = 0;
		try {
			amount = std::stoll(tokens[2]);
		}
		catch (...) {
			return {true, true, true, "Invalid amount: " + tokens[2]};
		}
		if (amount <= 0) {
			return {true, true, true, "Amount must be > 0."};
		}
		auto player = state->get_player(sender);
		if (not player) {
			return {true, true, true, "Player not found."};
		}
		if (res == "all") {
			player->add_resource(RES_FOOD, amount);
			player->add_resource(RES_WOOD, amount);
			player->add_resource(RES_GOLD, amount);
			player->add_resource(RES_STONE, amount);
		}
		else if (res == "food") {
			player->add_resource(RES_FOOD, amount);
		}
		else if (res == "wood") {
			player->add_resource(RES_WOOD, amount);
		}
		else if (res == "gold") {
			player->add_resource(RES_GOLD, amount);
		}
		else if (res == "stone") {
			player->add_resource(RES_STONE, amount);
		}
		else {
			return {true, true, true, "Unknown resource: " + res};
		}
		state->update_player_economies(now);
		return {true, true, false, "Added " + std::to_string(amount) + " " + res + "."};
	}
	if (cmd == "/fog") {
		if (tokens.size() < 2) {
			return {true, true, true, "Usage: /fog <on|off|reveal>"};
		}
		const auto mode = to_lower_copy(tokens[1]);
		if (mode == "off") {
			state->set_no_fog_cheat(true);
			state->set_reveal_map_cheat(false);
		}
		else if (mode == "reveal") {
			state->set_no_fog_cheat(false);
			state->set_reveal_map_cheat(true);
		}
		else {
			state->set_no_fog_cheat(false);
			state->set_reveal_map_cheat(false);
		}
		state->update_visibility(now);
		return {true, true, false, "Fog mode set to: " + mode};
	}
	if (cmd == "/victory") {
		if (const auto &runtime = this->sim.get_trigger_runtime()) {
			runtime->mission_state().declare_victory("console_victory");
			return {true, true, false, "Victory declared."};
		}
		return {true, true, true, "No TriggerRuntime active."};
	}
	if (cmd == "/kill") {
		if (tokens.size() < 2) {
			return {true, true, true, "Usage: /kill <entity_id>"};
		}
		entity_id_t eid = 0;
		auto [ptr, ec] = std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), eid);
		if (ec != std::errc{} or not state->has_game_entity(eid)) {
			return {true, true, true, "Entity not found: " + tokens[1]};
		}
		auto entity = state->get_game_entity(eid);
		entity->mark_dead();
		state->remove_game_entity(eid);
		return {true, true, false, "Removed entity " + std::to_string(eid) + "."};
	}
	if (cmd == "/entities") {
		std::optional<player_id_t> filter;
		if (tokens.size() >= 2) {
			player_id_t pid = 0;
			auto [ptr, ec] = std::from_chars(tokens[1].data(), tokens[1].data() + tokens[1].size(), pid);
			if (ec == std::errc{}) {
				filter = pid;
			}
		}
		std::ostringstream os;
		size_t count = 0;
		size_t shown = 0;
		for (const auto &[id, entity] : state->get_game_entities()) {
			if (entity->is_dead()) {
				continue;
			}
			if (filter) {
				auto ownership = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
				if (not ownership or ownership->get_owners().get(now) != *filter) {
					continue;
				}
			}
			++count;
			if (shown < 8) {
				if (shown > 0) {
					os << ", ";
				}
				os << id << ":" << entity->definition;
				++shown;
			}
		}
		return {true, true, false, "Entities(" + std::to_string(count) + "): " + os.str()};
	}
	if (cmd == "/spawn") {
		if (tokens.size() < 2) {
			return {true, true, true, "Usage: /spawn <fqon_or_name> [x y] [player]"};
		}
		const std::string fqon = resolve_short_or_fqon(state, tokens[1]);
		if (fqon.empty()) {
			return {true, true, true, "Unknown entity: " + tokens[1]};
		}
		player_id_t owner = sender;
		coord::phys3 pos = find_spawn_position(state, owner, now);
		if (tokens.size() >= 4) {
			try {
				pos = coord::phys3{std::stod(tokens[2]), std::stod(tokens[3]), 0.0};
			}
			catch (...) {
				return {true, true, true, "Invalid coordinates."};
			}
		}
		if (tokens.size() >= 5) {
			player_id_t pid = 0;
			auto [ptr, ec] = std::from_chars(tokens[4].data(), tokens[4].data() + tokens[4].size(), pid);
			if (ec == std::errc{}) {
				owner = pid;
			}
		}
		auto spawned = spawn_unit_at(this->sim, state, owner, fqon, pos, now);
		if (not spawned) {
			return {true, true, true, "Failed to spawn " + fqon};
		}
		state->update_visibility(now);
		state->update_player_economies(now);
		return {true, true, false, "Spawned " + fqon + " (id=" + std::to_string(spawned->get_id()) + ")."};
	}

	return {true, true, true, "Unknown command: " + tokens[0] + " (type /help)"};
}


ChatCommandResult ChatConsole::try_execute_cheat(const std::string &trimmed, player_id_t sender) {
	const std::string code = to_lower_copy(trimmed);
	const auto game = this->sim.get_game();
	const auto state = game ? game->get_state() : nullptr;
	const auto clock = this->sim.get_time_loop() ? this->sim.get_time_loop()->get_clock() : nullptr;
	const time::time_t now = clock ? clock->get_time() : time::time_t{0};

	if (code == "!mute") {
		this->muted = true;
		return {true, true, false, "Taunts muted."};
	}
	if (code == "!nomute") {
		this->muted = false;
		return {true, true, false, "Taunts unmuted."};
	}

	auto require_cheats = [&]() -> std::optional<ChatCommandResult> {
		if (not this->cheats_enabled) {
			return ChatCommandResult{true, true, true, "Cheats are disabled in this match."};
		}
		if (not state) {
			return ChatCommandResult{true, true, true, "GameState is not initialized."};
		}
		return std::nullopt;
	};

	// --- Resources ---
	if (code == "ninjaconnor" or code == "ninjalui" or code == "rowshep") {
		if (auto err = require_cheats()) { return *err; }
		if (auto player = state->get_player(sender)) {
			player->add_resource(RES_FOOD, 100000);
			player->add_resource(RES_WOOD, 100000);
			player->add_resource(RES_GOLD, 100000);
			player->add_resource(RES_STONE, 100000);
			state->update_player_economies(now);
			return {true, true, false, "Cheat activated: +100,000 all resources."};
		}
	}
	if (code == "cheese steak jimmy's" or code == "cheese steak jimmys") {
		if (auto err = require_cheats()) { return *err; }
		if (auto player = state->get_player(sender)) {
			player->add_resource(RES_FOOD, 10000);
			state->update_player_economies(now);
			return {true, true, false, "Cheat activated: +10,000 Food."};
		}
	}
	if (code == "lumberjack") {
		if (auto err = require_cheats()) { return *err; }
		if (auto player = state->get_player(sender)) {
			player->add_resource(RES_WOOD, 10000);
			state->update_player_economies(now);
			return {true, true, false, "Cheat activated: +10,000 Wood."};
		}
	}
	if (code == "robin hood") {
		if (auto err = require_cheats()) { return *err; }
		if (auto player = state->get_player(sender)) {
			player->add_resource(RES_GOLD, 10000);
			state->update_player_economies(now);
			return {true, true, false, "Cheat activated: +10,000 Gold."};
		}
	}
	if (code == "rock on") {
		if (auto err = require_cheats()) { return *err; }
		if (auto player = state->get_player(sender)) {
			player->add_resource(RES_STONE, 10000);
			state->update_player_economies(now);
			return {true, true, false, "Cheat activated: +10,000 Stone."};
		}
	}

	// --- Map & Fog of War ---
	if (code == "marco") {
		if (auto err = require_cheats()) { return *err; }
		const bool next = not state->is_reveal_map_cheat();
		state->set_reveal_map_cheat(next);
		state->update_visibility(now);
		return {true, true, false, next ? "Marco: Entire map revealed." : "Marco: Map reveal disabled."};
	}
	if (code == "polo") {
		if (auto err = require_cheats()) { return *err; }
		const bool next = not state->is_no_fog_cheat();
		state->set_no_fog_cheat(next);
		state->update_visibility(now);
		return {true, true, false, next ? "Polo: Fog of War removed." : "Polo: Fog of War restored."};
	}

	// --- Speed & Control ---
	if (code == "aegis") {
		if (auto err = require_cheats()) { return *err; }
		const bool next = not state->fast_actions();
		state->set_fast_actions_cheat(next);
		return {true, true, false, next ? "Aegis: Fast build, train & research ON." : "Aegis: Fast actions OFF."};
	}
	if (code == "natural wonders") {
		if (auto err = require_cheats()) { return *err; }
		this->natural_wonders_active = not this->natural_wonders_active;
		size_t transferred = 0;
		if (this->natural_wonders_active) {
			for (const auto &[id, entity] : state->get_game_entities()) {
				if (entity->is_dead()) {
					continue;
				}
				auto ownership = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
				const bool is_animal = entity->definition.find("sheep") != std::string::npos
				                    or entity->definition.find("deer") != std::string::npos
				                    or entity->definition.find("boar") != std::string::npos
				                    or entity->definition.find("wolf") != std::string::npos;
				if (ownership and ownership->get_owners().get(now) == 2 and is_animal) {
					ownership->set_owner(now, sender);
					++transferred;
				}
			}
			state->update_visibility(now);
		}
		return {true, true, false,
		        "Natural Wonders: Transferred " + std::to_string(transferred) + " Gaia animals to your control."};
	}
	if (code == "i r winner") {
		if (auto err = require_cheats()) { return *err; }
		if (const auto &runtime = this->sim.get_trigger_runtime()) {
			runtime->mission_state().declare_victory("cheat_i_r_winner");
			return {true, true, false, "I R WINNER: Victory declared!"};
		}
		return {true, true, true, "No TriggerRuntime active."};
	}
	if (code == "black death") {
		if (auto err = require_cheats()) { return *err; }
		size_t total_removed = 0;
		std::vector<player_id_t> enemy_ids;
		for (const auto &[pid, _] : state->get_players()) {
			if (pid != sender and state->get_stance(sender, pid) == DiplomaticStance::Enemy) {
				enemy_ids.push_back(pid);
			}
		}
		if (enemy_ids.empty()) {
			for (const auto &[pid, _] : state->get_players()) {
				if (pid != sender and pid != 2) {
					enemy_ids.push_back(pid);
				}
			}
		}
		for (player_id_t eid : enemy_ids) {
			total_removed += defeat_player_entities(state, eid, now);
		}
		state->update_visibility(now);
		state->update_player_economies(now);
		return {true, true, false, "Black Death: Eliminated " + std::to_string(total_removed) + " enemy entities."};
	}
	if (code.rfind("torpedo", 0) == 0) {
		if (auto err = require_cheats()) { return *err; }
		std::string num_str = trim_copy(std::string_view{code}.substr(7));
		if (not num_str.empty()) {
			int target_num = 0;
			auto [ptr, ec] = std::from_chars(num_str.data(), num_str.data() + num_str.size(), target_num);
			if (ec == std::errc{} and target_num >= 0) {
				player_id_t target_pid = static_cast<player_id_t>(target_num);
				if (not state->get_player(target_pid) and target_num >= 1
				    and state->get_player(static_cast<player_id_t>(target_num - 1))) {
					target_pid = static_cast<player_id_t>(target_num - 1);
				}
				const size_t removed = defeat_player_entities(state, target_pid, now);
				state->update_visibility(now);
				state->update_player_economies(now);
				return {true, true, false,
				        "Torpedo " + num_str + ": Eliminated " + std::to_string(removed) + " entities of player " + std::to_string(target_pid) + "."};
			}
		}
	}

	// --- Special Unit Spawns ---
	if (code == "how do you turn this on") {
		if (auto err = require_cheats()) { return *err; }
		const std::string fqon = resolve_unit_fqon(state, {
			"de2_base.data.game_entity.generic.cobra_car.cobra_car.CobraCar",
			"de2_base.data.game_entity.generic.paladin.paladin.Paladin",
			"de2_base.data.game_entity.generic.knight.knight.Knight",
		});
		const auto pos = find_spawn_position(state, sender, now);
		if (auto spawned = spawn_unit_at(this->sim, state, sender, fqon, pos, now)) {
			state->update_visibility(now);
			state->update_player_economies(now);
			return {true, true, false, "How do you turn this on: Spawned " + fqon + " (id=" + std::to_string(spawned->get_id()) + ")."};
		}
	}
	if (code == "photon man") {
		if (auto err = require_cheats()) { return *err; }
		const std::string fqon = resolve_unit_fqon(state, {
			"de2_base.data.game_entity.generic.photon_man.photon_man.PhotonMan",
			"de2_base.data.game_entity.generic.hand_cannoneer.hand_cannoneer.HandCannoneer",
			"de2_base.data.game_entity.generic.archer.archer.Archer",
		});
		const auto pos = find_spawn_position(state, sender, now);
		if (auto spawned = spawn_unit_at(this->sim, state, sender, fqon, pos, now)) {
			state->update_visibility(now);
			state->update_player_economies(now);
			return {true, true, false, "Photon Man: Spawned " + fqon + " (id=" + std::to_string(spawned->get_id()) + ")."};
		}
	}
	if (code == "furious the monkey boy") {
		if (auto err = require_cheats()) { return *err; }
		const std::string fqon = resolve_unit_fqon(state, {
			"de2_base.data.game_entity.generic.furious_the_monkey_boy.furious_the_monkey_boy.FuriousTheMonkeyBoy",
			"de2_base.data.game_entity.generic.berserk.berserk.Berserk",
			"de2_base.data.game_entity.generic.militia.militia.Militia",
		});
		const auto pos = find_spawn_position(state, sender, now);
		if (auto spawned = spawn_unit_at(this->sim, state, sender, fqon, pos, now)) {
			state->update_visibility(now);
			state->update_player_economies(now);
			return {true, true, false, "Furious the Monkey Boy: Spawned " + fqon + " (id=" + std::to_string(spawned->get_id()) + ")."};
		}
	}
	if (code == "alpaca simulator") {
		if (auto err = require_cheats()) { return *err; }
		const std::string fqon = resolve_unit_fqon(state, {
			"de2_base.data.game_entity.generic.alpaca.alpaca.Alpaca",
			"de2_base.data.game_entity.generic.sheep.sheep.Sheep",
			"de2_base.data.game_entity.generic.camel_scout.camel_scout.CamelScout",
		});
		const auto pos = find_spawn_position(state, sender, now);
		if (auto spawned = spawn_unit_at(this->sim, state, sender, fqon, pos, now)) {
			state->update_visibility(now);
			state->update_player_economies(now);
			return {true, true, false, "Alpaca Simulator: Spawned " + fqon + " (id=" + std::to_string(spawned->get_id()) + ")."};
		}
	}

	// --- Unit Replacements ---
	if (code == "yes we khan") {
		if (auto err = require_cheats()) { return *err; }
		const std::string target = resolve_unit_fqon(state, {
			"de2_base.data.game_entity.generic.genghis_khan.genghis_khan.GenghisKhan",
			"de2_base.data.game_entity.generic.knight.knight.Knight",
		});
		const size_t n = replace_player_units(this->sim, state, sender, {"mangudai"}, target, now);
		state->update_visibility(now);
		return {true, true, false, "Yes We Khan: Transformed " + std::to_string(n) + " Mangudai."};
	}
	if (code == "put on your capes") {
		if (auto err = require_cheats()) { return *err; }
		const std::string target = resolve_unit_fqon(state, {
			"de2_base.data.game_entity.generic.teutonic_knight.teutonic_knight.TeutonicKnight",
		});
		const size_t n = replace_player_units(this->sim, state, sender, {"militia", "man_at_arms", "long_swordsman"}, target, now);
		state->update_visibility(now);
		return {true, true, false, "Put on your capes: Transformed " + std::to_string(n) + " infantry into Teutonic Knights."};
	}
	if (code == "cameleon") {
		if (auto err = require_cheats()) { return *err; }
		const std::string target = resolve_unit_fqon(state, {
			"de2_base.data.game_entity.generic.flaming_camel.flaming_camel.FlamingCamel",
		});
		const size_t n = replace_player_units(this->sim, state, sender, {"camel_rider", "camel_scout", "camel_archer"}, target, now);
		state->update_visibility(now);
		return {true, true, false, "Cameleon: Transformed " + std::to_string(n) + " camels into Flaming Camels."};
	}

	if (code == "woof woof") {
		if (auto err = require_cheats()) { return *err; }
		return {true, true, false, "Woof woof! Stormy dogs enabled."};
	}

	return {};
}


ChatCommandResult ChatConsole::execute(const std::string &raw_input, player_id_t sender_player) {
	const std::string trimmed = trim_copy(raw_input);
	if (trimmed.empty()) {
		return {};
	}
	const auto game = this->sim.get_game();
	const auto state = game ? game->get_state() : nullptr;
	std::string sender_name = "Player " + std::to_string(sender_player + 1);
	if (state) {
		const auto custom_name = state->get_player_name(sender_player);
		if (not custom_name.empty()) {
			sender_name = custom_name;
		}
	}

	// 1. Python REPL prefixes: "py ", "python ", "/py ", ">>> "
	auto strip_py_prefix = [&](std::string_view sv) -> std::optional<std::string> {
		const std::string lower = to_lower_copy(sv);
		if (lower.rfind("py ", 0) == 0) {
			return trim_copy(sv.substr(3));
		}
		if (lower.rfind("python ", 0) == 0) {
			return trim_copy(sv.substr(7));
		}
		if (lower.rfind("/py ", 0) == 0) {
			return trim_copy(sv.substr(4));
		}
		if (sv.rfind(">>> ", 0) == 0) {
			return trim_copy(sv.substr(4));
		}
		if (sv.rfind(">>>", 0) == 0) {
			return trim_copy(sv.substr(3));
		}
		return std::nullopt;
	};

	if (auto py_code = strip_py_prefix(trimmed)) {
		this->push_entry(sender_player, sender_name, trimmed, ChatEntryKind::PYTHON);
		auto res = this->try_execute_python(*py_code);
		this->push_entry(sender_player, "Python", res.response, res.is_error ? ChatEntryKind::ERROR : ChatEntryKind::PYTHON);
		return res;
	}

	// 2. openage slash commands: "/..."
	if (trimmed[0] == '/') {
		this->push_entry(sender_player, sender_name, trimmed, ChatEntryKind::SYSTEM);
		auto res = this->try_execute_debug_command(trimmed, sender_player);
		if (not res.response.empty() and to_lower_copy(trimmed) != "/clear") {
			this->push_entry(sender_player, "Console", res.response, res.is_error ? ChatEntryKind::ERROR : ChatEntryKind::SYSTEM);
		}
		return res;
	}

	// 3. AoE2 cheat codes (case-insensitive, spaces preserved)
	if (auto cheat_res = this->try_execute_cheat(trimmed, sender_player); cheat_res.handled) {
		this->push_entry(sender_player, sender_name, trimmed, ChatEntryKind::CHEAT);
		this->push_entry(sender_player, "Cheat", cheat_res.response,
		                 cheat_res.is_error ? ChatEntryKind::ERROR : ChatEntryKind::CHEAT);
		return cheat_res;
	}

	// 4. Multiplayer-ready plain chat message
	this->push_entry(sender_player, sender_name, trimmed, ChatEntryKind::CHAT);
	log::log(INFO << "[Chat] " << sender_name << ": " << trimmed);
	return {false, true, false, sender_name + ": " + trimmed};
}

} // namespace openage::gamestate
