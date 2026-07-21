# openage RTS Gameplay Foundation - Design Spec

**Date**: 2026-07-22
**Status**: Draft (Phase 1 - vertical slice architecture)
**Supersedes/extends**: `~/.claude/plans/peaceful-chasing-mango.md` Phase A-C architecture assumptions
**Scope**: A reusable, deterministic, AoE2:DE-style RTS gameplay runtime foundation built on openage's existing `activity/`, event, GameEntity, component and system code. Not a full ECS rewrite.

**Position in the roadmap**: This is spec 1 of 3 for the William Wallace campaign:
1. **(THIS SPEC)** RTS gameplay foundation - the shared infrastructure (tick SimLoop, registries, typed events, command pipeline, pathfinding/locomotion, combat/damage/death, gather/production, AI, scenario runtime, replay/checksum). Vertical slice = Mission 1 playable end-to-end.
2. Mission 1 scenario spec - the concrete scenario wiring + Mission 1 trigger (built on this foundation).
3. Missions 2-5 roadmap - per-mission incremental mechanics (each adds one system/ability; no core rewrite).

Missions 2-5 are NOT detailed here; section 4.6 lists them as extension points.

This spec grounds every design decision in an audit of the current codebase (5 parallel audits: activity framework, event system, pathfinding/move, components/systems, command/determinism). Each section cites what is **reused** vs **new**.

---

## 0. Architecture locking summary (confirmed decisions)

1. **SimLoop replaces EventLoop as gameplay main driver.** EventLoop is demoted to a time-scheduling primitive + curve dependency propagation, non-authoritative for gameplay.
2. **`tick_id_t` (uint64) is the sole authoritative gameplay time**, 50 ms / 20 Hz (`TICK_DURATION = 50'000'000 ns`). No wall-clock or floating sim time drives gameplay.
3. **Activity only orchestrates; continuous gameplay is advanced by phase systems.**
4. **Commands execute at T+1** (issuer observes at T, `execute_tick = T+1`).
5. **Domain events are sorted and dispatched in one phase** (Phase 9); Activities wake on the **next tick**.
6. **Both growing central switches are deleted** (`send_command.cpp:67` by command_t, `activity.cpp:127-143` by system_id_t).
7. **Entity iteration, RNG, fixed-point, command log, replay and checksum** are all part of the foundation refactor.

---

## 1. Layered architecture and dependency graph

### 1.1 Dependency layers (top -> bottom, acyclic, single direction)

```
ISSUERS (three command sources, same API for unit behavior)
  Player Input │ Scripted AI │ Scenario Runtime
        │              │
        │              │ unit behavior: Command Pipeline
        │              │ non-unit behavior (SpawnEntity, ChangeDiplomacy,
        │              │   RevealArea, DeclareVictory, ObjectiveState):
        │              │   ScenarioEffectPipeline (deterministic, separate)
        ▼              ▼
COMMAND PIPELINE / SCENARIO EFFECT PIPELINE
  validation -> param canonicalization -> queue-mode (Replace/Append/
  Prepend/Conditional) -> interrupt-priority (Background/Normal/Forced/
  Uninterruptible) -> CommandLog append -> per-entity queue entry
  [Command carries issuer + issued_tick + execute_tick + issuer_sequence]
        │
        ▼
DETERMINISTIC SIMULATION LOOP  (principle #12)
  fixed-timestep tick, 13 frozen phases (see 1.3)
  wall-clock only decides how many sim ticks a render frame runs;
  render interpolates between snapshots, never writes back gameplay state
        │
        ├──────────────────┐
        ▼                  ▼
GAMEPLAY PHASE SYSTEMS     ACTIVITY GRAPH (control flow, principle #1,#2)
  (PhaseSystemScheduler      nodes: Sequence/XorGate/Condition/RunSystem/
   registers; run per        WaitEvent/WaitUntil/Loop/Transition/Interrupt/End
   TickPhase; continuous)    ActivityRuntimeState: root_graph + frame stack
  PathfindingSystem           + params + wait_state + generation
  LocomotionSystem                  │ RunSystem node -> ActivityTaskRegistry
  MovementResolutionSystem          │   -> ActivityTaskResult (variant)
  ProjectileSystem                 ▼   decides branch
  DamageSystem / DeathSystem
  ProductionSystem
  DeferredEntityCommitSystem
        │ read/write (field-level writer matrix, see 1.5)
        ▼
COMPONENTS (authoritative passive state + curves, principle #3)
  Position(curve) │ Live(curve HP) │ CommandQueue(curve) │
  Activity(curve runtime) │ ResourceStorage │ ProductionQueue │ Ownership
  NO embedded rule logic; NO on_attribute_change callback table (removed)
        │ curve changes(at) dependency propagation (legacy primitive, non-authoritative)
        ▼
LEGACY EVENTLOOP (demoted)
  curve compatibility │ legacy callbacks │ non-authoritative UI observer
  + AUTHORITATIVE TypedEventQueue (independent of Legacy EventLoop, see 2.6)
```

### 1.2 Dependency boundary rules

- **Scenario Runtime boundary is four-way**:
  - Unit behavior (move/attack/gather/build/train commands): **Command Pipeline** (same as Player and AI).
  - Non-unit behavior (SpawnEntity, ChangeDiplomacy, RevealArea, DeclareVictory, ObjectiveState): **ScenarioEffectPipeline** - independent, deterministic, but still flows through validation -> typed effect -> deferred commit -> replay/checksum. **ScenarioEffect never mutates Components directly.**
