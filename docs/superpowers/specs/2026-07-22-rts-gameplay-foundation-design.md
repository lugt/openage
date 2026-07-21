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
  Position(curve) │ Live(curve HP, scalar life_state) │ CommandQueue(curve) │
  Activity(curve runtime) │ GathererCargo + ResourceNodeState (scalars) │
  PlayerResourceLedger (array, scalar) │ PopulationState (scalar) │ ProductionQueue │ Ownership
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
enum class IssuerKind : uint8_t { Player, AI, Scenario, System };
enum class QueueMode : uint8_t { Replace, Append, Prepend, Conditional };

// CommandPriority describes the STRENGTH of an incoming command.
enum class CommandPriority : uint8_t {
    Background, Normal, Forced, SystemCritical
};
// ActivityInterruptPolicy describes whether the CURRENT activity may be interrupted.
// It is NOT a property of the incoming command (the old "Uninterruptible" misnomer).
enum class ActivityInterruptPolicy : uint8_t {
    Interruptible, ForcedOnly, SystemCriticalOnly
};

// Conditional QueueMode references a schema-defined predicate (NOT a lambda/std::function).
struct ConditionalQueueSpec {
    CommandConditionId condition;        // registered, schema-defined
    TypedConditionParams params;
};

struct CommandHeader {
    CommandId id;                        // canonical id (assigned after canonicalization)
    IssuerKind issuer_kind;
    player_id_t issuer;
    tick_id_t issued_tick;              // T (issuer observed)
    tick_id_t execute_tick;             // T + configured input delay (default T+1)
    uint64_t issuer_sequence;           // per-issuer deterministic increment
    QueueMode queue_mode;                // Replace/Append/Prepend/Conditional
    CommandPriority priority;            // Background/Normal/Forced/SystemCritical
    std::optional<ConditionalQueueSpec> condition;  // set iff queue_mode == Conditional
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

**Arbitration**: the incoming `CommandPriority` is compared against the current `ActivityInterruptPolicy`:
- `Background` -> only interrupts `Interruptible` activities that have nothing else pending.
- `Normal` -> interrupts `Interruptible`.
- `Forced` -> interrupts `Interruptible` and `ForcedOnly`.
- `SystemCritical` -> interrupts everything (death/despawn internal flow).

`issuer_kind_order` is part of the canonical sort key but does NOT implicitly decide authority. Authority is handled by validation + ownership + explicit `Forced`/`ScenarioOverride`:
- AI may only control its own player's entities.
- Player and AI do not simultaneously control one player unless explicitly allowed.
- Scenario normal commands obey ownership; Scenario forced takeover uses explicit `Forced`/`ScenarioOverride`.
- `System` commands are for death/despawn internal, non-rejectable flows.

**Canonical sort key**:
```
(execute_tick, issuer_kind_order, issuer_player_id, issuer_sequence, command_id)
```

**Subjects canonicalization (locked, first version)**: sort -> dedupe -> drop invalid ids at validation -> **per-entity single-subject command entries generated in entity-id order**. (Shared command-group alternative explicitly deferred.) Validation processes per entity in id order.

### 2.5 CommandIngressQueue vs CommandLog (separated)

Live simulation does NOT drain the command log. The Command Log is an append-only replay artifact, not a runtime queue. **Player input arrives outside the simulation** (render/input thread / platform event loop) into a `RawInputBuffer` / `PlayerCommandSource`; it is drained in Phase 1, NOT Phase 10. AI and Scenario observe in Phase 10.

```
Player input  -> RawInputBuffer / PlayerCommandSource  (OUTSIDE sim)
AI            -> (Phase 10 observation)
Scenario      -> (Phase 10 observation)
                 |
                 v  CommandPipeline::issue()
                 v  CommandIngressQueue
  Phase 1   canonical sort (execute_tick, issuer_kind_order, issuer_player_id,
                            issuer_sequence, command_id)
  Phase 2   validation/arbitration
            -> Accepted/Rejected result
            -> append CommandLog + CommandResultLog
```

Replay mode:
```
ReplayCommandSource -> reads CommandLog -> fills CommandIngressQueue (Phase 1)
```

The simulation NEVER reads wall-clock input timestamps. Once real input is assigned to a tick, that tick's commands are written to the command log.

Recorded per entry: canonicalized incoming command, validation result, rejection reason, resulting per-entity queue operation. Replay reproduces rejected commands too (divergence audit).

**Sort key** (must not depend on thread arrival or global atomic increment):
```
(execute_tick, issuer_kind_order, issuer_player_id, issuer_sequence, command_id)
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

**Movement result states** (MovementResolutionSystem output, distinguish):
```
MovementArrived | MovementBlocked | MovementNeedsRepath | MovementCancelled
```
- `MovementArrived`: reached goal (point) or within acceptance_radius (MoveIntoRange).
- `MovementNeedsRepath`: dynamic target moved beyond threshold (see RepathPolicy); Activity re-submits next tick.
- `MovementCancelled`: an **already-active** movement was cancelled by Stop / new command / death / forced interrupt. (A stale PathReady does NOT produce this - see stale handling below.)
- `MovementBlocked`: path blocked by terrain/unit.

**Stale result handling** (critical): a stale PathReady (old movement_generation) MUST NOT generate MovementCancelled. The authoritative path is simply dropped. WaitSpec uses `request_id + movement_generation + activity_generation`; on mismatch the result is discarded from the authoritative path. A non-authoritative diagnostic trace MAY be emitted. `MovementCancelled` only means an active movement was cancelled.

**Dynamic target repath policy** (don't emit TargetMoved on every coordinate change - moving targets change frequently):
```cpp
struct RepathPolicy {
    phys_t target_displacement_threshold;   // squared, fixed-point
    tick_id_t minimum_repath_interval;       // throttle
    uint32_t maximum_retries;                // then PathFailed
};
```
MovementResolutionSystem emits `MovementNeedsRepath` when the threshold is crossed; Activity re-submits the path next tick.

**MoveIntoRange distance semantics** (MUST be fixed-point, one definition chosen, NOT left to each system):
```
edge_to_edge_distance <= desired_action_range
   where edge_to_edge = center_distance - source_collision_radius - target_collision_radius
```
Use **squared distance** to avoid sqrt:
```cpp
distance_squared <= effective_range_squared
```

**Deterministic round-robin (anti-starvation)**: strictly finishing one large request before the next starves. Use:
```
requests sorted by request_id
each active request gets fixed expansion quantum per tick
total expansion <= global budget
```
Must define: per-request quantum, global node budget, request admission limit, PathFailed/PartialPath conditions, and how the flow-field Pathfinder is refactored into a **pausable/resumable search state**. (Phase 4 key technical risk - see 4.3.)

### 2.10 PhaseWorkQueue - typed inter-phase work items

PathRequest, StartMovementRequest, AttackIntentRequest, DamageRequest, SpawnRequest, ResourceTransferRequest, ProductionEnqueueRequest are NOT Domain Events. They are internal work items passed between phases via typed work queues.

```cpp
template<typename T>
class PhaseWorkQueue {
public:
    void push(T item);
    std::span<const T> sorted_items() const;   // canonical-sorted, stable
    void clear();
private:
    std::vector<T> items;
};
```

Each work queue type MUST define (in a registry table, see 4.x):
| Work queue | Producer phase | Consumer phase | Canonical sort key | In checksum | Must be empty at tick end | Cross-tick retention |
|---|---|---|---|---|---|---|
| PathRequestQueue | Phase 3 (Activity) | Phase 4 (Pathfinding) | request_id | yes | no | yes (in-flight search) |
| StartMovementRequestQueue | Phase 3 | Phase 5 (Locomotion) | requester_id | yes | yes | no |
| AttackIntentRequestQueue | Phase 3 | Phase 6 (Combat) | attacker_id | yes | yes | no |
| DamageRequestQueue | Phase 6 | Phase 7 (Damage) | (target_id, source_id, attack_id, hit_sequence) | yes | yes | no |
| ProjectileSpawnRequestQueue | Phase 6 | Phase 6 (Projectile) | projectile_id | yes | no | yes (in-flight) |
| SpawnRequestQueue | Phase 8 (Production) | Phase 11 (DeferredCommit) | reserved_entity_id | yes | yes | no |
| ResourceTransferRequestQueue | Phase 8 | Phase 8 (Economy) | (gatherer_id, dropoff_id) | yes | yes | no |
| ProductionEnqueueRequestQueue | Phase 2 | Phase 8 (Production) | producer_id | yes | yes | no |

**Rule**: an ActivityTask (e.g. SubmitPathRequest) pushes to a work queue; it does NOT gain arbitrary Component write access by "registering an intent". The owning phase system reads the queue and writes authoritative state via the writer matrix + WorldMutationBuffer.

Data flow:
```
ActivityTask -> typed request/work queue -> owning phase system consumes
             -> system modifies authoritative state -> system emits typed event
```

### 2.11 Attack cycle state + DamageRequest work item

Activity only **starts and stops** an AttackIntent; CombatSystem owns the continuous attack loop. Activity does NOT wait per-AttackPoint (that would force re-submitting intent each cycle or make the wait meaningless).

```cpp
enum class AttackCycleState : uint8_t { Ready, Windup, AttackPoint, Recovery, Cooldown };

struct AttackIntent {
    AttackId id;
    entity_id_t attacker;
    entity_id_t target;
    uint32_t attack_generation;     // bump on repose/target-change; stale intent dropped
    AttackCycleState state;
    tick_id_t state_until_tick;     // fixed-point tick math
};
// Cycle: Ready -> Windup -> AttackPoint -> Recovery -> Cooldown -> Ready (repeats)
```

At `AttackPoint`:
- melee: write DamageRequest to DamageRequestQueue.
- ranged: create/submit ProjectileSpawnRequest (ProjectileSystem advances on later ticks).
- MAY emit `AttackCommitted` / `AttackPointReached` for animation/audio/Scenario observers.
- Activity is NOT woken on ordinary AttackPoints.

Activity waits only on:
```
AttackNeedsReposition | AttackCancelled | TargetInvalidated | AttackerDied
```
`CombatResolutionSystem` emits `AttackNeedsReposition` when the target leaves range; CombatSystem stops the current intent; Activity next-tick enters MoveIntoRange.

**DamageRequest is a work item, NOT a Domain Event**:
```cpp
struct DamageRequest {
    AttackId attack_id;
    uint32_t hit_sequence;
    entity_id_t source;
    entity_id_t target;
    DamageProfileId damage_profile;
    fixed_t base_amount;
};
```
Phase 7 processes by strict order: `(target_entity_id, source_entity_id, attack_id, hit_sequence)`.

### 2.12 Damage phase sub-steps + extensible DamageResolver

Phase 7 is split into ordered sub-steps to define read-your-writes and commit points:
```
7.1 CollectAndSortDamageRequests   (DamageRequestQueue drain + sort)
7.2 ResolveDamage                  (compute final per DamageProfile)
7.3 CommitLiveMutations            (WorldMutationBuffer writes Live.hp)
7.4 DetectDeaths                   (read FINAL committed HP, not stale)
7.5 CommitLifeStateTransitions     (Alive -> Dying)
7.6 EmitDamageAndDeathEvents       (DamageApplied per hit; EntityDied once)
```
DeathSystem reads the **already-committed final HP** (after 7.3), not the old component value.

Multiple hits to the same target in one tick:
- applied in fixed order (7.1 sort);
- `DamageApplied` records each actual amount;
- `EntityDied` emitted exactly once;
- once LifeState = Dying, subsequent damage handling rule is explicit (v1: Dying entities take no further damage);
- the target's movement/attack/gather active intents are deterministically cancelled in Phase 7.

**`max(0, attack - armor)` is a v1 placeholder ONLY, NOT a permanent core rule.** Extensible resolver:
```cpp
struct DamageResolver {
    fixed_t resolve(DamageProfile, ArmorProfile, CombatModifiers);
    // future: damage class, armor class, bonus damage, minimum damage,
    //         projectile modifier, technology modifier, splash damage
};
```

### 2.13 Resource state (split components, array-backed, NOT FQON-keyed)

Do not use one ambiguous `ResourceStorage` for both cargo and player resources. Split:
```cpp
struct GathererCargo {                // on the gathering entity
    ResourceTypeId type;
    resource_amount_t amount;
    resource_amount_t capacity;
};
struct ResourceNodeState {            // on the resource node entity
    ResourceTypeId type;
    resource_amount_t remaining;
};
struct PlayerResourceLedger {        // on the player
    std::array<resource_amount_t, RESOURCE_TYPE_COUNT> amounts;  // 4: Food/Wood/Stone/Gold
};
struct PopulationState {             // on the player
    population_t used;
    population_t reserved;
    population_t capacity;
};
```
With only 4 resources in v1, use **stable-indexed `std::array`**, NOT `unordered_map<FQON, amount>`. Nyan FQON is used for load-time mapping to a stable `ResourceTypeId` only, never as an unordered hot-path key. Future data-driven resource types use stable ResourceTypeId + sorted flat container + resource registry/data hash in replay header.

**Curve vs scalar**: do NOT force Cargo/PlayerResources into curves just because the old component used curves. An authoritative fixed-tick component may be a plain deterministic scalar. Use curves ONLY when needed: querying historical ticks, render interpolation, legacy dependency propagation. Section 4 lists which state stays curve vs migrates to tick-state scalar.

### 2.14 Production queue semantics

Production queue is a building's continuous system-owned state; it is NOT modeled as a `Train Activity -> Wait ProductionCompleted`. Direct data flow:
```
TrainCommand -> Phase 2 ProductionCommandHandler
            -> validate ownership / queue capacity / resources / population
            -> atomic enqueue transaction (deduct + reserve pop + append)
            -> ProductionQueue
            -> Phase 8 ProductionSystem continuous advance
```

Not every Command must become an entity Activity:
| Command | Routing |
|---|---|
| Move, Attack, Gather, Build | drives an Activity |
| Train, Research, SetRallyPoint | directly modifies system-owned queue/state (no Activity) |
| StopProduction, CancelProduction | operates ProductionQueue |
| Scenario world effect | ScenarioEffectPipeline |

**Atomic enqueue transaction** (all-or-nothing):
```
validate cost
validate queue capacity
validate population capacity    (used + reserved + unit_population_cost <= capacity)
deduct resources
reserve population              (reserved += unit_population_cost)
append queue item
```
If any step fails, nothing commits.

Population judgement is `used + reserved + unit_population_cost <= capacity` (NOT `pop_used + train.queue < pop_cap`), because unit population cost may be != 1, queue items != population, multiple buildings enqueue same tick, cancel must release reserved, and spawn success transfers reserved->used.

**Spawn placement** (no off-by-one; T Phase 8 enqueue commits same-T Phase 11):
```
T Phase 8:   production timer completes
             reserve entity_id
             enqueue SpawnRequest (item -> ReadyToSpawn)
T Phase 11:  attempt commit spawn
             success: create full entity; reserved -> used; remove queue item;
                      enqueue EntitySpawned + UnitProduced, scheduled_tick = T+1
             failure: item stays ReadyToSpawn; NO UnitProduced emitted;
                      retry deterministic spawn placement next tick
T+1 Phase 9: EntitySpawned / UnitProduced dispatched; listeners may safely read the new entity
```
Listeners read the entity the tick after commit (T+1), NOT T+2.

**Events distinguished**:
- `ProductionTimerCompleted`: training time ended, entity may not yet have a birth position.
- `EntitySpawned`: entity actually entered the world.
- `UnitProduced`: production transaction complete, entity usable by external systems.
(v1: emit EntitySpawned + UnitProduced together on successful commit.)

Section 4 must define: cancel refund ratio, refund + reservation release on building destruction, spawn-point-blocked handling, population-capacity-drop-during-training handling.

---

## 3. End-to-end data flow (per vertical slice)

Tick-T perspective; every phase labeled. Activities wake the tick **after** their wait is satisfied (next-tick wake rule). Pathfinding may take T+k ticks. The four authoritative pipelines (section 3.6) are the locked summary.

### 3.1 MoveTo + MoveIntoRange (principle #8 async pathing)

Pathfinding does NOT necessarily complete in the same tick as submit, so timing is `T+k`, not default-T.

```
T     Phase 3  Activity SubmitPathRequest -> PathRequestQueue
               WaitForEvent(PathReady(request_id, generation) | PathFailed(request_id, generation))
T..T+k Phase 4  PathfindingSystem: deterministic round-robin; each request gets fixed
               expansion quantum; total <= global budget
T+k   Phase 4  search complete -> emit PathReady / PathFailed
T+k   Phase 9  event dispatch -> matches Activity wait state -> mark next-tick runnable
T+k+1 Phase 3  Activity resumes -> enqueue StartMovementRequest -> StartMovementRequestQueue
               WaitForEvent(MovementArrived | MovementBlocked | MovementNeedsRepath | MovementCancelled)
T+k+1 Phase 5  MovementIntentSystem consumes StartMovementRequest; LocomotionSystem advances
               every active intent one deterministic step from this phase
```

**Stale result handling**: a stale PathReady (old `movement_generation`, mismatched vs WaitSpec `request_id + movement_generation + activity_generation`) is dropped from the authoritative path (no MovementCancelled); a non-authoritative diagnostic trace MAY be emitted. `MovementCancelled` only means an **already-active** movement was cancelled (Stop / new command / death / forced interrupt).

**Dynamic target**: do NOT emit `MovementNeedsRepath` on every coordinate change. MovementResolutionSystem emits `MovementNeedsRepath` only when the target displacement crosses `RepathPolicy.target_displacement_threshold` (and `minimum_repath_interval` has passed, `maximum_retries` not exceeded). Activity re-submits the path next tick.

**MoveIntoRange**: `PathGoal.target = entity_id`, `acceptance_radius = attack/gather range`. Range check uses squared distance, edge-to-edge semantics (section 2.9): `center_dist² - (src_r + tgt_r)² <= effective_range²` (fixed-point). On met: `MovementArrived(in range)` -> Activity proceeds to Attack/Gather.

### 3.2 Attack -> Damage -> Death (principle #4 explicit damage)

Activity only **starts/stops** the AttackIntent; CombatSystem owns the continuous loop. Activity does NOT wait per-AttackPoint.

```
T   Phase 3  AttackTarget Activity: ValidateTarget (alive? in range?)
            - out of range: MoveIntoRange(target, attack_range) sub-activity (3.1)
            - in range: enqueue StartAttackRequest -> AttackIntentRequestQueue
            - WaitForEvent(AttackNeedsReposition | AttackCancelled | TargetInvalidated | AttackerDied)
T   Phase 6  CombatSystem (per-tick, each active AttackIntent): cycle
              Ready -> Windup -> AttackPoint -> Recovery -> Cooldown -> Ready (repeats)
            - at AttackPoint, melee: write DamageRequest -> DamageRequestQueue
                          ranged: create ProjectileSpawnRequest -> ProjectileSpawnRequestQueue
            - may emit AttackCommitted / AttackPointReached (animation/audio/Scenario; Activity NOT woken)
            - target left range: CombatResolutionSystem emits AttackNeedsReposition -> stop intent
T   Phase 7  (sub-steps 7.1-7.6, see 2.12)
            7.1 CollectAndSortDamageRequests   sort (target, source, attack_id, hit_sequence)
            7.2 ResolveDamage                  DamageResolver.resolve(profile, armor, mods)
            7.3 CommitLiveMutations            write Live.hp via WorldMutationBuffer (writer: DamageSystem)
            7.4 DetectDeaths                   read FINAL committed HP
            7.5 CommitLifeStateTransitions     Alive -> Dying (writer: DeathSystem)
            7.6 EmitDamageAndDeathEvents       DamageApplied (per hit) + EntityDied (once)
            - Dying entity: movement/attack/gather intents deterministically cancelled
            - enqueue despawn -> SpawnRequestQueue (despawn)
T   Phase 9  DamageApplied / EntityDied dispatched (6-tuple)
            - attacker Activity: TargetInvalidated (target died) -> Interrupt(TaskInterrupted{EntityDied})
T   Phase 10 Scenario/AI may read the dead entity's read-only FINAL state (this tick only)
T   Phase 11 DeferredEntityCommit: despawn commit; entity leaves active iteration set
            - after Phase 11, all entity pointers/references are INVALID; listeners hold entity IDs only
```

Satisfies #4: no `on_attribute_change` listener (Live callback table removed); DamageSystem explicitly resolves and writes Live via WorldMutationBuffer; HP<=0 detected by DeathSystem reading committed HP. `max(0, attack - armor)` is a v1 placeholder via the extensible `DamageResolver` (2.12).

### 3.3 Gather -> Carry -> Deposit (principle #3 controlled writers, multi-gatherer)

```
T   Phase 3  GatherResource Activity: SubmitPathRequest to resource node (MoveIntoRange, 3.1)
T+...        in range -> enqueue StartGatherRequest -> GatherRequestQueue
            - WaitForEvent(ResourceDepleted | CarryFull)
T   Phase 8  GatherSystem (per-tick): gather timer (ticks)
            - multi-gatherer on one node: intents sorted by
              (resource_node_id, gatherer_entity_id, gather_intent_id)
              actual_amount = min(gather_rate, cargo_remaining_capacity, node_remaining)
            - GathererCargo.amount += actual_amount  (writer: EconomySystem via ResourceTransferRequestQueue)
            - ResourceNodeState.remaining -= actual_amount
            - buffer ResourceGathered (records ACTUAL amount, not theoretical)
            - node depleted: ResourceDepleted -> repath to new node OR fail
            - cargo full: CarryFull -> ReturnResource sub-activity (MoveIntoRange to dropoff)
in range at dropoff -> Deposit:
            - EconomySystem.deposit(): ATOMIC transaction in one Economy commit
                GathererCargo.amount -= deposited   AND
                PlayerResourceLedger[type] += deposited
              (both mutations commit together; validation: ownership, dropoff accepts type,
               gatherer in range, valid cargo, valid dropoff)
            - buffer ResourceDeposited; reset cargo; loop back to gather
```

**Drop-off selection** "nearest dropoff" deterministic tie-break: `(path_cost, geometric_distance, dropoff_entity_id)` - never container iteration order.

4 resources (Food/Wood/Stone/Gold) are stable-indexed `ResourceTypeId` (2.13); nyan FQON is load-time mapping only.

### 3.4 Production (principle #3 + #11 deferred commit, no off-by-one)

TrainCommand does NOT drive an Activity; it goes to a system-owned queue.

```
T   Phase 1  TrainCommand ingested (execute_tick=T)
T   Phase 2  ProductionCommandHandler:
              atomic enqueue transaction:
                validate cost / queue capacity / population (used+reserved+cost<=capacity)
                deduct resources
                reserve population (reserved += unit_population_cost)
                append ProductionQueue item
              (any step fails -> nothing commits; reject logged)
... Phase 8  ProductionSystem continuous advance: accumulate timer (ticks)
T   Phase 8  timer completes
              reserve entity_id
              enqueue SpawnRequest (item -> ReadyToSpawn)
              emit ProductionTimerCompleted (timer done, may lack birth position)
T   Phase 11 attempt commit spawn (SAME tick T, not T+1):
              success: create full entity; reserved -> used; remove queue item;
                       enqueue EntitySpawned + UnitProduced, scheduled_tick = T+1
              failure (spawn point blocked): item stays ReadyToSpawn; NO UnitProduced;
                       retry deterministic spawn placement next tick
T+1 Phase 9  EntitySpawned / UnitProduced dispatched; listeners may safely read the new entity
```

Population judgement is `used + reserved + unit_population_cost <= capacity` (NOT `pop_used + train.queue < pop_cap`). Cancel releases reserved; spawn success transfers reserved->used. Section 4 defines cancel refund ratio, building-destruction refund + reservation release, spawn-point-blocked handling, capacity-drop-during-training.

### 3.5 Command T+1 + unified issuer (principle #7/#10)

Player input is NOT in Phase 10 - it arrives outside the simulation.

```
EXTERNAL   Player input -> RawInputBuffer / PlayerCommandSource  (render/input thread)
T   Phase 1  CommandIngest: drain raw input snapshot
              canonicalize selected entity IDs
              construct Command; issued_tick = T; execute_tick = T + configured_input_delay (default T+1)
              push CommandIngressQueue
T   Phase 10 AI observes snapshot at T -> issue commands (execute_tick=T+1)
              Scenario observes domain events/state at T -> issue commands/effects (execute_tick=T+1)
T+1 Phase 1  canonical sort (execute_tick, issuer_kind_order, issuer_player_id, issuer_sequence, command_id)
T+1 Phase 2  ValidationAndArbitration: per-entity id-order validation, queue-mode,
              CommandPriority vs ActivityInterruptPolicy -> Activity transition
              (CommandArbitrationSystem bumps ActivityRuntime generation)
T+1 Phase 3  Activity advances with the new command's intent
```

Replay: `ReplayCommandSource` injects recorded (execute_tick) commands in Phase 1. The simulation never reads wall-clock input timestamps.

Eliminates: AI-vs-player ordering (both observe at T, execute at T+1), Scenario recursive triggers, same-tick system reentry, issuer-order effects. Conflicting commands from different issuers on the same entity are resolved by canonical arbitration (sort key + CommandPriority vs ActivityInterruptPolicy), not by issuer_kind implicitly.

### 3.6 The four authoritative pipelines (locked summary)

```
Move:      Command -> Activity -> PathRequestQueue -> PathfindingSystem -> PathReady
           -> Activity -> StartMovementRequestQueue -> LocomotionSystem -> MovementResult -> Activity

Attack:    Command -> Attack Activity -> StartAttackRequestQueue -> CombatSystem (continuous cycle)
           -> DamageRequestQueue | ProjectileSpawnRequestQueue -> DamageSystem -> DeathSystem
           -> DamageApplied / EntityDied -> Activity, AI, Scenario

Gather:    Command -> Gather Activity -> Movement -> StartGatherRequestQueue -> GatherSystem
           -> GathererCargo / ResourceNodeState -> Deposit transaction -> PlayerResourceLedger
           -> Resource domain events

Production: TrainCommand -> ProductionCommandHandler -> atomic (deduct + reserve pop) + ProductionQueue
           -> ProductionSystem -> SpawnRequestQueue -> DeferredEntityCommit
           -> EntitySpawned / UnitProduced
```

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
- `command.{h,cpp}` - CommandHeader + CommandPayload variant (replaces virtual base); IssuerKind, CommandPriority, ActivityInterruptPolicy, QueueMode, ConditionalQueueSpec
- `command_pipeline.{h,cpp}` - issue() -> CommandIngressQueue
- `command_ingress_queue.{h,cpp}` - runtime queue (NOT the log)
- `command_log.{h,cpp}` - append-only replay artifact + CommandResultLog
- `command_canonicalizer.{h,cpp}` - subjects sort/dedupe/per-entity split
- `command_arbitration.{h,cpp}` - validation + queue-mode + CommandPriority-vs-ActivityInterruptPolicy -> Activity transition
- `raw_input_buffer.{h,cpp}` - PlayerCommandSource / RawInputBuffer (thread boundary, outside sim)
- `production_command_handler.{h,cpp}` - Phase 2 handler for TrainCommand -> atomic enqueue transaction
- `replay_command_source.{h,cpp}` - reads CommandLog into IngressQueue

**Phase work queues (`libopenage/gamestate/sim/work_queue/`)**
- `phase_work_queue.h` - template PhaseWorkQueue<T>
- `work_queue_registry.{h,cpp}` - registry table: producer/consumer phase, sort key, in-checksum, must-empty, cross-tick retention (the 2.10 table)
- `path_request_queue.{h,cpp}`, `start_movement_request_queue.{h,cpp}`, `attack_intent_request_queue.{h,cpp}`, `damage_request_queue.{h,cpp}`, `projectile_spawn_request_queue.{h,cpp}`, `spawn_request_queue.{h,cpp}`, `resource_transfer_request_queue.{h,cpp}`, `production_enqueue_request_queue.{h,cpp}`

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
- `movement_intent.{h,cpp}` - MovementIntentSystem (consumes StartMovementRequest)
- `movement_resolution.{h,cpp}` - MovementResolutionSystem (Arrived/Blocked/NeedsRepath/Cancelled; RepathPolicy)
- `combat.{h,cpp}` - CombatSystem (AttackIntent + AttackCycleState Ready/Windup/AttackPoint/Recovery/Cooldown; DamageRequest/ProjectileSpawn creation)
- `combat_resolution.{h,cpp}` - CombatResolutionSystem (AttackNeedsReposition when target leaves range)
- `projectile.{h,cpp}` - ProjectileSystem (per-tick projectile advance + impact)
- `damage.{h,cpp}` - DamageSystem (7.1-7.6 sub-steps; DamageResolver)
- `damage_resolver.{h,cpp}` - extensible DamageResolver (v1: max(0,amount-armor) placeholder; future: classes/bonuses/splash)
- `death.{h,cpp}` - DeathSystem (reads committed HP; Dying/Dead; despawn enqueue; cancel victim intents)
- `gather.{h,cpp}` - GatherSystem (multi-gatherer atomic allocation)
- `build.{h,cpp}` - BuildSystem
- `production.{h,cpp}` - ProductionSystem (continuous timer; ProductionTimerCompleted; SpawnRequest)
- `economy.{h,cpp}` - EconomySystem (atomic deposit transaction; cost deduction; population)
- `deferred_entity_commit.{h,cpp}` - DeferredEntityCommitSystem (spawn/despawn; spawn placement retry)
- `checksum.{h,cpp}` - ChecksumSystem (state hash per tick)

**Resource / population state (`libopenage/gamestate/component/api/`)**
- `gatherer_cargo.{h,cpp}` - GathererCargo (type/amount/capacity; scalar, not curve)
- `resource_node_state.{h,cpp}` - ResourceNodeState (type/remaining; scalar)
- `player_resource_ledger.{h,cpp}` - PlayerResourceLedger (std::array<ResourceTypeId>, scalar)
- `population_state.{h,cpp}` - PopulationState (used/reserved/capacity)
- `resource_type.{h,cpp}` - ResourceTypeId registry + FQON load-time mapping

**Pathfinding refactor (`libopenage/pathfinding/`)**
- `search_state.{h,cpp}` - pausable/resumable flow-field search state (extracted from synchronous `pathfinder.cpp`); stepwise continuation of A*+flow-field

**Determinism (`libopenage/gamestate/sim/determinism/`)**
- `replay.{h,cpp}` - replay runner (CommandLog -> SimLoop)
- `state_checksum.{h,cpp}` - canonical state serialization + hash (includes work-queue contents where in-checksum=yes)
- `deterministic_rng.{h,cpp}` - wires existing `rng::RNG` (xorshift128+, seedable/serializable, currently 0 instantiations) into sim

### 4.2 Modified files (existing)

- `libopenage/gamestate/simulation.{h,cpp}` - replace wall-clock `reach_time` loop with `DeterministicSimulationLoop::tick`; register phase systems; fix entity iteration to ordered
- `libopenage/gamestate/system/activity.{h,cpp}` - DELETE central `handle_subsystem` switch (`activity.cpp:123-143`); advance reads ActivityRuntimeState frame stack; microstep budget from ActivityExecutionContext; next-tick wake rule
- `libopenage/gamestate/event/send_command.cpp` - DELETE command_t switch (`:67`); route via CommandPipeline + canonicalizer
- `libopenage/gamestate/system/move.{h,cpp}` - REWRITE: remove synchronous path-bake into Position curve; Move is now an Activity (SubmitPathRequest -> Wait -> StartMovementRequest). The `move.cpp:112-167` speed/turn math is reused by LocomotionSystem.
- `libopenage/gamestate/component/internal/activity.h` - extend to ActivityRuntimeState (frame stack, params, wait_state, generation)
- `libopenage/gamestate/component/api/live.{h,cpp}` - REMOVE `on_attribute_change` callback table + `change_callbacks` map (`live.h:85-105`); death now DeathSystem-driven. Live.hp stays a curve (render interpolation); life_state is a tick scalar.
- `libopenage/gamestate/component/api/gather.{h,cpp}` - `carried_amount` plain int64 -> split into GathererCargo + ResourceNodeState (scalars; NOT curves - no historical query needed)
- `libopenage/gamestate/component/api/{build,train,production,resource_storage}.{h,cpp}` - fill in authoritative state (currently skeletons); resource_storage -> PlayerResourceLedger
- `libopenage/gamestate/component/internal/command_queue.{h,cpp}` - integrate QueueMode + cancel-by-tag (interrupt priority is on CommandHeader, not the queue)
- `libopenage/gamestate/player.{h,cpp}` - `resources` plain map -> PlayerResourceLedger (std::array, scalar); add PopulationState
- `libopenage/gamestate/game_state.{h,cpp}` / `libopenage/gamestate/map.cpp` - `unordered_map<entity_id_t,...>` -> ordered container (fixes `map.cpp:24` non-determinism TODO)
- `libopenage/event/event_loop.{h,cpp}` - demote: keep as time-scheduling primitive + curve dependency propagation; remove gameplay-main-drive role; add LegacyEventLoopAdapter for explicit unit-checked tick/time conversion
- `libopenage/gamestate/activity/types.h` - extend closed `node_t` enum with Sequence/Loop/Transition/Interrupt (Condition folded into XorGate; WaitEvent/WaitUntil as node types)
- `libopenage/gamestate/entity_factory.cpp` - rewrite activity graph construction to data-driven node definitions (replace arbitrary-lambda conditions/tasks); register systems/tasks
- `libopenage/gamestate/api/definitions.h` - ACTIVITY_TASK_SYSTEM_DEFS map -> ActivityTaskRegistry registrations; conditions/primers already const_map registry (reuse pattern)
- `libopenage/clock/clock.{h,cpp}` - separate sim clock (tick-driven) from render clock (wall-clock); wall-clock decides tick count per frame only
- `libopenage/main.cpp` / `libopenage/main.h` - `--tutorial` already exists; add `--replay <log>`, `--checksum-every N`, `--input-delay <ticks>`

### 4.3 Phase technical risks

**Phase 4 pathfinding (HIGHEST RISK)**: the flow-field `Pathfinder` (`pathfinder.cpp:24`) is currently a synchronous `get_path` that completes in one call. Refactoring into pausable/resumable `SearchState` that advances a fixed node budget per tick while preserving determinism is the single largest technical risk. Migration plan:
1. Extract the A* portal stage and flow-field integration stage into a stepwise `SearchState` (continuation, not coroutine) that can be advanced N nodes per call and serialized for cross-tick retention.
2. Keep the synchronous `get_path` as a non-authoritative fallback for the headless test fixture and the legacy demo bridge.
3. Define per-request quantum, global node budget, request admission limit; round-robin by request_id (anti-starvation).
4. Define PathFailed/PartialPath conditions: `OUT_OF_BOUNDS` -> PathFailed immediately; no-path-found within `maximum_retries * budget` -> PathFailed; nav_revision mismatch -> PathFailed.
5. Gate Phase 4 behind a dedicated test that replays identical request sets and asserts identical PathReady ordering across N runs and across machines.

**Activity frame stack**: the current single `current_node` (`activity.h:102`) cannot represent Sequence/Loop/sub-Activity. The new `ActivityRuntimeState` frame stack changes the serialization shape; replay headers must carry the activity graph data hash, and old save files are not compatible (acceptable - this is foundation work).

**Legacy EventLoop demotion**: many existing `OnceEventHandler` gameplay events (`spawn_entity`/`send_command`/`process_command`/`wait`) and curve `changes(at)` propagation depend on EventLoop. Demotion must preserve curve dependency propagation (still authoritative for curve re-eval) while routing gameplay events through TypedEventQueue. The one-way bridge lets legacy UI observers still function.

**Combat attack-cycle state**: storing per-AttackIntent cycle state (Windup/AttackPoint/Recovery/Cooldown) is new persistent sim state; must be in the checksum and serializable. Risk: getting the Activity/CombatSystem split right (Activity must not wait per-AttackPoint).

### 4.4 Test gates (per stage, runnable)

| Stage | Gate | Pass criterion |
|---|---|---|
| S0 Determinism harness | tick 2 ticks with no commands | checksum identical across N runs; RNG stream seeded identical; work queues that must-be-empty are empty at tick end |
| S1 Move vertical slice | issue MoveCommand, sim N ticks | entity arrives; identical checksum across runs; PathReady -> MovementArrived dispatched; pathfinding takes T+k correctly |
| S2 Attack->Damage->Death | attack intent, sim until target HP<=0 | AttackCycleState advances; DamageApplied + EntityDied dispatched; no on_attribute_change listener (assert absent); despawn committed Phase 11; victim intents cancelled Phase 7 |
| S3 Gather->Carry->Deposit | gather to capacity, return to TC | multi-gatherer no over-gather; GathererCargo + PlayerResourceLedger update atomically; ResourceGathered records ACTUAL amount; ResourceDeposited dispatched |
| S4 Production | TrainCommand with enough resources | atomic enqueue (deduct+reserve); entity spawned same-T Phase 11; EntitySpawned+UnitProduced dispatched T+1; pop cap enforces PopulationLimit |
| S5 Scripted AI | AI gathers + trains + attacks | AI issues via CommandPipeline only (assert no direct component mutation); execute_tick=T+1 |
| S6 Scenario Runtime | trigger on EntityDied -> SpawnEntity effect | effect via ScenarioEffectPipeline; deferred commit; checksum stable |
| S7 Replay + checksum | record command log, replay from empty state | identical state checksum at every tick; rejected commands reproduced; divergence audit passes |
| S8 Interrupt/edge cases | target dies mid-gather, repath mid-move, insufficient resources, production cancel, spawn-point blocked | each path yields a defined ActivityTaskResult (not silent drop); MovementCancelled / MovementNeedsRepath / TaskFailed{InsufficientResources} / TaskInterrupted{TargetInvalidated}; stale PathReady does NOT produce MovementCancelled (assert) |
| S9 Mission 1 end-to-end | `bin/run main --tutorial --headless` (or fixture) | gather 50 wood trigger completes; exit 0 |

### 4.5 Compatibility with existing code

- **Existing demo (`libopenage/main/demo/`)**: keeps using Legacy EventLoop via the one-way bridge; not broken by the SimLoop (sim runs alongside; demo opts out of tick-driven systems).
- **Existing `Move`/`Idle` systems**: `Idle` reused as a phase system; `Move` rewritten (old synchronous behavior removed). The `move.cpp:112-167` speed/turn math is reused by LocomotionSystem.
- **Existing flow-field Pathfinder**: reused as the algorithmic core; wrapped in `SearchState` for pausable/resumable execution. Synchronous `get_path` retained for non-authoritative fallback only.
- **Existing components (Position/CommandQueue/Activity/Ownership/Live)**: kept as curve-backed authoritative state; only `Live` loses its callback table; `Gather` splits into GathererCargo + ResourceNodeState (scalars); PlayerResources becomes the std::array PlayerResourceLedger (scalar).
- **Existing nyan data (tutorial_data.nyan, mission scenarios, APIScenario)**: unchanged at the schema level; the new systems consume the same nyan FQONs. Mission 2-5 scenario nyan files are added as their mechanics land.
- **Streaming convert output (`/tmp/openage_bfg_lazy256`)**: orthogonal; the de2_base modpack is consumable by the runtime once Mission scenarios point at real GameEntity FQONs (post-vertical-slice).

### 4.6 Known limitations and extension points

**Limitations (first version)**
- No projectiles visual fidelity (ranged attack uses placeholder; ProjectileSystem advances logically but visual is minimal).
- No Resistance full结算 (v1 DamageResolver is `max(0, amount - armor)` placeholder; Resistance component parsed but not fully consumed).
- No save/mid-tick snapshot (Activity microsteps are not in checksum).
- Single AI personality (scripted reactive rules only).
- Mission 2-5 scenarios not authored (only Mission 1 in vertical slice).
- No multi-threaded pathfinding (single-threaded round-robin; the 4 multi-threading invariants are specified for the future).

**Extension points (no core rewrite needed)**
- Projectiles: ProjectileSystem already exists as a phase system; visual projectile rendering plugs into existing WorldObject.
- Tech/research: ResearchSystem is a registered phase system (enum slot reserved); patch apply via existing libnyan Transaction API (plan A1-A4 already wired `add_player` patch consumption).
- Garrison / formations / diplomacy: each is a new phase system + Activity sub-graph; the registry/variant/frame-stack model accommodates them without central-switch edits.
- More campaign missions: each mission = a scenario nyan + a `create_mission_N_trigger` factory (pattern already in `trigger.h`); Scenario Runtime triggers/effects are extensible via the typed effect registry.

### 4.7 Curve vs tick-state scalar (migration decisions)

Use curves ONLY when needed: querying historical ticks, render interpolation, legacy dependency propagation. Otherwise authoritative fixed-tick component is a plain deterministic scalar.

| State | Type | Reason |
|---|---|---|
| Position.current | curve | render interpolation between ticks |
| Live.hp | curve | render interpolation; legacy dependency |
| Live.life_state | scalar | tick-transition only |
| CommandQueue | curve::Queue | legacy dependent-event propagation (`command_in_queue.cpp`) |
| ActivityRuntime.current_node | curve::Discrete | legacy dependent-event propagation |
| Ownership.owner | curve::Discrete | legacy |
| GathererCargo | scalar | no historical query; no render interp (UI reads current) |
| ResourceNodeState.remaining | scalar | same |
| PlayerResourceLedger | scalar | UI reads current; no interp |
| PopulationState | scalar | tick-transition |
| AttackIntent.cycle state | scalar | tick-transition; in checksum |
| ProductionQueue | scalar (ordered) | tick-transition |

### 4.8 PhaseWorkQueue checksum strategy

Each work queue declares `in_checksum` (yes/no) per the 2.10 table. Queues marked `in_checksum = yes` are serialized into the tick checksum in their canonical-sorted form (stable ordering). Queues with `cross-tick retention = yes` (PathRequestQueue in-flight, ProjectileSpawnRequestQueue in-flight) are also serialized so mid-search state is replay-identical. Queues with `must-be-empty at tick end = yes` are asserted empty at Phase 12 (a non-empty queue at checksum is a determinism bug -> test failure).

### 4.9 Combat attack-cycle state migration

`AttackIntent` persistent state (AttackCycleState, state_until_tick, attack_generation) is new sim state. Stored in a system-owned `std::vector<AttackIntent>` sorted by attacker_id (deterministic iteration). It is a tick scalar, included in the checksum, serializable. The Activity/CombatSystem split: Activity enqueues StartAttackRequest and waits only on AttackNeedsReposition/AttackCancelled/TargetInvalidated/AttackerDied; CombatSystem owns the repeating cycle and emits DamageRequest/ProjectileSpawn at AttackPoint WITHOUT waking the Activity.

### 4.10 Damage phase internal commit order

Phase 7 sub-steps 7.1-7.6 (2.12) define read-your-writes: ResolveDamage (7.2) reads committed profiles; CommitLiveMutations (7.3) writes Live.hp via WorldMutationBuffer; DetectDeaths (7.4) reads the FINAL committed HP (post-7.3, not stale); CommitLifeStateTransitions (7.5) writes life_state; EmitDamageAndDeathEvents (7.6) emits after all commits. Multiple hits same target same tick: applied in 7.1 sort order `(target, source, attack_id, hit_sequence)`; DamageApplied per hit; EntityDied once; once Dying, v1 rule = no further damage; victim active intents cancelled in Phase 7.

### 4.11 Resource node multi-gatherer atomic allocation

In Phase 8, gather intents on one node are sorted `(resource_node_id, gatherer_entity_id, gather_intent_id)` and allocated sequentially: `actual_amount = min(gather_rate, cargo_remaining_capacity, node_remaining)`. ResourceGathered records ACTUAL amount. This prevents over-gather (node_remaining decremented before the next gatherer reads it). Deposit is an atomic Economy transaction (GathererCargo.amount -= deposited AND PlayerResourceLedger[type] += deposited commit together).

### 4.12 Production population reservation

`PopulationState{used, reserved, capacity}`. Enqueue atomic transaction (4.1 production_command_handler): validate `used + reserved + unit_population_cost <= capacity`, then deduct resources + `reserved += unit_population_cost` + append queue item (all-or-nothing). Spawn success: `reserved -= cost; used += cost`. Cancel: `reserved -= cost` + refund resources (ratio defined below). Edge cases to define in implementation:
- **Cancel refund ratio**: v1 = full refund (cancel anytime before spawn).
- **Building destroyed with queued items**: full refund of all queue items + release reserved population.
- **Spawn point blocked**: item stays ReadyToSpawn, retry next tick (no UnitProduced emitted); define `max_spawn_retries` -> refund + TaskFailed if exhausted.
- **Population capacity drops during training** (e.g. house destroyed): if `used + reserved + ongoing > new_capacity`, the lowest-priority queue items are cancelled + refunded (define deterministic selection: oldest producer_id first).

### 4.13 Spawn placement failure and retry

Phase 11 DeferredEntityCommit attempts spawn commit for each ReadyToSpawn item in `(reserved_entity_id)` order. Success -> create entity, reserved->used, remove queue item, enqueue EntitySpawned+UnitProduced (scheduled_tick=T+1). Failure (spawn point blocked by unit/terrain) -> item stays ReadyToSpawn, NO UnitProduced emitted, retry deterministic spawn placement next tick. After `max_spawn_retries`, refund + TaskFailed (production item fails). Deterministic spawn-point selection (not container order): rotate around the building's rally/exit point by a fixed offset.

### 4.14 RawInputBuffer and simulation ingress thread boundary

Player input arrives on the render/input thread (platform event loop), OUTSIDE the DeterministicSimulationLoop. `RawInputBuffer` / `PlayerCommandSource` is the thread boundary: input is pushed from the input thread and snapshotted in Phase 1. The simulation NEVER reads wall-clock input timestamps; once real input is assigned to tick T (drained in Phase 1), T's commands are written to the command log. Thread safety: RawInputBuffer is a single-producer (input thread) / single-consumer (sim Phase 1) queue with a snapshot copy. AI and Scenario issue directly in Phase 10 (same thread as sim), so no thread boundary for them.

### 4.15 Activity-driven vs direct-system command classification

| Command | Routing | Rationale |
|---|---|---|
| Move, Attack, Gather, Build | Activity (SubmitXRequest -> WaitEvent) | entity-level behavior with phases (move-then-act) |
| Train, Research, SetRallyPoint | direct system-owned queue (Phase 2 handler) | continuous system state, no per-entity control flow |
| StopProduction, CancelProduction | operates ProductionQueue (Phase 2) | queue mutation |
| Stop, Idle | Activity interrupt (CommandPriority vs ActivityInterruptPolicy) | cancels current activity |
| Scenario world effect (Spawn/Diplomacy/Reveal/Victory/Objective) | ScenarioEffectPipeline | non-unit, but still validation/typed/deferred/replay |
| System (death/despawn internal) | SystemCritical, non-rejectable | internal flow |

### 4.16 Replay divergence tests per vertical slice

Each vertical slice has a dedicated replay divergence test (beyond S7 generic):

| Slice | Replay test | Divergence assertion |
|---|---|---|
| Move | record MoveCommand log, replay | identical Position.current at every tick; identical PathReady order; identical movement_generation sequence |
| Attack | record AttackCommand log, replay | identical DamageApplied sequence (amounts + order); identical EntityDied timing; identical AttackCycleState transitions |
| Gather | record GatherCommand log, replay | identical ResourceGathered ACTUAL amounts; identical node_remaining depletion sequence; identical PlayerResourceLedger at every tick |
| Production | record TrainCommand log, replay | identical entity_id allocation; identical spawn commit timing; identical PopulationState transitions; identical refund events |
| Interrupt/edge | record mixed log with target-death-mid-gather, repath, cancel | identical ActivityTaskResult sequence; identical MovementCancelled vs stale-PathReady-drop distinction; identical TaskInterrupted{TargetInvalidated} timing |

All replay tests assert: identical state checksum at every tick (Phase 12), rejected commands reproduced, divergence audit (CommandResultLog comparison) passes.

### 4.17 Acceptance criteria (from the brief, mapped)

- Activity Graph and gameplay rules have no responsibility confusion (RunSystem only orchestrates; phase systems compute; Activity does not wait per-AttackPoint). - 1.2, 2.2, 2.11, 3.2
- Damage does not depend on a generic attribute-change listener (`on_attribute_change` removed; DamageSystem/DeathSystem explicit 7.1-7.6). - 2.12, 3.2, 4.2
- Player, AI, Scenario use the same command pipeline (Scenario non-unit effects use ScenarioEffectPipeline but still validation/typed/deferred/replay). - 1.2, 2.4, 3.5, 4.15
- New behavior does not require editing a growing central switch (two switches deleted; registries + variants + PhaseWorkQueue). - 4.2
- Interrupt, target-death, path-failure, insufficient-resources, production-cancel paths have defined results (ActivityTaskResult variant; stale PathReady dropped not MovementCancelled; spawn-point retry). - 2.3, 3.x, 4.4 S8, 4.13
- Identical command log + identical initial state -> identical checksum (per-tick; work queues in-checksum serialized). - 4.4 S7, 4.8, 4.16
- Architecture extends to projectiles, tech, garrison, formations, diplomacy, more missions without core rewrite. - 4.6
