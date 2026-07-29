// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Demo 11 — Damage effect path (P3.2).
//
// Asserts that combat damage flows through a dedicated
// ApplyDiscreteEffectHandler rather than being applied by direct
// target_live->set_attribute inside AttackHandler. The structural
// evidence is the new "game.apply_discrete_effect" event being
// registered on the production EventLoop, observable by:
//   * dispatching that event directly and seeing one HP decrement
//     on a live target,
//   * dispatching it against a missing target and seeing a clean
//     no-op (no throw, no crash).
// Behavioural evidence (functional correctness preserved across
// the refactor) is the same five-hit / 6-damage / 2.0s-cooldown
// sequence pinned in P3.1, plus a cooldown-gate check between
// hits.
//
// Failure semantics: this demo fails the launch when the new
// event type is not registered (create_event throws); it fails
// an assertion when the per-hit HP / timestamp invariants drift
// from the P3.1 characterization.

#pragma once

#include "../../util/compiler.h"
// pxd: from libopenage.util.path cimport Path


namespace openage {

namespace util {
class Path;
}

namespace gamestate::tests {

void simulation_demo_11(const util::Path &path);

}  // namespace gamestate::tests
}  // namespace openage