- **AI only uses the Command Pipeline.** AI never knows Activity internal nodes and never bypasses resource/pop/cooldown/path validation.
- **Activity does not compute rules.** A RunSystem node only calls an ActivityTaskRegistry handler and branches on ActivityTaskResult.
- **Components contain no gameplay mutation rules.** All authoritative field writes go through a declared owner system or deterministic mutation pipeline (field-level writer matrix, 1.5).
- **EventLoop no longer drives gameplay main advance.** SimLoop is the main driver.

### 1.3 Frozen tick phases (13 phases, central definition, NOT registry order)

```
run_tick(T):
  Phase 0  BeginTick                 (NOT current_tick++ here)
  Phase 1  CommandIngest              drain CommandIngressQueue, canonical sort
  Phase 2  CommandValidationAndArbitration  validation + queue-mode + interrupt-priority
  Phase 3  ActivityEvaluation         advance graphs (microstep<=32, control-only;
                                        wait nodes do NOT wake here)
  Phase 4  Pathfinding                request_id round-robin, fixed expansion quantum
  Phase 5  LocomotionAndSpatial       LocomotionSystem per-tick advance +
                                        MovementResolutionSystem result
  Phase 6  CombatAndProjectile        attack interval / projectile advance
  Phase 7  DamageAndDeath             DamageSystem armor calc -> Live;
                                        HP<=0 -> DeathSystem (Dying/Dead)
  Phase 8  GatherBuildProduction       gather/build/train progress; reserve entity ids
  Phase 9  DomainEventDispatch        6-tuple sort, dispatch; mark Activity wait satisfied
                                        (wake next tick T+1)
  Phase 10 ScenarioAndAIObservation   AI/Scenario observe domain events, issue commands
                                        (execute_tick = T+1)
  Phase 11 DeferredEntityCommit       spawn/despawn committed uniformly
  Phase 12 ChecksumAndReplayRecord    state checksum + append command/replay log
  Phase 13 EndTick                     current_tick = T + 1
```

`current_tick = T+1` happens **after EndTick**, never `++` in BeginTick, to avoid off-by-one for tick 0, first command, and checksum.

### 1.4 Authority invariants

- Command uses `execute_tick`. Event uses `scheduled_tick`. Component timeline writes authoritative state only at tick boundaries. Gameplay timers use integer ticks.
- wall-clock only decides how many sim ticks a render frame executes. Simulation lag does NOT skip ticks; it delays display or caps per-frame catch-up.
- Render may interpolate between two sim snapshots; interpolated results never write back gameplay state.

### 1.5 Field-level component writer matrix

"Systems are the sole writer of Components" is too coarse - Position is legitimately written by spawn, teleport, knockback, scenario placement, save/load restore, deferred commit. The correct constraint:

> Components contain no gameplay mutation rules; all authoritative field writes go through a declared owner system or deterministic mutation pipeline. Mutations first enter a `WorldMutationBuffer` and commit at a fixed phase position. Arbitrary systems must not obtain unrestricted mutable component pointers.

| Component / field      | Allowed writers                                                  |
| ---------------------- | --------------------------------------------------------------- |
| Position.current       | LocomotionSystem, SpawnCommitSystem, TeleportSystem             |
| Live.hp                | DamageSystem, HealSystem, SpawnCommitSystem                      |
| Live.life_state        | DeathSystem, SpawnCommitSystem                                  |
| PlayerResources        | EconomySystem, Spawn/Scenario initialization                    |
| ProductionQueue        | ProductionSystem, CommandArbitrationSystem                     |
| ActivityRuntime        | ActivitySystem, CommandArbitrationSystem, DeathSystem          |
| CommandQueue           | CommandPipeline (ingest/arbitration), ScenarioEffect (effects)  |

---

## 2. Core interface definitions (revised per 11 corrections)

### 2.1 SimulationClock - no tick/legacy-time mixing

**Strong id types (defined once here, used throughout)** - all are distinct `StrongId` aliases to prevent accidental mixing:
```cpp
using tick_id_t        = uint64_t;                                    // authoritative time
using SystemId          = StrongId<struct SystemTag,          uint32_t>;
using ActivityTaskId    = StrongId<struct ActivityTaskTag,    uint32_t>;
using EventTypeId       = StrongId<struct EventTypeTag,      uint32_t>;
using EventId           = StrongId<struct EventIdTag,         uint64_t>; // assigned post-sort
using CommandId         = StrongId<struct CommandTag,         uint64_t>;
using CorrelationId     = StrongId<struct CorrelationTag,   uint64_t>; // request_id/attack_id/production_id
using request_id_t      = StrongId<struct PathRequestTag,    uint64_t>;
using entity_id_t       = StrongId<struct EntityTag,         uint64_t>;
using activity_id_t     = StrongId<struct ActivityTag,       uint32_t>;
using node_id_t         = StrongId<struct NodeTag,            uint32_t>;
using player_id_t       = StrongId<struct PlayerTag,         uint32_t>;
```
Two distinct generation fields, NOT to be conflated:
- `activity_generation` (ActivityRuntimeState) - bumped when an Activity is cancelled/replaced; `EventWaitSpec.activity_generation` must match for an event to wake that Activity (prevents a stale event waking a new Activity).
- `movement_generation` (PathRequest) - bumped on new Move/Repath; `PathReady` carries it and LocomotionSystem validates it (prevents a stale path overwriting a newer Move). Independent of activity_generation.

