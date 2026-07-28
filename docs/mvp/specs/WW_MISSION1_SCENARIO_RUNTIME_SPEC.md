# William Wallace Mission 1 — Scenario Runtime Specification

**Document type:** Product and engineering specification  
**Target repository:** openage extension using Age of Empires II: Definitive Edition resources  
**Primary consumer:** Claude Code or another repository-level development agent  
**Status:** Execution baseline; changes require evidence and an explicit spec amendment  
**Date:** 2026-07-28

---

## 1. Purpose

This specification defines the requirements for advancing the project from a converter-focused prototype to an openage-native implementation of **William Wallace Mission 1**.

The project must produce three separately verifiable outcomes:

1. `MISSION_1_SCENARIO_IMPORTABLE`
2. `MISSION_1_HEADLESS_SEMANTIC`
3. `MISSION_1_WINDOWED_PLAYABLE`

These labels are not interchangeable. A loader smoke test, an in-memory combat demo, or a process surviving a timeout does not prove that the mission is playable.

---

## 2. Reported baseline — verify before relying on it

The development record reports the following state. Claude Code must reproduce or invalidate each claim before using it as a dependency:

- A lazy/streaming converter backend exists and uses mmap, ID-to-offset access, accessors, and iterators.
- A constrained lazy conversion reportedly completed below a 500 MiB release budget.
- `engine + de2_base` reportedly loads without unresolved nyan references, with loader time around 3 seconds.
- A `ww_mvp` modpack and a hand-authored William Wallace Mission 1 reconstruction exist.
- A headless event-loop driver can jump to the next event time and has fractional-time characterization tests.
- A minimal combat chain reportedly exists: command, approach, cooldown, damage, death/removal, objective completion.
- The current mission is not proven to be an import of the original AoE2 DE scenario.
- Terrain may still use a `testscene` fixture.
- Objective completion may still use polling.
- Attack damage, death, and removal may still be coupled in the attack handler.
- A real windowed scene with correct DE2 terrain, sprites, input, and victory UI has not been conclusively demonstrated.

The baseline is evidence to investigate, not a reason to skip tests.

---

## 3. Product terminology

### 3.1 Original Mission 1

A mission whose map, terrain, placements, players, diplomacy, triggers, and objectives originate from the actual AoE2 DE William Wallace Mission 1 scenario data.

### 3.2 Mission 1 reconstruction

A hand-authored openage scenario that approximates Mission 1 and references real `de2_base` entities. It must be described as a reconstruction and must not be presented as an imported original scenario.

### 3.3 Testscene

An isolated fixture namespace for renderer, loader, or gameplay tests. Gaben images, tanks, colored grids, synthetic terrain, and other diagnostic assets belong only here.

### 3.4 Headless

Simulation and data execution without Presenter, Qt window, OpenGL context, GPU, wall-clock sleeps, or offscreen-renderer substitutions.

### 3.5 Windowed playable

A real application window is created, the renderer submits frames, actual game resources are visible, input works, and the mission can reach victory through user interaction.

---

## 4. Strategic constraints

### 4.1 Converter is supporting infrastructure

Converter work is allowed only when it directly blocks one of the following:

- reading Mission 1 source scenario data;
- representing Mission 1 map, terrain, placements, players, diplomacy, or triggers;
- loading a Mission 1 FQON;
- exporting the exact sprite, animation, sound, or terrain media required by Mission 1;
- reproducing a fresh end-to-end asset build.

Do not resume broad converter correctness campaigns, all-civilization cleanup, all-terrain export, all-projectile coverage, or loader optimization without a Mission 1 blocker and a failing test.

### 4.2 Lazy conversion is mandatory

All real DE2 conversion runs must use the existing lazy/streaming path.

Forbidden:

- falling back to eager conversion;
- loading the full gamespec into a second object tree;
- rebuilding complete ID-to-object dictionaries for convenience;
- retaining all converted media or nyan objects until final export;
- silently using stale output from a previous conversion.

The release memory gate remains **peak RSS below 500 MiB** unless the user explicitly changes it.

### 4.3 Source formats do not enter simulation

The C++ simulation must not parse AoE2 DE campaign or scenario files directly.

Required boundary:

