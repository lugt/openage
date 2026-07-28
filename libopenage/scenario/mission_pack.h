// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Mission Pack reader (P2.7).
//
// Reads the deterministic output of
// ``openage.scenario.writer.write_mission_pack``:
//
//   - ``scenario.json``        structural metadata (this file is the
//                              canonical input; the others are
//                              informational cross-checks)
//   - ``object_table.csv``     one row per EntityPlacement (FQON, owner,
//                              position, rotation); cross-checked against
//                              scenario.json
//   - ``trigger_graph.json``   trigger / condition / effect histograms
//                              + node / edge counts
//   - ``coverage.json``        "only implementation list" (per plan §P4.1)
//
// The reader is the bridge between the Python writer and the C++
// production loader. It is intentionally tolerant of new optional
// fields (forward compatibility) but REFUSES to silently accept unknown
// condition / effect kinds or unresolved entity / tile FQONs (per
// spec §10.1 'unsupported variants fail/report clearly').
//
// No source-format parser runs here. The reader only consumes the
// Mission Pack; the original AoE2 DE scenario importer remains a
// documented Path D BLOCKER (see docs/mvp/reports/mission1-scenario-
// feasibility.md).

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>


namespace openage::gamestate {


class MissionPackError : public std::runtime_error {
public:
	explicit MissionPackError(const std::string &msg) : std::runtime_error{msg} {}
};


// ---------------------------------------------------------------------------
// IR-shaped structs (mirror the Python openage.scenario.ir module)
// ---------------------------------------------------------------------------


struct MissionChunkDefinition {
	int64_t offset_x{0};
	int64_t offset_y{0};
	int64_t size_x{0};
	int64_t size_y{0};
	std::vector<std::string> tile_fqons;  // row-major, length == size_x * size_y
};


struct MissionMapDefinition {
	int64_t width{0};
	int64_t height{0};
	int64_t terrain_ids_count{0};
	std::vector<std::string> terrain_ids_unique;
	int64_t elevation_count{0};
	std::vector<int64_t> elevation_unique;
	std::vector<MissionChunkDefinition> chunks;  // P2.7 chunk boundaries
};


struct MissionPlayerDefinition {
	int64_t player_id{0};
	std::string name;
};


struct MissionEntityPlacement {
	std::string scenario_object_id;       // stable name the loader registers
	std::string entity_definition_fqon;   // resolves via nyan at load time
	int64_t player_id{0};
	int64_t pos_x{0};
	int64_t pos_y{0};
	double rotation{0.0};
};


struct MissionTriggerCondition {
	std::string kind;
	std::string target_scenario_object_id;  // empty if N/A
	std::string target_area_id;
	std::string target_trigger_id;
	int64_t target_player_id{-1};  // -1 = not set
};


struct MissionTriggerEffect {
	std::string kind;
	std::string target_scenario_object_id;
	std::string target_area_id;
	std::string target_trigger_id;
	int64_t target_player_id{-1};
};


struct MissionTriggerDefinition {
	std::string trigger_id;
	bool initially_enabled{true};
	bool looping{false};
	double delay{0.0};
	std::vector<MissionTriggerCondition> conditions;
	std::vector<MissionTriggerEffect> effects;
};


struct MissionAreaDefinition {
	std::string area_id;
	std::string kind;
	std::unordered_map<std::string, std::string> geometry;
};


struct MissionVictoryDefinition {
	std::string kind;
	std::vector<std::string> required_removals;
};


struct MissionPack {
	std::string schema_version;
	std::string produced_at_commit;
	std::string scenario_kind;        // "reconstruction" until Path C unblocks
	std::string scenario_fqon;
	std::string player_setup_fqon;
	std::string source_file;
	std::string source;               // "hand-authored" until Path C unblocks
	bool is_imported_original{false};
	std::string source_status;        // "BLOCKED" until Path C unblocks

	MissionMapDefinition map;
	std::vector<MissionPlayerDefinition> players;
	std::vector<MissionEntityPlacement> placements;
	std::vector<MissionAreaDefinition> areas;
	std::vector<MissionTriggerDefinition> triggers;
	MissionVictoryDefinition victory;
};


// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------


// Read a Mission Pack from ``dir``. ``dir`` must contain the four
// files written by openage.scenario.writer.write_mission_pack.
//
// Raises ``MissionPackError`` on missing / malformed files, on
// schema-version mismatch, on FQON resolution errors, or on
// unsupported condition / effect kinds (per spec §10.1).
//
// The reader does NOT itself load anything into GameState; that is
// the loader's job. The reader is pure data in -> typed struct out.
MissionPack read_mission_pack(const std::string &dir);


// Validate that every required EntityPlacement scenario_object_id,
// trigger_id, area_id, and entity_fqon is well-formed. Does NOT
// resolve FQONs against the nyan database (the loader does that).
// Reports the first error via MissionPackError.
void validate_mission_pack(const MissionPack &pack);


}  // namespace openage::gamestate