```cpp
// libopenage/gamestate/sim/simulation_clock.h
using tick_id_t = uint64_t;

struct SimulationClock {
    static constexpr std::chrono::nanoseconds TICK_DURATION{50'000'000}; // 50ms, 20Hz

    tick_id_t current_tick() const;
private:
    tick_id_t current = 0;
};
// tick/time conversion is ONLY in the Legacy EventLoop Adapter, with explicit
// unit-checked conversion. Authoritative gameplay systems MUST NOT use
// wall-clock or legacy floating simulation time.
```

### 2.2 Two separate registries (NOT one)

#### PhaseSystemScheduler - per-tick fixed continuous systems

```cpp
struct PhaseSystemDescriptor {
    SystemId id;
    TickPhase phase;
    uint32_t stable_order;           // explicit order WITHIN a phase
    std::vector<SystemId> runs_after; // optional dependency, topo-resolved at freeze
};

class PhaseSystemScheduler {
public:
    void register_system(PhaseSystemDescriptor, std::unique_ptr<IPhaseSystem>);
    void freeze();                    // topo-sort by runs_after; order fixed after this
    void run_phase(TickPhase phase, SimulationContext &ctx);
};
```
Includes: PathfindingSystem, LocomotionSystem, MovementResolutionSystem, ProjectileSystem, DamageSystem, DeathSystem, ProductionSystem, DeferredEntityCommitSystem. Within a phase, TickPhase alone does not determine order -> explicit `stable_order` or frozen topo order. **Registration call order is never relied upon.**

#### ActivityTaskRegistry - Activity RunSystem node handlers

```cpp
using ActivityTaskId = StrongId<struct ActivityTaskTag, uint32_t>;

class ActivityTaskRegistry {
public:
    void register_handler(ActivityTaskId, ActivityTaskHandler);
    ActivityTaskResult execute(ActivityTaskId, entity_id_t,
                                const ActivityTaskParams&, ActivityExecutionContext&) const;
    void freeze();
};
```
Includes: ValidateTarget, SubmitPathRequest, StartMovementIntent, SubmitAttackIntent, StartGatherIntent. This replaces `activity.cpp::handle_subsystem`. It is NOT merged with `PhaseSystemScheduler`.

### 2.3 ActivityTaskResult - strong typed variant, no strings / 0-sentinel

Phase systems do NOT return ActivityTaskResult (they are continuous). Only Activity RunSystem tasks do.

```cpp
enum class ActivityTaskFailure : uint16_t {
    InvalidTarget, TargetUnavailable, PathUnavailable,
    InsufficientResources, PopulationLimit, ActivityCancelled, UnsupportedOperation
};
enum class ActivityInterruptReason : uint16_t {
    ReplacedByCommand, ForcedCommand, EntityDied, TargetInvalidated, ScenarioOverride
};

struct TaskCompleted {};
struct TaskWaitUntil { tick_id_t resume_tick; };
struct TaskWaitForEvent { EventWaitSpec wait_spec; };
struct TaskFailed { ActivityTaskFailure reason; };
struct TaskInterrupted { ActivityInterruptReason reason; };

using ActivityTaskResult = std::variant<
    TaskCompleted, TaskWaitUntil, TaskWaitForEvent, TaskFailed, TaskInterrupted>;

// Waiting for an event stores a MATCH CONDITION, not an ungenerated EventId:
struct EventWaitSpec {
    EventTypeId type;
    std::optional<entity_id_t> source;
    std::optional<entity_id_t> target;
    CorrelationId correlation;       // maps to request_id / attack_id / production_id
    uint32_t activity_generation;    // prevents stale events waking a replaced Activity
};
```

### 2.4 Command - variant with header, not virtual base

```cpp
struct CommandHeader {
    CommandId id;                    // canonical id (assigned after canonicalization)
    player_id_t issuer;
    tick_id_t issued_tick;          // T (issuer observed)
    tick_id_t execute_tick;         // T+1 (default)
    uint64_t issuer_sequence;        // per-issuer deterministic increment
    QueueMode queue_mode;            // Replace/Append/Prepend/Conditional
    InterruptPriority priority;      // Background/Normal/Forced/Uninterruptible
};

struct MoveCommand    { std::vector<entity_id_t> subjects; scene_pos_t destination; };
struct AttackCommand  { std::vector<entity_id_t> subjects; entity_id_t target; };
struct GatherCommand  { std::vector<entity_id_t> subjects; entity_id_t resource; };
struct BuildCommand   { std::vector<entity_id_t> subjects; nyan::fqon_t game_entity_fqon; scene_pos_t target; };
struct TrainCommand   { entity_id_t producer; nyan::fqon_t creatable_fqon; };
struct StopCommand    { std::vector<entity_id_t> subjects; };
struct IdleCommand    { std::vector<entity_id_t> subjects; };

using CommandPayload = std::variant<
    StopCommand, IdleCommand, MoveCommand, AttackCommand,
    GatherCommand, BuildCommand, TrainCommand>;

struct Command { CommandHeader header; CommandPayload payload; };
```

**Subjects canonicalization (locked, first version)**: sort -> dedupe -> drop invalid ids at validation -> **per-entity single-subject command entries generated in entity-id order**. (Shared command-group alternative explicitly deferred.) Validation processes per entity in id order.