```text
AoE2 DE source
  -> source-specific parser/extractor
  -> normalized Scenario IR
  -> openage-native mission pack
  -> C++ Scenario Loader
  -> GameState and EventLoop
```

### 4.4 Event and Curve responsibilities

- **Curve** stores time-dependent state and history.
- **Event** causes or schedules state transitions.
- **Command** represents player or mission intent.
- **Effect** applies a discrete gameplay result.
- **Trigger** maps conditions to effects/events.

Do not encode the whole mission as one curve. Do not use polling where an existing dependency or lifecycle event can drive reevaluation.

---

## 5. Scope

### 5.1 In scope

- Original Mission 1 source feasibility investigation.
- A normalized Scenario IR.
- Scenario pack generation or a source-independent intermediate dump.
- Terrain dimensions, tile IDs, elevation, and chunk loading.
- Player setup and diplomacy.
- Entity placement and stable scenario-object mapping.
- A minimal trigger runtime covering types actually used by Mission 1.
- Event-driven attack effects, health depletion, death, deferred removal, and objective completion.
- Deterministic-enough event ordering within the supported runtime contract.
- Headless regression execution using next-event jumps.
- Mission 1 media closure for visible units, buildings, terrain, and required sounds.
- A reproducible windowed launch script and manual acceptance procedure.

### 5.2 Out of scope

- Mission 2–5.
- Generic campaign UI, campaign unlock progression, cutscene system, or save progression.
- Complete AoE2 scenario/campaign format support beyond fields required by Mission 1.
- Multiplayer, lockstep networking, replay productization, or general AI.
- Full Activity Graph combat migration unless required by a failing Mission 1 acceptance test.
- Full DE2 media export.
- Full terrain catalog conversion.
- Broad loader caching or batching when startup is already below 30 seconds.
- Perfect gameplay balance or complete AoE2 damage bonus replication.

---

## 6. Architecture requirements

## 6.1 Scenario IR

The converter or extractor must produce a source-independent Scenario IR. Names may follow repository conventions, but the model must preserve the following semantics:

```cpp
struct ScenarioDefinition {
    ScenarioId id;
    MapDefinition map;
    std::vector<PlayerDefinition> players;
    std::vector<EntityPlacement> placements;
    std::vector<AreaDefinition> areas;
    std::vector<TriggerDefinition> triggers;
    VictoryDefinition victory;
};
```

```cpp
struct MapDefinition {
    uint32_t width;
    uint32_t height;
    std::vector<TerrainId> terrain_ids;
    std::vector<int8_t> elevation;
};
```

```cpp
struct EntityPlacement {
    ScenarioObjectId scenario_object_id;
    EntityDefinitionId entity_definition;
    PlayerId owner;
    Coordinate position;
    float rotation;
    AttributeOverrides overrides;
};
```

```cpp
struct TriggerDefinition {
    TriggerId id;
    bool initially_enabled;
    bool looping;
    time::time_t delay;
    std::vector<ConditionDefinition> conditions;
    std::vector<EffectDefinition> effects;
};
```

Requirements:

- Dense terrain and elevation data must not be represented as one nyan object per tile.
- Source-specific IDs may be preserved in debug metadata, but runtime APIs must use normalized types.
- Unknown fields, conditions, and effects must be reported; they must not be silently discarded.
- The IR must be serializable to a stable debug form for golden tests.

## 6.2 Mission pack layout

A mission pack should separate relationship-rich data from dense map data. A valid implementation may use equivalent repository-native formats.

Recommended layout:

```text
ww_mvp/
  modpack.toml
  william_wallace/mission_01/
    scenario.nyan
    triggers.nyan
    terrain.bin
    placements.bin
    debug/
      scenario.json
      object_table.csv
      trigger_graph.json
      coverage.json
```

The mission pack must reference `de2_base` entities and media. It must not copy complete Villager, Town Center, terrain, or civilization definitions into the mission namespace.

## 6.3 Scenario Loader

The Scenario Loader must initialize, in a documented order:

1. terrain and chunks;
2. players;
3. diplomacy;
4. placed entities;
5. `ScenarioObjectId -> GameEntityId` mapping;
6. areas and dependency watchers;
7. trigger instances;
8. initial objective and timer events.

