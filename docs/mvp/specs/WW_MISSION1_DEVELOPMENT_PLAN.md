# William Wallace Mission 1 — Development and Verification Plan

**Companion specification:** `WW_MISSION1_SCENARIO_RUNTIME_SPEC.md`  (in this directory)  
**Execution mode:** Claude Code repository workflow  
**Date:** 2026-07-28

---

## 1. Operating instructions for Claude Code

1. Read the companion specification before modifying code.
2. Treat all reported historical results as claims to reproduce, not guaranteed facts.
3. Work continuously through the phases below while repository-local problems remain solvable.
4. Do not stop after writing a plan or analysis; produce code, tests, scripts, reports, and atomic commits.
5. Do not ask the user for information that can be discovered from the repository, CLI help, git history, logs, build files, or configured asset paths.
6. Do not invent commands or flags. Resolve them from actual help output and record them in this file or the phase report.
7. Use `ninja -j1` by default where prior evidence indicates parallel builds can exceed the memory limit. Increase only after measuring.
8. Never fall back from lazy conversion to eager conversion.
9. Use the actual production loader and simulation paths. Test doubles may isolate units but cannot replace integration acceptance.
10. Update status only after a command has run and evidence exists.

---

## 2. Execution loop

For each increment:

```text
select the next unmet acceptance requirement
  -> reproduce current behavior
  -> add or identify a failing behavior test
  -> make the smallest architecture-consistent change
  -> build
  -> run focused tests
  -> run mandatory regressions
  -> inspect logs and generated artifacts
  -> update the phase report
  -> atomic commit
  -> continue
```

When a test fails:

- preserve the first failure and complete stack/log;
- identify whether the failure is source parsing, generated data, loader, simulation, renderer, test, or environment;
- fix the earliest incorrect layer;
- do not bypass the failure with fallback assets or weaker assertions.

---

## 3. Phase overview

| Phase | Goal | Exit label or output |
|---|---|---|
| P0 | Reality and baseline audit | trusted baseline report |
| P1 | Original Scenario feasibility | source decision + IR dump |
| P2 | Scenario IR and loader | map/players/placements load headlessly |
| P3 | Gameplay lifecycle cleanup | effect → health → death → removal |
| P4 | Trigger and objective runtime | event-driven mission completion |
| P5 | Terrain and media closure | real Mission 1 visual asset closure |
| P6 | Fresh headless semantic acceptance | `MISSION_1_HEADLESS_SEMANTIC` |
| P7 | Windowed integration and manual acceptance | `MISSION_1_WINDOWED_PLAYABLE` |

P1–P4 are the primary Scenario/Simulation workstream. P5 is a constrained asset workstream and must not expand into full converter coverage.

---

# P0 — Reality and baseline audit

## P0.1 Capture repository state

Record:

```text
repository root
branch
HEAD SHA
git status --short
recent relevant commits
build directory
installed/runtime asset root
Python executable and environment
game executable
available modpacks
```

Create:

```text
docs/mvp/reports/baseline-reality-audit.md
```

(Living docs/mvp/specs/WW_MISSION1_DEVELOPMENT_PLAN.md and
 WW_MISSION1_SCENARIO_RUNTIME_SPEC.md are the equivalents that
 followed the original 2026-07-28 session into this branch.)

## P0.2 Resolve actual commands

Use repository help and build configuration to resolve and record commands for:

- configuring/building;
- running focused C++ tests;
- running event-loop demos/regressions;
- loading nyan/modpacks;
- launching a scenario headlessly;
- launching the real windowed game;
- running the lazy converter;
- running nyan/media validators.

Do not continue using guessed historical commands if they do not match the current CLI.

## P0.3 Reproduce reported converter and loader state

Check:

- lazy backend is selected in the real call path;
- latest converted output manifest and commit match;
- database loads `engine`, `de2_base`, and `ww_mvp`;
- scenario and player setup FQONs resolve;
- loader timing is measured from the actual command;
- no unresolved nyan error is hidden by grep or exception guards.

Do not run a full conversion unless the output is missing, stale, or incompatible. If conversion is required, run under the existing memory limit and record peak RSS.