### 2.5 CommandIngressQueue vs CommandLog (separated)

Live simulation does NOT drain the command log. The Command Log is an append-only replay artifact, not a runtime queue.

```
Player / AI / Scenario
  -> CommandPipeline::issue()
  -> CommandIngressQueue
  -> Phase 1 canonical sort  (execute_tick, issuer, issuer_sequence, command_id)
  -> Phase 2 validation/arbitration
  -> Accepted/Rejected result
  -> append CommandLog + CommandResultLog
```

Replay mode:
```
ReplayCommandSource -> reads CommandLog -> fills CommandIngressQueue
```

Recorded per entry: canonicalized incoming command, validation result, rejection reason, resulting per-entity queue operation. Replay reproduces rejected commands too (divergence audit).

**Sort key** (must not depend on thread arrival or global atomic increment):
```
(execute_tick, issuer, issuer_sequence, command_id)
```
`issuer_sequence` is per-issuer deterministic increment. Global `command_seq` is **only a log index**, never the sole sim sort key.

### 2.6 TypedEventQueue - independent of Legacy EventLoop

"TypedEvent wraps the existing EventLoop" is rejected - too risky. Legacy EventLoop uses strings, `std::any`, legacy time, heap insertion semantics; it cannot be the authoritative truth.

```
Authoritative TypedEventQueue
  |- DomainEventDispatch uses it
  |- Activity WaitEvent uses it
  |- Replay/checksum use it
  +- one-way bridge to Legacy EventLoop / UI observer

Legacy EventLoop
  |- curve compatibility
  +- legacy callbacks (non-authoritative observer)
```
Authoritative events MUST NOT enter Legacy EventLoop first and return.

```cpp
using EventPayload = std::variant<
    PathReady, PathFailed, MovementArrived, MovementBlocked,
    DamageApplied, EntityDied, ResourceDeposited, UnitProduced /* ... */>;

struct EventEnvelope {
    EventHeader header;     // includes origin_phase, producer_system_stable_order,
                            // source_entity_id, event_type_id, producer_local_sequence
    EventPayload payload;   // variant, no subclass inheritance
};
```
`EventId` is a distinct strong id type; global increment alone does not make it "strong". EventId is assigned after canonical sort, or excluded from sim ordering.

### 2.7 EventOriginPhase + strict total order

All events dispatch in Phase 9. So `EventPhase` is misnamed; rename to the **producing** phase:

```cpp
enum class EventOriginPhase : uint8_t {
    Pathfinding, LocomotionAndSpatial, CombatAndProjectile,
    DamageAndDeath, GatherBuildProduction, DeferredEntityCommit
};
```

Strict total order key (the 5-tuple could collide if two systems produce same source+type with local_sequence=0; use the 6-tuple):
```
( scheduled_tick,
  origin_phase,
  producer_system_stable_order,
  source_entity_id,
  event_type_id,
  producer_local_sequence )
```
`producer_local_sequence` allocated by the centralized phase event buffer in deterministic producer order.

### 2.8 ActivityRuntimeState - frame stack (not single current_node)

A single `current_node` cannot hold Sequence/Loop/Transition/Interrupt/sub-Activity state. Explicit runtime frame stack:

```cpp
struct ActivityFrame {
    activity_id_t graph_id;
    node_id_t node_id;
    uint32_t child_index;
    uint32_t loop_iteration;
};

struct EventWaitState {
    EventWaitSpec spec;
    // bookkeeping for match evaluation
};

struct ActivityRuntimeState {
    activity_id_t root_graph;
    std::vector<ActivityFrame> frames;       // stack
    ActivityParamBlock params;              // schema-defined typed data, NO std::any
    std::optional<EventWaitState> wait_state;
    uint32_t generation;                     // bump on cancel/replacement -> stale events fail
};
```

Requirements:
- graph definition immutable;
- graph/schema has stable version or hash;
- replay header includes activity graph data hash;
- ParamBlock is schema-defined typed data (no `std::any`);
- cancel/replacement bumps `generation`, invalidating stale events.
- `microsteps_used` lives in `ActivityExecutionContext`, reset per tick, NOT long-term component state (no mid-tick snapshot -> not in save/checksum).

### 2.9 PathRequest - MoveIntoRange + stale-result protection

```cpp
struct PathGoal {
    std::variant<scene_pos_t, entity_id_t> target;   // point or dynamic entity
    phys_t acceptance_radius;                         // for MoveIntoRange
};

struct PathRequest {
    request_id_t id;
    entity_id_t requester;
    uint32_t movement_generation;     // bump on new Move/Repath; Locomotion validates
    scene_pos_t start;
    PathGoal goal;
    MovementProfileId movement_profile;
    phys_t collision_radius;
    NavRevision nav_revision;        // invalidate path if terrain changed
    tick_id_t submit_tick;
};
```
PathReady/PathFailed carry: request_id, requester, movement_generation, nav_revision, path result. LocomotionSystem validates generation before accepting (prevents stale path overwriting a new Move/Repath).

**Deterministic round-robin (anti-starvation)**: strictly finishing one large request before the next starves. Use:
```
requests sorted by request_id
each active request gets fixed expansion quantum per tick
total expansion <= global budget
```
Must define: per-request quantum, global node budget, request admission limit, PathFailed/PartialPath conditions, and how the flow-field Pathfinder is refactored into a **pausable/resumable search state**. (Phase 4 key technical risk - see 4.x.)

