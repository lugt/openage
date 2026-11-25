// Copyright 2026+ the openage authors. See copying.md for legal info.

#include "save_load.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "coord/phys.h"
#include "error/error.h"
#include "event/event_loop.h"
#include "gamestate/component/api/build.h"
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
#include "time/clock.h"
#include "time/time_loop.h"
#include "util/path.h"


namespace openage::gamestate {

namespace {

const nyan::fqon_t ATTR_HEALTH = "de2_base.data.util.attribute.types.Health";

std::string escape_field(std::string_view input) {
	std::string out;
	out.reserve(input.size());
	for (char ch : input) {
		if (ch == '\\') {
			out += "\\\\";
		}
		else if (ch == '|') {
			out += "\\p";
		}
		else if (ch == '\n') {
			out += "\\n";
		}
		else if (ch == '\r') {
			out += "\\r";
		}
		else {
			out.push_back(ch);
		}
	}
	return out;
}

std::string unescape_field(std::string_view input) {
	std::string out;
	out.reserve(input.size());
	for (size_t i = 0; i < input.size(); ++i) {
		if (input[i] == '\\' and i + 1 < input.size()) {
			const char next = input[++i];
			if (next == '\\') {
				out.push_back('\\');
			}
			else if (next == 'p') {
				out.push_back('|');
			}
			else if (next == 'n') {
				out.push_back('\n');
			}
			else if (next == 'r') {
				out.push_back('\r');
			}
			else {
				out.push_back(next);
			}
		}
		else {
			out.push_back(input[i]);
		}
	}
	return out;
}

std::vector<std::string> split_pipe(std::string_view line) {
	std::vector<std::string> fields;
	size_t start = 0;
	for (size_t i = 0; i <= line.size(); ++i) {
		if (i == line.size() or line[i] == '|') {
			fields.push_back(unescape_field(line.substr(start, i - start)));
			start = i + 1;
		}
	}
	return fields;
}

} // namespace


std::string GameSaveManager::directory(const util::Path &root) {
	return (root / "cfg" / "saves").resolve_native_path("w");
}

std::string GameSaveManager::serialize(GameSimulation &sim) {
	if (sim.has_ai()) {
		throw Error{MSG(err) << "AI checkpoint restoration requires stable entity IDs and pending production events"};
	}
	const auto game = sim.get_game();
	const auto state = game ? game->get_state() : nullptr;
	if (not state) {
		return {};
	}
	const auto clock = sim.get_time_loop() ? sim.get_time_loop()->get_clock() : nullptr;
	const time::time_t now = clock ? clock->get_time() : time::time_t{0};

	std::ostringstream out;
	out << std::setprecision(10);
	out << "OPENAGE_SAVE_V1|"
	    << now.to_double() << "|"
	    << (clock ? clock->get_speed().to_double() : 1.0) << "|"
	    << (state->is_fog_enabled() ? 1 : 0) << "|"
	    << (state->is_reveal_map_cheat() ? 1 : 0) << "|"
	    << (state->is_no_fog_cheat() ? 1 : 0) << "|"
	    << (state->fast_actions() ? 1 : 0) << "\n";

	std::vector<player_id_t> player_ids;
	for (const auto &[pid, _] : state->get_players()) {
		player_ids.push_back(pid);
	}
	std::ranges::sort(player_ids);
	for (player_id_t pid : player_ids) {
		const auto player = state->get_player(pid);
		Is_True(player != nullptr, ("GameSaveManager::serialize: player %u is null", pid));
		out << "PLAYER|" << pid << "|" << escape_field(state->get_player_name(pid)) << "\n";

		out << "SCORE|" << pid << "|" << player->score_ledger.destroyed_value
		    << "|" << player->score_ledger.research_value << "\n";
		if (const auto visibility = state->visibility(pid)) {
			out << "EXPLORED|" << pid << "|" << visibility->width << "|" << visibility->height << "|";
			for (const auto tile : visibility->tiles) {
				out << (tile == TileVisibility::Unexplored ? '0' : '1');
			}
			out << "\n";
		}

		std::vector<std::pair<std::string, int64_t>> res_list;
		for (const auto &[fqon, amt] : player->get_resources()) {
			res_list.emplace_back(fqon, amt);
		}
		std::ranges::sort(res_list);
		for (const auto &[fqon, amt] : res_list) {
			out << "RES|" << pid << "|" << escape_field(fqon) << "|" << amt << "\n";
		}

		std::vector<std::string> techs;
		for (const auto &t : player->get_researched()) {
			techs.push_back(t);
		}
		std::ranges::sort(techs);
		for (const auto &t : techs) {
			out << "TECH|" << pid << "|" << escape_field(t) << "\n";
		}
	}

	for (player_id_t a : player_ids) {
		for (player_id_t b : player_ids) {
			if (a != b) {
				out << "DIPLO|" << a << "|" << b << "|" << static_cast<int>(state->get_stance(a, b)) << "\n";
			}
		}
	}

	std::vector<entity_id_t> entity_ids;
	for (const auto &[eid, entity] : state->get_game_entities()) {
		if (entity and not entity->is_dead()) {
			entity_ids.push_back(eid);
		}
	}
	std::ranges::sort(entity_ids);
	for (entity_id_t eid : entity_ids) {
		const auto &entity = state->get_game_entity(eid);
		auto pos_comp = entity->try_get_component_as<component::Position>(component::component_t::POSITION);
		auto own_comp = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		if (not pos_comp or not own_comp) {
			continue;
		}
		const auto pos = pos_comp->get_positions().get(now);
		const auto owner = own_comp->get_owners().get(now);
		int64_t hp = -1;
		if (auto live = entity->try_get_component_as<component::Live>(component::component_t::LIVE)) {
			if (auto val = live->get_attribute_value(now, "de2_base.data.util.attribute.types.Health")) {
				hp = *val;
			}
		}
		int64_t build_progress = -1;
		if (auto build = entity->try_get_component_as<component::Build>(component::component_t::BUILD)) {
			build_progress = build->get_progress();
		}
		out << "ENTITY|" << escape_field(entity->definition) << "|"
		    << owner << "|"
		    << pos.ne.to_double() << "|" << pos.se.to_double() << "|" << pos.up.to_double() << "|"
		    << hp << "|" << build_progress << "|" << (entity->unpacked ? 1 : 0) << "\n";
	}
	return out.str();
}


bool GameSaveManager::deserialize(GameSimulation &sim,
                                  const std::string &snapshot_text,
                                  std::string *error_out) {
	const auto game = sim.get_game();
	const auto state = game ? game->get_state() : nullptr;
	const auto &factory = sim.get_entity_factory();
	const auto &loop = sim.get_event_loop();
	if (not state or not factory or not loop) {
		if (error_out) {
			*error_out = "GameSimulation is not initialized.";
		}
		return false;
	}

	if (sim.has_ai()) {
		if (error_out) { *error_out = "Loading with active AI is unavailable until complete AI checkpoints are supported."; }
		return false;
	}
	std::istringstream in{snapshot_text};
	std::string line;
	if (not std::getline(in, line)) {
		if (error_out) {
			*error_out = "Empty save file.";
		}
		return false;
	}
	const auto header = split_pipe(line);
	if (header.size() < 7 or header[0] != "OPENAGE_SAVE_V1") {
		if (error_out) {
			*error_out = "Unsupported or corrupted save header.";
		}
		return false;
	}

	struct EntityRecord {
		std::string fqon;
		player_id_t owner{0};
		double ne{0.0};
		double se{0.0};
		double up{0.0};
		int64_t hp{-1};
		int64_t build_progress{-1};
		bool unpacked{false};
	};
	struct ResRecord {
		player_id_t pid{0};
		std::string fqon;
		int64_t amount{0};
	};
	struct TechRecord {
		player_id_t pid{0};
		std::string fqon;
	};
	struct DiploRecord {
		player_id_t a{0};
		player_id_t b{0};
		int stance{0};
	};
	struct PlayerNameRecord {
		player_id_t pid{0};
		std::string name;
	};

	std::map<player_id_t, ScoreLedger> score_records;
	std::map<player_id_t, VisibilitySnapshot> exploration_records;
	std::vector<PlayerNameRecord> player_names;
	std::vector<ResRecord> res_records;
	std::vector<TechRecord> tech_records;
	std::vector<DiploRecord> diplo_records;
	std::vector<EntityRecord> entity_records;

	while (std::getline(in, line)) {
		if (line.empty()) {
			continue;
		}
		const auto f = split_pipe(line);
		if (f.empty()) {
			continue;
		}
		try {
			if (f[0] == "PLAYER" and f.size() >= 3) {
				player_names.push_back({static_cast<player_id_t>(std::stoul(f[1])), f[2]});
			}
			else if (f[0] == "SCORE" or f[0] == "EXPLORED") {
				const bool score = f[0] == "SCORE";
				if (f.size() != (score ? 4 : 5)) {
					if (error_out) { *error_out = "Malformed score or exploration record."; }
					return false;
				}
				const auto pid = static_cast<player_id_t>(std::stoull(f[1]));
				if (not state->get_players().contains(pid)) {
					if (error_out) { *error_out = "Score record references an unknown player."; }
					return false;
				}
				if (score) {
					const ScoreLedger ledger{std::stoll(f[2]), std::stoll(f[3])};
					if (ledger.destroyed_value < 0 or ledger.research_value < 0
					    or not score_records.emplace(pid, ledger).second) {
						if (error_out) { *error_out = "Invalid or duplicate score record."; }
						return false;
					}
				}
				else {
					const auto map = state->get_map();
					const auto width = std::stoull(f[2]);
					const auto height = std::stoull(f[3]);
					if (not map or width != map->get_size()[0] or height != map->get_size()[1]
					    or f[4].size() != width * height
					    or f[4].find_first_not_of("01") != std::string::npos
					    or exploration_records.contains(pid)) {
						if (error_out) { *error_out = "Invalid or duplicate exploration record."; }
						return false;
					}
					VisibilitySnapshot visibility;
					visibility.width = width;
					visibility.height = height;
					for (const auto tile : f[4]) {
						visibility.tiles.push_back(tile == '1' ? TileVisibility::Explored : TileVisibility::Unexplored);
					}
					exploration_records.emplace(pid, std::move(visibility));
				}
			}
			else if (f[0] == "RES" and f.size() >= 4) {
				res_records.push_back({static_cast<player_id_t>(std::stoul(f[1])), f[2], std::stoll(f[3])});
			}
			else if (f[0] == "TECH" and f.size() >= 3) {
				tech_records.push_back({static_cast<player_id_t>(std::stoul(f[1])), f[2]});
			}
			else if (f[0] == "DIPLO" and f.size() >= 4) {
				diplo_records.push_back({
					static_cast<player_id_t>(std::stoul(f[1])),
					static_cast<player_id_t>(std::stoul(f[2])),
					std::stoi(f[3]),
				});
			}
			else if (f[0] == "ENTITY" and f.size() >= 9) {
				entity_records.push_back({
					f[1],
					static_cast<player_id_t>(std::stoul(f[2])),
					std::stod(f[3]),
					std::stod(f[4]),
					std::stod(f[5]),
					std::stoll(f[6]),
					std::stoll(f[7]),
					std::stoi(f[8]) != 0,
				});
			}
		}
		catch (const std::exception &ex) {
			if (error_out) {
				*error_out = std::string{"Malformed save record: "} + ex.what();
			}
			return false;
		}
	}

	const auto clock = sim.get_time_loop() ? sim.get_time_loop()->get_clock() : nullptr;
	const time::time_t now = clock ? clock->get_time() : time::time_t{0};

	state->set_fog_enabled(header[3] != "0");
	state->set_reveal_map_cheat(header[4] != "0");
	state->set_no_fog_cheat(header[5] != "0");
	state->set_fast_actions_cheat(header[6] != "0");

	for (const auto &[id, player] : state->get_players()) {
		const auto found = score_records.find(id);
		player->score_ledger = found == score_records.end() ? ScoreLedger{} : found->second;
	}
	state->restore_exploration(std::move(exploration_records));
	for (const auto &pn : player_names) {
		if (state->get_player(pn.pid)) {
			state->set_player_name(pn.pid, pn.name);
		}
	}
	for (const auto &rr : res_records) {
		if (auto player = state->get_player(rr.pid)) {
			player->set_resource(rr.fqon, rr.amount);
		}
	}
	for (const auto &tr : tech_records) {
		if (auto player = state->get_player(tr.pid)) {
			player->record_research(tr.fqon);
		}
	}
	for (const auto &dr : diplo_records) {
		state->set_stance(dr.a, dr.b, static_cast<DiplomaticStance>(dr.stance));
	}

	// Remove all existing entities before restoring saved entities.
	std::vector<entity_id_t> existing_ids;
	for (const auto &[eid, _] : state->get_game_entities()) {
		existing_ids.push_back(eid);
	}
	for (entity_id_t eid : existing_ids) {
		if (state->has_game_entity(eid)) {
			state->get_game_entity(eid)->mark_dead();
			state->remove_game_entity(eid);
		}
	}

	for (const auto &er : entity_records) {
		try {
			auto entity = factory->add_game_entity(loop, state, er.owner, er.fqon, false);
			if (not entity) {
				continue;
			}
			if (auto pos_comp = entity->try_get_component_as<component::Position>(component::component_t::POSITION)) {
				pos_comp->set_position(now, coord::phys3{er.ne, er.se, er.up});
			}
			if (auto own_comp = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP)) {
				own_comp->set_owner(now, er.owner);
			}
			if (er.hp >= 0) {
				if (auto live = entity->try_get_component_as<component::Live>(component::component_t::LIVE)) {
					live->set_attribute(now, ATTR_HEALTH, er.hp);
				}
			}
			if (er.build_progress >= 0) {
				if (auto build = entity->try_get_component_as<component::Build>(component::component_t::BUILD)) {
					build->set_progress(er.build_progress);
					build->exact_progress = static_cast<double>(er.build_progress);
				}
			}
			entity->unpacked = er.unpacked;
			factory->attach_entity_renderer(entity);
			state->add_game_entity(entity);
			if (auto activity = entity->try_get_component_as<component::Activity>(component::component_t::ACTIVITY)) {
				activity->init(now);
				entity->get_manager()->run_activity_system(now);
			}
		}
		catch (const std::exception &ex) {
			log::log(WARN << "GameSaveManager::deserialize skipped entity " << er.fqon << ": " << ex.what());
		}
	}