## P0.4 Reproduce headless tests

Run all existing simulation demos and Mission 1 regressions. For each, record:

- exact binary/test target;
- whether it loads a real mission pack;
- whether it initializes GameSimulation/Game/GameState;
- whether it uses Presenter or renderer;
- whether it uses test fixtures;
- pass/fail and exit code.

Explicitly establish what each test proves and does not prove.

## P0.5 Audit the mission identity

Identify the current production scenario path and classify it as:

- imported original Mission 1;
- hand-authored Mission 1 reconstruction;
- testscene fixture;
- mixed/ambiguous.

Confirm that formal logs use accurate names:

```text
Scenario kind: William Wallace Mission 1 MVP reconstruction
```

until an original scenario import is actually used.

## P0.6 Audit windowed startup without claiming visual success

Run the canonical launch path long enough to capture the first real failure or success. Record:

- window creation;
- renderer initialization;
- frame-loop start;
- scenario load;
- first runtime error;
- exit status.

Do not mark windowed rendering as passed without a visible scene or screenshot/user observation.

## P0 exit criteria

- Baseline report committed.
- Actual commands resolved.
- Every historical claim classified as reproduced, contradicted, or not tested.
- The next failure is known at a specific layer.

Suggested commit:

```text
docs: audit William Wallace Mission 1 runtime baseline
```

---

# P1 — Original Mission 1 Scenario feasibility spike

This is the highest-priority new investigation.

## P1.1 Locate source data

Search configured AoE2 DE resources and repository code for:

- William Wallace campaign container;
- Mission 1 scenario file/resource;
- existing campaign/scenario readers;
- Genie or DE2 structures exposing scenario data;
- prior SCX/SCN parser code, tests, or documentation.

Do not copy or commit proprietary source assets.

## P1.2 Select one evidence-based path

### Path A — Existing importer

- run it against Mission 1;
- document command and parser path;
- dump the normalized content;
- identify gaps.

### Path B — Existing low-level parser, no converter

Implement a Mission 1 mini importer for only:

- map dimensions;
- terrain tile IDs;
- elevation;
- players;
- diplomacy;
- placed entities;
- areas/locations;
- triggers, conditions, and effects.

### Path C — No usable parser

Implement a source-specific offline extractor with a strict boundary to Scenario IR. Do not add source parsing to C++ simulation.

### Path D — Source unavailable or unsupported

Produce a blocker report with exact evidence. Continue using the reconstruction only if clearly labeled; do not claim import completion.

## P1.3 Produce a Scenario feasibility dump

Required output:

```text
docs/mvp/reports/mission1-scenario-feasibility.md
build/mission1-scenario-dump/scenario.json
build/mission1-scenario-dump/object_table.csv
build/mission1-scenario-dump/trigger_graph.json
build/mission1-scenario-dump/coverage.json
```

The dump must contain:

- source identifier/hash where legally appropriate;
- map width and height;
- terrain/elevation counts and checksums;
- player list;
- diplomacy matrix;
- placement count and histogram by source object type/player;
- trigger count;
- condition/effect type histogram;
- unsupported fields/types.

## P1.4 Add extraction tests

Add behavior/golden tests for:

- fixed source metadata or a legal synthetic fixture;
- map array dimensions;
- stable placement IDs;
- trigger count/type coverage;
- unknown type reporting;
- no silent data loss.

## P1 exit criteria

Either:

```text
MISSION_1_SCENARIO_IMPORTABLE candidate exists
```

or a documented, evidence-backed blocker exists.

Suggested commits:

```text
scenario: add Mission 1 source feasibility inspector
scenario: dump Mission 1 trigger and placement coverage
```

---

# P2 — Scenario IR and production loader

## P2.1 Define normalized model

Implement repository-consistent equivalents of:

- `ScenarioDefinition`;
- `MapDefinition`;
- `PlayerDefinition`;
- `EntityPlacement`;
- `AreaDefinition`;
- `TriggerDefinition`;
- condition/effect variants;
- `ScenarioObjectId`.