---

## 3. End-to-end data flow (per vertical slice)

Tick-T perspective; every phase labeled. Activities wake the tick **after** their wait is satisfied (next-tick wake rule).

### 3.1 MoveTo + MoveIntoRange (principle #8 async pathing)

```
T   Phase 3  Activity: MoveTo graph
            - ValidateTarget (ActivityTask) -> Completed
            - SubmitPathRequest -> PathRequest{movement_generation=G, nav_revision=N}
            - WaitEvent(PathReady | PathFailed) -> sleeps (TaskWaitForEvent)
T   Phase 4  PathfindingSystem: round-robin by request_id, fixed expansion quantum
            - on complete: buffer PathReady{request_id, movement_generation=G, nav_revision=N, waypoints}
T   Phase 5  LocomotionSystem idle for this entity (path not yet accepted)
T   Phase 9  PathReady dispatched (6-tuple) -> Activity wait matched, wakes T+1
T+1 Phase 3  Activity resumes: StartMovementIntent -> registers movement intent
            - WaitEvent(MovementArrived | MovementBlocked | TargetMoved | MovementCancelled)
T+1 Phase 5  LocomotionSystem: advances every active intent one deterministic step
            - validates movement_generation == path generation (else drop, emit MovementCancelled)
            - MovementResolutionSystem: judges Arrived/Blocked/TargetMoved each tick
            - on arrive: buffer MovementArrived
T+1 Phase 9  MovementArrived dispatched -> Activity wakes T+2 -> Complete (or Repath on TargetMoved)
```

**MoveIntoRange**: `PathGoal.target = entity_id`, `acceptance_radius = attack/gather range`. LocomotionSystem each tick checks `dist(pos, target.pos) <= acceptance_radius`; if met, MovementArrived(in range) and Activity proceeds to Attack/Gather.

**Stale protection**: new Move/Repath bumps `movement_generation`; a stale PathReady carries the old generation and is dropped by LocomotionSystem, which emits MovementCancelled.

### 3.2 Attack -> Damage -> Death (principle #4 explicit damage)

```
T   Phase 3  AttackTarget Activity: ValidateTarget (alive? in range?)
            - out of range: MoveIntoRange(target, attack_range) sub-activity (3.1)
            - in range: SubmitAttackIntent -> register attack with CombatSystem
            - WaitEvent(AttackPointReached | TargetInvalidated)
T   Phase 6  CombatSystem (per-tick, each active attack intent):
            - cooldown timer (integer ticks); when ready: buffer AttackPointReached
            - melee: create DamageRequest{target, amount, armor_pen, source, correlation=attack_id}
            - ranged: create Projectile{target, ...}; ProjectileSystem advances on later ticks
T   Phase 7  DamageSystem: consume DamageRequest + projectile impacts
            - armor: final = max(0, amount - armor)  [fixed-point]
            - write Live.hp via WorldMutationBuffer (writer: DamageSystem)
            - buffer DamageApplied{source, target, amount, final}
            - if Live.hp <= 0: DeathSystem sets Live.life_state = Dying, buffer EntityDied, enqueue despawn
T   Phase 9  DamageApplied / EntityDied dispatched (6-tuple)
            - attacker Activity: TargetInvalidated (target died) -> Interrupt(TaskInterrupted{EntityDied})
            - Scenario observes EntityDied (Phase 10)
T   Phase 11 DeferredEntityCommit: despawn commit (entity leaves iteration set)
```

Satisfies #4: no `on_attribute_change` listener (Live callback table removed); DamageSystem explicitly computes armor and writes Live; HP<=0 handled by DeathSystem, not an attribute listener.

### 3.3 Gather -> Carry -> Deposit (principle #3 controlled writers)

```
T   Phase 3  GatherResource Activity: SubmitPathRequest to resource node (MoveIntoRange)
T+...        in range -> StartGatherIntent -> register with GatherSystem
            - WaitEvent(ResourceDepleted | CarryFull)
T   Phase 8  GatherSystem (per-tick): gather timer (ticks)
            - on interval: ResourceStorage.carried[fqon] += amount  (writer: EconomySystem)
              buffer ResourceGathered
            - node depleted: ResourceDepleted -> repath or fail
            - carried == capacity: CarryFull -> ReturnResource sub-activity (MoveIntoRange to TC)
in range at dropoff -> Deposit:
            - EconomySystem: PlayerResources += carried (writer: EconomySystem)
            - buffer ResourceDeposited; reset carried; loop back to gather
```

4 resources (Food/Wood/Stone/Gold) distinguished by nyan Resource FQON. `ResourceStorage` holds a curve-backed map (fixes the audit gap where `Gather.carried_amount` was a plain non-curve int64).

### 3.4 Production (principle #3 + #11 deferred commit)

```
T   Phase 3  Train Activity: SubmitTrainIntent -> register with ProductionSystem
            - WaitEvent(ProductionCompleted | InsufficientResources)
T   Phase 8  ProductionSystem (per-tick):
            - on enqueue: EconomySystem.has_enough pre-check (does NOT spend yet)
            - if enough: spend NOW (writer: EconomySystem), add to ProductionQueue timer
            - accumulate timer (ticks)
            - on done: reserve entity_id (Phase 8), enqueue SpawnRequest (NOT created yet)
              buffer UnitProduced{scheduled_tick = T+1, reserved_id}
T   Phase 9  UnitProduced dispatched next tick
T+1 Phase 11 DeferredEntityCommit: commit reserved entity (spawn)
T+1 Phase 9 (next) EntitySpawned dispatched -> Scenario/AI observers
```

