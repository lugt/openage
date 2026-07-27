// Copyright 2023-2023 the openage authors. See copying.md for legal info.

#pragma once

#include "util/path.h"


namespace openage::gamestate::tests {

/**
 * Show the initialization of a game instance.
 */
void simulation_demo_0(const util::Path &path);

/**
 * William Wallace Mission 1 -- Phase H0 minimal headless regression.
 *
 * Loads the Mission 1 scenario via the standard runtime initialization
 * path, resolves the three PlacedEntities by their stable instance
 * FQONs, dispatches a single ATTACK against EnemyVillager through
 * the existing send_command event channel, advances the simulation
 * tick-by-tick (NO fixed tick, NO sleep), and asserts both the
 * entity-removal and the objective-finished stop conditions before
 * the deadline. A stable, pointer-free snapshot is emitted on any
 * assertion failure.
 *
 * Headless: this demo never touches Presenter, Qt, or any OpenGL
 * context, so it is safe to run on a CI box with no DISPLAY/GPU.
 */
void simulation_demo_7(const util::Path &path);

/**
 * Phase H0.5 fractional EventLoop characterisation (demo 8).
 *
 * Pins down three invariants the H0.5 manual-time driver relies on:
 *   - peek_next_event_time() returns the front-of-queue scheduled time
 *     (no pop, no reorder, no schedule).
 *   - A scheduled event at t=1.5 is dispatched at t=1.5 on next
 *     reach_time call, NOT at t=2.0 or any later time-unit.
 *   - reach_time(t) drains ALL pending events with time <= t in a
 *     single call. After draining a same-time batch, peek reports
 *     the next distinct event time, not nullopt; later-time events
 *     are not invoked before earlier-time ones.
 *
 * Same-time insertion ordering is NOT asserted: the PairingHeap-backed
 * EventStore uses SharedPtrLess for ties, which is non-stable.
 */
void simulation_demo_8(const util::Path &path);

} // namespace openage::gamestate::tests
