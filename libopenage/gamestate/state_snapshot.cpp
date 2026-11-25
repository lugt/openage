// Copyright 2026+ the openage authors. See copying.md for legal info.

module;

#include <algorithm>
#include <cstdint>
#include <limits>
#include <locale>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "coord/phys.h"
#include "curve/discrete.h"
#include "error/error.h"
#include "event/event_loop.h"
#include "event/evententity.h"
#include "event/eventhandler.h"
#include "event/state.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/player.h"
#include "gamestate/types.h"
#include "log/message.h"
#include "rng/rng.h"
#include "time/time.h"

export module openage.gamestate.state_snapshot;

import openage.gamestate.command_source;
import nyan;

export namespace openage::gamestate::headless {

/**
 * Player resource state in hundredths (1.00 == 100).
 */
struct headless_player {
	player_id_t id{0};
	int64_t food_centi{0};
	int64_t wood_centi{0};
	int64_t gold_centi{0};
	int64_t stone_centi{0};
};

/**
 * Deterministic entity record backed by a real GameEntity and time curves.
 */
struct headless_entity {
	entity_id_t id{0};
	player_id_t owner{0};
	std::string unit_type{};
	int64_t x_centi{0};
	int64_t y_centi{0};
	int64_t hp_centi{0};
	int64_t max_hp_centi{0};
	bool moving{false};
	int64_t target_x_centi{0};
	int64_t target_y_centi{0};
	int64_t speed_centi{0};
	std::shared_ptr<GameEntity> entity{};
	std::shared_ptr<curve::Discrete<int64_t>> hp_curve{};
};

/**
 * Headless deterministic simulation world with reversible text snapshots.
 *
 * Runs without windowing, OpenGL/Vulkan, or audio devices, driving commands
 * from an ICommandSource through openage's EventLoop and fixed-point curves.
 */
class HeadlessWorld {
public:
	HeadlessWorld();

	HeadlessWorld(const HeadlessWorld &) = delete;
	HeadlessWorld &operator=(const HeadlessWorld &) = delete;
	HeadlessWorld(HeadlessWorld &&) noexcept = default;
	HeadlessWorld &operator=(HeadlessWorld &&) noexcept = default;

	/**
	 * Build a fresh world from a parsed scenario definition at tick 0.
	 */
	static HeadlessWorld from_scenario(const scenario_definition &scenario);

	/**
	 * Reconstruct a world from a plain-text snapshot produced by serialize().
	 */
	static HeadlessWorld deserialize(const std::string &snapshot_text);

	/**
	 * Export the world state as deterministic plain text (sorted IDs, 2 decimals).
	 */
	std::string serialize() const;

	/**
	 * Advance the world by \p num_ticks ticks, polling \p source at each tick.
	 */
	void step_ticks(uint64_t num_ticks, ICommandSource &source);

	/**
	 * Convert a discrete tick number into simulation fixed-point time (0.25s/tick).
	 */
	static time::time_t tick_to_time(uint64_t tick);

	uint64_t get_tick() const;
	int get_map_width() const;
	int get_map_height() const;
	std::map<player_id_t, headless_player> get_players() const;
	std::map<entity_id_t, headless_entity> get_entities() const;

	/**
	 * Internal step hook invoked by the EventLoop tick handler.
	 */
	void execute_scheduled_tick(uint64_t tick, const std::vector<sim_command> &commands);

private:
	void init_event_wiring();
	void add_player(const headless_player &player);
	headless_entity build_entity_record(entity_id_t id,
	                                    player_id_t owner,
	                                    std::string unit_type,
	                                    int64_t x_centi,
	                                    int64_t y_centi,
	                                    int64_t hp_centi,
	                                    int64_t max_hp_centi,
	                                    time::time_t at);
	void sync_entity_position(headless_entity &rec, time::time_t at);
	void sync_entity_hp(headless_entity &rec, time::time_t at);
	void apply_command(const sim_command &cmd, time::time_t at);

	std::shared_ptr<openage::event::EventLoop> event_loop;
	std::shared_ptr<nyan::Database> database;
	std::shared_ptr<GameState> game_state;
	std::shared_ptr<openage::event::EventEntity> driver_entity;
	rng::RNG rng;
	uint64_t current_tick{0};
	int map_width{16};
	int map_height{16};
	entity_id_t next_entity_id{1};
	std::set<player_id_t> players{};
	std::map<entity_id_t, headless_entity> entities{};
};

} // namespace openage::gamestate::headless