Deferred visibility: ProductionSystem reserves id + SpawnRequest in Phase 8, entity created in Phase 11. A UnitProduced listener MUST NOT assume the entity exists - use the reserved_id handle. First version: dispatch after commit.

Population check before enqueue: `Player.pop_used + train.queue < Player.pop_cap`, else `TaskFailed{PopulationLimit}`.

### 3.5 Command T+1 + unified issuer (principle #7/#10)

```
T   Phase 10  Player input buffered; AI observes state at T -> issue (execute_tick=T+1);
              Scenario observes domain events at T -> issue/effects (execute_tick=T+1)
T+1 Phase 1   CommandIngest: drain CommandIngressQueue, canonical sort
              (execute_tick, issuer, issuer_sequence, command_id)
T+1 Phase 2   ValidationAndArbitration: per-entity id-order validation, queue-mode, interrupt-priority
              -> Activity transition (CommandArbitrationSystem bumps ActivityRuntime generation)
T+1 Phase 3   Activity advances with the new command's intent
```

Eliminates: AI-vs-player ordering (both T+1), Scenario recursive triggers, same-tick system reentry, issuer-order effects.

## 4. Migration file list, test gates, compatibility

### 4.1 New files (greenfield)

**Sim foundation (`libopenage/gamestate/sim/`)**
- `simulation_clock.{h,cpp}` - tick_id_t, SimulationClock (TICK_DURATION 50ms)
- `simulation_context.{h,cpp}` - per-tick context (current tick, budget, RNG, mutation buffer, microstep counter)
- `deterministic_simulation_loop.{h,cpp}` - the 13-phase tick (replaces wall-clock-driven `simulation.cpp:52-54`)
- `tick_phase.h` - TickPhase enum (13 values)
- `world_mutation_buffer.{h,cpp}` - staged component mutations committed at fixed phase positions
- `entity_id_allocator.{h,cpp}` - deterministic, ordered entity id allocation (replaces unordered_map iteration)

**Registries (`libopenage/gamestate/sim/`)**
- `phase_system_scheduler.{h,cpp}` - PhaseSystemDescriptor + IPhaseSystem + freeze/topo
- `activity_task_registry.{h,cpp}` - ActivityTaskId + ActivityTaskHandler + freeze
- `system_result.h` - ActivityTaskResult variant + TaskCompleted/TaskWaitUntil/TaskWaitForEvent/TaskFailed/TaskInterrupted + failure/interrupt enums + EventWaitSpec

**Typed events (`libopenage/gamestate/sim/event/`)**
- `typed_event_queue.{h,cpp}` - authoritative queue (independent of Legacy EventLoop)
- `event_envelope.{h,cpp}` - EventHeader + EventPayload variant
- `event_origin_phase.h` - EventOriginPhase enum
- `event_ordering.{h,cpp}` - 6-tuple strict total order
- `event_bridge.{h,cpp}` - one-way bridge to Legacy EventLoop / UI observer
- payloads: `path_ready.h`, `movement_events.h`, `damage_events.h`, `resource_events.h`, `production_events.h`, `spawn_events.h`

**Command pipeline (`libopenage/gamestate/command/`)**
- `command.{h,cpp}` - CommandHeader + CommandPayload variant (replaces virtual base)
- `command_pipeline.{h,cpp}` - issue() -> CommandIngressQueue
- `command_ingress_queue.{h,cpp}` - runtime queue (NOT the log)
- `command_log.{h,cpp}` - append-only replay artifact + CommandResultLog
- `command_canonicalizer.{h,cpp}` - subjects sort/dedupe/per-entity split
- `command_arbitration.{h,cpp}` - validation + queue-mode + interrupt-priority -> Activity transition
- `replay_command_source.{h,cpp}` - reads CommandLog into IngressQueue

**Scenario effect pipeline (`libopenage/gamestate/scenario/`)**
- `scenario_effect_pipeline.{h,cpp}` - SpawnEntity/ChangeDiplomacy/RevealArea/DeclareVictory/ObjectiveState effects; validation -> typed effect -> deferred commit -> replay/checksum
- `scenario_runtime.{h,cpp}` - trigger/condition/effect/objective engine; observes domain events; issues commands via CommandPipeline
- `scenario_trigger_types.h` - UnitEnteredArea/EntityDied/TimerExpired/ResourceReached/ObjectiveCompleted
- `scenario_effect_types.h` - SpawnEntity/IssueCommand/ChangeDiplomacy/ShowDialogue/EnableTechnology/DeclareVictory

**AI (`libopenage/gamestate/ai/`)**
- `scripted_ai.{h,cpp}` - reactive rules; observes domain events; issues via CommandPipeline

