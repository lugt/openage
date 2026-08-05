// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {


/**
 * Demo 25 - P4.4B Footprint construction contract.
 * Demo 26 - P4.4B PlacementValidator contract.
 * Demo 27 - P4.4B StaticOccupancyGrid contract.
 * Demo 28 - P4.4C required/optional Footprint contract.
 * Demo 29 - P4.4C GridAStarPathfinder contract.
 * Demo 30 - P4.4C mover footprint clearance.
 * Demo 31 - P4.4C segmented movement + waypoint events.
 * Demo 32 - P4.4C Mission 1 TC obstacle routing.
 * Demo 35 - P4.4D ContextActionResolver contract.
 * Demo 36 - P4.4D windowed-style move routing.
 * Demo 37 - P4.4D.1 AttackApproach service contract.
 * Demo 38 - P4.4D.1 pointer-driven Mission 1 combat lifecycle.
 * Demo 42 - P4.4F.1 swept AABB + DynamicOccupancy runtime.
 * Demo 46 - P4.4D.2 + P4.4F multi-unit formation + dynamic occupancy.
 * Demo 47 - P4.4F.1 MovementReservation + narrow passage.
 * Demo 48 - P4.4F.1 group move via GroupMoveHandler.
 * Demo 49 - P4.4F.1 real multi-unit no-overlap runtime.
 */
void simulation_demo_25(const util::Path &path);
void simulation_demo_26(const util::Path &path);
void simulation_demo_27(const util::Path &path);
void simulation_demo_28(const util::Path &path);
void simulation_demo_29(const util::Path &path);
void simulation_demo_30(const util::Path &path);
void simulation_demo_31(const util::Path &path);
void simulation_demo_32(const util::Path &path);
void simulation_demo_35(const util::Path &path);
void simulation_demo_36(const util::Path &path);
void simulation_demo_37(const util::Path &path);
void simulation_demo_38(const util::Path &path);
void simulation_demo_42(const util::Path &path);
void simulation_demo_46(const util::Path &path);
void simulation_demo_47(const util::Path &path);
void simulation_demo_48(const util::Path &path);
void simulation_demo_49(const util::Path &path);


} // namespace openage::gamestate::tests