The loader must not rely on entity creation order as a substitute for scenario object identity.

## 6.4 Terrain

Terrain requirements:

- map dimensions are preserved;
- tile IDs are preserved or mapped through an explicit conversion table;
- elevation is preserved;
- terrain chunks are populated through a batch/snapshot API rather than excessive per-tile factory calls;
- pathfinding passability and renderer data are derived from the same loaded terrain state;
- static terrain is not modeled as a time curve;
- dynamic occupancy is separate from terrain type/elevation.

A testscene terrain may remain temporarily for simulation work, but final `MISSION_1_WINDOWED_PLAYABLE` acceptance requires a real converted DE2 terrain used by the mission.

## 6.5 Runtime trigger model

Each trigger must have explicit runtime state, including:

- enabled state;
- fire count;
- looping/one-shot behavior;
- delay;
- dependency registrations;
- conditions;
- ordered effects.

A trigger must be reevaluated through event dependencies or scheduled times, not by scanning all triggers every simulation tick.

Effects from one trigger must have stable ordering using data such as:

```text
simulation_time
trigger_id
effect_index
```

Pointer addresses must not be treated as deterministic semantic ordering.

## 6.6 Gameplay lifecycle

The target lifecycle is:

```text
AttackCommand
  -> approach/movement events
  -> AttackEvent
  -> ApplyDiscreteEffect
  -> Health Curve update
  -> HealthDepleted/DeathEvent
  -> Dead state
  -> deferred RemovalEvent
  -> indexes/renderer/selection/pathing cleanup
  -> mission-relevant lifecycle event
  -> objective completion
```

Requirements:

- Attack handlers do not directly own generic death semantics.
- Damage is applied through the existing effect system or a repository-consistent `ApplyDiscreteEffect` path.
- Death fires once.
- Removal is deferred and safe during event iteration.
- Stale future attacks against removed entities become harmless and do not recreate or double-remove entities.
- Objective completion is event-driven.
- The final implementation has no objective polling API or polling counter in the production path.

## 6.7 Headless driver

The headless scenario driver must:

- initialize real GameSimulation/Game/GameState paths;
- load the mission pack through the same database/loader used by the game;
- advance to `EventLoop::peek_next_event_time()` or an equivalent next-event API;
- execute all events at the selected timestamp as one batch according to EventLoop semantics;
- support fractional timestamps;
- enforce maximum event batches and simulation deadlines;
- print a useful failure snapshot;
- avoid wall-clock sleep.

## 6.8 Windowed runtime

The windowed runtime must use the same mission pack and scenario FQON as headless tests.

It must prove:

- Qt/application window creation;
- OpenGL renderer initialization at the supported version;
- repeated frame submission;
- visible terrain;
- visible Player Villager, Town Center, and mission enemy/targets;
- entity selection feedback;
- right-click movement;
- attack command dispatch from user input;
- visible enemy removal/death result;
- visible mission objective/victory state;
- clean shutdown.

Process survival alone is insufficient.

---

## 7. Minimum Mission 1 trigger coverage

The exact implementation list must be generated from a real source dump. The following are candidates, not permission to guess.

Possible conditions:

- `TimerElapsed`
- `EntityInArea`
- `EntityDestroyed`
- `EntityAlive`
- `EntityOwnedBy`
- `PlayerOwnsCount`
- `AttributeCompare`
- `TriggerFired`
- a semantic tutorial-selection signal if the source requires selection

Possible effects:

- `DisplayInstruction`
- `SetObjective`
- `CompleteObjective`
- `IssueMoveCommand`
- `IssueAttackCommand`
- `ActivateTrigger`
- `DeactivateTrigger`
- `CreateEntity`
- `RemoveEntity`
- `ChangeOwnership`
- `DeclareVictory`
- `DeclareDefeat`

Only types present in the Mission 1 coverage report should be implemented during this project.

---

## 8. Data and asset integrity requirements

### 8.1 Fresh output

A final end-to-end test must use a fresh asset root. It must not combine:

- new nyan with old media;
- old nyan with new media;
- test fixtures with production mission paths;
- output from different converter commits without an explicit manifest.

### 8.2 Asset provenance

