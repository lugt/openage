// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "error/error.h"
#include "gamestate/types.h"
#include "log/message.h"
#include "util/json_minimal.h"

export module openage.gamestate.command_source;

export import openage.util.snapshot_text;

export namespace openage::gamestate::headless {

/**
 * Convert a JSON numeric value into exact hundredths (1.00 == 100).
 */
int64_t centi_from_json(const util::json::Value &val);

/**
 * Deterministic simulation command kinds consumed per tick.
 */
enum class sim_command_kind_t {
	MOVE,
	DAMAGE,
	ADD_RESOURCE,
	SPAWN,
	STOP,
};

/**
 * Deterministic command entry consumed at a discrete simulation tick.
 */
struct sim_command {
	uint64_t tick{0};
	sim_command_kind_t kind{sim_command_kind_t::STOP};
	entity_id_t entity_id{0};
	player_id_t player_id{0};
	std::string unit_type{};
	std::string resource_type{};
	int64_t x_centi{0};
	int64_t y_centi{0};
	int64_t speed_centi{0};
	int64_t amount_centi{0};
	int64_t hp_centi{0};
	int64_t max_hp_centi{0};
};

/**
 * Initial player stockpile entry in a synthetic scenario.
 */
struct scenario_player_init {
	player_id_t id{0};
	int64_t food_centi{0};
	int64_t wood_centi{0};
	int64_t gold_centi{0};
	int64_t stone_centi{0};
};

/**
 * Initial unit placement entry in a synthetic scenario.
 */
struct scenario_unit_init {
	entity_id_t id{0};
	player_id_t owner{0};
	std::string unit_type{};
	int64_t x_centi{0};
	int64_t y_centi{0};
	int64_t hp_centi{0};
	int64_t max_hp_centi{0};
};

/**
 * Self-contained deterministic scenario description loaded from JSON.
 */
struct scenario_definition {
	uint64_t seed{1};
	int map_width{16};
	int map_height{16};
	std::vector<scenario_player_init> players{};
	std::vector<scenario_unit_init> units{};
	std::vector<sim_command> commands{};
};

/**
 * Unified command stream interface decoupling simulation ticks from UI,
 * scripted JSON scenarios, lockstep network streams, and replay files.
 */
class ICommandSource {
public:
	virtual ~ICommandSource() = default;

	/**
	 * Fetch all commands scheduled to execute at \p tick in deterministic order.
	 */
	virtual std::vector<sim_command> poll_commands(uint64_t tick) = 0;
};

/**
 * JSON-backed scenario and command stream for headless simulation tests.
 */
class JsonScenarioCommandSource final : public ICommandSource {
public:
	explicit JsonScenarioCommandSource(scenario_definition scenario);

	/**
	 * Parse a scenario and command schedule from a JSON string.
	 */
	static JsonScenarioCommandSource from_json_text(const std::string &json_text);

	/**
	 * Parse a scenario and command schedule from a JSON file path.
	 */
	static JsonScenarioCommandSource from_json_file(const std::string &path);

	/**
	 * Access the parsed initial scenario metadata, map, players, and units.
	 */
	const scenario_definition &get_scenario() const;

	std::vector<sim_command> poll_commands(uint64_t tick) override;

private:
	scenario_definition scenario;
	std::map<uint64_t, std::vector<sim_command>> commands_by_tick;
};

} // namespace openage::gamestate::headless

