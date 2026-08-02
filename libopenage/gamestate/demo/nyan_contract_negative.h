// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {


/**
 * Demo 15 - P4.1.1 nyan contract negative test.
 *
 * Pins the behavior of require_nonempty_fqon() at the contract
 * boundary:
 *   * empty FQON -> throws NyanContractError with full context
 *   * non-empty FQON -> accepts silently
 *   * error message must contain the runtime context (handler_id,
 *     event_type, simulation_time, entity_id, entity_fqon,
 *     field_name, requested_fqon)
 *
 * This is the negative-test gate for the C3 runtime contracts.
 */
void simulation_demo_15(const util::Path &path);


}  // namespace openage::gamestate::tests
