// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {


/**
 * Demo 20 - P4.3 Mission 1 data-driven victory (TDD GREEN).
 *
 * Pins the PRODUCTION data-driven victory path end-to-end. The demo
 * boots Mission 1 through GameSimulation::initialize, which constructs
 * the TriggerRuntime and synthesizes the defeat_enemy trigger from the
 * Mission Pack's VictoryDefinition.required_removals. The demo does
 * NOT hand-author any trigger -- it drives the production removal
 * chain and asserts victory through the public is_objective_finished()
 * contract.
 *
 *   1. After initialize, the mission is NOT finished and the
 *      EnemyVillager is present in the GameState.
 *   2. A NON-target removal (PlayerVillager) through the production
 *      game.removal event does NOT finish the mission: the
 *      defeat_enemy trigger watches only EnemyVillager, so the
 *      TriggerRuntime observer's notify does not fire the trigger.
 *   3. The TARGET removal (EnemyVillager) through the production
 *      game.removal event finishes the mission: RemovalEventHandler
 *      fires the TriggerRuntime observer -> trigger.evaluate ->
 *      defeat_enemy fires -> DeclareVictory -> MissionState::VICTORY
 *      -> is_objective_finished() == true. The entity is removed.
 *   4. Idempotent: advancing the clock further keeps the mission
 *      finished (no re-fire regression).
 *
 * This is the P4.3 acceptance pin: Mission 1 victory is data-driven
 * (synthesized from required_removals) and flows through the
 * TriggerRuntime, with no WilliamWallaceMission01ObjectiveController
 * in the production path.
 */
void simulation_demo_20(const util::Path &path);


} // namespace openage::gamestate::tests