namespace openage::gamestate::headless {

namespace {

constexpr int64_t RAW_TIME_PER_TICK = 16384; // 0.25s in FixedPoint<int64_t, 16>


uint64_t integer_sqrt(uint64_t n) {
	if (n == 0) {
		return 0;
	}
	uint64_t x = n;
	uint64_t y = (x + 1ULL) / 2ULL;
	while (y < x) {
		x = y;
		y = (x + n / x) / 2ULL;
	}
	return x;
}


coord::phys3 centi_to_phys3(int64_t x_centi, int64_t y_centi) {
	int64_t raw_ne = (x_centi * 65536LL) / 100LL;
	int64_t raw_se = (y_centi * 65536LL) / 100LL;
	return coord::phys3{
		coord::phys_t::from_raw_value(raw_ne),
		coord::phys_t::from_raw_value(raw_se),
		coord::phys_t::from_int(0),
	};
}


class HeadlessDriverEntity final : public openage::event::EventEntity {
public:
	explicit HeadlessDriverEntity(const std::shared_ptr<openage::event::EventLoop> &loop) :
		EventEntity{loop} {}

	size_t id() const override {
		return 1;
	}

	std::string idstr() const override {
		return "headless.driver";
	}
};


class HeadlessTickState final : public openage::event::State {
public:
	HeadlessTickState(HeadlessWorld &world,
	                  uint64_t tick,
	                  std::vector<sim_command> commands) :
		State{nullptr},
		world{&world},
		tick{tick},
		commands{std::move(commands)} {}

	HeadlessWorld *world;
	uint64_t tick;
	std::vector<sim_command> commands;
};


class HeadlessStepHandler final : public openage::event::OnceEventHandler {
public:
	HeadlessStepHandler() :
		OnceEventHandler{"headless.step_tick"} {}

	void setup_event(const std::shared_ptr<openage::event::Event> & /*event*/,
	                 const std::shared_ptr<openage::event::State> & /*state*/) override {}

	void invoke(openage::event::EventLoop & /*loop*/,
	            const std::shared_ptr<openage::event::EventEntity> & /*target*/,
	            const std::shared_ptr<openage::event::State> &state,
	            const time::time_t & /*now*/,
	            const param_map & /*params*/) override {
		auto tick_state = std::dynamic_pointer_cast<HeadlessTickState>(state);
		if (not tick_state or tick_state->world == nullptr) {
			throw Error{MSG(err) << "headless.step_tick invoked with invalid state"};
		}
		tick_state->world->execute_scheduled_tick(tick_state->tick, tick_state->commands);
	}

