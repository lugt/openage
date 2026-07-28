// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Mission Pack production loader (P2.7).
//
// Wires a :class:`MissionPack` (read by mission_pack.h) into the
// production game-state path: TerrainFactory + EntityFactory +
// GameState, in the spec §6.3 order:
//
//   1. terrain and chunks (TerrainFactory::add_chunk_with_tiles,
//      driven by MissionPack.chunks)
//   2. players (existing Game::add_player flow, but driven by
//      MissionPack.players)
//   3. diplomacy (empty matrix for the MVP reconstruction; the
//      extension point is here)
//   4. placements (EntityFactory::add_game_entity + Position /
//      Ownership / Activity wiring, driven by MissionPack.placements)
//   5. ScenarioObjectId -> GameEntityId mapping
//      (GameState::register_named_entity, deterministic by
//      placement-list order)
//   6. areas (loaded as definitions, no dependency watchers yet)
//   7. triggers (loaded as definitions, NOT executed in P2.7)
//   8. initial objective events (driven by MissionPack.victory)
//
// Spec §6.3 forbids relying on entity-creation order as a substitute
// for scenario object identity. The loader assigns GameEntityIds in
// placement-list order (0..N-1) and registers each ScenarioObjectId
// explicitly via GameState::register_named_entity, so the mapping is
// stable across runs and independent of nyan resolution timing.
//
// No source-format parser runs here. The loader only consumes the
// Mission Pack; the original AoE2 DE scenario importer remains a
// documented Path D BLOCKER (see
// docs/mvp/reports/mission1-scenario-feasibility.md).

#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "mission_pack.h"
#include "event/event_loop.h"
#include "gamestate/game_state.h"
#include "util/path.h"


namespace openage::gamestate {


class EntityFactory;
class TerrainFactory;
class Map;


// Raised when the loader cannot proceed (validation, FQON resolution,
// missing players, etc.). The error message is the user-visible
// explanation; the C++ integration test asserts on substrings.
class MissionPackLoadError : public std::runtime_error {
public:
	explicit MissionPackLoadError(const std::string &msg)
		: std::runtime_error{msg} {}
};


// Load a Mission Pack into the supplied game state. The state is
// expected to be empty (no terrain / no entities / no players yet).
//
// @param event_loop     the simulation event loop
// @param state          the GameState to populate
// @param entity_factory the EntityFactory used to spawn entities
// @param terrain_factory the TerrainFactory used to build chunks
// @param pack           the Mission Pack to load
//
// Throws MissionPackLoadError on validation failure, FQON
// resolution failure, or any other inability to materialise the
// scenario. On success, the GameState contains the terrain, players,
// placements, and a ScenarioObjectId -> GameEntityId mapping.
//
// The loader is the P2.7 production path. The legacy nyan-only path
// in Game::load_scenario (which read Scenario.terrain_chunks and
// Scenario.initial_entities directly) is removed; only this loader
// runs for the Mission 1 reconstruction.
void load_mission_pack_into_game_state(
	const std::shared_ptr<openage::event::EventLoop> &event_loop,
	const std::shared_ptr<GameState> &state,
	const std::shared_ptr<EntityFactory> &entity_factory,
	const std::shared_ptr<TerrainFactory> &terrain_factory,
	const MissionPack &pack);


}  // namespace openage::gamestate