For each mission-critical asset, the verification report must identify:

- FQON;
- metadata file;
- final media path;
- file existence;
- file size;
- modification time;
- source conversion run/manifest;
- whether it is `de2_base`, `ww_mvp`, or `testscene`.

At minimum audit:

- Player Villager sprite and Idle/Walk/Attack animations;
- enemy sprite and relevant animations;
- Town Center sprite;
- Mission 1 terrain definition and texture;
- selection indicator;
- required instruction/victory UI resources;
- required sound effects if enabled.

### 8.3 Test fixture isolation

The production Mission 1 path must not load:

- Gaben imagery;
- tank fixtures;
- colored/grid diagnostic textures;
- `TutorialGrass` or equivalent mislabeled synthetic terrain;
- presenter-only demo entities.

Temporary use of `engine.test.testscene.TestSceneGrass` is allowed only before final windowed acceptance and must be logged as diagnostic terrain.

---

## 9. CLI and script requirements

The repository must provide one user-facing launch script and one automated verification script.

Recommended names:

```text
scripts/run_ww_mission1.sh
scripts/verify_ww_mission1.sh
```

`run_ww_mission1.sh` must:

- run from the repository root regardless of current directory;
- print commit, git status summary, Python executable, game executable, asset root, modpacks, scenario FQON, and player setup FQON;
- validate required files before launch;
- refuse silent conversion;
- refuse silent fallback to stale assets;
- save stdout/stderr to a named log;
- launch the real windowed game by default;
- propagate a meaningful exit status.

`verify_ww_mission1.sh` must:

- build with a memory-safe parallelism level;
- run import and database-load smoke tests;
- run nyan/media/terrain closure checks;
- run headless mission regressions;
- optionally probe windowed capability when a real graphics environment is present;
- distinguish timeout, environment block, test failure, and crash.

All command-line flags must be resolved from the repository's actual `--help`; do not invent flags.

---

## 10. Test requirements

## 10.1 Unit tests

Required coverage:

- Scenario IR validation and round-trip/debug serialization;
- terrain dimension and dense-array validation;
- scenario-object mapping stability;
- trigger enable/disable and one-shot/looping behavior;
- each Mission 1 condition type;
- each Mission 1 effect type;
- effect ordering;
- ApplyDiscreteEffect updates the Health curve;
- Health depletion emits one death event;
- deferred removal occurs once;
- stale attacks after death are ignored;
- player-unit removal does not falsely satisfy enemy objective;
- all configured enemy targets must be removed before victory;
- next-event peeking and fractional-time batch semantics.

Source-text tests may supplement behavior tests but cannot be the primary evidence.

## 10.2 Integration tests

Required coverage:

- load `engine + de2_base + ww_mvp` from a fresh asset root;
- resolve the Mission 1 Scenario and PlayerSetup FQONs;
- load terrain, players, diplomacy, and placements;
- spawn expected named scenario instances;
- run movement and area-trigger flow;
- run attack, effect, death, removal, objective, and victory flow;
- report unknown trigger types rather than skipping them;
- no nyan `NameError`, `MemberNotFoundError`, or member type errors.

## 10.3 Repetition and sanitizers

For the final headless lifecycle test:

- Debug: 20 successful runs;
- normal build: 50 successful runs;
- ASan: 5 successful runs;
- UBSan: 5 successful runs.

Every run must satisfy:

```text
enemy_removed=1
objective_finished=1
objective_polling_absent=1
deadline_not_reached=1
event_budget_not_exhausted=1
exit=0
```

Do not claim cross-platform deterministic hashing unless a stable semantic ordering and hash contract are explicitly implemented and tested.

## 10.4 Windowed acceptance

Windowed acceptance requires a real graphics environment. Evidence must include:

- launch command and log;
- renderer/vendor/version details;
- frame progression evidence;
- screenshot or user-observed window;
- visible correct terrain and entities;
- manual input checklist results;
- clean exit.

If OpenGL requirements are unavailable, report:

```text
WINDOWED_FULL: ENVIRONMENT_BLOCKED
```

Do not weaken renderer requirements or describe offscreen/headless execution as a windowed pass.

---

## 11. Completion definitions