Add explicit schema/version information to serialized mission data.

## P2.2 Validate IR

Validation must reject or report:

- width/height mismatch;
- incorrect dense-array size;
- duplicate scenario object IDs;
- invalid owners;
- missing entity definition references;
- trigger references to missing objects/areas/triggers;
- unsupported condition/effect variants;
- cyclic activation that violates explicit policy.

## P2.3 Implement mission-pack writer

Write source-independent mission output. Use compact dense storage for terrain/elevation and repository-native structured data for relationships.

The writer must be deterministic for the same normalized input, aside from explicitly ignored metadata such as generation timestamp.

## P2.4 Implement Scenario Loader

Load in this order:

```text
terrain
-> players
-> diplomacy
-> placements
-> scenario-object mapping
-> areas/watchers
-> triggers
-> initial events/objectives
```

Expose the object mapping to trigger resolution without exposing source parser details.

## P2.5 Terrain snapshot path

Complete or add a batch terrain API that creates chunks from terrain/elevation snapshots. Ensure:

- all tiles are populated;
- chunk boundaries are correct;
- pathfinding and renderer consume compatible terrain state;
- no per-tile event/curve object explosion.

The first P2 pass may use testscene terrain only when real terrain export is not ready, but it must load map dimensions and placements from Scenario IR.

## P2.6 Integration tests

Add tests for:

- IR validation failures;
- mission pack round-trip/load;
- map dimensions/checksum;
- player/diplomacy creation;
- placement count and stable `ScenarioObjectId -> GameEntityId` mapping;
- invalid FQON failure;
- unknown trigger type failure/reporting.

## P2 exit criteria

- A ScenarioDefinition loads through the production game-state path.
- Headless startup has map, players, diplomacy, and placed entities.
- No source-format parser runs inside simulation.
- Existing reconstruction can be represented by the same IR/loader.

Suggested commits:

```text
scenario: define normalized scenario IR
scenario: serialize Mission 1 mission pack
scenario: load terrain players and placements
scenario: map source object IDs to runtime entities
```

---

# P3 — Gameplay lifecycle cleanup

Use the existing minimal combat scenario as the protected fixture while migrating architecture.

## P3.1 Characterize current lifecycle

Before changing behavior, add tests that record:

- attack command dispatch;
- approach behavior;
- cooldown timing;
- current HP mutation;
- current death/removal timing;
- stale scheduled attack behavior;
- objective completion timing.

Do not encode known incorrect implementation details as permanent requirements; mark characterization assertions that will intentionally change.

## P3.2 Route damage through effects

Target flow:

```text
AttackEvent -> ApplyDiscreteEffect -> Health state/curve
```

Requirements:

- attack scheduling remains separate from effect application;
- damage effect contains attacker/target/effect context;
- one hit applies exactly once;
- cooldown-before-hit and same-timestamp behavior are tested;
- attack code no longer directly owns generic HP mutation after migration.

## P3.3 Emit generic death

Implement a reusable health-depletion/death mechanism:

- any supported damage source can cause death;
- death occurs once;
- death state is observable;
- attack, movement, and other active commands are cancelled or invalidated safely;
- all attackers targeting the dead entity are made safe.

## P3.4 Defer removal

Implement deferred entity removal:

- no unsafe container mutation during event handler iteration;
- indexes are removed once;
- selection, collision, pathfinding occupancy, command targets, and renderer-facing state are cleaned consistently;
- stale events check entity existence/liveness and become no-ops;
- double removal is impossible or harmless and diagnosed.

## P3.5 Mandatory tests after every increment

Run:

- event-loop fractional/batch tests;
- existing simulation demos;
- Mission 1 combat regression;
- new focused lifecycle test;
- debug build at memory-safe parallelism.

## P3 exit criteria

The following is demonstrated by behavior tests:

```text
command
-> approach
-> attack event
-> discrete effect
-> health depletion
-> one death
-> deferred removal
```

Suggested commits:

```text
test: characterize Mission 1 combat lifecycle
gameplay: route attack damage through discrete effects
gameplay: emit death from health depletion
gameplay: defer dead entity removal
```

