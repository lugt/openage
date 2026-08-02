# P4.2 / P4.3 Trigger Runtime + Mission 1 Data-Driven Objective - Report

## 0. Scope

P4.2 evolves Mission 1's `ObjectiveController` into a general,
data-driven scenario trigger runtime. P4.3 migrates Mission 1 onto
trigger data and removes the hand-rolled controller from the
production path. The stop point is
`P4_TRIGGER_RUNTIME_COMPLETE` / `MISSION_1_DATA_DRIVEN_OBJECTIVE_COMPLETE`.
P5 is not started.

The runtime lives in `openage::scenario::trigger` (not
`openage::gamestate::scenario::trigger`). The scenario IR types
(`MissionTriggerCondition/Effect/Definition`) remain in
`openage::gamestate` (`scenario/mission_pack.h`).

## 1. Runtime architecture

The runtime is a layered set of value + strategy types. Each layer
depends only on layers below it, so the commits compile in order.

```
trigger_definition.{h,cpp}   TriggerDefinition snapshot, TriggerState /
                             ConditionKind / EffectKind enums,
                             DependencyKey (kind, key)
mission_state.{h,cpp}        MissionLifecycle (RUNNING/VICTORY/DEFEAT)
                             + ObjectiveState (INACTIVE/ACTIVE/COMPLETED)
trigger_condition.{h,cpp}    TriggerCondition strategy + 3 impls
trigger_effect.{h,cpp}       TriggerEffect strategy + enqueue impls
trigger_instance.{h,cpp}     one trigger's state machine + fire()
trigger_runtime.{h,cpp}      registry, dependency index, handlers
```

### State machine

```
DISABLED --(set enabled)-->          ARMED
ARMED    --(conds hold, delay==0)--> FIRED --> COMPLETED  (non-looping)
ARMED    --(conds hold, delay>0)---> WAITING_DELAY
WAITING_DELAY --(delay elapsed,
                 conds still hold)--> FIRED --> COMPLETED
WAITING_DELAY --(delay elapsed,
                 conds false)-------> ARMED   (cancel)
```

### Evaluation flow

1. `RemovalEventHandler` invokes its observers after
   `gstate->remove_game_entity`. The production observer (registered
   in `GameSimulation::register_trigger_handler`) forwards each
   removal to `runtime->notify_entity_removed(id, at)`.
2. `notify_entity_removed` looks up `DependencyKey{ENTITY, id}` in
   the dependency index and schedules a `trigger.evaluate` event at
   the removal's reference time for every subscribed trigger.
3. `TriggerEvaluateHandler` (a `OnceEventHandler`) dispatches into
   `evaluate_trigger`, which re-checks conditions, routes through
   `WAITING_DELAY` for delayed triggers, and calls `fire()`.
4. `fire()` enqueues effects in stable `effect_index` order as
   internal events (`set_objective_state`, `activate_trigger`,
   `declare_victory`, ...). The event loop's strict-weak-ordering
   comparator (established in P4.1.1) makes that order deterministic
   across runs.

### Kind dispatch

The IR kind string is the CamelCase token emitted by the scenario
writer and validated by `validate_mission_pack`
(`SUPPORTED_CONDITION_KINDS` / `SUPPORTED_EFFECT_KINDS` in
`mission_pack.cpp`). The runtime dispatchers (`make_trigger_condition`,
`make_trigger_effect`) match those exact tokens; unknown kinds log a
warning and are skipped (the IR writer is the error path, not the
runtime). The internal event names (`set_objective_state`, etc.) are
a separate snake_case namespace from the IR data kinds.

## 2. P4.3 production wiring

`GameSimulation` now owns a `scenario::trigger::TriggerRuntime`
instead of the `WilliamWallaceMission01ObjectiveController`:

- `initialize()` constructs the runtime on the simulation's event
  loop + GameState, registers its handlers, calls
  `wire_trigger_data`, and registers the removal observer.
- `wire_trigger_data(mission_pack_dir)` re-reads the Mission Pack,
  feeds `pack.triggers` to the runtime, then synthesizes one
  `defeat_enemy` trigger per `VictoryDefinition.required_removals`
  (`EntityDestroyed` + `SetObjective(ACTIVE)` + `CompleteObjective`
  + `DeclareVictory`). This is the data signal: the committed
  scenario dump carries zero triggers (pinned to 0 by
  `test_p1_dump.py`), so the `defeat_enemy` trigger is synthesized
  from `required_removals` in C++ (scheme B). The loader registers
  each named entity under both the full instance FQON and the short
  scenario_object_id, so the short id resolves directly.
- `is_objective_finished()` reads the runtime's `MissionState`
  victory flag (was: the `ObjectiveController`).

The `ObjectiveController` is retained as a legacy unit-test harness
(demos 8-11, 14 build it directly); it is no longer on the
production path.

## 3. Handler-collision regression (key finding)

After the P4.3 production wiring, demos 17-19 regressed: triggers
stayed `ARMED` (state=1) and never reached `COMPLETED` /
`WAITING_DELAY`.

### Root cause