	state->update_visibility(now);
	state->update_player_economies(now);
	return true;
}


bool GameSaveManager::save_to_file(GameSimulation &sim,
                                   const std::string &file_path,
                                   std::string *error_out) {
	if (sim.has_ai()) {
		if (error_out) { *error_out = "Saving with active AI is unavailable until complete AI checkpoints are supported."; }
		return false;
	}
	const std::string payload = GameSaveManager::serialize(sim);
	if (payload.empty()) {
		if (error_out) {
			*error_out = "No active game state to save.";
		}
		return false;
	}
	try {
		std::filesystem::path p{file_path};
		if (p.has_parent_path()) {
			std::filesystem::create_directories(p.parent_path());
		}
		std::ofstream out{p, std::ios::binary | std::ios::trunc};
		if (not out.is_open()) {
			if (error_out) {
				*error_out = "Could not open file for writing: " + file_path;
			}
			return false;
		}
		out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
		return out.good();
	}
	catch (const std::exception &ex) {
		if (error_out) {
			*error_out = ex.what();
		}
		return false;
	}
}


bool GameSaveManager::load_from_file(GameSimulation &sim,
                                     const std::string &file_path,
                                     std::string *error_out) {
	try {
		std::ifstream in{file_path, std::ios::binary};
		if (not in.is_open()) {
			if (error_out) {
				*error_out = "Save file not found: " + file_path;
			}
			return false;
		}
		std::ostringstream buf;
		buf << in.rdbuf();
		return GameSaveManager::deserialize(sim, buf.str(), error_out);
	}
	catch (const std::exception &ex) {
		if (error_out) {
			*error_out = ex.what();
		}
		return false;
	}
}


std::vector<SaveSlotInfo> GameSaveManager::list_saves(const std::string &saves_dir) {
	std::vector<SaveSlotInfo> result;
	std::error_code ec;
	if (not std::filesystem::exists(saves_dir, ec)) {
		return result;
	}
	for (const auto &entry : std::filesystem::directory_iterator(saves_dir, ec)) {
		if (not entry.is_regular_file() or entry.path().extension() != ".oasave") {
			continue;
		}
		std::string summary;
		std::ifstream in{entry.path()};
		std::string first_line;
		if (std::getline(in, first_line)) {
			const auto fields = split_pipe(first_line);
			if (fields.size() >= 2 and fields[0] == "OPENAGE_SAVE_V1") {
				summary = "t=" + fields[1] + "s";
			}
		}
		result.push_back({
			entry.path().stem().string(),
			entry.path().string(),
			summary,
		});
	}
	std::ranges::sort(result, {}, &SaveSlotInfo::name);
	return result;
}

} // namespace openage::gamestate
