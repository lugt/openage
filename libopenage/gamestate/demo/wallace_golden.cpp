// Copyright 2026+ the openage authors. See copying.md for legal info.

#include "wallace_golden.h"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <vector>

#include "coord/tile.h"
#include "cvar/cvar.h"
#include "event/event_loop.h"
#include "gamestate/action_option.h"
#include "gamestate/component/api/build.h"
#include "gamestate/component/api/live.h"
#include "gamestate/component/internal/commands/types.h"
#include "gamestate/component/internal/ownership.h"
#include "gamestate/component/internal/position.h"
#include "gamestate/event/send_command.h"
#include "gamestate/game.h"
#include "gamestate/game_entity.h"
#include "gamestate/game_state.h"
#include "gamestate/manager.h"
#include "gamestate/player.h"
#include "gamestate/simulation.h"
#include "scenario/trigger/original_scenario.h"
#include "scenario/trigger/trigger_runtime.h"
#include "time/time_loop.h"
#include "util/path.h"

namespace openage::gamestate::tests {
namespace {

using component::component_t;

std::string lifecycle_name(scenario::trigger::MissionLifecycle state) {
	switch (state) {
	case scenario::trigger::MissionLifecycle::RUNNING:
		return "RUNNING";
	case scenario::trigger::MissionLifecycle::VICTORY:
		return "VICTORY";
	case scenario::trigger::MissionLifecycle::DEFEAT:
		return "DEFEAT";
	}
	return "UNKNOWN";
}

std::vector<entity_id_t> resolve_targets(const GameState &state,
                                         const util::json::Value &spec,
                                         const time::time_t &now,
                                         entity_id_t last_site) {
	std::vector<entity_id_t> result;
	auto resolve_one = [&](const std::string &token) {
		if (token == "last_site") {
			if (last_site != 0) {
				result.push_back(last_site);
			}
			return;
		}
		if (token.starts_with("genie:") or token.starts_with("enemy_genie:")) {
			const bool enemy = token.starts_with("enemy_genie:");
			const auto rest = token.substr(enemy ? 12 : 6);
			const auto colon = rest.find(':');
			const int64_t genie_id = std::stoll(colon == std::string::npos ? rest : rest.substr(0, colon));
			const int64_t pick_idx = colon == std::string::npos ? -1 : std::stoll(rest.substr(colon + 1));
			const player_id_t target_owner = enemy ? 1 : 0;
			std::vector<entity_id_t> matches;
			for (const auto &[id, entity] : state.get_game_entities()) {
				const auto owner = entity->try_get_component_as<component::Ownership>(component_t::OWNERSHIP);
				if (not entity->is_dead() and entity->genie_id == genie_id and owner and owner->get_owners().get(now) == target_owner) {
					matches.push_back(id);
				}
			}
			std::ranges::sort(matches);
			if (pick_idx >= 0) {
				if (static_cast<size_t>(pick_idx) < matches.size()) {
					result.push_back(matches[static_cast<size_t>(pick_idx)]);
				}
			}
			else {
				result.insert(result.end(), matches.begin(), matches.end());
			}
			return;
		}
		if (auto id = state.get_entity_id_by_instance(token)) {
			result.push_back(*id);
		}
	};
	if (spec.is_string()) {
		resolve_one(spec.as_string());
	}
	else if (spec.is_array()) {
		for (const auto &item : spec.as_array()) {
			if (item.is_string()) {
				resolve_one(item.as_string());
			}
		}
	}
	return result;
}

entity_id_t find_nearest_resource(const GameState &state,
                                  entity_id_t worker_id,
                                  const std::string &resource_name,
                                  const time::time_t &now) {
	if (not state.has_game_entity(worker_id)) {
		return 0;
	}
	const auto worker = state.get_game_entity(worker_id);
	const auto start = worker->try_get_component_as<component::Position>(component_t::POSITION)->get_positions().get(now);
	const auto fqon = "de2_base.data.util.resource.types." + resource_name;
	double best_dist = 1e9;
	entity_id_t best_id = 0;
	for (const auto &[id, entity] : state.get_game_entities()) {
		if (entity->is_dead() or not entity->resource_spot or entity->resource_spot->requires_hunting
		    or entity->resource_spot->resource != fqon or entity->resource_spot->remaining <= 0) {
			continue;
		}
		const auto pos = entity->try_get_component_as<component::Position>(component_t::POSITION)->get_positions().get(now);
		const double dist = std::hypot((pos.ne - start.ne).to_double(), (pos.se - start.se).to_double());
		if (dist < best_dist or (dist == best_dist and id < best_id)) {
			best_dist = dist;
			best_id = id;
		}
	}
	return best_id;
}

void write_snapshot(std::ostringstream &out,
                    int64_t tick,
                    const time::time_t &now,
                    const GameState &state,
                    const scenario::trigger::TriggerRuntime &runtime,
                    const std::vector<std::string> &track_instances,
                    entity_id_t last_site) {
	out << std::fixed << std::setprecision(2);
	out << "=== TICK " << tick << " (t=" << now.to_double() << "s) ===\n";
	out << "mission: " << lifecycle_name(runtime.mission_state().lifecycle())
	    << " | victory=" << (runtime.mission_state().is_victory() ? 1 : 0)
	    << " | defeat=" << (runtime.mission_state().lifecycle() == scenario::trigger::MissionLifecycle::DEFEAT ? 1 : 0) << "\n";
	const auto p0 = state.get_player(0);
	const auto econ = state.player_economy(0);
	out << "resources[0]:"
	    << " Food=" << p0->get_resource("de2_base.data.util.resource.types.Food")
	    << " Wood=" << p0->get_resource("de2_base.data.util.resource.types.Wood")
	    << " Stone=" << p0->get_resource("de2_base.data.util.resource.types.Stone")
	    << " Gold=" << p0->get_resource("de2_base.data.util.resource.types.Gold")
	    << " pop=" << econ.population << "/" << econ.population_capacity << "\n";

	std::map<std::pair<player_id_t, int64_t>, int64_t> counts;
	constexpr int64_t tracked_genies[]{38, 70, 74, 83, 109, 12};
	for (const auto &[id, entity] : state.get_game_entities()) {
		if (entity->is_dead()) {
			continue;
		}
		if (std::ranges::find(tracked_genies, entity->genie_id) == std::end(tracked_genies)) {
			continue;
		}
		const auto owner = entity->try_get_component_as<component::Ownership>(component_t::OWNERSHIP);
		if (owner) {
			const auto pid = owner->get_owners().get(now);
			if (pid == 0 or pid == 1) {
				++counts[{pid, entity->genie_id}];
			}
		}
	}
	out << "tracked_counts:";
	for (const auto &[key, count] : counts) {
		out << " p" << key.first << "_g" << key.second << "=" << count;
	}
	out << "\n";

	const nyan::fqon_t health = "de2_base.data.util.attribute.types.Health";
	for (const auto &inst : track_instances) {
		const auto id = state.get_entity_id_by_instance(inst);
		if (not id or not state.has_game_entity(*id)) {
			out << "unit[" << inst << "]: missing=1\n";
			continue;
		}
		const auto entity = state.get_game_entity(*id);
		const auto owner = entity->try_get_component_as<component::Ownership>(component_t::OWNERSHIP);
		const auto pos = entity->try_get_component_as<component::Position>(component_t::POSITION)->get_positions().get(now);
		const auto live = entity->try_get_component_as<component::Live>(component_t::LIVE);
		const auto hp = live ? live->get_attribute_value(now, health).value_or(-1) : -1;
		const auto max_hp = live ? live->get_attribute_max(health).value_or(-1) : -1;
		out << "unit[" << inst << "]: owner=" << (owner ? static_cast<int64_t>(owner->get_owners().get(now)) : -1)
		    << " hp=" << hp << "/" << max_hp
		    << " pos=(" << pos.ne.to_double() << "," << pos.se.to_double() << ")"
		    << " dead=" << (entity->is_dead() ? 1 : 0) << "\n";
	}
	if (last_site != 0 and state.has_game_entity(last_site)) {
		const auto site = state.get_game_entity(last_site);
		const auto build = site->try_get_component_as<component::Build>(component_t::BUILD);
		out << "last_site: progress=" << (build ? build->get_progress() : -1)
		    << " builders=" << (build ? build->active_builders.size() : 0) << "\n";
	}
}

} // namespace

std::string run_wallace_golden(const util::Path &path, const std::string &scenario_json, std::vector<double> &timings) {
	using Clock = std::chrono::steady_clock;
	auto checkpoint = Clock::now();
	timings.assign(6, 0.0);
	auto record = [&](size_t stage) {
		const auto now = Clock::now();
		timings[stage] += std::chrono::duration<double>(now - checkpoint).count();
		checkpoint = now;
	};
	const auto config = util::json::parse(scenario_json);
	const int64_t expected_mission = config.at("mission").as_int();
	const double tick_seconds = config.has("tick_seconds") ? config.at("tick_seconds").as_number() : 1.0;
	const int64_t total_ticks = config.at("total_ticks").as_int();

	std::set<int64_t> snapshot_ticks;
	if (config.has("snapshot_ticks")) {
		for (const auto &t : config.at("snapshot_ticks").as_array()) {
			snapshot_ticks.insert(t.as_int());
		}
	}
	snapshot_ticks.insert(total_ticks);

	std::vector<std::string> track_instances;
	if (config.has("track_instances")) {
		for (const auto &inst : config.at("track_instances").as_array()) {
			track_instances.push_back(inst.as_string());
		}
	}

	auto simulation = std::make_shared<GameSimulation>(
		path, std::make_shared<cvar::CVarManager>(path), std::make_shared<time::TimeLoop>(),
		"ww_mvp.campaign.william_wallace.mission_01.PlayerSetup",
		"ww_mvp.campaign.william_wallace.mission_01.Mission");
	simulation->set_modpacks({"de2_base", "ww_mvp"});
	simulation->initialize();

	auto state = simulation->get_game()->get_state();
	for (const auto &[id, entity] : state->get_game_entities()) {
		if (entity->has_component(component_t::MOVE)) {
			entity->get_manager()->run_activity_system(time::time_t::zero());
		}
	}

	auto runtime = simulation->get_trigger_runtime();
	auto original = runtime->original_scenario();
	if (not original) {
		throw std::runtime_error{"Wallace golden runner requires an installed original scenario"};
	}
	if (original->config().at("mission").as_int() != expected_mission) {
		throw std::runtime_error{"Installed mission mismatch: expected " + std::to_string(expected_mission)};
	}

	if (config.has("setup_effects")) {
		for (const auto &effect : config.at("setup_effects").as_array()) {
			original->execute(effect, time::time_t::zero());
		}
	}
	if (config.has("ai")) {
		simulation->configure_ai(scenario_json);
	}

	std::map<int64_t, std::vector<util::json::Value>> commands_by_tick;
	if (config.has("commands")) {
		for (const auto &cmd : config.at("commands").as_array()) {
			commands_by_tick[cmd.at("tick").as_int()].push_back(cmd);
		}
	}

	const auto loop = simulation->get_event_loop();
	const auto commander = simulation->get_commander();
	time::time_t now = time::time_t::zero();
	entity_id_t last_site = 0;
	std::ostringstream out;
	record(0);

	for (int64_t tick = 1; tick <= total_ticks; ++tick) {
		now = time::time_t::from_double(static_cast<double>(tick) * tick_seconds);
		if (config.has("ai")) {
			record(1);
			loop->reach_time(now, state);
			record(2);
		}
		if (const auto it = commands_by_tick.find(tick); it != commands_by_tick.end()) {
			for (const auto &cmd : it->second) {
				const auto type = cmd.at("type").as_string();
				if (type == "select") {
					original->select(resolve_targets(*state, cmd.at("targets"), now, last_site), now);
				}
				else if (type == "move") {
					const auto ids = resolve_targets(*state, cmd.at("targets"), now, last_site);
					const coord::phys3 dest{
						coord::phys_t::from_double(cmd.at("x").as_number()),
						coord::phys_t::from_double(cmd.at("y").as_number()),
						0,
					};
					loop->create_event("game.send_command", commander, state, now, {
						{"type", component::command::command_t::MOVE},
						{"entity_ids", ids},
						{"target", dest},
						{"issuer", player_id_t{0}},
					});
				}
				else if (type == "idle") {
					const auto ids = resolve_targets(*state, cmd.at("targets"), now, last_site);
					loop->create_event("game.send_command", commander, state, now, {
						{"type", component::command::command_t::IDLE},
						{"entity_ids", ids},
						{"issuer", player_id_t{0}},
					});
				}
				else if (type == "attack") {
					const auto ids = resolve_targets(*state, cmd.at("targets"), now, last_site);
					const auto targets = resolve_targets(*state, cmd.at("target"), now, last_site);
					if (not ids.empty() and not targets.empty()) {
						loop->create_event("game.send_command", commander, state, now, {
							{"type", component::command::command_t::ATTACK},
							{"entity_ids", ids},
							{"target_id", targets.front()},
							{"issuer", player_id_t{0}},
						});
					}
				}
				else if (type == "gather") {
					const auto ids = resolve_targets(*state, cmd.at("targets"), now, last_site);
					const auto res = cmd.at("resource").as_string();
					for (const auto wid : ids) {
						const auto spot = find_nearest_resource(*state, wid, res, now);
						if (spot == 0) {
							throw std::runtime_error{"No available resource spot for " + res};
						}
						loop->create_event("game.send_command", commander, state, now, {
							{"type", component::command::command_t::GATHER},
							{"entity_ids", std::vector<entity_id_t>{wid}},
							{"target_id", spot},
							{"issuer", player_id_t{0}},
						});
					}
				}
				else if (type == "build") {
					const auto actors = resolve_targets(*state, cmd.at("actor"), now, last_site);
					if (not actors.empty()) {
						const auto actor = state->get_game_entity(actors.front());
						const auto suffix = "." + cmd.at("building").as_string();
						const auto opt = std::ranges::find_if(actor->action_options, [&](const auto &o) {
							return o.kind == ActionKind::Build and o.subject.ends_with(suffix);
						});
						if (opt == actor->action_options.end()) {
							throw std::runtime_error{"Missing build option " + suffix};
						}
						entity_id_t prev_max_id = 0;
						for (const auto &[eid, _] : state->get_game_entities()) {
							prev_max_id = std::max(prev_max_id, eid);
						}
						const coord::phys3 pos{
							coord::phys_t::from_double(cmd.at("x").as_number()),
							coord::phys_t::from_double(cmd.at("y").as_number()),
							0,
						};
						loop->create_event("game.economy_action", commander, state, now, {
							{"actor", actors.front()},
							{"option", opt->id},
							{"position", pos},
							{"issuer", player_id_t{0}},
						});
						loop->reach_time(now, state);
						for (const auto &[eid, ent] : state->get_game_entities()) {
							if (eid > prev_max_id and ent->definition == opt->subject) {
								last_site = eid;
								break;
							}
						}
					}
				}
				else if (type == "build_site") {
					const auto actors = resolve_targets(*state, cmd.at("actor"), now, last_site);
					const auto sites = resolve_targets(*state, cmd.at("site"), now, last_site);
					if (not actors.empty() and not sites.empty()) {
						loop->create_event("game.economy_action", commander, state, now, {
							{"actor", actors.front()},
							{"site", sites.front()},
							{"issuer", player_id_t{0}},
						});
					}
				}
				else if (type == "train") {
					const auto actors = resolve_targets(*state, cmd.at("actor"), now, last_site);
					if (not actors.empty()) {
						const auto actor = state->get_game_entity(actors.front());
						const auto suffix = "." + cmd.at("unit").as_string();
						const auto opt = std::ranges::find_if(actor->action_options, [&](const auto &o) {
							return o.kind == ActionKind::Train and o.subject.ends_with(suffix);
						});
						if (opt == actor->action_options.end()) {
							throw std::runtime_error{"Missing train option " + suffix};
						}
						loop->create_event("game.economy_action", commander, state, now, {
							{"actor", actors.front()},
							{"option", opt->id},
							{"issuer", player_id_t{0}},
						});
					}
				}
				else if (type == "add_resource") {
					const auto pid = static_cast<player_id_t>(cmd.at("player").as_int());
					const auto fqon = "de2_base.data.util.resource.types." + cmd.at("resource").as_string();
					state->get_player(pid)->add_resource(fqon, cmd.at("amount").as_int());
				}
				else {
					throw std::runtime_error{"Unsupported wallace golden command: " + type};
				}
			}
		}

		record(1);
		loop->reach_time(now, state);
		record(2);
		state->update_visibility(now);
		record(3);
		state->update_player_economies(now);
		record(4);

		if (snapshot_ticks.contains(tick)) {
			write_snapshot(out, tick, now, *state, *runtime, track_instances, last_site);
			if (config.has("ai")) {
				out << simulation->ai_snapshot(now);
			}
		}
		record(5);
	}

	if (config.has("ai")) {
		out << "AI_OPERATIONS\n" << simulation->ai_trace();
	}
	return out.str();
}

} // namespace openage::gamestate::tests
