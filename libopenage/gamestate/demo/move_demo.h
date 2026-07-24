// Copyright 2026-2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {

/**
 * Headless MoveCommand integration test (demo id 1).
 *
 * Spawns the tutorial villager via the real Mission 1 scenario, issues a
 * programmatic MoveCommand, advances simulation time, and asserts that the
 * entity actually moved toward the destination. This is the automated,
 * no-click verification of the full movement chain:
 *
 *   send_command -> SendCommandHandler -> CommandQueue.add_command
 *   -> changes() notifies process_command dependency (built by
 *   primer_command_in_queue) -> ProcessCommandHandler -> run_activity_system
 *   -> Move::move_command -> find_path -> position curve update.
 *
 * Headless runs have no renderer, so attach_renderer() (which normally kicks
 * the activity once via run_activity_system(0) to build the primer) never
 * fires. We therefore invoke run_activity_system(0) explicitly after start().
 * If movement still fails with the primer in place, the break is downstream
 * (pathfinder / position curve) and the diagnostic logs in move.cpp pinpoint it.
 */
void simulation_demo_1(const util::Path &path);

/**
 * Headless selection + move integration test (demo id 2).
 *
 * Reproduces the --tutorial right-click path end-to-end without a window or
 * mouse: verifies the villager has the SELECTABLE component (mounted by
 * entity_factory from the Selectable ability), runs the real DragSelectHandler
 * to populate a selected-id list, feeds that list into game.send_command
 * (exactly what the controller.cpp move_entity binding does on right-click),
 * and asserts the villager moves. This catches the Phase 0.5 bug #13 root
 * cause -- missing Selectable ability -> drag_select skips the villager ->
 * get_selected() empty -> right-click sends empty entity_ids -> no movement.
 */
void simulation_demo_2(const util::Path &path);

/**
 * Headless real-camera picking test (demo id 3).
 *
 * Replaces the identity-camera assumption. Calls screen_to_world_on_terrain
 * with the tutorial's actual camera parameters (scene_pos, viewport, real_zoom
 * derived from presenter.cpp look_at_coord(scene3{10,10,0}) + calc_look_at) and
 * clicks at known screen positions, outputting every intermediate coordinate
 * (NDC, ray origin, ray/terrain intersection, final scene3). Asserts: screen
 * center -> camera focus; right -> +se; left -> -se; top -> +ne (no Y flip);
 * bottom -> -ne; center is viewport/DPR-invariant. If any fails, the first
 * failing step's logged values pinpoint the picking math error.
 */
void simulation_demo_3(const util::Path &path);

} // namespace openage::gamestate::tests
