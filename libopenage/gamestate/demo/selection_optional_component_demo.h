// Copyright 2026 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {


/**
 * Demo 50 - selecting an entity that has no optional components.
 *
 * Regression test for the crash that terminated the Mission 1
 * windowed launch with
 *
 *   std::out_of_range: unordered_map::at: key not found
 *
 * raised from
 * SelectionModel::wire_health_callback -> GameEntity::get_component.
 *
 * A TownCenter is a building: it has no Attack ability, so the
 * entity factory never mounts an Attack component on it
 * (entity_factory.cpp only mounts Attack for ApplyDiscreteEffect /
 * RangedDiscreteEffect abilities). Selecting the TownCenter therefore
 * asks for a component the entity legitimately does not have.
 *
 * GameEntity::get_component() is documented-by-usage to be a
 * throwing lookup (mandatory component), while the presenter view
 * models treat an absent component as "no data" and null-check the
 * result. The presenter layer must therefore use the non-throwing
 * try_get_component_as<T>() lookup, and must not crash when the
 * selected entity has no Attack / Live / Ownership component.
 *
 * Pins:
 *   1. Premise: the TownCenter really has no Attack component.
 *   2. SelectionModel::set_multi_selection() on the TownCenter does
 *      not throw and selects it.
 *   3. SelectedEntityView on the TownCenter does not throw and
 *      reports empty fields (per its documented contract).
 *   4. A multi-selection of TownCenter + villager still wires the
 *      health callback for the villager (the fix must not disable
 *      health wiring for entities that DO have an Attack component).
 *   5. try_get_component_as<T>() separates the two failure modes that
 *      the raw try_get_component() + dynamic_pointer_cast() pattern
 *      conflates into a segfault:
 *        - absent  -> nullptr (legal; the caller handles it)
 *        - present but not a T -> a diagnosable error, not a null
 *          dereference. Pin 5 requests Live via the ATTACK enum, which
 *          the villager does have, and requires an error rather than a
 *          misleading nullptr.
 */
void simulation_demo_50(const util::Path &path);


} // namespace openage::gamestate::tests