---

# P4 — Trigger runtime and event-driven objectives

## P4.1 Build trigger coverage from P1

Use `coverage.json` as the only implementation list. Do not guess or prebuild the full AoE2 trigger language.

Rank trigger types by:

1. required for Mission 1 progression;
2. required for Mission 1 victory;
3. frequency;
4. dependency on unfinished systems.

## P4.2 Implement TriggerInstance

Each trigger must own or reference:

- trigger ID;
- enabled state;
- fire count;
- looping/one-shot state;
- delay;
- condition set;
- ordered effects;
- dependency registrations.

## P4.3 Implement condition dependencies

Examples:

```text
Health/death/removal -> object-destroyed conditions
Position/area membership -> entity-in-area conditions
Ownership -> ownership/count conditions
Mission clock -> timer conditions
Trigger state -> trigger-fired/enabled conditions
Input semantic signal -> selection tutorial condition, if required
```

Avoid all-trigger-per-tick scans.

## P4.4 Implement ordered effects

Schedule effects individually with a stable semantic order based on trigger/effect IDs. Effects must use standard command/effect/lifecycle paths rather than directly mutating unrelated systems.

## P4.5 Replace objective polling

Replace production calls such as `tick_objective_controller()` with lifecycle or trigger events.

Tests must prove:

- enemy target removal can complete the objective;
- player-unit removal does not complete the enemy objective;
- unrelated entity removal does not complete the objective;
- all targets are required when multiple targets are configured;
- objective completion fires once;
- victory is scheduled/observed;
- the polling API/counter is absent from the production path.

## P4.6 Trigger tests

For every implemented Mission 1 type:

- false condition does not fire;
- transition to true reevaluates correctly;
- delay is respected;
- one-shot does not repeat;
- looping repeats only under defined semantics;
- enable/disable works;
- ordered effects are stable;
- unsupported variants fail/report clearly.

## P4 exit criteria

- Mission progression and victory use trigger/event semantics.
- No production objective polling remains.
- Trigger runtime uses dependency-driven reevaluation.
- Existing combat and event-loop regressions remain green.

Suggested commits:

```text
scenario: add dependency-driven trigger instances
scenario: implement Mission 1 trigger conditions
scenario: execute ordered Mission 1 effects
scenario: complete objectives from lifecycle events
```

---

# P5 — Mission 1 terrain and media closure

This phase is intentionally narrow.

## P5.1 Audit current asset provenance

Create:

```text
docs/mvp/reports/mission1-asset-provenance.md
```

Trace the current mission FQON to final files for:

- Villager animations;
- enemy animations;
- Town Center;
- selection indicator;
- terrain nyan/texture/image;
- attack/death resources where used;
- instruction/victory UI resources;
- sound resources where enabled.

Classify each as:

```text
fresh converted
stale converted
testscene fallback
missing
not required
```

## P5.2 Mission media request closure

Use the existing media request filtering mechanism to export only the Mission 1 transitive media closure.

Requirements:

- preserve required metadata dependencies;
- no eager full-media conversion;
- fresh output manifest;
- missing referenced media is an error;
- report requested/exported/skipped counts;
- remain under the converter memory gate.

## P5.3 Real DE2 terrain

Implement the minimum terrain media path required by Mission 1:

- identify the actual source terrain IDs/textures used;
- support required DDS decode/export if missing;
- generate the relevant `.terrain/.texture/image` closure;
- replace the production mission's testscene terrain reference;
- retain testscene terrain only in tests.

Do not implement every DE2 terrain.

## P5.4 Asset closure tests

Validate:

- every mission-critical media reference resolves;
- files are nonempty and decodable;
- metadata and images come from the same conversion manifest;
- production FQONs do not resolve to Gaben/tank/grid/testscene-only assets;
- renderer-facing terrain dimensions/metadata are consistent.

## P5 exit criteria

- The production Mission 1 path uses real converted DE2 terrain.
- Required entity media closes from a fresh lazy conversion.
- Asset provenance report contains no unexplained stale/mixed source.

Suggested commits:

```text
convert: complete William Wallace media closure
convert: export required DE2 DDS terrain
scenario: use converted DE2 terrain in Mission 1
test: reject testscene assets in Mission 1 production path
```

---

# P6 — Fresh headless semantic acceptance

## P6.1 Build a fresh asset root

Execute the repository-resolved equivalent of:

```text
create empty asset root
-> install engine data
-> lazy-convert de2_base with Mission 1 media filter
-> install/build ww_mvp mission pack
-> validate nyan/media/terrain closure
```

Record:

- exact commands;
- converter backend proof;
- source/output paths;
- manifest IDs;
- wall time;
- peak RSS;
- loader time.

Do not reuse old output.

## P6.2 Run headless semantic scenarios

At minimum:

### Movement/progression test

- load scenario;
- issue or trigger required movement;
- next-event-jump until destination/area condition;
- verify expected trigger/objective transition.

### Combat/victory test

- issue AttackCommand;
- approach automatically;
- next-event-jump;
- verify ApplyDiscreteEffect;
- verify Health update;
- verify generic Death;
- verify deferred removal;
- verify event-driven objective;
- verify Victory;
- verify no polling;
- verify no stale future attack effect;
- verify no double removal.

## P6.3 Failure budgets

The driver must stop with diagnostic failure when:

- no next event exists before completion;
- simulation deadline is exceeded;
- event-batch budget is exceeded;
- required entity/trigger disappears unexpectedly;
- unsupported trigger/effect is reached.

Print:

- simulation time;
- next event time;
- event batch count;
- pending event estimate if available;
- relevant entity health/liveness;
- trigger/objective states;
- last executed semantic events if tracing exists.

## P6.4 Repetition and sanitizers

Run:

- Debug x20;
- normal x50;
- ASan x5;
- UBSan x5.

Store logs or a summarized matrix in:

```text
docs/mvp/reports/mission1-headless-acceptance.md
```

## P6.5 Declare status accurately

Only when all specification requirements pass, record:

```text
MISSION_1_HEADLESS_SEMANTIC: PASS
```

Otherwise use `FAIL` or `BLOCKED` with evidence.

Suggested commits:

```text
test: validate Mission 1 headless semantic completion
docs: record Mission 1 headless acceptance
```

---

# P7 — Windowed integration and manual acceptance

## P7.1 Create canonical scripts

Implement or harden:

```text
scripts/run_ww_mission1.sh
scripts/verify_ww_mission1.sh
```

Both must share the same scenario FQON, player setup FQON, asset root, and modpack selection.

## P7.2 Graphics environment probe

Record:

```text
DISPLAY
WAYLAND_DISPLAY
Qt platform
OpenGL vendor
OpenGL renderer
OpenGL core version
```

If the required real graphics context is unavailable, mark windowed acceptance `ENVIRONMENT_BLOCKED` and do not weaken requirements.

## P7.3 Automated windowed smoke

The smoke must verify through logs/instrumentation:

- real window created;
- renderer initialized;
- render loop submitted multiple frames;
- production scenario loaded;
- expected initial instances spawned;
- no nyan/runtime exception;
- no crash/core dump;
- application can shut down cleanly.

A timeout can show continued process life but cannot prove the scene looks correct.

## P7.4 Visual evidence

Capture a screenshot where the environment permits, showing:

- real Mission 1 terrain;
- Player Villager;
- Town Center;
- relevant enemy/mission target;
- no Gaben/tank/grid fixture.

When automated capture is impossible, keep the real window open and request only the final user visual confirmation. Do not claim the visual gate passed without evidence.

## P7.5 Manual checklist

The user or a real interactive test must confirm:

- unit is visible;
- clicking selects the unit;
- selection feedback is visible;
- right-click ground moves the unit;
- unit movement and orientation are reasonable;
- right-click enemy issues attack;
- approach and repeated attacks occur;
- enemy visibly dies/is removed;
- objective/victory UI is visible;
- restart or relaunch works;
- clean shutdown works.

## P7.6 Final report

Create:

```text
docs/mvp/reports/mission1-windowed-acceptance.md
```