## 11.1 `MISSION_1_SCENARIO_IMPORTABLE`

All must be true:

- The original Mission 1 source location and format are identified.
- The repository's existing parser/import support is documented.
- A real source dump or import produces map dimensions, terrain/elevation, players, diplomacy, placements, and trigger coverage.
- Unsupported fields and trigger types are reported.
- Scenario IR passes validation and golden checks.
- The output is not mislabeled as a reconstruction.

If original data cannot legally or technically be parsed in the current environment, the result must be `BLOCKED` with evidence; it must not be silently replaced by a hand-authored layout.

## 11.2 `MISSION_1_HEADLESS_SEMANTIC`

All must be true:

- A ScenarioDefinition is loaded through the production loader path.
- Real or explicitly declared reconstruction placements are used.
- The event loop advances by next-event time, including fractional times.
- Damage uses ApplyDiscreteEffect or the repository-standard discrete effect path.
- Health is represented through the intended curve/state model.
- Generic death and deferred removal are implemented.
- Mission objective completion is event-driven.
- Objective polling is absent from production Mission 1.
- Move, combat, removal, and victory regressions pass.
- Fresh-root integration, repetition, ASan, and UBSan gates pass.
- No GPU, OpenGL, DISPLAY, Qt offscreen, or wall-clock sleeps are required.

## 11.3 `MISSION_1_WINDOWED_PLAYABLE`

All `MISSION_1_HEADLESS_SEMANTIC` requirements plus:

- The real game window opens.
- The renderer submits frames.
- A real converted DE2 terrain is used by the mission.
- Correct DE2 game entities and animations are visible.
- No Gaben, tank, grid, or testscene-only visual appears in the production mission.
- The player can select the unit.
- The player can move the unit.
- The player can issue an attack command.
- The enemy visibly dies/is removed.
- Mission instruction/objective/victory UI is visible.
- The application exits cleanly.

---

## 12. Required reports and artifacts

Claude Code must maintain or produce:

```text
docs/mvp/reports/baseline-reality-audit.md
docs/mvp/reports/mission1-scenario-feasibility.md
docs/mvp/reports/mission1-trigger-coverage.json
docs/mvp/reports/mission1-asset-provenance.md
docs/mvp/reports/mission1-headless-acceptance.md
docs/mvp/reports/mission1-windowed-acceptance.md
```

Each report must include:

- commit and working-tree state;
- exact commands;
- exit codes;
- relevant logs/artifact paths;
- observed result;
- pass/fail/blocked classification;
- unsupported or deferred items;
- no unsupported claims.

---

## 13. Change and commit policy

- Keep commits atomic and domain-specific.
- Do not combine converter, scenario, gameplay, renderer, and documentation changes without a demonstrated indivisible dependency.
- Add a failing behavior test before or with each bug fix.
- Do not edit generated nyan/media as the source fix.
- Do not weaken assertions to match incorrect behavior.
- Do not force-push or rewrite already shared history.
- After every gameplay lifecycle commit, run the existing event-loop characterization tests and Mission 1 headless regression.

Recommended commit families:

```text
scenario: add Mission 1 source feasibility dump
scenario: define normalized scenario IR
scenario: load terrain and placements from scenario definition
gameplay: route attack damage through discrete effects
gameplay: emit generic death from health depletion
gameplay: defer dead entity removal
scenario: complete objectives from lifecycle events
convert: export Mission 1 DE2 terrain media
test: validate Mission 1 headless semantic completion
script: add reproducible Mission 1 launch and verification
docs: record Mission 1 windowed acceptance
```

---

## 14. Stop conditions

Stop the current implementation loop and report evidence when:

- legal/source data required for the original scenario is unavailable;
- the lazy source data is corrupt or requires a user choice about destructive recovery;
- a real graphics environment is unavailable for windowed acceptance;
- the same root cause has received two evidence-based fixes without progress;
- a proposed solution would require violating this specification's architecture or memory constraints.

A blocker in one workstream does not stop independent tests, reports, or implementation in another workstream.

---

## 15. Final project rule

Do not start Mission 2, resource gathering, building, training, research, campaign progression, or general AI until the user explicitly accepts the Mission 1 completion status and its evidence.