namespace openage::gamestate::headless {

namespace {

int64_t json_integer(const util::json::Value &value) {
	if (not value.is_int() or value.as_int() < 0) {
		throw Error{MSG(err) << "Expected a nonnegative JSON integer"};
	}
	return value.as_int();
}

const std::string &json_string(const util::json::Value &value) {
	if (not value.is_string() or value.as_string().empty()
	    or value.as_string().find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != std::string::npos) {
		throw Error{MSG(err) << "Expected a nonempty ASCII identifier"};
	}
	return value.as_string();
}

const util::json::Array &json_array(const util::json::Value &value) {
	if (not value.is_array()) {
		throw Error{MSG(err) << "Expected a JSON array"};
	}
	return value.as_array();
}

scenario_definition parse_scenario_root(const util::json::Value &root) {
	if (not root.is_object()) {
		throw Error{MSG(err) << "JSON scenario root must be an object"};
	}

	scenario_definition def{};
	if (root.has("seed")) {
		int64_t raw_seed = json_integer(root.at("seed"));
		if (raw_seed < 0) {
			throw Error{MSG(err) << "JSON scenario seed must be non-negative"};
		}
		def.seed = static_cast<uint64_t>(raw_seed);
	}

	if (root.has("map")) {
		const auto &map_obj = root.at("map");
		if (json_integer(map_obj.at("width")) > 10000 or json_integer(map_obj.at("height")) > 10000) {
			throw Error{MSG(err) << "Synthetic map dimensions exceed 10000"};
		}
		def.map_width = static_cast<int>(json_integer(map_obj.at("width")));
		def.map_height = static_cast<int>(json_integer(map_obj.at("height")));
		if (def.map_width <= 0 or def.map_height <= 0) {
			throw Error{MSG(err) << "JSON scenario map dimensions must be positive"};
		}
	}

	if (root.has("players")) {
		for (const auto &p : json_array(root.at("players"))) {
			scenario_player_init player{};
			player.id = static_cast<player_id_t>(json_integer(p.at("id")));
			if (p.has("food")) {
				player.food_centi = centi_from_json(p.at("food"));
			}
			if (p.has("wood")) {
				player.wood_centi = centi_from_json(p.at("wood"));
			}
			if (p.has("gold")) {
				player.gold_centi = centi_from_json(p.at("gold"));
			}
			if (p.has("stone")) {
				player.stone_centi = centi_from_json(p.at("stone"));
			}
			def.players.push_back(player);
		}
	}

	if (root.has("units")) {
		for (const auto &u : json_array(root.at("units"))) {
			scenario_unit_init unit{};
			unit.id = static_cast<entity_id_t>(json_integer(u.at("id")));
			unit.owner = static_cast<player_id_t>(json_integer(u.at("owner")));
			unit.unit_type = json_string(u.at("type"));
			unit.x_centi = centi_from_json(u.at("x"));
			unit.y_centi = centi_from_json(u.at("y"));
			unit.hp_centi = centi_from_json(u.at("hp"));
			unit.max_hp_centi = u.has("max_hp") ? centi_from_json(u.at("max_hp")) : unit.hp_centi;
			def.units.push_back(std::move(unit));
		}
	}

	if (root.has("commands")) {
		for (const auto &c : json_array(root.at("commands"))) {
			sim_command cmd{};
			int64_t raw_tick = json_integer(c.at("tick"));
			if (raw_tick <= 0) {
				throw Error{MSG(err) << "Command tick must be >= 1, got " << raw_tick};
			}
			cmd.tick = static_cast<uint64_t>(raw_tick);

			const std::string &op = json_string(c.at("op"));
			if (op == "move") {
				cmd.kind = sim_command_kind_t::MOVE;
				cmd.entity_id = static_cast<entity_id_t>(json_integer(c.at("entity_id")));
				cmd.x_centi = centi_from_json(c.at("x"));
				cmd.y_centi = centi_from_json(c.at("y"));
				cmd.speed_centi = centi_from_json(c.at("speed"));
			}
			else if (op == "damage") {
				cmd.kind = sim_command_kind_t::DAMAGE;
				cmd.entity_id = static_cast<entity_id_t>(json_integer(c.at("entity_id")));
				cmd.amount_centi = centi_from_json(c.at("amount"));
			}
			else if (op == "add_resource") {
				cmd.kind = sim_command_kind_t::ADD_RESOURCE;
				cmd.player_id = static_cast<player_id_t>(json_integer(c.at("player_id")));
				cmd.resource_type = json_string(c.at("resource"));
				cmd.amount_centi = centi_from_json(c.at("amount"));
			}
			else if (op == "spawn") {
				cmd.kind = sim_command_kind_t::SPAWN;
				if (c.has("entity_id")) {
					cmd.entity_id = static_cast<entity_id_t>(json_integer(c.at("entity_id")));
				}
				cmd.player_id = static_cast<player_id_t>(json_integer(c.at("owner")));
				cmd.unit_type = json_string(c.at("type"));
				cmd.x_centi = centi_from_json(c.at("x"));
				cmd.y_centi = centi_from_json(c.at("y"));
				cmd.hp_centi = centi_from_json(c.at("hp"));
				cmd.max_hp_centi = c.has("max_hp") ? centi_from_json(c.at("max_hp")) : cmd.hp_centi;
			}
			else if (op == "stop") {
				cmd.kind = sim_command_kind_t::STOP;
				cmd.entity_id = static_cast<entity_id_t>(json_integer(c.at("entity_id")));
			}
			else {
				throw Error{MSG(err) << "Unsupported scenario command op: " << op};
			}

			def.commands.push_back(std::move(cmd));
		}
	}

	return def;
}

} // namespace


int64_t centi_from_json(const util::json::Value &val) {
	if (not val.is_number()) {
		throw Error{MSG(err) << "Expected numeric JSON value"};
	}
	double number = val.as_number();
	if (not std::isfinite(number) or number < 0 or number > 1000000) {
		throw Error{MSG(err) << "Synthetic scenario numbers must be in [0, 1000000]"};
	}
	double scaled = number * 100;
	auto rounded = std::llround(scaled);
	if (std::abs(scaled - static_cast<double>(rounded)) > 0.000001) {
		throw Error{MSG(err) << "Synthetic scenario numbers require at most two decimal places"};
	}
	return rounded;
}


JsonScenarioCommandSource::JsonScenarioCommandSource(scenario_definition scenario) :
	scenario{std::move(scenario)},
	commands_by_tick{} {
	for (const auto &cmd : this->scenario.commands) {
		this->commands_by_tick[cmd.tick].push_back(cmd);
	}
}


JsonScenarioCommandSource JsonScenarioCommandSource::from_json_text(const std::string &json_text) {
	try {
		auto root = util::json::parse(json_text);
		return JsonScenarioCommandSource{parse_scenario_root(root)};
	}
	catch (const util::json::JsonError &exc) {
		throw Error{MSG(err) << "Failed to parse JSON scenario text: " << exc.what()};
	}
}


JsonScenarioCommandSource JsonScenarioCommandSource::from_json_file(const std::string &path) {
	try {
		auto root = util::json::parse_file(path);
		return JsonScenarioCommandSource{parse_scenario_root(root)};
	}
	catch (const util::json::JsonError &exc) {
		throw Error{MSG(err) << "Failed to parse JSON scenario file '" << path << "': " << exc.what()};
	}
}


const scenario_definition &JsonScenarioCommandSource::get_scenario() const {
	return this->scenario;
}


std::vector<sim_command> JsonScenarioCommandSource::poll_commands(uint64_t tick) {
	auto it = this->commands_by_tick.find(tick);
	if (it == this->commands_by_tick.end()) {
		return {};
	}
	return it->second;
}

} // namespace openage::gamestate::headless
