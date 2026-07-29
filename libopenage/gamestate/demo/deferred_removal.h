// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Demo 13 — Deferred dead-entity removal (P3.4).
//
// Asserts that entity removal happens via a deferred RemovalEvent,
// not synchronously inside DeathEventHandler (or any other handler).
// Validates:
//   * Removal is idempotent: duplicate removal events are no-ops.
//   * Removal happens at a safe point in the event loop (after the
//     dispatcher's call stack returns).
//   * Stale attack follow-up after removal is a clean no-op.
//   * Stale effect event after removal is a clean no-op.
//   * Multi-attacker scenario: one target with two attackers; only
//     one removal event fires.
//   * `state->has_game_entity(target_id)` flips to false after the
//     removal event dispatches.
//   * DeathEventHandler no longer contains synchronous
//     remove_game_entity.

#pragma once

#include "../../util/compiler.h"
// pxd: from libopenage.util.path cimport Path


namespace openage {

namespace util {
class Path;
}

namespace gamestate::tests {

void simulation_demo_13(const util::Path &path);

}  // namespace gamestate::tests
}  // namespace openage