	time::time_t predict_invoke_time(const std::shared_ptr<openage::event::EventEntity> & /*target*/,
	                                 const std::shared_ptr<openage::event::State> & /*state*/,
	                                 const time::time_t &at) override {
		return at;
	}
};


std::vector<std::string_view> split_tokens(std::string_view line) {
	std::vector<std::string_view> out;
	size_t pos = 0;
	while (pos < line.size()) {
		while (pos < line.size() and (line[pos] == ' ' or line[pos] == '\t')) {
			pos += 1;
		}
		if (pos >= line.size()) {
			break;
		}
		size_t end = pos;
		while (end < line.size() and line[end] != ' ' and line[end] != '\t') {
			end += 1;
		}
		out.push_back(line.substr(pos, end - pos));
		pos = end;
	}
	return out;
}


std::map<std::string_view, std::string_view> parse_kv_tokens(
	const std::vector<std::string_view> &tokens,
	size_t start_idx) {
	std::map<std::string_view, std::string_view> kvs;
	for (size_t i = start_idx; i < tokens.size(); ++i) {
		auto eq = tokens[i].find('=');
		if (eq == std::string_view::npos or eq == 0 or eq + 1 >= tokens[i].size()) {
			throw Error{MSG(err) << "Malformed snapshot token: " << std::string{tokens[i]}};
		}
		auto key = tokens[i].substr(0, eq);
		auto val = tokens[i].substr(eq + 1);
		auto [_, inserted] = kvs.emplace(key, val);
		if (not inserted) {
			throw Error{MSG(err) << "Duplicate snapshot key: " << std::string{key}};
		}
	}
	return kvs;
}


std::string_view require_kv(const std::map<std::string_view, std::string_view> &kvs,
                            std::string_view key) {
	auto it = kvs.find(key);
	if (it == kvs.end()) {
		throw Error{MSG(err) << "Missing required snapshot key: " << std::string{key}};
	}
	return it->second;
}


int64_t parse_i64(std::string_view text) {
	if (text.empty()) {
		throw Error{MSG(err) << "Expected integer in snapshot, got empty value"};
	}
	std::string s{text};
	size_t idx = 0;
	long long val = 0;
	try {
		val = std::stoll(s, &idx, 10);
	}
	catch (...) {
		throw Error{MSG(err) << "Invalid integer in snapshot: " << s};
	}
	if (idx != s.size()) {
		throw Error{MSG(err) << "Trailing characters in integer: " << s};
	}
	return static_cast<int64_t>(val);
}


uint64_t parse_u64(std::string_view text) {
	if (text.empty() or text.front() == '-') {
		throw Error{MSG(err) << "Expected unsigned integer in snapshot: " << std::string{text}};
	}
	std::string s{text};
	size_t idx = 0;
	unsigned long long val = 0;
	try {
		val = std::stoull(s, &idx, 10);
	}
	catch (...) {
		throw Error{MSG(err) << "Invalid unsigned integer in snapshot: " << s};
	}
	if (idx != s.size()) {
		throw Error{MSG(err) << "Trailing characters in unsigned integer: " << s};
	}
	return static_cast<uint64_t>(val);
}


std::pair<std::string_view, std::string_view> parse_paren_pair(std::string_view text) {
	if (text.size() < 5 or text.front() != '(' or text.back() != ')') {
		throw Error{MSG(err) << "Expected '(a,b)' tuple, got: " << std::string{text}};
	}
	auto inner = text.substr(1, text.size() - 2);
	auto comma = inner.find(',');
	if (comma == std::string_view::npos or inner.find(',', comma + 1) != std::string_view::npos) {
		throw Error{MSG(err) << "Expected 2-element tuple, got: " << std::string{text}};
	}
	return {inner.substr(0, comma), inner.substr(comma + 1)};
}

} // namespace


HeadlessWorld::HeadlessWorld() :
	event_loop{std::make_shared<openage::event::EventLoop>()},
	database{nyan::Database::create()},
	game_state{std::make_shared<GameState>(this->database, this->event_loop)},
	driver_entity{nullptr},
	rng{1} {
	this->database->load("snapshot.nyan", [](const std::string &name) {
		return std::make_shared<nyan::File>(name, "!version 1\n\nLive():\n    pass\n");
	});
	this->init_event_wiring();
}


void HeadlessWorld::init_event_wiring() {
	this->driver_entity = std::make_shared<HeadlessDriverEntity>(this->event_loop);
	this->event_loop->add_event_handler(std::make_shared<HeadlessStepHandler>());
}


time::time_t HeadlessWorld::tick_to_time(uint64_t tick) {
	if (tick > static_cast<uint64_t>(std::numeric_limits<int64_t>::max() / RAW_TIME_PER_TICK)) {
		throw Error{MSG(err) << "Tick outside fixed-point time range"};
	}
	return time::time_t::from_raw_value(static_cast<int64_t>(tick) * RAW_TIME_PER_TICK);
}


headless_entity HeadlessWorld::build_entity_record(entity_id_t id,
                                                   player_id_t owner,
                                                   std::string unit_type,
                                                   int64_t x_centi,
                                                   int64_t y_centi,
                                                   int64_t hp_centi,
                                                   int64_t max_hp_centi,
                                                   time::time_t at) {
	if (id == std::numeric_limits<entity_id_t>::max() or unit_type.empty()
	    or unit_type.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") != std::string::npos
	    or x_centi < 0 or x_centi > static_cast<int64_t>(this->map_width) * 100
	    or y_centi < 0 or y_centi > static_cast<int64_t>(this->map_height) * 100
	    or hp_centi <= 0 or max_hp_centi < hp_centi or max_hp_centi > 100000000) {
		throw Error{MSG(err) << "Invalid synthetic entity state"};
	}
	auto ge = std::make_shared<GameEntity>(id);
	ge->definition = unit_type;

	auto pos_comp = std::make_shared<component::Position>(this->event_loop);
	pos_comp->set_position(at, centi_to_phys3(x_centi, y_centi));
	ge->add_component(pos_comp);

	auto own_comp = std::make_shared<component::Ownership>(
		this->event_loop,
		owner,
		at);
	ge->add_component(own_comp);

	auto hp_curve = std::make_shared<curve::Discrete<int64_t>>(
		this->event_loop,
		static_cast<size_t>(id),
		"headless.hp." + std::to_string(id));
	hp_curve->set_last(at, hp_centi);
	auto ability = this->game_state->get_db_view()->get_object("snapshot.Live");
	auto live = std::make_shared<component::Live>(this->event_loop, ability, at);
	live->add_attribute(at, "snapshot.Health", hp_curve);
	ge->add_component(live);

	this->game_state->add_game_entity(ge);

	headless_entity rec{};
	rec.id = id;
	rec.owner = owner;
	rec.unit_type = std::move(unit_type);
	rec.x_centi = x_centi;
	rec.y_centi = y_centi;
	rec.hp_centi = hp_centi;
	rec.max_hp_centi = max_hp_centi;
	rec.entity = std::move(ge);
	rec.hp_curve = std::move(hp_curve);
	return rec;
}


void HeadlessWorld::sync_entity_position(headless_entity &rec, time::time_t at) {
	auto pos_comp = rec.entity->try_get_component_as<component::Position>(
		component::component_t::POSITION);
	if (not pos_comp) {
		throw Error{MSG(err) << "Missing headless Position component"};
	}
	pos_comp->set_position(at, centi_to_phys3(rec.x_centi, rec.y_centi));
}


void HeadlessWorld::sync_entity_hp(headless_entity &rec, time::time_t at) {
	auto live = rec.entity->try_get_component_as<component::Live>(component::component_t::LIVE);
	live->set_attribute(at, "snapshot.Health", rec.hp_centi);
}


void HeadlessWorld::add_player(const headless_player &record) {
	if (record.food_centi < 0 or record.wood_centi < 0 or record.gold_centi < 0 or record.stone_centi < 0) {
		throw Error{MSG(err) << "Negative synthetic player resources"};
	}
	auto player = std::make_shared<Player>(record.id, this->game_state->get_db_view());
	player->add_resource("food", record.food_centi);
	player->add_resource("wood", record.wood_centi);
	player->add_resource("gold", record.gold_centi);
	player->add_resource("stone", record.stone_centi);
	this->game_state->add_player(player);
}


HeadlessWorld HeadlessWorld::from_scenario(const scenario_definition &scenario) {
	if (scenario.map_width <= 0 or scenario.map_width > 10000
	    or scenario.map_height <= 0 or scenario.map_height > 10000) {
		throw Error{MSG(err) << "Invalid synthetic map dimensions"};
	}
	HeadlessWorld world{};
	world.rng.seed(scenario.seed);
	world.current_tick = 0;
	world.map_width = scenario.map_width;
	world.map_height = scenario.map_height;
	world.next_entity_id = 1;

	for (const auto &p : scenario.players) {
		headless_player player{};
		player.id = p.id;
		player.food_centi = p.food_centi;
		player.wood_centi = p.wood_centi;
		player.gold_centi = p.gold_centi;
		player.stone_centi = p.stone_centi;
		auto [_, inserted] = world.players.insert(player.id);
		world.add_player(player);
		if (not inserted) {
			throw Error{MSG(err) << "Duplicate player id in scenario: " << player.id};
		}
	}

	time::time_t t0 = tick_to_time(0);
	for (const auto &u : scenario.units) {
		if (not world.players.contains(u.owner)) {
			throw Error{MSG(err) << "Unit " << u.id << " references unknown player " << u.owner};
		}
		auto rec = world.build_entity_record(
			u.id,
			u.owner,
			u.unit_type,
			u.x_centi,
			u.y_centi,
			u.hp_centi,
			u.max_hp_centi,
			t0);
		auto [_, inserted] = world.entities.emplace(u.id, std::move(rec));
		if (not inserted) {
			throw Error{MSG(err) << "Duplicate entity id in scenario: " << u.id};
		}
		if (u.id >= world.next_entity_id) {
			world.next_entity_id = u.id + 1;
		}
	}

	return world;
}


void HeadlessWorld::apply_command(const sim_command &cmd, time::time_t at) {
	switch (cmd.kind) {
	case sim_command_kind_t::MOVE: {
		auto it = this->entities.find(cmd.entity_id);
		if (it == this->entities.end()) {
			throw Error{MSG(err) << "MOVE targets missing entity " << cmd.entity_id};
		}
		if (cmd.speed_centi <= 0) {
			throw Error{MSG(err) << "MOVE speed must be > 0 for entity " << cmd.entity_id};
		}
		int64_t max_x = static_cast<int64_t>(this->map_width) * 100LL;
		int64_t max_y = static_cast<int64_t>(this->map_height) * 100LL;
		it->second.target_x_centi = std::clamp(cmd.x_centi, int64_t{0}, max_x);
		it->second.target_y_centi = std::clamp(cmd.y_centi, int64_t{0}, max_y);
		it->second.speed_centi = cmd.speed_centi;
		it->second.moving = (it->second.x_centi != it->second.target_x_centi
		                     or it->second.y_centi != it->second.target_y_centi);
		break;
	}

	case sim_command_kind_t::STOP: {
		auto it = this->entities.find(cmd.entity_id);
		if (it == this->entities.end()) {
			throw Error{MSG(err) << "STOP targets missing entity " << cmd.entity_id};
		}
		it->second.moving = false;
		it->second.target_x_centi = 0;
		it->second.target_y_centi = 0;
		it->second.speed_centi = 0;
		break;
	}

	case sim_command_kind_t::DAMAGE: {
		auto it = this->entities.find(cmd.entity_id);
		if (it == this->entities.end()) {
			throw Error{MSG(err) << "DAMAGE targets missing entity " << cmd.entity_id};
		}
		// Deterministic critical roll from world RNG exercises RNG state across save/restore.
		int64_t crit_bonus_centi = static_cast<int64_t>(this->rng.random_range(0, 3)) * 50LL;
		int64_t total_damage = cmd.amount_centi + crit_bonus_centi;
		it->second.hp_centi = std::max<int64_t>(0, it->second.hp_centi - total_damage);
		this->sync_entity_hp(it->second, at);
		break;
	}

	case sim_command_kind_t::ADD_RESOURCE: {
		auto it = this->players.find(cmd.player_id);
		if (it == this->players.end()) {
			throw Error{MSG(err) << "ADD_RESOURCE targets missing player " << cmd.player_id};
		}
		if (cmd.resource_type != "food" and cmd.resource_type != "wood"
		    and cmd.resource_type != "gold" and cmd.resource_type != "stone") {
			throw Error{MSG(err) << "Unknown resource type: " << cmd.resource_type};
		}
		if (cmd.amount_centi < 0) {
			throw Error{MSG(err) << "ADD_RESOURCE requires a nonnegative amount"};
		}
		const auto &player = this->game_state->get_player(cmd.player_id);
		if (player->get_resource(cmd.resource_type) > std::numeric_limits<int64_t>::max() - cmd.amount_centi) {
			throw Error{MSG(err) << "Resource amount overflow"};
		}
		player->add_resource(cmd.resource_type, cmd.amount_centi);
		break;
	}

	case sim_command_kind_t::SPAWN: {
		if (not this->players.contains(cmd.player_id)) {
			throw Error{MSG(err) << "SPAWN references unknown player " << cmd.player_id};
		}
		entity_id_t spawn_id = cmd.entity_id != 0 ? cmd.entity_id : this->next_entity_id;
		if (this->entities.contains(spawn_id)) {
			throw Error{MSG(err) << "SPAWN collides with existing entity " << spawn_id};
		}
		auto rec = this->build_entity_record(
			spawn_id,
			cmd.player_id,
			cmd.unit_type,
			cmd.x_centi,
			cmd.y_centi,
			cmd.hp_centi,
			cmd.max_hp_centi,
			at);
		this->entities.emplace(spawn_id, std::move(rec));
		if (spawn_id >= this->next_entity_id) {
			this->next_entity_id = spawn_id + 1;
		}
		break;
	}
	}
}


void HeadlessWorld::execute_scheduled_tick(uint64_t tick,
                                           const std::vector<sim_command> &commands) {
	time::time_t at = tick_to_time(tick);

	for (const auto &cmd : commands) {
		this->apply_command(cmd, at);
	}

	// Advance deterministic per-tick RNG state so tick progression is part of world state.
	(void)this->rng.random();

	int64_t max_x = static_cast<int64_t>(this->map_width) * 100LL;
	int64_t max_y = static_cast<int64_t>(this->map_height) * 100LL;

	for (auto it = this->entities.begin(); it != this->entities.end();) {
		auto &rec = it->second;
		if (rec.hp_centi <= 0) {
			rec.entity->mark_dead();
			this->game_state->remove_game_entity(rec.id);
			it = this->entities.erase(it);
			continue;
		}

		if (rec.moving) {
			int64_t dx = rec.target_x_centi - rec.x_centi;
			int64_t dy = rec.target_y_centi - rec.y_centi;
			uint64_t dist_sq = static_cast<uint64_t>(dx * dx) + static_cast<uint64_t>(dy * dy);
			uint64_t dist = integer_sqrt(dist_sq);

			if (dist <= static_cast<uint64_t>(rec.speed_centi)) {
				rec.x_centi = rec.target_x_centi;
				rec.y_centi = rec.target_y_centi;
				rec.moving = false;
				rec.target_x_centi = 0;
				rec.target_y_centi = 0;
				rec.speed_centi = 0;
			}
			else {
				int64_t step_x = (dx * rec.speed_centi) / static_cast<int64_t>(dist);
				int64_t step_y = (dy * rec.speed_centi) / static_cast<int64_t>(dist);
				rec.x_centi = std::clamp(rec.x_centi + step_x, int64_t{0}, max_x);
				rec.y_centi = std::clamp(rec.y_centi + step_y, int64_t{0}, max_y);
			}
			this->sync_entity_position(rec, at);
		}

		++it;
	}

	this->current_tick = tick;
}


void HeadlessWorld::step_ticks(uint64_t num_ticks, ICommandSource &source) {
	if (num_ticks > std::numeric_limits<uint64_t>::max() - this->current_tick) {
		throw Error{MSG(err) << "Tick count overflow"};
	}
	(void)tick_to_time(this->current_tick + num_ticks);
	for (uint64_t i = 0; i < num_ticks; ++i) {
		uint64_t next_tick = this->current_tick + 1;
		time::time_t at = tick_to_time(next_tick);
		auto cmds = source.poll_commands(next_tick);
		auto tick_state = std::make_shared<HeadlessTickState>(
			*this,
			next_tick,
			std::move(cmds));
		this->event_loop->create_event(
			"headless.step_tick",
			this->driver_entity,
			tick_state,
			at);
		this->event_loop->reach_time(at, tick_state);
	}
}


std::string HeadlessWorld::serialize() const {
	std::ostringstream rng_stream;
	rng_stream.imbue(std::locale::classic());
	rng_stream << this->rng;
	std::string rng_raw = rng_stream.str();
	for (char &ch : rng_raw) {
		if (ch == ' ') {
			ch = ':';
		}
	}

	std::ostringstream out;
	out.imbue(std::locale::classic());
	out << "WORLD tick=" << this->current_tick
		<< " map=(" << this->map_width << "," << this->map_height << ")"
		<< " rng=" << rng_raw
		<< " next_id=" << this->next_entity_id << "\n";

	for (auto pid : this->players) {
		const auto &p = this->game_state->get_player(pid);
		out << "PLAYER id=" << pid
			<< " food=" << util::format_centi(p->get_resource("food"))
			<< " wood=" << util::format_centi(p->get_resource("wood"))
			<< " gold=" << util::format_centi(p->get_resource("gold"))
			<< " stone=" << util::format_centi(p->get_resource("stone")) << "\n";
	}

	for (const auto &[eid, e] : this->get_entities()) {
		out << "ENTITY id=" << eid
			<< " owner=" << e.owner
			<< " type=" << e.unit_type
			<< " pos=(" << util::format_centi(e.x_centi) << "," << util::format_centi(e.y_centi) << ")"
			<< " hp=" << util::format_centi(e.hp_centi) << "/" << util::format_centi(e.max_hp_centi)
			<< " action=";
		if (e.moving) {
			out << "move("
				<< util::format_centi(e.target_x_centi) << ","
				<< util::format_centi(e.target_y_centi) << ","
				<< util::format_centi(e.speed_centi) << ")";
		}
		else {
			out << "idle";
		}
		out << "\n";
	}

	return out.str();
}


HeadlessWorld HeadlessWorld::deserialize(const std::string &snapshot_text) {
	HeadlessWorld world{};
	bool saw_world = false;

	size_t line_start = 0;
	while (line_start < snapshot_text.size()) {
		size_t nl = snapshot_text.find('\n', line_start);
		std::string_view line = (nl == std::string::npos) ? std::string_view{snapshot_text}.substr(line_start) : std::string_view{snapshot_text}.substr(line_start, nl - line_start);
		line_start = (nl == std::string::npos) ? snapshot_text.size() : nl + 1;

		if (not line.empty() and line.back() == '\r') {
			line.remove_suffix(1);
		}
		if (line.empty()) {
			continue;
		}

		auto tokens = split_tokens(line);
		if (tokens.empty()) {
			continue;
		}

		std::string_view tag = tokens[0];
		auto kvs = parse_kv_tokens(tokens, 1);

		if (tag == "WORLD") {
			if (saw_world) {
				throw Error{MSG(err) << "Duplicate WORLD header in snapshot"};
			}
			saw_world = true;
			world.current_tick = parse_u64(require_kv(kvs, "tick"));
			(void)tick_to_time(world.current_tick);

			auto [w_str, h_str] = parse_paren_pair(require_kv(kvs, "map"));
			world.map_width = static_cast<int>(parse_i64(w_str));
			world.map_height = static_cast<int>(parse_i64(h_str));
			if (parse_i64(w_str) != world.map_width or parse_i64(h_str) != world.map_height) {
				throw Error{MSG(err) << "Map dimensions out of range"};
			}
			if (world.map_width <= 0 or world.map_height <= 0 or world.map_width > 10000 or world.map_height > 10000) {
				throw Error{MSG(err) << "Invalid map dimensions in snapshot"};
			}

			std::string rng_str{require_kv(kvs, "rng")};
			for (char &ch : rng_str) {
				if (ch == ':') {
					ch = ' ';
				}
			}
			std::istringstream rng_stream{rng_str};
			rng_stream.imbue(std::locale::classic());
			rng_stream >> world.rng;
			if (rng_stream.peek() != std::char_traits<char>::eof()) {
				throw Error{MSG(err) << "Trailing RNG state data"};
			}

			world.next_entity_id = static_cast<entity_id_t>(parse_u64(require_kv(kvs, "next_id")));
			if (world.next_entity_id == 0) {
				throw Error{MSG(err) << "Invalid next entity ID"};
			}
		}
		else if (tag == "PLAYER") {
			if (not saw_world) {
				throw Error{MSG(err) << "PLAYER record before WORLD header"};
			}
			headless_player p{};
			p.id = static_cast<player_id_t>(parse_u64(require_kv(kvs, "id")));
			p.food_centi = util::parse_centi(require_kv(kvs, "food"));
			p.wood_centi = util::parse_centi(require_kv(kvs, "wood"));
			p.gold_centi = util::parse_centi(require_kv(kvs, "gold"));
			p.stone_centi = util::parse_centi(require_kv(kvs, "stone"));
			auto [_, inserted] = world.players.insert(p.id);
			world.add_player(p);
			if (not inserted) {
				throw Error{MSG(err) << "Duplicate PLAYER id in snapshot: " << p.id};
			}
		}
		else if (tag == "ENTITY") {
			if (not saw_world) {
				throw Error{MSG(err) << "ENTITY record before WORLD header"};
			}
			entity_id_t id = static_cast<entity_id_t>(parse_u64(require_kv(kvs, "id")));
			player_id_t owner = static_cast<player_id_t>(parse_u64(require_kv(kvs, "owner")));
			if (not world.players.contains(owner)) {
				throw Error{MSG(err) << "Restored entity references missing player"};
			}
			if (not world.players.contains(owner)) {
				throw Error{MSG(err) << "ENTITY " << id << " references missing player " << owner};
			}
			std::string unit_type{require_kv(kvs, "type")};

			auto [x_str, y_str] = parse_paren_pair(require_kv(kvs, "pos"));
			int64_t x_centi = util::parse_centi(x_str);
			int64_t y_centi = util::parse_centi(y_str);

			auto hp_field = require_kv(kvs, "hp");
			auto slash = hp_field.find('/');
			if (slash == std::string_view::npos) {
				throw Error{MSG(err) << "Malformed hp field in snapshot: " << std::string{hp_field}};
			}
			int64_t hp_centi = util::parse_centi(hp_field.substr(0, slash));
			int64_t max_hp_centi = util::parse_centi(hp_field.substr(slash + 1));

			time::time_t at = tick_to_time(world.current_tick);
			auto rec = world.build_entity_record(
				id,
				owner,
				std::move(unit_type),
				x_centi,
				y_centi,
				hp_centi,
				max_hp_centi,
				at);

			auto action = require_kv(kvs, "action");
			if (action == "idle") {
				rec.moving = false;
			}
			else if (action.starts_with("move(") and action.ends_with(")")) {
				auto inner = action.substr(5, action.size() - 6);
				auto c1 = inner.find(',');
				auto c2 = (c1 == std::string_view::npos) ? std::string_view::npos : inner.find(',', c1 + 1);
				if (c1 == std::string_view::npos or c2 == std::string_view::npos
				    or inner.find(',', c2 + 1) != std::string_view::npos) {
					throw Error{MSG(err) << "Malformed move() action in snapshot: " << std::string{action}};
				}
				rec.moving = true;
				rec.target_x_centi = util::parse_centi(inner.substr(0, c1));
				rec.target_y_centi = util::parse_centi(inner.substr(c1 + 1, c2 - c1 - 1));
				rec.speed_centi = util::parse_centi(inner.substr(c2 + 1));
				if (rec.speed_centi <= 0 or rec.speed_centi > 100000000
				    or rec.target_x_centi < 0 or rec.target_x_centi > world.map_width * 100LL
				    or rec.target_y_centi < 0 or rec.target_y_centi > world.map_height * 100LL) {
					throw Error{MSG(err) << "Invalid restored movement"};
				}
			}
			else {
				throw Error{MSG(err) << "Unsupported action in snapshot: " << std::string{action}};
			}

			auto [_, inserted] = world.entities.emplace(id, std::move(rec));
			if (not inserted) {
				throw Error{MSG(err) << "Duplicate ENTITY id in snapshot: " << id};
			}
			if (id >= world.next_entity_id) {
				throw Error{MSG(err) << "Invalid next entity ID in snapshot"};
			}
		}
		else {
			throw Error{MSG(err) << "Unknown snapshot record tag: " << std::string{tag}};
		}
	}

	if (not saw_world) {
		throw Error{MSG(err) << "Empty snapshot text (missing WORLD header)"};
	}

	// Advance the fresh EventLoop clock to the restored tick so subsequent events schedule monotonically.
	if (world.current_tick > 0) {
		auto dummy_state = std::make_shared<openage::event::State>(world.event_loop);
		world.event_loop->reach_time(tick_to_time(world.current_tick), dummy_state);
	}

	if (world.serialize() != snapshot_text) {
		throw Error{MSG(err) << "Snapshot is not a canonical serialized world"};
	}
	return world;
}


uint64_t HeadlessWorld::get_tick() const {
	return this->current_tick;
}


int HeadlessWorld::get_map_width() const {
	return this->map_width;
}


int HeadlessWorld::get_map_height() const {
	return this->map_height;
}


std::map<player_id_t, headless_player> HeadlessWorld::get_players() const {
	std::map<player_id_t, headless_player> records;
	for (auto id : this->players) {
		const auto &player = this->game_state->get_player(id);
		records.emplace(id, headless_player{id, player->get_resource("food"), player->get_resource("wood"), player->get_resource("gold"), player->get_resource("stone")});
	}
	return records;
}


std::map<entity_id_t, headless_entity> HeadlessWorld::get_entities() const {
	auto records = this->entities;
	auto at = tick_to_time(this->current_tick);
	for (auto &[id, record] : records) {
		const auto &entity = this->game_state->get_game_entity(id);
		auto position = entity->try_get_component_as<component::Position>(component::component_t::POSITION);
		auto owner = entity->try_get_component_as<component::Ownership>(component::component_t::OWNERSHIP);
		auto live = entity->try_get_component_as<component::Live>(component::component_t::LIVE);
		auto pos = position->get_positions().get(at);
		// Synthetic positions use hundredths; invert their fixed-point quantization exactly.
		record.x_centi = (pos.ne.get_raw_value() * 100 + 32768) / 65536;
		record.y_centi = (pos.se.get_raw_value() * 100 + 32768) / 65536;
		record.owner = owner->get_owners().get(at);
		record.unit_type = entity->definition;
		record.hp_centi = live->get_attribute_value(at, "snapshot.Health").value();
	}
	return records;
}

} // namespace openage::gamestate::headless