Declare one:

```text
MISSION_1_WINDOWED_PLAYABLE: PASS
MISSION_1_WINDOWED_PLAYABLE: FAIL
MISSION_1_WINDOWED_PLAYABLE: ENVIRONMENT_BLOCKED
```

Suggested commits:

```text
script: add reproducible Mission 1 launch and verification
test: add Mission 1 windowed runtime smoke
docs: record Mission 1 windowed acceptance
```

---

## 4. Mandatory regression matrix

Run after any change in the corresponding column.

| Change area | Focused tests | Mandatory regressions |
|---|---|---|
| converter/parser | parser behavior/golden | lazy memory gate, nyan load, asset closure |
| Scenario IR/writer | schema/round-trip | mission pack load, placement mapping |
| terrain loader | chunk/tile/elevation | headless scenario load, pathfinding smoke |
| EventLoop | event unit tests | fractional batch demo, all Mission 1 headless tests |
| combat/effect | effect and cooldown | existing demos, combat/victory regression |
| death/removal | lifecycle unit tests | stale event, double removal, sanitizer runs |
| triggers/objectives | condition/effect tests | movement progression and combat victory |
| media/terrain | decode/reference tests | fresh asset closure, real loader |
| presenter/renderer | renderer smoke | headless suite must remain independent |
| scripts/CLI | shell tests/import smoke | fresh-root verify script |

---

## 5. Status tracking template

Maintain a concise status table in the active report:

| Requirement | Status | Evidence | Commit | Next action |
|---|---|---|---|---|
| Baseline reproduced | NOT RUN | — | — | P0 |
| Scenario source located | NOT RUN | — | — | P1 |
| Scenario IR dump | NOT RUN | — | — | P1 |
| Production Scenario Loader | NOT RUN | — | — | P2 |
| Damage through effect | NOT RUN | — | — | P3 |
| Generic death | NOT RUN | — | — | P3 |
| Deferred removal | NOT RUN | — | — | P3 |
| Event-driven objective | NOT RUN | — | — | P4 |
| Real terrain closure | NOT RUN | — | — | P5 |
| Fresh headless acceptance | NOT RUN | — | — | P6 |
| Windowed playable | NOT RUN | — | — | P7 |

Allowed statuses:

```text
NOT RUN
IN PROGRESS
PASS
FAIL
BLOCKED
DEFERRED BY SPEC
```

Do not use “done” without linked evidence.

---

## 6. Scope-control questions before each proposed task

Before implementing a task, answer internally:

1. Which specification requirement does this satisfy?
2. What failing test or missing evidence proves it is needed?
3. Is this the earliest incorrect layer?
4. Does it expand converter or engine scope beyond Mission 1?
5. Can the change be verified headlessly?
6. What mandatory regressions must run?
7. What is the atomic commit boundary?

If no specification requirement is served, do not perform the task.

---

## 7. Prohibited shortcuts

- Do not label the reconstruction as an imported original mission.
- Do not make validator output clean by generating meaningless placeholder objects.
- Do not hand-edit converted output as the source fix.
- Do not use stale assets to make startup pass.
- Do not replace real terrain with a renamed test texture.
- Do not add polling to avoid dependency/event work.
- Do not directly remove entities from an actively iterated container.
- Do not treat pointer-address order as deterministic game semantics.
- Do not use sleeps to advance headless simulation.
- Do not disable the renderer and call the result windowed.
- Do not weaken assertions because the current implementation fails them.
- Do not start Mission 2–5.

---

## 8. Final handoff format

At the end of each Claude Code session, output:

1. **Current phase and exact status.**
2. **Commits created**, one line each.
3. **Files changed**, grouped by converter/scenario/gameplay/test/script/docs.
4. **Commands run and exit codes.**
5. **Tests passed/failed/not run.**
6. **First remaining failure**, with log path.
7. **Memory and timing measurements**, where applicable.
8. **Generated artifacts and report paths.**
9. **Claims explicitly not proven.**
10. **The next single executable increment.**

The final handoff must not replace missing execution with a future plan.
