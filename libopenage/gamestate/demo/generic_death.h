// Copyright 2026 the openage authors. See copying.md for legal info.
//
// Demo 12 — Generic death event (P3.3).
//
// Asserts that an entity's death is derived from a generic Health
// threshold transition (not from inline checks inside
// AttackHandler or ApplyDiscreteEffectHandler), and that:
//   * HP<=0 fires at most one DeathEvent per entity (idempotent)
//   * same-timestamp multiple damage effects still produce one death
//   * a non-Attack source (direct ApplyDiscreteEffect dispatch) also
//     triggers death
//   * once dead, the entity no longer accepts further damage and
//     does not generate a second death
//   * Attack follow-ups after death are dropped cleanly
//
// The structural evidence is the new "game.death" event being
// registered on the production EventLoop.

#pragma once

#include "../../util/compiler.h"
// pxd: from libopenage.util.path cimport Path


namespace openage {

namespace util {
class Path;
}

namespace gamestate::tests {

void simulation_demo_12(const util::Path &path);

}  // namespace gamestate::tests
}  // namespace openage