**Phase systems (`libopenage/gamestate/system/`)**
- `pathfinding.{h,cpp}` - PathfindingSystem (pausable/resumable search state, round-robin budget)
- `locomotion.{h,cpp}` - LocomotionSystem (per-tick intent advance, generation validation)
- `movement_resolution.{h,cpp}` - MovementResolutionSystem (Arrived/Blocked/TargetMoved/Cancelled)
- `combat.{h,cpp}` - CombatSystem (attack cooldown, DamageRequest/Projectile creation)
- `projectile.{h,cpp}` - ProjectileSystem (per-tick projectile advance + impact)
- `damage.{h,cpp}` - DamageSystem (armor calc, Live.hp write)
- `death.{h,cpp}` - DeathSystem (Dying/Dead, despawn enqueue)
- `gather.{h,cpp}` - GatherSystem
- `build.{h,cpp}` - BuildSystem
- `train.{h,cpp}` / `production.{h,cpp}` - TrainSystem/ProductionSystem
- `deferred_entity_commit.{h,cpp}` - DeferredEntityCommitSystem
- `checksum.{h,cpp}` - ChecksumSystem (state hash per tick)

**Pathfinding refactor (`libopenage/pathfinding/`)**
- `search_state.{h,cpp}` - pausable/resumable flow-field search state (extracted from synchronous `pathfinder.cpp`)

**Determinism (`libopenage/gamestate/sim/determinism/`)**
- `replay.{h,cpp}` - replay runner (CommandLog -> SimLoop)
- `state_checksum.{h,cpp}` - canonical state serialization + hash
- `deterministic_rng.{h,cpp}` - wires existing `rng::RNG` (xorshift128+, seedable/serializable, currently 0 instantiations) into sim

### 4.2 Modified files (existing)

- `libopenage/gamestate/simulation.{h,cpp}` - replace wall-clock `reach_time` loop with `DeterministicSimulationLoop::tick`; register phase systems; fix entity iteration to ordered
- `libopenage/gamestate/system/activity.{h,cpp}` - DELETE central `handle_subsystem` switch (`activity.cpp:123-143`); advance reads ActivityRuntimeState frame stack; microstep budget from ActivityExecutionContext; next-tick wake rule
- `libopenage/gamestate/event/send_command.cpp` - DELETE command_t switch (`:67`); route via CommandPipeline + canonicalizer
- `libopenage/gamestate/system/move.{h,cpp}` - REWRITE: remove synchronous path-bake into Position curve; Move is now an Activity (SubmitPathRequest -> Wait -> StartMovementIntent)
- `libopenage/gamestate/component/internal/activity.h` - extend to ActivityRuntimeState (frame stack, params, wait_state, generation)
- `libopenage/gamestate/component/api/live.{h,cpp}` - REMOVE `on_attribute_change` callback table + `change_callbacks` map (`live.h:85-105`); death now DeathSystem-driven
- `libopenage/gamestate/component/api/gather.{h,cpp}` - `carried_amount` plain int64 -> curve-backed ResourceStorage map
- `libopenage/gamestate/component/api/{build,train,production,resource_storage}.{h,cpp}` - fill in authoritative state (currently skeletons)
- `libopenage/gamestate/component/internal/command_queue.{h,cpp}` - integrate queue-mode + interrupt-priority + cancel-by-tag
- `libopenage/gamestate/player.{h,cpp}` - `resources` plain map -> curve-backed (replayable)
- `libopenage/gamestate/game_state.{h,cpp}` / `libopenage/gamestate/map.cpp` - `unordered_map<entity_id_t,...>` -> ordered container (fixes `map.cpp:24` non-determinism TODO)
- `libopenage/event/event_loop.{h,cpp}` - demote: keep as time-scheduling primitive + curve dependency propagation; remove gameplay-main-drive role; add LegacyEventLoopAdapter for explicit unit-checked tick/time conversion
- `libopenage/gamestate/activity/types.h` - extend closed `node_t` enum with Sequence/Loop/Transition/Interrupt (Condition folded into XorGate; WaitEvent/WaitUntil as node types)
- `libopenage/gamestate/entity_factory.cpp` - rewrite activity graph construction to data-driven node definitions (replace arbitrary-lambda conditions/tasks); register systems/tasks
- `libopenage/gamestate/api/definitions.h` - ACTIVITY_TASK_SYSTEM_DEFS map -> ActivityTaskRegistry registrations; conditions/primers already const_map registry (reuse pattern)
- `libopenage/clock/clock.{h,cpp}` - separate sim clock (tick-driven) from render clock (wall-clock); wall-clock decides tick count per frame only
- `libopenage/main.cpp` / `libopenage/main.h` - `--tutorial` already exists; add `--replay <log>` and `--checksum-every N`

### 4.3 Phase technical risks

**Phase 4 pathfinding (HIGHEST RISK)**: the flow-field `Pathfinder` (`pathfinder.cpp:24`) is currently a synchronous `get_path` that completes in one call. Refactoring into pausable/resumable `SearchState` that advances a fixed node budget per tick while preserving determinism is the single largest technical risk. Mitigation: (a) keep the synchronous pathfinder as a non-authoritative fallback for the headless test fixture; (b) build `SearchState` as a stepwise continuation of the existing A*+flow-field algorithm; (c) gate Phase 4 behind a dedicated test that replays identical request sets and asserts identical PathReady ordering across runs.

**Activity frame stack**: the current single `current_node` (`activity.h:102`) cannot represent Sequence/Loop/sub-Activity. The new `ActivityRuntimeState` frame stack changes the serialization shape; replay headers must carry the activity graph data hash, and old save files are not compatible (acceptable - this is foundation work).

**Legacy EventLoop demotion**: many existing `OnceEventHandler` gameplay events (`spawn_entity`/`send_command`/`process_command`/`wait`) and curve `changes(at)` propagation depend on EventLoop. Demotion must preserve curve dependency propagation (still authoritative for curve re-eval) while routing gameplay events through TypedEventQueue. The one-way bridge lets legacy UI observers still function.

