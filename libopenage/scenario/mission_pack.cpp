// Copyright 2026 the openage authors. See copying.md for legal info.

#include "mission_pack.h"

#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>

#include "util/json_minimal.h"


namespace openage::gamestate {


namespace {


// Spec §7 candidate condition/effect kinds. Mirrors
// openage.scenario.validation.SUPPORTED_*_KINDS. The validator runs
// in the Python side; this C++ side reports unsupported kinds as
// errors (per spec §10.1 'unsupported variants fail/report clearly').
const std::set<std::string> SUPPORTED_CONDITION_KINDS = {
	"TimerElapsed", "EntityInArea", "EntityDestroyed",
	"EntityAlive", "EntityOwnedBy", "PlayerOwnsCount",
	"AttributeCompare", "TriggerFired",
};
const std::set<std::string> SUPPORTED_EFFECT_KINDS = {
	"DisplayInstruction", "SetObjective", "CompleteObjective",
	"IssueMoveCommand", "IssueAttackCommand", "ActivateTrigger",
	"DeactivateTrigger", "CreateEntity", "RemoveEntity",
	"ChangeOwnership", "DeclareVictory", "DeclareDefeat",
};


}  // namespace


MissionPack read_mission_pack(const std::string &dir) {
	const std::string scenario_path = dir + "/scenario.json";
	auto root = util::json::parse_file(scenario_path);

	MissionPack pack;

	// ---- top-level metadata ---------------------------------------
	if (!root.has("schema_version")) {
		throw MissionPackError{scenario_path + ": missing schema_version"};
	}
	pack.schema_version = root.at("schema_version").as_string();
	if (pack.schema_version != "p2-ir-v1") {
		throw MissionPackError{
			scenario_path + ": unsupported schema_version=" + pack.schema_version
			+ " (expected p2-ir-v1)"
		};
	}
	if (root.has("produced_at_commit")) {
		pack.produced_at_commit = root.at("produced_at_commit").as_string();
	}
	if (root.has("scenario_kind")) {
		pack.scenario_kind = root.at("scenario_kind").as_string();
	}
	if (root.has("scenario_fqon")) {
		pack.scenario_fqon = root.at("scenario_fqon").as_string();
	}
	if (root.has("player_setup_fqon")) {
		pack.player_setup_fqon = root.at("player_setup_fqon").as_string();
	}

	const auto &sd = root.at("ScenarioDefinition");

	pack.source = sd.at("source").as_string();
	pack.source_file = sd.at("source_file").as_string();
	if (sd.has("source_status")) {
		pack.source_status = sd.at("source_status").as_string();
	}
	if (sd.has("is_imported_original")) {
		pack.is_imported_original = sd.at("is_imported_original").as_bool();
	}

	// ---- map ------------------------------------------------------
	const auto &md = sd.at("MapDefinition");
	pack.map.width = md.at("width").as_int();
	pack.map.height = md.at("height").as_int();
	pack.map.terrain_ids_count = md.at("terrain_ids_count").as_int();
	for (const auto &v : md.at("terrain_ids_unique").as_array()) {
		pack.map.terrain_ids_unique.push_back(v.as_string());
	}
	pack.map.elevation_count = md.at("elevation_count").as_int();
	for (const auto &v : md.at("elevation_unique").as_array()) {
		pack.map.elevation_unique.push_back(v.as_int());
	}

	for (const auto &chunk_val : md.at("chunks").as_array()) {
		const auto &chunk = chunk_val.as_object();
		MissionChunkDefinition c;
		c.offset_x = chunk.at("offset").at("x").as_int();
		c.offset_y = chunk.at("offset").at("y").as_int();
		c.size_x = chunk.at("size").at("x").as_int();
		c.size_y = chunk.at("size").at("y").as_int();
		for (const auto &fqon : chunk.at("tile_fqons").as_array()) {
			c.tile_fqons.push_back(fqon.as_string());
		}
		if (static_cast<int64_t>(c.tile_fqons.size()) != c.size_x * c.size_y) {
			throw MissionPackError{
				scenario_path + ": chunk at ("
				+ std::to_string(c.offset_x) + ","
				+ std::to_string(c.offset_y) + ") has "
				+ std::to_string(c.tile_fqons.size())
				+ " tile_fqons but size is "
				+ std::to_string(c.size_x) + "x"
				+ std::to_string(c.size_y)
			};
		}
		pack.map.chunks.push_back(std::move(c));
	}

	// ---- players --------------------------------------------------
	for (const auto &pval : sd.at("players").as_array()) {
		const auto &p = pval.as_object();
		MissionPlayerDefinition pd;
		pd.player_id = p.at("PlayerDefinition.player_id").as_int();
		pd.name = p.at("PlayerDefinition.name").as_string();
		pack.players.push_back(std::move(pd));
	}

	// ---- placements -----------------------------------------------
	for (const auto &eval : sd.at("EntityPlacement").as_array()) {
		const auto &e = eval.as_object();
		MissionEntityPlacement ep;
		ep.scenario_object_id = e.at("ScenarioObjectId").as_string();
		ep.entity_definition_fqon = e.at("entity_definition_fqon").as_string();
		ep.player_id = e.at("PlayerId").as_int();
		ep.pos_x = e.at("position").at("x").as_int();
		ep.pos_y = e.at("position").at("y").as_int();
		ep.rotation = e.at("rotation").as_float();
		pack.placements.push_back(std::move(ep));
	}

	// ---- areas (optional; spec §6.1 area_definition) --------------
	if (sd.has("AreaDefinition")) {
		for (const auto &aval : sd.at("AreaDefinition").as_array()) {
			MissionAreaDefinition ad;
			ad.area_id = aval.at("area_id").as_string();
			ad.kind = aval.at("kind").as_string();
			if (aval.has("geometry")) {
				for (const auto &kv : aval.at("geometry").as_object()) {
					ad.geometry.emplace(kv.first, kv.second.as_string());
				}
			}
			pack.areas.push_back(std::move(ad));
		}
	}

	// ---- triggers (definitions, NOT executed in P2.7) -------------
	if (sd.has("TriggerDefinition")) {
		for (const auto &tval : sd.at("TriggerDefinition").as_array()) {
			const auto &t = tval.as_object();
			MissionTriggerDefinition td;
			td.trigger_id = t.at("trigger_id").as_string();
			td.initially_enabled = t.at("initially_enabled").as_bool();
			td.looping = t.at("looping").as_bool();
			td.delay = t.at("delay").as_float();

			for (const auto &cval : t.at("conditions").as_array()) {
				MissionTriggerCondition cond;
				cond.kind = cval.at("kind").as_string();
				if (cval.has("target_scenario_object_id") && cval.at("target_scenario_object_id").is_string()) {
					cond.target_scenario_object_id = cval.at("target_scenario_object_id").as_string();
				}
				if (cval.has("target_area_id") && cval.at("target_area_id").is_string()) {
					cond.target_area_id = cval.at("target_area_id").as_string();
				}
				if (cval.has("target_trigger_id") && cval.at("target_trigger_id").is_string()) {
					cond.target_trigger_id = cval.at("target_trigger_id").as_string();
				}
				if (cval.has("target_player_id") && cval.at("target_player_id").is_int()) {
					cond.target_player_id = cval.at("target_player_id").as_int();
				}
				td.conditions.push_back(std::move(cond));
			}
			for (const auto &eval : t.at("effects").as_array()) {
				MissionTriggerEffect eff;
				eff.kind = eval.at("kind").as_string();
				if (eval.has("target_scenario_object_id") && eval.at("target_scenario_object_id").is_string()) {
					eff.target_scenario_object_id = eval.at("target_scenario_object_id").as_string();
				}
				if (eval.has("target_area_id") && eval.at("target_area_id").is_string()) {
					eff.target_area_id = eval.at("target_area_id").as_string();
				}
				if (eval.has("target_trigger_id") && eval.at("target_trigger_id").is_string()) {
					eff.target_trigger_id = eval.at("target_trigger_id").as_string();
				}
				if (eval.has("target_player_id") && eval.at("target_player_id").is_int()) {
					eff.target_player_id = eval.at("target_player_id").as_int();
				}
				td.effects.push_back(std::move(eff));
			}
			pack.triggers.push_back(std::move(td));
		}
	}

	// ---- victory --------------------------------------------------
	const auto &vd = sd.at("VictoryDefinition");
	pack.victory.kind = vd.at("kind").as_string();
	if (vd.has("required_removals")) {
		for (const auto &r : vd.at("required_removals").as_array()) {
			pack.victory.required_removals.push_back(r.as_string());
		}
	}

	validate_mission_pack(pack);
	return pack;
}


void validate_mission_pack(const MissionPack &pack) {
	if (pack.map.width <= 0 || pack.map.height <= 0) {
		throw MissionPackError{
			"invalid map dimensions: "
			+ std::to_string(pack.map.width) + "x"
			+ std::to_string(pack.map.height)
		};
	}
	if (pack.map.width * pack.map.height != pack.map.terrain_ids_count) {
		throw MissionPackError{
			"terrain_ids_count does not match width*height: "
			+ std::to_string(pack.map.terrain_ids_count)
			+ " vs "
			+ std::to_string(pack.map.width * pack.map.height)
		};
	}
	if (pack.map.width * pack.map.height != pack.map.elevation_count) {
		throw MissionPackError{
			"elevation_count does not match width*height: "
			+ std::to_string(pack.map.elevation_count)
			+ " vs "
			+ std::to_string(pack.map.width * pack.map.height)
		};
	}

	// Chunk coverage: every tile belongs to exactly one chunk.
	std::vector<bool> tile_seen(pack.map.terrain_ids_count, false);
	for (const auto &chunk : pack.map.chunks) {
		for (int64_t dy = 0; dy < chunk.size_y; ++dy) {
			for (int64_t dx = 0; dx < chunk.size_x; ++dx) {
				int64_t x = chunk.offset_x + dx;
				int64_t y = chunk.offset_y + dy;
				if (x < 0 || y < 0 || x >= pack.map.width || y >= pack.map.height) {
					throw MissionPackError{
						"chunk at ("
						+ std::to_string(chunk.offset_x) + ","
						+ std::to_string(chunk.offset_y)
						+ ") has out-of-bounds tile ("
						+ std::to_string(x) + "," + std::to_string(y) + ")"
					};
				}
				int64_t idx = y * pack.map.width + x;
				if (tile_seen[idx]) {
					throw MissionPackError{
						"tile (" + std::to_string(x) + ","
						+ std::to_string(y)
						+ ") is covered by more than one chunk"
					};
				}
				tile_seen[idx] = true;
			}
		}
	}
	for (size_t i = 0; i < tile_seen.size(); ++i) {
		if (!tile_seen[i]) {
			int64_t x = static_cast<int64_t>(i) % pack.map.width;
			int64_t y = static_cast<int64_t>(i) / pack.map.width;
			throw MissionPackError{
				"tile (" + std::to_string(x) + ","
				+ std::to_string(y) + ") is not covered by any chunk"
			};
		}
	}

	// Players: unique ids, non-empty names.
	std::unordered_set<int64_t> player_ids;
	for (const auto &p : pack.players) {
		if (!player_ids.insert(p.player_id).second) {
			throw MissionPackError{
				"duplicate player_id " + std::to_string(p.player_id)
			};
		}
		if (p.name.empty()) {
			throw MissionPackError{
				"player_id " + std::to_string(p.player_id) + " has empty name"
			};
		}
	}

	// Placements: unique scenario_object_ids, valid player, non-empty
	// FQON.
	std::unordered_set<std::string> scenario_obj_ids;
	for (const auto &ep : pack.placements) {
		if (ep.scenario_object_id.empty()) {
			throw MissionPackError{"placement has empty scenario_object_id"};
		}
		if (!scenario_obj_ids.insert(ep.scenario_object_id).second) {
			throw MissionPackError{
				"duplicate scenario_object_id " + ep.scenario_object_id
			};
		}
		if (ep.entity_definition_fqon.empty()) {
			throw MissionPackError{
				"placement " + ep.scenario_object_id + " has empty entity_definition_fqon"
			};
		}
		if (player_ids.find(ep.player_id) == player_ids.end()) {
			throw MissionPackError{
				"placement " + ep.scenario_object_id
				+ " has unknown owner player_id "
				+ std::to_string(ep.player_id)
			};
		}
		if (ep.pos_x < 0 || ep.pos_y < 0
		    || ep.pos_x >= pack.map.width || ep.pos_y >= pack.map.height) {
			throw MissionPackError{
				"placement " + ep.scenario_object_id + " position out of bounds"
			};
		}
	}

	// Triggers: unique ids, no unsupported kinds.
	std::unordered_set<std::string> trigger_ids;
	for (const auto &t : pack.triggers) {
		if (!trigger_ids.insert(t.trigger_id).second) {
			throw MissionPackError{
				"duplicate trigger_id " + t.trigger_id
			};
		}
		for (const auto &c : t.conditions) {
			if (SUPPORTED_CONDITION_KINDS.find(c.kind) == SUPPORTED_CONDITION_KINDS.end()) {
				throw MissionPackError{
					"trigger " + t.trigger_id + " has unsupported condition kind "
					+ c.kind + " (silent-drop is forbidden by spec §10.1)"
				};
			}
		}
		for (const auto &e : t.effects) {
			if (SUPPORTED_EFFECT_KINDS.find(e.kind) == SUPPORTED_EFFECT_KINDS.end()) {
				throw MissionPackError{
					"trigger " + t.trigger_id + " has unsupported effect kind "
					+ e.kind + " (silent-drop is forbidden by spec §10.1)"
				};
			}
		}
	}

	// Trigger cross-refs.
	for (const auto &t : pack.triggers) {
		for (const auto &c : t.conditions) {
			if (!c.target_scenario_object_id.empty()
			    && scenario_obj_ids.find(c.target_scenario_object_id)
			       == scenario_obj_ids.end()) {
				throw MissionPackError{
					"trigger " + t.trigger_id + " condition references unknown "
					"scenario_object_id " + c.target_scenario_object_id
				};
			}
			if (!c.target_trigger_id.empty()
			    && trigger_ids.find(c.target_trigger_id)
			       == trigger_ids.end()) {
				throw MissionPackError{
					"trigger " + t.trigger_id + " condition references unknown "
					"trigger_id " + c.target_trigger_id
				};
			}
		}
		for (const auto &e : t.effects) {
			if (!e.target_scenario_object_id.empty()
			    && scenario_obj_ids.find(e.target_scenario_object_id)
			       == scenario_obj_ids.end()) {
				throw MissionPackError{
					"trigger " + t.trigger_id + " effect references unknown "
					"scenario_object_id " + e.target_scenario_object_id
				};
			}
			if (!e.target_trigger_id.empty()
			    && trigger_ids.find(e.target_trigger_id)
			       == trigger_ids.end()) {
				throw MissionPackError{
					"trigger " + t.trigger_id + " effect references unknown "
					"trigger_id " + e.target_trigger_id
				};
			}
		}
	}

	if (pack.victory.kind.empty()) {
		throw MissionPackError{"victory kind is empty"};
	}
}


}  // namespace openage::gamestate