The `EventLoop` handler store is an
`unordered_map<string, shared_ptr<EventHandler>>` keyed by handler
id (`event/event_loop.h:170`). `add_event_handler` uses `insert`,
which is a no-op for a key that already exists -- the FIRST
registration wins. Demos 17-19 boot the production simulation
(`initialize()` registers the production `trigger.evaluate` handler
on the simulation's loop) and then construct a SECOND
`TriggerRuntime` on the SAME loop. The second `register_handlers()`
is silently ignored; the demo's `trigger.evaluate` events route into
the production runtime, which does not know the demo's trigger id,
logs "unknown trigger_id", and no-ops. The trigger never fires.

Demo 20 was unaffected because it uses the production runtime
directly (one runtime, no collision).

### Fix

Demos 17-19 are unit tests for the runtime; they boot the
production simulation solely to obtain a GameState with the named
entities loaded. They now construct their `TriggerRuntime` on a
FRESH `EventLoop` (`std::make_shared<openage::event::EventLoop>()`)
instead of `simulation->get_event_loop()`. The fresh loop's handler
store has only the demo's handlers, so its `trigger.evaluate` events
dispatch correctly. The GameState is shared (read/remove only); the
simulation's loop is left dormant. This keeps the unit test
isolated from the production wiring regardless of how `initialize()`
evolves.

## 4. Validation

### C++ demos (`bin/run test --demo gamestate.tests.simulation_demo N`)

| Demo | Phase | Result |
|------|-------|--------|
| 7  | production victory (attack -> death -> removal -> VICTORY via trigger runtime) | PASS |
| 8  | EventLoop fractional-time batching | PASS |
| 9-16 | P3/P4.1 regression (damage, death, removal, objective, native load, lifecycle, nyan contract, condition) | PASS |
| 17 | P4.2 trigger evaluation single-shot | PASS |
| 18 | P4.2 trigger delay (WAITING_DELAY) | PASS |
| 19 | P4.2 effect ordering (stable effect_index) | PASS |
| 20 | P4.3 Mission 1 data-driven victory (production path) | PASS |

Full matrix 7-20: 14/14 PASS.

### Python suites

| Suite | Result |
|-------|--------|
| `openage/scenario/tests/` | 103 passed |
| `openage/validate_nyan/tests/` | 3 passed |
| `openage/convert/.../test_de2_profiles.py` | 25 passed |

(pytest is not in the venv; run via the homebrew `python3 -m pytest`
which imports `openage` from the repo root.)

### Production smoke (90 s)

```
bin/run main --asset-dir assets --no-convert --no-prompts --headless \
             --modpacks engine de2_base ww_mvp --window-size 400 300 --scenario
```

92 s, then SIGTERM. Exit 143 (the test's SIGTERM, not a crash).
Crash-marker count: 0. The P4.3 wiring is visible in the log:

```
load_mission_pack_into_game_state: victory required_removal target=EnemyVillager
  (synthesized into a defeat_enemy trigger by GameSimulation::wire_trigger_data)
GameSimulation::wire_trigger_data: triggers=0 (data) + 1 (synthesized
  from required_removals); runtime trigger_count=1
GameSimulation::initialize: phase complete (game + trigger runtime ready;
  lifecycle=Initialized)
Time loop started
... (92 s, no exception)
SIGTERM received
```

## 5. Architectural invariants preserved

The P3 attack/effect/death/removal chain and the P4.1.1 stable
ordering are intact. The only production-path change is the owner of
the mission objective state: `ObjectiveController` ->
`TriggerRuntime`. `is_objective_finished()` remains the public
contract; demo 7 (production) and demo 20 (data-driven) both assert
on it. The `RemovalEventHandler` observer pattern is unchanged; P4.3
swaps which observer is registered.

## 6. Atomic commits

| SHA | Subject |
|-----|---------|
| `6a94eb61` | trigger: add core definition + mission state types |
| `4a486f29` | trigger: add condition interface + EntityRemoved/TimerElapsed/TriggerFired |
| `065069e0` | trigger: add effect interface + enqueue implementations |
| `d43931a6` | trigger: add TriggerInstance + TriggerRuntime + event handlers |
| `4411b02b` | test: add P4.2 trigger runtime TDD demos (16-19) |
| `dd148e35` | gamestate: wire TriggerRuntime into GameSimulation production path |
| `6aa99aa4` | test: add P4.3 Mission 1 data-driven victory demo (20) |
| (this commit) | docs: record P4.2/P4.3 trigger runtime + data-driven objective |

Commits 1-4 build in layer order (each layer depends only on layers
below). Commit 5 adds the demos that exercise the runtime. Commit 6
rewires production. Commit 7 adds the production-path integration
demo. The demo files stage incrementally: `tests.cpp` /
`demo/CMakeLists.txt` carry demos 16-19 at commit 5 and demo 20 at
commit 7.

## 7. Handoff / P5 entry point

P4 is complete. Mission 1's objective is data-driven: the
`defeat_enemy` trigger synthesized from
`VictoryDefinition.required_removals`, executed by the
`TriggerRuntime`, declares victory through the production event
chain, and `is_objective_finished()` reads it.

Per the session constraint, P5 (media closure) is NOT started.

Known follow-ups (out of scope for this session):
- The scenario dump carries zero triggers (`test_p1_dump.py` pins
  it). When the dump is regenerated with real triggers, `wire_trigger_data`
  will feed them directly and the `required_removals` synthesis
  becomes a fallback.
- Several effect kinds (`CreateEntity`, `RemoveEntity`,
  `ChangeOwnership`, `IssueMoveCommand`, `IssueAttackCommand`) are
  stubbed (enqueue + log); they are ready for later spikes without
  touching the dispatch contract.
- `TriggerStateCondition` is pessimistic-false until a state
  provider is wired; the IR does not yet carry a target-state field.
- Ubuntu crosscheck of the full matrix is pending (per the standing
  cross-platform boundary); macOS is green.