### 4.4 Test gates (per stage, runnable)

| Stage | Gate | Pass criterion |
|---|---|---|
| S0 Determinism harness | tick 2 ticks with no commands | checksum identical across N runs; RNG stream seeded identical |
| S1 Move vertical slice | issue MoveCommand, sim N ticks | entity arrives; identical checksum across runs; MovementArrived dispatched |
| S2 Attack->Damage->Death | attack intent, sim until target HP<=0 | DamageApplied + EntityDied dispatched; no on_attribute_change listener fired (assert absent); despawn committed in Phase 11 |
| S3 Gather->Carry->Deposit | gather to capacity, return to TC | ResourceStorage.carried fills then empties; PlayerResources increments; ResourceGathered + ResourceDeposited dispatched |
| S4 Production | TrainCommand with enough resources | cost spent at enqueue; entity spawned Phase 11; UnitProduced then EntitySpawned dispatched in order; pop cap enforces PopulationLimit |
| S5 Scripted AI | AI gathers + trains + attacks | AI issues via CommandPipeline only (assert no direct component mutation); execute_tick=T+1 |
| S6 Scenario Runtime | trigger on EntityDied -> SpawnEntity effect | effect via ScenarioEffectPipeline; deferred commit; checksum stable |
| S7 Replay + checksum | record command log, replay from empty state | identical state checksum at every tick; rejected commands reproduced; divergence audit passes |
| S8 Interrupt/edge cases | target dies mid-gather, repath mid-move, insufficient resources, production cancel | each path yields a defined ActivityTaskResult (not silent drop); MovementCancelled / TaskFailed{InsufficientResources} / TaskInterrupted{TargetInvalidated} |
| S9 Mission 1 end-to-end | `bin/run main --tutorial --headless` (or fixture) | gather 50 wood trigger completes; exit 0 |

### 4.5 Compatibility with existing code

- **Existing demo (`libopenage/main/demo/`)**: keeps using Legacy EventLoop via the one-way bridge; not broken by the SimLoop (sim runs alongside; demo opts out of tick-driven systems).
- **Existing `Move`/`Idle` systems**: `Idle` reused as a phase system; `Move` rewritten (old synchronous behavior removed). The `move.cpp:112-167` speed/turn math is reused by LocomotionSystem.
- **Existing flow-field Pathfinder**: reused as the algorithmic core; wrapped in `SearchState` for pausable/resumable execution. Synchronous `get_path` retained for non-authoritative fallback only.
- **Existing components (Position/CommandQueue/Activity/Ownership/Live)**: kept as curve-backed authoritative state; only `Live` loses its callback table and `Gather` gains curve-backed storage.
- **Existing nyan data (tutorial_data.nyan, mission scenarios, APIScenario)**: unchanged at the schema level; the new systems consume the same nyan FQONs. Mission 2-5 scenario nyan files are added as their mechanics land.
- **Streaming convert output (`/tmp/openage_bfg_lazy256`)**: orthogonal; the de2_base modpack is consumable by the runtime once Mission scenarios point at real GameEntity FQONs (post-vertical-slice).

### 4.6 Known limitations and extension points

**Limitations (first version)**
- No projectiles visual fidelity (ranged attack uses placeholder; ProjectileSystem advances logically but visual is minimal).
- No Resistance full结算 (fixed damage with armor flat reduction; Resistance component parsed but not fully consumed).
- No save/mid-tick snapshot (Activity microsteps are not in checksum).
- Single AI personality (scripted reactive rules only).
- Mission 2-5 scenarios not authored (only Mission 1 in vertical slice).
- No multi-threaded pathfinding (single-threaded round-robin; the 4 multi-threading invariants are specified for the future).

**Extension points (no core rewrite needed)**
- Projectiles: ProjectileSystem already exists as a phase system; visual projectile rendering plugs into existing WorldObject.
- Tech/research: ResearchSystem is a registered phase system (enum slot reserved); patch apply via existing libnyan Transaction API (plan A1-A4 already wired `add_player` patch consumption).
- Garrison / formations / diplomacy: each is a new phase system + Activity sub-graph; the registry/variant/frame-stack model accommodates them without central-switch edits.
- More campaign missions: each mission = a scenario nyan + a `create_mission_N_trigger` factory (pattern already in `trigger.h`); Scenario Runtime triggers/effects are extensible via the typed effect registry.

### 4.7 Acceptance criteria (from the brief, mapped)

- Activity Graph and gameplay rules have no responsibility confusion (RunSystem only orchestrates; phase systems compute). - sections 1.2, 2.2, 3.x
- Damage does not depend on a generic attribute-change listener (`on_attribute_change` removed; DamageSystem/DeathSystem explicit). - 2.x, 3.2, 4.2
- Player, AI, Scenario use the same command pipeline (Scenario non-unit effects use ScenarioEffectPipeline but still validation/typed/deferred/replay). - 1.2, 2.4, 3.5
- New behavior does not require editing a growing central switch (two switches deleted; registries + variants). - 4.2
- Interrupt, target-death, path-failure, insufficient-resources, production-cancel paths have defined results. - 2.3, 3.x, 4.4 S8
- Identical command log + identical initial state -> identical checksum. - 4.4 S7
- Architecture extends to projectiles, tech, garrison, formations, diplomacy, more missions without core rewrite. - 4.6
