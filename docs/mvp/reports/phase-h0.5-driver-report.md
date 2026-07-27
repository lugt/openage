# Phase H0.5 — Headless Driver next-event-jump + Polling Debt

Status date: 2026-07-28
Branch: `mvp/william-wallace`
Commits ahead of `9c19e5ae` (last pushed at H0 freeze): **3**
Pushed state at start of H0.5: `9c19e5ae` (H0 minimal baseline, demo 7 added).

## 1. H0 is a temporary fixed-step regression, NOT a driver

Phase H0 added the William Wallace Mission 1 headless regression as
`simulation_demo_7`. The demo exists to pin the Mission 1 scenario
plumbing (FQON-resolved PlacedEntities, ATTACK command channel,
objective controller reachability) before any time-driver work.
Its loop advanced time by a fixed `current = current + 1` per
iteration, which is a **regression smoke**, not a driver.

Phase H0.5 replaces the fixed step with a next-event-jump driver
that advances by the scheduled event time. H0's fixed-step loop is
**superseded** in mvp/william-wallace; it is preserved in git
history (commit `9c19e5ae`) for traceability but is no longer the
canonical headless driver.

Future phases (H1+) will adopt the same next-event-jump pattern in
the production simulation loop.

## 2. New runtime API surface (NOT a doc-only change)

Phase H0.5 adds two new public methods on `GameSimulation` and one
new public method on `EventLoop`. These were **not** present in H0:

  - `GameSimulation::is_objective_finished() const -> bool`
    (`libopenage/gamestate/simulation.h`)
  - `GameSimulation::tick_objective_controller(time::time_t now)`
    (`libopenage/gamestate/simulation.h`)
  - `EventLoop::peek_next_event_time() const -> std::optional<time::time_t>`
    (`libopenage/event/event_loop.h`)

Both Simulation methods forward to the existing
`ObjectiveController::is_finished()` /
`ObjectiveController::on_update(time_t)` API; the Simulation-level
methods exist so headless tests can observe the controller without
reaching past the encapsulation boundary.

`peek_next_event_time()` is a side-effect-free read; it does not
pop, reorder, or schedule anything. The read is protected by the
same `recursive_mutex` that `reach_time()` acquires transitively
via `execute_events()`. The mutex is recursive so a future H1+
production driver that calls `peek()` from inside a `reach_time()`
event handler does not deadlock.

## 3. Phase D temporary debt — objective controller polling

Phase D (Mission 1 Architecture Cleanup) is **not started** by this
commit chain. Until Phase D lands:

  - `tick_objective_controller(now)` is invoked once per event batch
    in both `simulation_demo_7` (H0.5 headless regression) and the
    production simulation loop.
  - The objective controller polls game state on every batch
    instead of consuming a dedicated `MissionCompletedEvent`.
  - This is the only path through which a headless run can observe
    a Victory state.

Phase D will:
  1. Add a `MissionCompletedEvent` to the EventHandler hierarchy.
  2. Wire the attacker/attacked-death path to emit it when the
     last enemy PlacedEntity is removed from `GameState`.
  3. Subscribe the `ObjectiveController` to that event in
     `setup_event`.
  4. Delete `tick_objective_controller()` and the
     `objective_poll_count` counter from `simulation_demo_7`.

Until then, `tick_objective_controller()` and the
`ObjectiveController::on_update()` polling branch are
**TEMPORARY DEBT**. The TODO lives in:
  - `libopenage/gamestate/demo/ww_mission1_regression.cpp` (the
    H0.5 next-event-jump loop's Phase D TEMPORARY DEBT comment).
  - `libopenage/gamestate/objective/objective_controller.cpp`
    (the on_update polling branch itself; this comment was added
    earlier and is intentionally left in place for Phase D pickup).

## 4. Verification matrix

| Demo | What it pins | Result at H0.5 freeze |
|------|--------------|-----------------------|
| 7 | WW Mission 1 headless regression, next-event-jump driver | PASS — `enemy_removed=1 objective_finished=1 time_advances=6 event_batches=6 objective_poll_count=6 last_dispatched_t=21.28976` |
| 8 | EventLoop fractional-time invariants (peek + reach_time + same-time batching) | PASS — peek hit 1.5 exactly, 3 same-time events batched in one reach_time(1.5), tail at t=2.5 dispatched in order, queue reported empty after dispatch |

Both demos run under `bin/run test --demo gamestate.tests.simulation_demo <N>`
on a headless box (no DISPLAY, no GPU, no media). The runtime
initialisation path is identical to `Engine::initialize_runtime()`;
no worker thread is spawned.

## 5. Local working tree state (intentional untracked)

The local working tree on `mvp/william-wallace` after this commit
chain contains **5 intentional untracked items**, none of which are
part of the H0.5 commit chain:

  1. `patches/` (top-level directory)
  2. `patches/5104aea7-residuals/error-CMakeLists.txt`
  3. `patches/5104aea7-residuals/exception_guard-CMakeLists.txt`
  4. `patches/5104aea7-residuals/validate_nyan-CMakeLists.txt`
  5. `patches/5104aea7-residuals/output_checks.py`

These are copies of the four 5104aea7 "restore lost cmake/python
registrations" residual files, preserved as documentation for the
Phase B converter run. The originals remain in their `openage/...`
locations (also untracked) so `cmake configure` keeps working --
the tracked `openage/CMakeLists.txt` `add_subdirectory(...)` lines
still reference them.

`PhaseB-status.md` (a /clear transcript artifact at the repo root)
was deleted as part of H0.5 cleanup; it was superseded by
`docs/mvp/reports/phase-b-converter-final-report.md`.

## 6. Out of scope (explicit non-goals)

  - **Phase D** (objective controller event-driven): not started.
  - **DE2 DDS terrain**: deferred to Phase D per the earlier
    FIX-3 decision; demo 7 runs on fixture terrain only.
  - **OpenGL downgrade or full H1**: out of scope for H0.5.
  - **macOS work**: parallel-trackable from pushed SHA `9c19e5ae`.
    See `docs/mvp/macos-handoff.md`.