# Headless Simulation Roadmap

Phase H0 ships only the minimum Phase D protection net -- a single
Mission 1 regression that proves the runtime can be driven to
completion without Qt, an OpenGL context, or a Renderer. This
document records the items that the headless test stack does NOT
cover yet, with intent to land them as Phase H1+ once Phase D
gameplay refactors (Attack / Death / Remove / ApplyDiscreteEffect /
event-notification objective) have stabilised.

Phase H0 ships (commit 12d17538's neighbourhood):

- `libopenage/gamestate/demo/ww_mission1_regression.cpp`
  `simulation_demo_7` runs after `Engine::initialize_runtime()` style
  setup. Manual-time advance loop (tick by tick, no fixed dt, no
  sleep, no shell-timeout-as-success). One ATTACK against
  EnemyVillager via the existing `game.send_command` event path.
- Stable-name lookup only:
  `PlayerVillager` / `PlayerTownCenter` / `EnemyVillager`. No raw
  `entity_id_t` exposed to the test, no map iteration, no pointer
  arithmetic.
- Failure snapshot is stable, sorted, pointer-free -- emitted only
  on assertion failure.

## H1 -- Command API + replay

- A clean C++ wrapper around `event_loop->create_event("game.send_command", ...)` so future tests do not need to know the
  param-map convention by heart. Wraps `Attack` and `Move` (and any
  Phase-D `Build` / `Train` / `Research` that lands first), exposes
  `issue_*_command(handle_stable_name, ...)` and returns a typed
  result so tests can assert enqueue success in addition to
  downstream effects.
- Replayable command scripts:
  - JSON command scripts parsed from a string, so a failing test can
    attach the script that produced the failure.
  - Loaded replayable scripts from `assets/test/headless/ww_mission1.json`
    (no codegen; just `JSON.parse`).
- Per-test CLI:
      bin/run test --demo william_wallace_mission1 [--replay PATH]
      bin/run test --demo william_wallace_mission1 --fail-fast-on first_3
- The replay script lives next to the test (under `assets/test/headless/`)
  so users can re-run the exact same sequence after a regression.

## H2 -- Generic event-trace observer + JSON snapshot

- An event observer implementation that hooks the existing
  `EventLoop` and records the event stream as a JSON document (or
  a dense binary form, tbd on size).
- Each snapshot records:
  - `current_time`
  - entity state diff (only entities / attributes that changed
    since the previous snapshot, so multi-MB growth is bounded)
  - command queue
  - objective controller state
  - event-log trace
- Snapshot hash (BLAKE2b truncated to 64 bits, tbd on collision risk
  in real workloads) for cheap comparability across runs.

## H3 -- Deterministic RNG + lockstep + networking

These come together or stay split depending on how the Phase D
combat / damage refactor lands.

- Deterministic RNG: the engine currently uses `std::rand` and
  threaded clocks in a few places. Phase H3 requires a single
  `RandomEngine` injected into every component that needs
  non-determinism, seeded from a deterministic seed in the
  headless test runner.
- Lockstep emulation: a CLI flag `--lockstep-step size` that
  batches events and step through them deterministically (similar to
  how a deterministic RTS replay works).
- Networking was the original use case for headless determinism;
  ship the surface area only if `doc/superpowers/.../` work
  describes a concrete networking milestone.

## Things deliberately NOT in H0

The user spec is explicit that H0 is the **minimum** gate. None of
the below is in scope for Phase H0 or H1; they are recorded so a
later reader does not assume they were forgotten.

- **Damage channel rewire (ApplyDiscreteEffect vs Attack path).**
  Out of scope per Phase D. The H0 test passes because the legacy
  attack handler still drains HP directly; once Phase D rewires
  damage to flow through the Effect system, this test will need a
  re-spec'd HP-curve assertion (the current one assumes the legacy
  6-per-hit curve).
- **Health <= 0 -> Death event abstraction.** Out of scope per
  Phase D. The H0 test passes because AttackHandler still
  removes-on-death inline; once Death becomes a first-class event,
  the test should subscribe to `on_death` instead of polling
  `state->has_game_entity(enemy_id)`.
- **Removal deferral.** Out of scope per Phase D. The H0 test
  asserts `enemy_removed` synchronously inside the same tick the
  death happens; deferral would shift the assertion to a later
  tick window.
- **Event-notification objective controller.** Out of scope per
  Phase D. The H0 test polls `is_objective_finished()` once per
  tick; the eventual objective should fire a
  `MissionCompletedEvent` instead.
- **DDS real-grass terrain.** Out of scope per Phase E / FIX-3.
  H0 runs on the `TestsceneGrass` test fixture; real terrain is
  load-bearing only for Stage 4C windowed FULL on macOS.
- **MacOS-only Qt-platform assertions / native event loop.**
  Out of scope for headless. macOS agent runs Stage 4C; H0
  intentionally does not depend on the Qt platform plugin at all.
- **Generic event-trace observer.** Scheduled for H2.
- **Snapshot hash.** Scheduled for H2.
- **Generic per-instance FQON lookup beyond the three WW test targets.**
  The H0 test only resolves the three FQONs that Mission 1's
  load_scenario registers. A generic helper for arbitrary
  PlacedEntities needs the load_scenario FQON schema finalised;
  Phase D scope.

## What H0 buys today

Even without H1-H3, `simulation_demo_7` is a working smoke that:

- runs `Engine::initialize_runtime()`'s exact path (no new
  initialisation code in the test);
- loads Mission 1 + PlayerSetup via the modpack closure
  `[engine, de2_base, ww_mvp]`;
- exercises the existing `game.send_command` event channel with no
  new wiring;
- asserts the canonical stop conditions (enemy removed, objective
  finished) before the deadline;
- emits a deterministically-ordered failure snapshot if any
  assertion fires, without embedding pointer values or thread IDs;
- produces the same `WW MISSION 1 HEADLESS REGRESSION: PASS`
  marker on every Ubuntu run verified by the G2 commit chain.

That is the Phase D protection net. Everything else listed above is
upgradable from this baseline.
