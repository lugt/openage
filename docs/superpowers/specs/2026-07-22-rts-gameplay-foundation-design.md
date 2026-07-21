# openage RTS Gameplay Foundation - Design Spec

**Date**: 2026-07-22
**Status**: Final (consistency-hardened, locked for implementation)
**Supersedes/extends**: `~/.claude/plans/peaceful-chasing-mango.md` Phase A-C architecture assumptions
**Scope**: A reusable, deterministic, AoE2:DE-style RTS gameplay runtime foundation built on openage's existing `activity/`, event, GameEntity, component and system code. Not a full ECS rewrite. Architecture is locked: SimLoop-driven, fixed-timestep, async pathfinding. (Options B/C - EventLoop-as-driver and synchronous Move - are explicitly rejected.)

**Position in the roadmap**: spec 1 of 3 for the William Wallace campaign:
1. **(THIS SPEC)** RTS gameplay foundation - shared infrastructure (tick SimLoop, registries, typed events, command pipeline, pathfinding/locomotion, combat/damage/death, gather/build/production, AI, scenario runtime, replay/checksum). Deliverable = a **Mission-1-equivalent mechanics-complete headless/tutorial harness** verifying gather, objective, AI, scenario effect, and replay. It is NOT the formal Mission 1 scenario.
2. Mission 1 scenario spec - concrete map, dialogue, unit roster, trigger wiring (formal Mission 1).
3. Missions 2-5 roadmap - per-mission incremental mechanics (each adds one system/ability; no core rewrite).

Grounded in a 5-way audit of the current codebase (activity framework, event system, pathfinding/move, components/systems, command/determinism). Each section cites **reused** vs **new**.

---

## 0. Architecture locking summary (confirmed decisions)

1. **SimLoop is the sole gameplay driver** (mutually exclusive RuntimeMode, see 2.16). EventLoop demoted to a non-authoritative scheduling primitive + curve dependency propagation.
2. **`tick_id_t` (uint64) is the sole authoritative gameplay time**, 50 ms / 20 Hz (`TICK_DURATION = 50'000'000 ns`). No wall-clock or floating sim time drives gameplay.
3. **Activity only orchestrates; continuous gameplay is advanced by phase systems.**
4. **Commands and ScenarioEffects execute at T+1** (issuer observes at T, `execute_tick = T+1`).
5. **Domain events are sorted and dispatched in one phase** (Phase 9); Activities wake on the **next tick**.
6. **Both growing central switches are deleted** (`send_command.cpp:67` by command_t, `activity.cpp:127-143` by system_id_t).
7. **Entity iteration, RNG, fixed-point, command/effect log, replay and checksum** are all part of the foundation refactor.
8. **No dual authoritative state**: deterministic runtime state is authoritative; legacy curves survive only as read-only compatibility mirrors.

---

## 1. Layered architecture and dependency graph

### 1.1 Dependency layers (top -> bottom, acyclic, single direction)

```
ISSUERS
  Player Input │ Scripted AI │ Scenario Runtime
        │              │
        │              │ unit behavior: Command Pipeline
        │              │ non-unit behavior (SpawnEntity, ChangeDiplomacy,
        │              │   RevealArea, DeclareVictory, ObjectiveState):
        │              │   ScenarioEffectPipeline (deterministic, separate)
        ▼              ▼
COMMAND PIPELINE / SCENARIO EFFECT PIPELINE
  CommandIngressQueue  +  ScenarioEffectIngressQueue
  -> canonicalization
  -> validation/arbitration
  -> per-entity operation  (or per-effect operation)
  -> stage CommandAuditRecord / ScenarioEffectAuditRecord
  -> Phase 12 commit CommandLog/CommandResultLog + EffectLog
  [Header carries IssuerRef + issued_tick + execute_tick + issuer_sequence]
  CommandPriority:    Background / Normal / Forced / SystemCritical
  ActivityInterruptPolicy: Interruptible / ForcedOnly / SystemCriticalOnly
        │
        ▼
DETERMINISTIC SIMULATION LOOP  (principle #12)
  fixed-timestep tick, 14 frozen phases (see 1.3)
  wall-clock only decides how many sim ticks a render frame runs;
  render interpolates between snapshots, never writes back gameplay state
        │
        ├──────────────────┐
        ▼                  ▼
GAMEPLAY PHASE SYSTEMS     ACTIVITY GRAPH (control flow, principle #1,#2)
  (PhaseSystemScheduler      nodes: Sequence/XorGate/Condition/RunSystem/
   registers; run per        WaitEvent/WaitUntil/Loop/Transition/Interrupt/End
   TickPhase; continuous)    ActivityRuntimeState: root_graph + frame stack
  PathfindingSystem           + params + wait_state + generation (SCALAR, authoritative)
  LocomotionSystem                  │ RunSystem node -> ActivityTaskRegistry
  MovementResolutionSystem          │   -> ActivityTaskResult (variant)
  ProjectileSystem                 ▼   decides branch
  DamageSystem / DeathSystem
  ProductionSystem / EconomySystem
  BuildSystem / DeferredEntityCommitSystem
        │ read/write (field-level writer matrix, see 1.5)
        ▼
COMPONENTS (authoritative passive state; curve OR scalar per 4.7, principle #3)
  Position(curve) │ Live(curve HP, scalar life_state) │
  CommandQueue(scalar authoritative; legacy curve::Queue = read-only mirror) │
  ActivityRuntime(scalar frame stack; legacy current_node curve = read-only mirror) │
  GathererCargo + ResourceNodeState (scalars) │
  PlayerResourceLedger (array, scalar) │ PopulationState (scalar) │
  ProductionQueue │ Ownership
  NO embedded rule logic; NO on_attribute_change callback table (removed)
        │ curve changes(at) dependency propagation = LEGACY PRIMITIVE, NON-authoritative,
        │ one-way observer only (legacy callbacks MUST NOT mutate gameplay state)
        ▼
LEGACY EVENTLOOP (demoted, RuntimeMode::LegacyDemo only)
  + AUTHORITATIVE TypedEventQueue (independent of Legacy EventLoop, see 2.6)
```

### 1.2 Dependency boundary rules

- **Scenario Runtime boundary is four-way**:
  - Unit behavior (move/attack/gather/build/train commands): **Command Pipeline** (same as Player and AI).
  - Non-unit behavior (SpawnEntity, ChangeDiplomacy, RevealArea, DeclareVictory, ObjectiveState): **ScenarioEffectPipeline** - independent, deterministic, full T+1 ingress/validation/commit/log timing (see 1.3 phases 1/2). **ScenarioEffect never mutates Components directly.** `ShowDialogue` is a presentation effect: its occurrence is logged, but it does NOT modify authoritative gameplay state.
- **AI only uses the Command Pipeline.** AI never knows Activity internal nodes and never bypasses resource/pop/cooldown/path validation.
- **Activity does not compute rules.** A RunSystem node only calls an ActivityTaskRegistry handler and branches on ActivityTaskResult.
- **Components contain no gameplay mutation rules.** All authoritative field writes go through a declared owner system or deterministic mutation pipeline (field-level writer matrix, 1.5).
- **EventLoop no longer drives gameplay main advance.** SimLoop is the main driver.

### 1.3 Frozen tick phases (14 phases, central definition, NOT registry order)

Phase 1 and Phase 2 process BOTH Command and ScenarioEffect. Renamed to reflect combined intent:

```
run_tick(T):
  Phase 0  BeginTick                 (NOT current_tick++ here)
  Phase 1  InputAndIntentIngest       drain CommandIngressQueue + ScenarioEffectIngressQueue,
                                        canonical sort
  Phase 2  IntentValidationAndArbitration  validate + queue-placement + CommandPriority-vs-
                                        ActivityInterruptPolicy; ScenarioEffect validate/canonicalize;
                                        ProductionCommandHandler atomic enqueue here
  Phase 3  ActivityEvaluation         advance graphs (microstep<=32, control-only;
                                        wait nodes do NOT wake here)
  Phase 4  Pathfinding                request_id round-robin, fixed expansion quantum
                                        (advances PathSearchStateStore, NOT a work queue)
  Phase 5  LocomotionAndSpatial      LocomotionSystem per-tick advance +
                                        MovementResolutionSystem result
  Phase 6  CombatAndProjectile       substeps 6.1 CombatCycle / 6.2 CommitProjectileSpawns /
                                        6.3 AdvanceExistingProjectiles / 6.4 DetectImpacts
  Phase 7  DamageAndDeath            substeps 7.1-7.6 (see 2.12)
  Phase 8  GatherBuildProduction     gather/build/train progress (NO entity_id allocation here)
  Phase 9  DomainEventDispatch       6-tuple sort, dispatch; mark Activity wait satisfied
                                        (wake next tick T+1)
  Phase 10 ScenarioAndAIObservation  AI/Scenario observe domain events, issue commands/effects
                                        (execute_tick = T+1); apply diplomacy/objective mutations
  Phase 11 DeferredEntityCommit     canonical-sort SpawnRequests -> allocate entity_ids ->
                                        commit spawn; despawn commit (DespawnRequestQueue)
  Phase 12 ChecksumAndReplayRecord   state checksum + flush CommandLog/CommandResultLog/EffectLog
  Phase 13 EndTick                   finalize tick T; current_tick = T + 1
```

`current_tick = T+1` is assigned in **Phase 13 EndTick only**, never in BeginTick, to avoid off-by-one for tick 0, first command, and checksum.

**ScenarioEffect timing (locked)**:
```
Phase 10 (T):  Scenario issues effect with execute_tick = T+1
Phase 1 (T+1): drain ScenarioEffectIngressQueue
Phase 2 (T+1): validate + canonicalize ScenarioEffect
owning phase:  apply diplomacy/objective/etc. mutation (Phase 10 for diplomacy; Phase 11 for Spawn/Despawn)
Phase 11:      commit SpawnEntity/DespawnEntity effects
Phase 12:      effect log + checksum
```

### 1.4 Authority invariants

- Command uses `execute_tick`. Event uses `scheduled_tick`. Component timeline writes authoritative state only at tick boundaries. Gameplay timers use integer ticks.
- wall-clock only decides how many sim ticks a render frame executes. Simulation lag does NOT skip ticks; it delays display or caps per-frame catch-up.
- Render may interpolate between two sim snapshots; interpolated results never write back gameplay state.

### 1.5 Field-level component writer matrix

"Systems are the sole writer of Components" is too coarse - Position is legitimately written by spawn, teleport, knockback, scenario placement, save/load restore, deferred commit. The correct constraint:

> Components contain no gameplay mutation rules; all authoritative field writes go through a declared owner system or deterministic mutation pipeline. Mutations first enter a `WorldMutationBuffer` and commit at a fixed phase position. Arbitrary systems must not obtain unrestricted mutable component pointers.

| Component / field      | Allowed writers                                                  |
| ---------------------- | --------------------------------------------------------------- |
| Position.current       | LocomotionSystem, DeferredEntityCommitSystem, TeleportSystem    |
| Live.hp                | DamageSystem, HealSystem, DeferredEntityCommitSystem            |
| Live.life_state        | DeathSystem, DeferredEntityCommitSystem                       |
| PlayerResourceLedger   | EconomySystem, Spawn/Scenario initialization                    |
| PopulationState        | ProductionCommandHandler, DeferredEntityCommitSystem          |
| ProductionQueue        | ProductionCommandHandler, ProductionSystem                     |
| ActivityRuntime        | ActivitySystem, CommandArbitrationSystem, DeathSystem          |
| CommandQueue           | CommandPipeline (ingest/arbitration), ScenarioEffect (effects)  |
| GathererCargo          | GatherSystem (via GatherAllocationTransaction)                |
| ResourceNodeState      | GatherSystem (via GatherAllocationTransaction)                 |

---

## 2. Core interface definitions

### 2.1 SimulationClock + stable id types

```cpp
using tick_id_t = uint64_t;                                    // authoritative time

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

**Stable id types** (distinct StrongId aliases; all assigned from explicit constants / generated schema / stable data ids, NEVER from runtime registration call order; duplicate id -> freeze failure):

```cpp
using tick_id_t        = uint64_t;
using SystemId          = StrongId<struct SystemTag,          uint32_t>;
using ActivityTaskId    = StrongId<struct ActivityTaskTag,    uint32_t>;
using EventTypeId       = StrongId<struct EventTypeTag,      uint32_t>;
using EventId           = StrongId<struct EventIdTag,         uint64_t>; // assigned POST-sort
using PathRequestId     = StrongId<struct PathRequestTag,    uint64_t>;
using AttackId          = StrongId<struct AttackTag,          uint64_t>;
using GatherIntentId    = StrongId<struct GatherIntentTag,    uint64_t>;
using ProductionItemId  = StrongId<struct ProductionItemTag,  uint64_t>;
using MovementGenerationKey = StrongId<struct MovementGenTag, uint64_t>;
using entity_id_t      = StrongId<struct EntityTag,         uint64_t>;
using activity_id_t     = StrongId<struct ActivityTag,       uint32_t>;
using node_id_t        = StrongId<struct NodeTag,            uint32_t>;
using player_id_t      = StrongId<struct PlayerTag,         uint32_t>;
using ResourceTypeId    = StrongId<struct ResourceTypeTag,  uint16_t>;
using CommandConditionId = StrongId<struct CommandCondTag,   uint32_t>;
using IssuerStableId    = StrongId<struct IssuerTag,         uint64_t>;
```

**Two distinct generation fields** (do not conflate):
- `activity_generation` (ActivityRuntimeState) - bumped on Activity cancel/replacement; `EventWaitSpec.activity_generation` must match for an event to wake that Activity.
- `movement_generation` (PathRequest) - bumped on new Move/Repath; PathReady carries it; LocomotionSystem validates it. Independent of activity_generation.

### 2.2 Two separate registries

#### PhaseSystemScheduler - per-tick fixed continuous systems

```cpp
struct PhaseSystemDescriptor {
    SystemId id;                        // explicit constant, stable
    TickPhase phase;
    uint32_t stable_order;              // explicit order WITHIN a phase
    std::vector<SystemId> runs_after;   // optional dependency, topo-resolved at freeze
};
class PhaseSystemScheduler {
public:
    void register_system(PhaseSystemDescriptor, std::unique_ptr<IPhaseSystem>);
    void freeze();                      // topo-sort by runs_after; tie-break by (stable_order, SystemId);
                                        // duplicate id or dependency cycle -> freeze FAILS; single canonical order
    void run_phase(TickPhase phase, SimulationContext &ctx);
};
```
Includes: PathfindingSystem, LocomotionSystem, MovementResolutionSystem, ProjectileSystem, DamageSystem, DeathSystem, ProductionSystem, EconomySystem, BuildSystem, DeferredEntityCommitSystem. Within a phase, order is `(stable_order, SystemId)` after topo resolution; multiple legal topo orders still yield one result. **Registration call order is never relied upon.** Registry manifest/hash written to replay header (2.17).

#### ActivityTaskRegistry - Activity RunSystem node handlers

```cpp
using ActivityTaskId = StrongId<struct ActivityTaskTag, uint32_t>;  // explicit constant, stable
class ActivityTaskRegistry {
public:
    void register_handler(ActivityTaskId, ActivityTaskHandler);  // duplicate id -> freeze FAILS
    ActivityTaskResult execute(ActivityTaskId, entity_id_t,
                                const ActivityTaskParams&, ActivityExecutionContext&) const;
    void freeze();
};
```
Includes: ValidateTarget, SubmitPathRequest, StartMovementRequest, SubmitAttackRequest, StartGatherRequest, StartBuildRequest. Replaces `activity.cpp::handle_subsystem`. NOT merged with PhaseSystemScheduler.

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
struct TaskWaitForEvent { EventWaitSpec wait_spec; };   // supports WaitAny, see 2.6
struct TaskFailed { ActivityTaskFailure reason; };
struct TaskInterrupted { ActivityInterruptReason reason; };
using ActivityTaskResult = std::variant<
    TaskCompleted, TaskWaitUntil, TaskWaitForEvent, TaskFailed, TaskInterrupted>;
```

### 2.4 Command - variant with header, no circular id

**Command identity is NOT "assigned after sort then used to sort" (circular).** `RootCommandId` is determinable at issue time; per-entity `EntityCommandId` is derived by canonicalization.

```cpp
enum class IssuerKind : uint8_t { Player, AI, Scenario, System };

struct IssuerRef {
    IssuerKind kind;
    IssuerStableId stable_id;              // NOT player_id; Scenario/System have no player
    std::optional<player_id_t> acting_player;  // used for OWNERSHIP validation only
};

struct RootCommandId {
    IssuerKind kind;
    IssuerStableId issuer;                  // = IssuerRef.stable_id
    uint64_t issuer_sequence;               // per-issuer deterministic increment
};
struct EntityCommandId { RootCommandId root; entity_id_t subject; };
```

`IssuerKind::System` is reserved ONLY for internal control instructions that genuinely must enter arbitration (death/despawn are NOT modeled as commands - they use system mutation/work items; see 4.15).

```cpp
enum class QueuePlacement : uint8_t { Replace, Append, Prepend };  // placement only
struct CommandExecutionCondition {                              // condition is orthogonal to placement
    CommandConditionId id;                                       // registered, schema-defined
    TypedConditionParams params;                                 // NO lambda / std::function
};
enum class CommandPriority : uint8_t { Background, Normal, Forced, SystemCritical };
enum class ActivityInterruptPolicy : uint8_t { Interruptible, ForcedOnly, SystemCriticalOnly };

struct CommandHeader {
    IssuerRef issuer;
    tick_id_t issued_tick;                  // T
    tick_id_t execute_tick;                 // T + configured input delay (default T+1)
    QueuePlacement placement;               // Replace/Append/Prepend
    std::optional<CommandExecutionCondition> condition;  // set iff conditional
    CommandPriority priority;
    RootCommandId root_id;                   // determinable at issue time
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

**QueuePlacement semantics**:
- `Replace`: when interrupt policy allows, cancel current Activity, clear pending queue, execute new command.
- `Append`: do NOT interrupt current Activity; add to queue tail.
- `Prepend`: insert at pending-queue head; whether it interrupts current Activity is decided by `CommandPriority` vs `ActivityInterruptPolicy` (placement does not override priority).
- `Condition`: evaluated in Phase 2 at execute_tick using current authoritative state; false -> explicit rejection/skip result (logged).

**Arbitration** (incoming `CommandPriority` vs current `ActivityInterruptPolicy`):
- `Background` interrupts only `Interruptible` with nothing else pending.
- `Normal` interrupts `Interruptible`.
- `Forced` interrupts `Interruptible` and `ForcedOnly`.
- `SystemCritical` interrupts everything.

`issuer_kind_order` is part of the canonical sort key but does NOT implicitly decide authority; authority is ownership validation + explicit Forced/ScenarioOverride.

**`ActivityInterruptPolicy` authoritative source (locked)**: stored on the **current Activity root/frame metadata** (the Activity graph declares each node's interrupt policy), entered into checksum. CommandArbitration reads it from there - it does NOT guess from implicit node types.

**Canonical sort keys** (NO post-sort-assigned id):
```
incoming root commands:   (execute_tick, issuer_kind_order, issuer_stable_id, issuer_sequence)
per-entity operations:    (execute_tick, issuer_kind_order, issuer_stable_id, issuer_sequence, subject_entity_id)
```

### 2.5 CommandIngressQueue vs CommandLog (separated)

Live simulation does NOT drain the command log. The Command Log is an append-only replay artifact, flushed in **Phase 12 only** (single authoritative write point). Player input arrives outside the simulation.

```
Player input  -> RawInputBuffer / PlayerCommandSource  (OUTSIDE sim, input thread)
AI            -> (Phase 10 observation)
Scenario unit-behavior -> (Phase 10 observation)
                 |
                 v  CommandPipeline::issue()
                 v  CommandIngressQueue
  Phase 1   canonical sort (root sort key above)
  Phase 2   validation/arbitration -> per-entity operation in id order
            -> Accepted/Rejected result (CommandResultLog entry STAGED)
  Phase 12  flush CommandLog + CommandResultLog (single authoritative write)
```

Replay: `ReplayCommandSource` reads CommandLog -> fills CommandIngressQueue in Phase 1. Simulation NEVER reads wall-clock input timestamps.

Sort key never depends on thread arrival or global atomic increment. Global `command_seq` is **only a log index**, never the sim sort key.

### 2.6 TypedEventQueue - independent of Legacy EventLoop; WaitAny

TypedEvent does NOT wrap the existing EventLoop (strings/`std::any`/legacy time/heap insertion cannot be authoritative truth).

```
Authoritative TypedEventQueue
  |- DomainEventDispatch (Phase 9) uses it
  |- Activity WaitEvent uses it
  |- Replay/checksum use it
  +- one-way bridge to Legacy EventLoop / UI observer
Legacy EventLoop  -> curve compatibility + legacy callbacks (NON-authoritative observer; RuntimeMode::LegacyDemo only)
```
Authoritative events MUST NOT enter Legacy EventLoop first. Legacy callbacks MUST NOT mutate gameplay state (one-way observer only).

```cpp
using EventPayload = std::variant<
    PathReady, PathFailed, MovementArrived, MovementBlocked, MovementNeedsRepath, MovementCancelled,
    AttackNeedsReposition, AttackCancelled, AttackCommitted, AttackPointReached,
    ProjectileImpact, DamageApplied, DamageIgnored, EntityDied,
    ResourceGathered, ResourceDeposited, ResourceDepleted,
    ProductionTimerCompleted, EntitySpawned, UnitProduced, ProductionFailed,
    BuildingCompleted, BuildingCancelled>;
struct EventEnvelope { EventHeader header; EventPayload payload; };  // variant, no subclass
```

**EventWaitSpec supports WaitAny** (Activities wait on OR-sets like `PathReady | PathFailed`):
```cpp
using CorrelationKey = std::variant<PathRequestId, AttackId, GatherIntentId,
                                     ProductionItemId, MovementGenerationKey>;
struct EventMatchSpec {
    EventTypeId type;
    std::optional<entity_id_t> source;
    std::optional<entity_id_t> target;
    CorrelationKey correlation;
};
struct EventWaitSpec {
    std::vector<EventMatchSpec> any_of;   // canonical sorted; matches if ANY fires
    uint32_t activity_generation;          // stale-event guard
};
```
`EventId` is assigned AFTER canonical sort (post-sort), or excluded from sim ordering.

### 2.7 EventOriginPhase + strict total order

All events dispatch in Phase 9. `EventPhase` is renamed to the **producing** phase (`EventOriginPhase`):
```cpp
enum class EventOriginPhase : uint8_t {
    Pathfinding, LocomotionAndSpatial, CombatAndProjectile,
    DamageAndDeath, GatherBuildProduction, DeferredEntityCommit
};
```
Strict total order key (6-tuple, avoids same-source+type local_sequence=0 collision):
```
( scheduled_tick,
  origin_phase,
  producer_system_stable_order,
  source_entity_id,
  event_type_id,
  producer_local_sequence )
```
`producer_local_sequence` allocated by the centralized phase event buffer in deterministic producer order.

### 2.8 ActivityRuntimeState - scalar frame stack (authoritative), legacy curve = mirror

A single `current_node` cannot hold Sequence/Loop/sub-Activity state. Explicit runtime frame stack. **Authoritative state is scalar; the legacy `current_node` curve survives only as a read-only compatibility mirror (no dual authority).**

```cpp
struct ActivityFrame {
    activity_id_t graph_id;
    node_id_t node_id;
    uint32_t child_index;
    uint32_t loop_iteration;
};
struct EventWaitState { EventWaitSpec spec; /* match bookkeeping */ };
struct ActivityRuntimeState {
    activity_id_t root_graph;
    std::vector<ActivityFrame> frames;        // stack (SCALAR authoritative)
    ActivityParamBlock params;               // schema-defined typed data, NO std::any
    std::optional<EventWaitState> wait_state;
    uint32_t generation;                      // bump on cancel/replacement
    ActivityInterruptPolicy interrupt_policy; // from graph metadata, in checksum
};
// LegacyActivityNodeCurve: optional read-only compatibility mirror (non-authoritative)
```
Requirements: graph definition immutable; graph/schema has stable version/hash; replay header includes activity graph data hash; ParamBlock schema-defined (no `std::any`); cancel/replacement bumps `generation`; `microsteps_used` lives in `ActivityExecutionContext` (reset per tick, NOT in save/checksum).

### 2.9 PathRequest + MoveIntoRange math (corrected)

```cpp
struct PathGoal {
    std::variant<scene_pos_t, entity_id_t> target;   // point or dynamic entity
    phys_t acceptance_radius;                         // for MoveIntoRange
};
struct PathRequest {
    PathRequestId id;
    entity_id_t requester;
    uint32_t movement_generation;     // bump on new Move/Repath
    scene_pos_t start;
    PathGoal goal;
    MovementProfileId movement_profile;
    phys_t collision_radius;
    NavRevision nav_revision;        // invalidate path if terrain changed
    tick_id_t submit_tick;
};
```
PathReady/PathFailed carry: request_id, requester, movement_generation, nav_revision, path result.

**MoveIntoRange distance - CORRECT squared formula** (edge-to-edge):
```
center_distance <= source_radius + target_radius + desired_action_range
// squared comparison (fixed-point):
center_distance_squared <= square(source_radius + target_radius + desired_action_range)
```
All values fixed-point with defined overflow/rounding policy. `target_displacement_threshold` stored squared is named `target_displacement_threshold_sq`.

**Movement result states**: `MovementArrived | MovementBlocked | MovementNeedsRepath | MovementCancelled`
- `MovementArrived`: reached goal (point) or within acceptance_radius (MoveIntoRange).
- `MovementNeedsRepath`: dynamic target crossed `RepathPolicy` threshold; Activity re-submits next tick.
- `MovementCancelled`: an **already-active** movement was cancelled (Stop/new command/death/forced interrupt). A stale PathReady does NOT produce this.
- `MovementBlocked`: path blocked.

**Stale result handling**: stale PathReady (old movement_generation, mismatched vs WaitSpec `request_id + movement_generation + activity_generation`) is dropped from the authoritative path (no MovementCancelled); non-authoritative diagnostic trace MAY be emitted.

**RepathPolicy** (don't emit on every coordinate change):
```cpp
struct RepathPolicy {
    phys_t target_displacement_threshold_sq;   // squared, fixed-point
    tick_id_t minimum_repath_interval;
    uint32_t maximum_retries;                   // repath ONLY; not for single-search failure
};
```

**PathFailed conditions** (NOT `maximum_retries * budget`):
- goal/start invalid;
- open set/frontier exhausted (no path);
- deterministic `max_total_expansions` safety cap;
- nav_revision invalidated;
- request explicitly cancelled.

`maximum_retries` is for dynamic-target repath only, NOT for judging a single search's no-path result.

**Synchronous `get_path` (the existing flow-field Pathfinder) usage - LOCKED**: ONLY for `RuntimeMode::LegacyDemo`, offline oracle comparison, and debug diagnostics. It is NOT a headless-test fallback, NOT a replay fallback, NOT a mission-acceptance fallback, NOT a production deterministic runtime fallback. All authoritative headless tests MUST run the real resumable SearchState.

### 2.10 PhaseWorkQueue vs PersistentStateStore (separated)

Work queues ONLY transfer work between phases; they are NOT persistent state. Persistent in-flight state lives in a system-owned `*StateStore`. Cross-tick retention belongs to the store, not the queue.

| Work queue (transient) | Producer | Consumer | Sort key | In checksum | Must-empty @ tick end |
|---|---|---|---|---|---|
| StartMovementRequestQueue | Phase 3 | Phase 5 | requester_id | yes | yes |
| AttackIntentRequestQueue | Phase 3 | Phase 6.1 | attacker_id | yes | yes |
| DamageRequestQueue | Phase 6.2 | Phase 7.1 | (target,source,attack_id,hit_seq) | yes | yes |
| SpawnRequestQueue | Phase 8 / Scenario | Phase 11 | SpawnRequestKey | yes | yes |
| DespawnRequestQueue | Phase 7 / Scenario | Phase 11 | entity_id | yes | yes |
| ResourceTransferRequestQueue | Phase 8 | Phase 8 (Economy) | (gatherer_id, dropoff_id) | yes | yes |

| Persistent state store (cross-tick, serialized+checksum) | Owner |
|---|---|
| PathSearchStateStore | PathfindingSystem (in-flight search state) |
| ProjectileStateStore | ProjectileSystem (in-flight projectiles) |
| PendingSpawnStateStore | DeferredEntityCommitSystem (spawn placement retry state) |

**Work queue rule**: an ActivityTask pushes to a work queue; it does NOT gain Component write access. The owning phase system consumes the queue and writes authoritative state via the writer matrix + WorldMutationBuffer.

**ProductionEnqueueRequestQueue is DELETED** (it contradicted the Phase-2 atomic enqueue). ProductionCommandHandler performs the atomic enqueue transaction entirely within Phase 2's deterministic commit point (see 2.14).

### 2.11 Attack cycle state + DamageRequest work item

Activity only starts/stops an AttackIntent; CombatSystem owns the continuous loop. Activity does NOT wait per-AttackPoint.

```cpp
enum class AttackCycleState : uint8_t { Ready, Windup, AttackPoint, Recovery, Cooldown };
struct AttackIntent {
    AttackId id;
    entity_id_t attacker;
    entity_id_t target;
    uint32_t attack_generation;
    AttackCycleState state;
    tick_id_t state_until_tick;
};
// Cycle: Ready -> Windup -> AttackPoint -> Recovery -> Cooldown -> Ready (repeats)
```
At `AttackPoint`: melee -> write DamageRequest to DamageRequestQueue; ranged -> create ProjectileSpawnRequest (committed in 6.2). MAY emit AttackCommitted/AttackPointReached for animation/audio/Scenario (Activity NOT woken).

Activity waits only on: `AttackNeedsReposition | AttackCancelled | TargetInvalidated | AttackerDied`. CombatResolutionSystem emits `AttackNeedsReposition` when target leaves range; CombatSystem stops the intent; Activity next-tick enters MoveIntoRange.

**Phase 6 substeps (locked, avoids zero-tick projectile flight)**:
```
6.1 CombatCycle              advance AttackIntent cycles; at AttackPoint create DamageRequest / ProjectileSpawnRequest
6.2 CommitProjectileSpawns   new projectiles enter ProjectileStateStore with spawn_tick = T
6.3 AdvanceExistingProjectiles   advance projectiles whose spawn_tick < T (NOT newly spawned this tick)
6.4 DetectProjectileImpacts   impacts -> DamageRequest for next tick's Phase 7
```

`DamageRequest` is a work item, NOT a Domain Event:
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

### 2.12 Damage phase sub-steps - sequential working HP

Phase 7 split into ordered sub-steps. **Resolve per-target with a working HP** (NOT resolve-all-then-detect - that contradicts "Dying => no further damage").

```
7.1 CollectAndSortDamageRequests   drain DamageRequestQueue; sort (target, source, attack_id, hit_sequence)
7.2 ResolveDamage (per target, sequential working state):
      working_hp = committed hp at phase start
      for DamageRequest in canonical order:
          if target already marked lethal:
              emit DamageIgnored{actual=0, reason=AlreadyDying}   // CHOSEN: emit DamageIgnored
          else:
              actual = DamageResolver.resolve(profile, armor, mods)
              working_hp -= actual
              record DamageApplied{actual}
              if working_hp <= 0: mark target pending death (lethal)
7.3 CommitLiveMutations            commit working_hp once per target (WorldMutationBuffer -> Live.hp)
7.4 DetectDeaths                   read FINAL committed HP
7.5 CommitLifeStateTransitions     Alive -> Dying (writer: DeathSystem); cancel victim movement/attack/gather intents
7.6 EmitDamageAndDeathEvents       ordered DamageApplied (+ DamageIgnored) per hit; EntityDied exactly once
```

**Chosen for post-lethal hits**: emit `DamageApplied{actual=0, reason=AlreadyDying}` (i.e. `DamageIgnored`) so the stream is fully auditable. Dying entities take no further HP loss. DeathSystem reads committed HP (post-7.3), not stale.

`max(0, attack - armor)` is a **v1 placeholder ONLY**, behind an extensible resolver:
```cpp
struct DamageResolver {
    fixed_t resolve(DamageProfile, ArmorProfile, CombatModifiers);
    // future: damage class, armor class, bonus damage, minimum damage,
    //         projectile modifier, technology modifier, splash damage
};
```

### 2.13 Resource state (split components, array-backed, NOT FQON-keyed)

```cpp
struct GathererCargo { ResourceTypeId type; resource_amount_t amount; resource_amount_t capacity; };
struct ResourceNodeState { ResourceTypeId type; resource_amount_t remaining; };
struct PlayerResourceLedger { std::array<resource_amount_t, RESOURCE_TYPE_COUNT> amounts; };  // 4: Food/Wood/Stone/Gold
struct PopulationState { population_t used; population_t reserved; population_t capacity; };
```
4 resources -> stable-indexed `std::array`, NOT `unordered_map<FQON, amount>`. Nyan FQON = load-time mapping to stable `ResourceTypeId` only. Future data-driven types use stable ResourceTypeId + sorted flat container + resource registry hash in replay header.

**Curve vs scalar**: do NOT force cargo/ledger into curves. Authoritative fixed-tick component may be a plain deterministic scalar. Curves only for historical query / render interpolation / legacy dependency propagation. See 4.7.

### 2.14 Production queue semantics (no off-by-one, atomic enqueue)

Production queue is a building's continuous system-owned state, NOT a `Train Activity -> Wait`. Direct data flow:
```
TrainCommand -> Phase 2 ProductionCommandHandler
            -> validate ownership / queue capacity / resources / population
            -> atomic enqueue transaction (ALL within Phase 2 deterministic commit point):
                 validate cost / queue capacity / population (used+reserved+cost<=capacity)
                 deduct resources
                 reserve population (reserved += unit_population_cost)
                 append ProductionQueue item (uses ProductionItemId as provisional handle)
            -> ProductionQueue
            -> Phase 8 ProductionSystem continuous advance
```
If any step fails, nothing commits. `ProductionEnqueueRequestQueue` deleted (Phase-2 transaction replaces it).

**Population judgement**: `used + reserved + unit_population_cost <= capacity` (NOT `pop_used + train.queue < pop_cap`).

**Spawn placement (no off-by-one; entity_id allocated in Phase 11, NOT Phase 8)**:
```
T Phase 8:   production timer completes
             enqueue SpawnRequest{SpawnRequestKey} (item -> ReadyToSpawn)
             emit ProductionTimerCompleted (timer done, no entity yet)
             (NO entity_id allocated here)
T Phase 11:  canonical-sort ALL SpawnRequests by SpawnRequestKey
             -> validate placement
             -> allocate entity_ids (in sorted order)
             -> commit spawn (create full entity; reserved -> used; remove queue item)
             -> enqueue EntitySpawned THEN UnitProduced (scheduled_tick = T+1)
             failure (spawn point blocked): item stays ReadyToSpawn in PendingSpawnStateStore;
                       NO UnitProduced; deterministic retry next tick
T+1 Phase 9: EntitySpawned -> UnitProduced dispatched (STRICT order); listeners may read entity
```

**SpawnRequestKey** (deterministic, pre-id):
```
(effect/source kind, owner_player_id, producer_entity_id OR scenario_effect_id,
 production_queue_index, request_sequence)
```

**Population capacity drop (v1 locked, simple)**: reserved items keep their reservation; capacity drop only blocks NEW enqueue; existing Ready/Training items are NOT auto-cancelled; after spawn, `used` may temporarily exceed `capacity`; new production is rejected until `used + reserved <= capacity`. Auto-cancel, if ever needed, must be a data-driven `PopulationOverflowPolicy` (NOT hardcoded "oldest producer first").

**Spawn blocked result** is NOT `TaskFailed`:
```
ReadyToSpawn -> deterministic retry (indefinite by default)
OR SpawnRetryPolicy{max_retries, failure_action} -> on exhaustion emit ProductionFailed{SpawnPlacementUnavailable}
```

**Events distinguished**: `ProductionTimerCompleted` (timer ended, may lack birth position) / `EntitySpawned` (entity entered world) / `UnitProduced` (transaction complete, usable). v1 emits EntitySpawned + UnitProduced together on successful commit, in strict order.

Section 4 defines: cancel refund ratio (v1 = full refund before spawn), building-destruction refund + reservation release, spawn-point-blocked handling, capacity-drop handling (above).

### 2.15 Command/event/effect/work-item schema + registry stability

All registries (SystemId, ActivityTaskId, EventTypeId, ResourceTypeId, CommandConditionId) use **stable IDs from explicit constants / generated schema / stable data ids**, NEVER runtime registration call order. Duplicate id -> freeze FAILS. Registry manifest/hash written to replay header (2.17).

### 2.16 RuntimeMode (mutually exclusive)

```cpp
enum class RuntimeMode { LegacyDemo, DeterministicGameplay, Replay };
```
- `LegacyDemo`: old demo uses legacy runtime; does NOT run deterministic gameplay systems.
- `DeterministicGameplay`: SimLoop is the SOLE gameplay driver; no live player input outside RawInputBuffer.
- `Replay`: SimLoop + ReplayCommandSource; no live player input.
- Two loops MUST NOT simultaneously write the same World/GameState. "SimLoop runs alongside Legacy EventLoop" is forbidden.

### 2.17 Replay Manifest + canonical serializer

```cpp
struct ReplayHeader {
    uint32_t replay_schema_version;
    Hash initial_simulation_manifest_hash;
    Hash game_data_hash;
    Hash map_scenario_id;  // + seed
    Hash activity_graph_hash;
    Hash command_event_effect_workitem_schema_hash;
    Hash system_scheduler_manifest_hash;
    Hash resource_registry_hash;
    std::chrono::nanoseconds tick_duration;
    RNGStreamStates rng_initial_states;
};
```
`CanonicalStateSerializer` -> schema-defined canonical bytes; `StateChecksum` -> hash; `ChecksumSystem` -> Phase 12 orchestration (single serializer, no duplicate impl in `system/checksum.*` + `state_checksum.*`).

**Replay acceptance (corrected)**: NOT "from empty state". Correct condition:
```
identical InitialSimulationManifest + identical data/schema hashes + identical RNG state
+ identical command/effect stream -> identical per-tick checksum
```
Hash mismatch -> refuse to load.

### 2.18 RNG stream contract

NOT one global RNG. Stable stream IDs; per-system / per-player / per-scenario streams; stream state in checksum/replay header; gameplay does not depend on whether another system consumed one more random; entity-level randomness uses stable derived streams; wall-clock seed forbidden; unordered iteration MUST NOT decide RNG consumption order.

---

## 3. End-to-end data flow (per vertical slice)

Tick-T perspective; every phase labeled. Activities wake the tick **after** their wait is satisfied (next-tick wake rule). Pathfinding may take T+k ticks.

### 3.1 MoveTo + MoveIntoRange (principle #8 async pathing)

```
T     Phase 3  Activity SubmitPathRequest -> PathRequestQueue (transient) -> PathSearchStateStore
               WaitForEvent(PathReady | PathFailed)  (WaitAny)
T..T+k Phase 4  PathfindingSystem: round-robin by request_id; each request fixed expansion quantum;
               total <= global budget; advances PathSearchStateStore (persistent)
T+k   Phase 4  search complete -> emit PathReady / PathFailed
T+k   Phase 9  event dispatch -> matches Activity wait -> mark next-tick runnable
T+k+1 Phase 3  Activity resumes -> enqueue StartMovementRequest -> StartMovementRequestQueue
               WaitForEvent(MovementArrived | MovementBlocked | MovementNeedsRepath | MovementCancelled)
T+k+1 Phase 5  MovementIntentSystem consumes StartMovementRequest; LocomotionSystem advances
               every active intent one deterministic step; MovementResolutionSystem judges result
```
Stale PathReady (old movement_generation) dropped from authoritative path (no MovementCancelled). MoveIntoRange uses the corrected squared formula (2.9). RepathPolicy for dynamic targets.

### 3.2 Attack -> Damage -> Death (principle #4 explicit damage)

```
T   Phase 3  AttackTarget Activity: ValidateTarget
            - out of range: MoveIntoRange(target, attack_range) sub-activity (3.1)
            - in range: enqueue StartAttackRequest -> AttackIntentRequestQueue
            - WaitForEvent(AttackNeedsReposition | AttackCancelled | TargetInvalidated | AttackerDied)
T   Phase 6  CombatSystem (6.1-6.4): AttackIntent cycle; at AttackPoint melee->DamageRequestQueue,
            ranged->ProjectileSpawnRequest (spawn_tick=T, advanced next tick)
T   Phase 7  (7.1-7.6, sequential working HP, see 2.12)
            - DamageApplied per hit; DamageIgnored for post-lethal; EntityDied once
            - victim intents cancelled in 7.5
            - enqueue despawn -> DespawnRequestQueue
T   Phase 9  DamageApplied/DamageIgnored/EntityDied dispatched
            - attacker Activity: TargetInvalidated -> Interrupt(TaskInterrupted{EntityDied})
T   Phase 10 Scenario/AI read dead entity's read-only FINAL state (this tick only)
T   Phase 11 DeferredEntityCommit: despawn commit; entity leaves active set
            - after Phase 11, entity pointers INVALID; listeners hold entity IDs only
```
Satisfies #4: no on_attribute_change listener; DamageSystem sequential working HP; DeathSystem reads committed HP.

### 3.3 Gather -> Carry -> Deposit (principle #3, multi-gatherer atomic)

```
T   Phase 3  GatherResource Activity: SubmitPathRequest to resource node (MoveIntoRange, 3.1)
T+...        in range -> enqueue StartGatherRequest -> GatherRequestQueue
            - WaitForEvent(ResourceDepleted | CarryFull)
T   Phase 8  GatherSystem: gather timer (ticks)
            - multi-gatherer on one node: intents sorted (resource_node_id, gatherer_entity_id, gather_intent_id)
              GatherAllocationTransaction (ATOMIC): ResourceNodeState.remaining -= actual
                                                    AND GathererCargo.amount += actual
              actual = min(gather_rate, cargo_remaining_capacity, node_remaining)
            - buffer ResourceGathered (ACTUAL amount)
            - node depleted: ResourceDepleted -> repath or fail
            - cargo full: CarryFull -> ReturnResource (MoveIntoRange to dropoff)
in range at dropoff -> Deposit:
            - EconomySystem.deposit(): ATOMIC transaction (ResourceTransferRequestQueue)
                GathererCargo.amount -= deposited  AND  PlayerResourceLedger[type] += deposited
              validation: ownership, dropoff accepts type, in-range, valid cargo/dropoff
            - buffer ResourceDeposited; reset cargo; loop
```
Drop-off "nearest" deterministic tie-break: `(path_cost, geometric_distance, dropoff_entity_id)`.

### 3.4 Build (full vertical slice)

```
BuildCommand
-> Phase 2 validate ownership / placement / resources; atomic resource deduction
-> SpawnFoundationRequest -> Phase 11 foundation commit (foundation entity, Active=Foundation)
-> Build Activity: MoveIntoRange(builders -> foundation)
-> StartBuildRequest -> BuildRequestQueue
-> Phase 8 BuildSystem accumulates fixed-point contribution (per builder, sorted by builder_entity_id)
-> BuildingCompleted -> foundation state transitions Foundation -> Active
```
Defined: placement validation; foundation entity state; multiple builders same-tick contribution order (builder_entity_id); builder interrupted/dead (contribution stops, foundation remains); foundation destroyed (refund per policy); completion exactly once; resource refund policy; collision/nav revision update; replay/checksum.

### 3.5 Production (principle #3 + #11, no off-by-one)

See 2.14 for the full locked flow: Phase 2 atomic enqueue (deduct + reserve pop); Phase 8 timer -> SpawnRequest (no id yet); Phase 11 canonical-sort -> allocate id -> commit; EntitySpawned then UnitProduced scheduled_tick=T+1; spawn-blocked -> PendingSpawnStateStore retry / ProductionFailed.

### 3.6 Command/Effect T+1 + unified issuer (principle #7/#10/#11)

```
EXTERNAL   Player input -> RawInputBuffer / PlayerCommandSource (input thread)
T   Phase 1  InputAndIntentIngest: drain raw input + ScenarioEffectIngressQueue;
              canonicalize selected entity IDs; construct Command; issued_tick=T; execute_tick=T+input_delay
T   Phase 10 AI observes snapshot T -> issue (execute_tick=T+1)
              Scenario observes domain events/state T -> issue command/effect (execute_tick=T+1)
T+1 Phase 1  canonical sort (root key: execute_tick, issuer_kind_order, issuer_stable_id, issuer_sequence)
T+1 Phase 2  IntentValidationAndArbitration: per-entity id-order validation, QueuePlacement,
              CommandPriority vs ActivityInterruptPolicy -> Activity transition (bump generation)
T+1 Phase 3  Activity advances with the new command's intent
```
ScenarioEffect parallel: Phase 10 issue -> Phase 1 (T+1) drain -> Phase 2 validate/canonicalize -> owning-phase apply -> Phase 11 commit (spawn/despawn) -> Phase 12 effect log + checksum.

Eliminates: AI-vs-player ordering (both T+1), Scenario recursive triggers, same-tick system reentry, issuer-order effects. Conflicting commands resolved by canonical arbitration (sort key + CommandPriority vs ActivityInterruptPolicy).

### 3.7 The five authoritative pipelines (locked summary)

```
Move:       Command -> Activity -> PathRequestQueue/PathSearchStateStore -> PathfindingSystem -> PathReady
            -> Activity -> StartMovementRequestQueue -> LocomotionSystem -> MovementResult -> Activity

Attack:     Command -> Attack Activity -> AttackIntentRequestQueue -> CombatSystem (continuous cycle)
            -> DamageRequestQueue | ProjectileStateStore -> DamageSystem -> DeathSystem
            -> DamageApplied/DamageIgnored/EntityDied -> Activity, AI, Scenario

Gather:     Command -> Gather Activity -> Movement -> GatherRequestQueue -> GatherSystem
            -> GatherAllocationTransaction (GathererCargo + ResourceNodeState) -> Deposit transaction
            -> PlayerResourceLedger -> Resource domain events

Build:      Command -> Phase 2 atomic deduction -> SpawnFoundationRequest -> Phase 11 foundation commit
            -> Build Activity -> BuildRequestQueue -> BuildSystem -> BuildingCompleted

Production: TrainCommand -> ProductionCommandHandler (Phase 2 atomic) -> ProductionQueue
            -> ProductionSystem -> SpawnRequestQueue -> Phase 11 (alloc id + commit)
            -> EntitySpawned -> UnitProduced
```

---

## 4. Migration file list, test gates, compatibility

### 4.1 New files (greenfield)

**Sim foundation (`libopenage/gamestate/sim/`)**
- `simulation_clock.{h,cpp}`; `simulation_context.{h,cpp}`; `deterministic_simulation_loop.{h,cpp}`; `tick_phase.h` (14 values); `world_mutation_buffer.{h,cpp}`; `runtime_mode.h`

**Entity store (split from allocator)**
- `entity_id_allocator.{h,cpp}` (allocation in Phase 11 post-sort); `entity_store.{h,cpp}` (hash lookup); `ordered_entity_view.{h,cpp}` (authoritative iteration only)

**Registries**
- `phase_system_scheduler.{h,cpp}`; `activity_task_registry.{h,cpp}`; `system_result.h`; `registry_manifest.{h,cpp}` (stable-id manifest + hash)

**Typed events**
- `typed_event_queue.{h,cpp}`; `event_envelope.{h,cpp}`; `event_origin_phase.h`; `event_ordering.{h,cpp}`; `event_bridge.{h,cpp}`; payloads `*.h`

**Command pipeline**
- `command.{h,cpp}`; `command_pipeline.{h,cpp}`; `command_ingress_queue.{h,cpp}`; `command_log.{h,cpp}`; `command_canonicalizer.{h,cpp}`; `command_arbitration.{h,cpp}`; `raw_input_buffer.{h,cpp}`; `production_command_handler.{h,cpp}`; `replay_command_source.{h,cpp}`

**Scenario effect pipeline**
- `scenario_effect_pipeline.{h,cpp}`; `scenario_effect_ingress_queue.{h,cpp}`; `scenario_effect_log.{h,cpp}`; `scenario_effect_canonicalizer.{h,cpp}`; `scenario_effect_result.{h,cpp}`; `scenario_runtime.{h,cpp}`; `scenario_trigger_types.h`; `scenario_effect_types.h`

**Phase work queues + persistent stores**
- `phase_work_queue.h`; `work_queue_registry.{h,cpp}`; `start_movement_request_queue.*`; `attack_intent_request_queue.*`; `damage_request_queue.*`; `spawn_request_queue.*`; `despawn_request_queue.*`; `resource_transfer_request_queue.*`; `build_request_queue.*`
- `path_search_state_store.{h,cpp}`; `projectile_state_store.{h,cpp}`; `pending_spawn_state_store.{h,cpp}`

**AI**: `scripted_ai.{h,cpp}`

**Phase systems (`libopenage/gamestate/system/`)**
- `pathfinding.{h,cpp}`; `locomotion.{h,cpp}` (fixed-point math); `movement_intent.{h,cpp}`; `movement_resolution.{h,cpp}`; `combat.{h,cpp}`; `combat_resolution.{h,cpp}`; `projectile.{h,cpp}`; `damage.{h,cpp}`; `damage_resolver.{h,cpp}`; `death.{h,cpp}`; `gather.{h,cpp}`; `build.{h,cpp}`; `production.{h,cpp}`; `economy.{h,cpp}`; `deferred_entity_commit.{h,cpp}`

**Resource/population state (`libopenage/gamestate/component/api/`)**
- `gatherer_cargo.{h,cpp}`; `resource_node_state.{h,cpp}`; `player_resource_ledger.{h,cpp}`; `population_state.{h,cpp}`; `resource_type.{h,cpp}`

**Pathfinding refactor**: `search_state.{h,cpp}` (pausable/resumable flow-field)

**Determinism + replay**
- `canonical_state_serializer.{h,cpp}`; `replay_header.{h,cpp}`; `simulation_manifest.{h,cpp}`; `schema_hash.{h,cpp}`; `scheduler_manifest.{h,cpp}`; `divergence_report.{h,cpp}`; `checksum_system.{h,cpp}`; `replay.{h,cpp}`; `deterministic_rng.{h,cpp}` (per-system/player/scenario streams)

### 4.2 Modified files (existing)

- `libopenage/gamestate/simulation.{h,cpp}` - replace wall-clock `reach_time` loop with `DeterministicSimulationLoop::tick`; register phase systems; ordered entity view
- `libopenage/gamestate/system/activity.{h,cpp}` - DELETE central `handle_subsystem` switch (`activity.cpp:123-143`); advance reads scalar ActivityRuntimeState frame stack; next-tick wake rule
- `libopenage/gamestate/event/send_command.cpp` - DELETE command_t switch (`:67`); route via CommandPipeline + canonicalizer
- `libopenage/gamestate/system/move.{h,cpp}` - REWRITE: remove synchronous path-bake; Move is an Activity. `move.cpp:112-167` math reused for SEMANTICS/algorithm structure ONLY; authoritative math migrated to fixed-point in LocomotionSystem (no `double` authoritative)
- `libopenage/gamestate/component/internal/activity.h` - extend to ActivityRuntimeState (scalar frame stack, params, wait_state, generation, interrupt_policy); legacy current_node curve = read-only mirror
- `libopenage/gamestate/component/api/live.{h,cpp}` - REMOVE on_attribute_change callback table + change_callbacks map (`live.h:85-105`); death DeathSystem-driven. Live.hp stays curve (render interp); life_state scalar.
- `libopenage/gamestate/component/api/gather.{h,cpp}` - `carried_amount` -> split GathererCargo + ResourceNodeState (scalars)
- `libopenage/gamestate/component/api/{build,train,production,resource_storage}.{h,cpp}` - fill authoritative state; resource_storage -> PlayerResourceLedger
- `libopenage/gamestate/component/internal/command_queue.{h,cpp}` - deterministic queue authoritative (QueuePlacement + cancel-by-tag); legacy `curve::Queue` = read-only mirror; `command_in_queue` no longer drives authoritative Activity transition
- `libopenage/gamestate/player.{h,cpp}` - resources -> PlayerResourceLedger (array, scalar); add PopulationState
- `libopenage/gamestate/game_state.{h,cpp}` / `libopenage/gamestate/map.cpp` - hash map retained for ID LOOKUP only; authoritative iteration via `ordered_active_ids()` (fixes `map.cpp:24` non-determinism TODO); NO global std::map replacement
- `libopenage/event/event_loop.{h,cpp}` - demote; LegacyEventLoopAdapter (explicit unit-checked tick/time conversion); legacy callbacks one-way observer only
- `libopenage/gamestate/activity/types.h` - extend node_t enum (Sequence/Loop/Transition/Interrupt; Condition folded into XorGate; WaitEvent/WaitUntil as nodes)
- `libopenage/gamestate/entity_factory.cpp` - data-driven activity graph construction (no arbitrary-lambda conditions/tasks); register systems/tasks
- `libopenage/gamestate/api/definitions.h` - ACTIVITY_TASK_SYSTEM_DEFS -> ActivityTaskRegistry registrations
- `libopenage/clock/clock.{h,cpp}` - separate sim clock (tick) from render clock (wall-clock)
- `libopenage/main.cpp` / `libopenage/main.h` - `--tutorial` exists; add `--replay <log>`, `--checksum-every N`, `--input-delay <ticks>`, `--runtime-mode {legacy|deterministic|replay}`

### 4.3 Phase technical risks

**Phase 4 pathfinding (HIGHEST RISK)**: synchronous `get_path` (`pathfinder.cpp:24`) -> pausable/resumable `SearchState`/`PathSearchStateStore`. Migration: (1) extract A* portal + flow-field into stepwise continuation advancing N nodes/call, serializable; (2) sync `get_path` retained ONLY for LegacyDemo/oracle/debug (never authoritative - 2.9); (3) define per-request quantum, global budget, admission limit, round-robin; (4) PathFailed conditions per 2.9; (5) gate behind identical-request-set replay test.

**Activity frame stack**: single `current_node` -> scalar frame stack; serialization shape changes; replay headers carry graph hash; old saves incompatible (acceptable foundation work).

**Legacy EventLoop demotion**: preserve curve dependency propagation; route gameplay events through TypedEventQueue; one-way bridge for UI. RuntimeMode mutual exclusion (2.16).

**Combat cycle state**: per-AttackIntent cycle state is new persistent sim state (in checksum, serializable). Risk: Activity/CombatSystem split (Activity must not wait per-AttackPoint).

### 4.4 Test gates (per stage, runnable; all authoritative tests use resumable SearchState, never sync fallback)

| Stage | Gate | Pass criterion |
|---|---|---|
| **S0** Determinism harness | (A-L below) | see 4.4.1 |
| S1 Move vertical slice | issue MoveCommand, sim N ticks | entity arrives; identical checksum across runs; PathReady->MovementArrived; pathfinding T+k |
| S2 Attack->Damage->Death | attack intent, sim until HP<=0 | AttackCycleState advances; DamageApplied + DamageIgnored + EntityDied; no on_attribute_change (assert absent); despawn committed Phase 11; victim intents cancelled 7.5; no zero-tick projectile flight |
| S3 Gather->Carry->Deposit | gather to capacity, return to TC | multi-gatherer no over-gather (GatherAllocationTransaction atomic); cargo+ledger atomic; ResourceGathered records ACTUAL; ResourceDeposited |
| S4 Build | BuildCommand, sim to completion | placement validated; foundation commit Phase 11; multi-builder contribution order; BuildingCompleted exactly once; refund on foundation destroyed |
| S5 Production | TrainCommand with enough resources | atomic enqueue (deduct+reserve); id allocated Phase 11 post-sort; EntitySpawned strictly before UnitProduced; pop cap -> CommandRejected; blocked spawn -> retry; retry exhausted -> ProductionFailed (NOT TaskFailed); multiple producers same tick; cancellation/refund; producer destroyed; capacity drop; retry does not re-deduct; events exactly once |
| S6 Scripted AI | AI gathers + trains + attacks | AI issues via CommandPipeline only (assert no direct component mutation); execute_tick=T+1 |
| S7 Scenario Runtime | trigger on EntityDied -> SpawnEntity effect | effect via ScenarioEffectPipeline; Phase 1/2/11 timing; deferred commit; checksum stable |
| S8 Replay + checksum | replay from identical InitialSimulationManifest (NOT empty) | identical per-tick checksum; rejected commands/effects reproduced; divergence audit passes; hash mismatch refused |
| S9 Interrupt/edge cases | (layered results, see 4.4.2) | each path yields a LAYERED result per 4.4.2 |
| S10 Mission-1-equivalent foundation harness | `bin/run main --tutorial --headless` (or fixture) | gather 50 wood objective completes via Scenario Runtime; AI active; scenario effect fires; replay passes; exit 0 |

#### 4.4.1 S0 expanded (determinism)
A. empty world 10,000 ticks, multi-run checksum identical
B. headless / 30 / 60 / 144 FPS / multi-tick catch-up identical
C. unordered container reserve/bucket count changed -> identical
D. command ingress arrival order shuffled -> canonical result identical
E. event/work-buffer memory layout changed -> dispatch order identical
F. RNG stream isolation (per-system/player/scenario independent)
G. duplicate registry ID -> freeze FAILS
H. scheduler dependency cycle -> freeze FAILS
I. tick 0 / T+1 / delayed command boundary tests
J. work queue must-empty assertion at Phase 12
K. replay schema/hash mismatch -> refused
L. debug/release fixed-point results identical

#### 4.4.2 S8 layered failure results (NOT all ActivityTaskResult)
| Condition | Result type |
|---|---|
| Move failure | ActivityTaskFailed / movement event |
| Train resources insufficient | CommandRejected |
| Population limit | CommandRejected |
| Production cancelled | ProductionCancelled |
| Spawn blocked | ReadyToSpawn retry |
| Spawn retry exhausted | ProductionFailed{SpawnPlacementUnavailable} |
| Scenario effect invalid | ScenarioEffectRejected |
| stale PathReady | silent authoritative drop + optional diagnostic |

### 4.5 Compatibility with existing code

- `RuntimeMode::LegacyDemo`: old demo keeps legacy runtime; does NOT run deterministic systems (mutual exclusion 2.16).
- `Idle` reused as an **Activity / default no-intent state**, NOT a continuous phase system.
- Existing flow-field Pathfinder: algorithmic core reused in `SearchState`; sync `get_path` ONLY LegacyDemo/oracle/debug.
- Existing components (Position/CommandQueue/Activity/Ownership/Live): kept; Live loses callback table; Gather splits; PlayerResources -> PlayerResourceLedger. No dual authority: deterministic queue + ActivityRuntimeState scalar stack are authoritative; legacy curves = read-only mirrors.
- Existing nyan data (tutorial_data.nyan, mission scenarios, APIScenario): unchanged schema; new systems consume same FQONs.
- Streaming convert output (`/tmp/openage_bfg_lazy256`): orthogonal; consumed once Mission scenarios point at real GameEntity FQONs.

### 4.6 Known limitations and extension points

**Limitations (v1)**
- No projectile visual fidelity (ProjectileSystem advances logically; visual minimal).
- v1 DamageResolver = `max(0, amount - armor)` placeholder (Resistance parsed, not fully consumed).
- No save/mid-tick snapshot (Activity microsteps not in checksum).
- Single AI personality (scripted reactive rules).
- Formal Mission 1 + Missions 2-5 not authored here (spec 2/3).
- No multi-threaded pathfinding (single-threaded round-robin; 4 multi-threading invariants specified for future).

**Extension points (no core rewrite)**
- Projectiles, tech/research (libnyan Transaction; plan A1-A4 wired), garrison/formations/diplomacy (new phase system + Activity sub-graph), more campaign missions (scenario nyan + `create_mission_N_trigger`).

### 4.7 Curve vs tick-state scalar (migration decisions)

Curves ONLY for historical query / render interpolation / legacy dependency propagation.

| State | Type | Reason |
|---|---|---|
| Position.current | curve | render interpolation |
| Live.hp | curve | render interpolation; legacy dependency |
| Live.life_state | scalar | tick-transition |
| CommandQueue | scalar authoritative | deterministic; legacy `curve::Queue` = read-only mirror |
| ActivityRuntime | scalar frame stack | deterministic; legacy current_node curve = read-only mirror |
| Ownership.owner | curve::Discrete | legacy |
| GathererCargo | scalar | no historical query |
| ResourceNodeState.remaining | scalar | same |
| PlayerResourceLedger | scalar | UI reads current |
| PopulationState | scalar | tick-transition |
| AttackIntent cycle state | scalar | tick-transition; in checksum |
| ProductionQueue | scalar (ordered) | tick-transition |

### 4.8 PhaseWorkQueue checksum strategy

Each work queue declares `in_checksum` (per 2.10 table). Queues serialized in canonical-sorted form. Queues with `must-empty at tick end = yes` are asserted empty at Phase 12 (non-empty = determinism bug -> test failure). Persistent stores (`PathSearchStateStore`, `ProjectileStateStore`, `PendingSpawnStateStore`) are serialized+checksummed (in-flight state replay-identical).

### 4.9 Acceptance criteria (from the brief, mapped)

- Activity Graph vs gameplay rules: no confusion (RunSystem only orchestrates; phase systems compute; Activity not wait per-AttackPoint). - 1.2, 2.2, 2.11, 3.2
- Damage no generic attribute-change listener (removed; 7.1-7.6 sequential working HP). - 2.12, 3.2, 4.2
- Player/AI/Scenario same command pipeline (Scenario non-unit effects via ScenarioEffectPipeline, still validation/typed/deferred/replay). - 1.2, 2.4, 3.6, 4.15
- New behavior needs no growing central switch (two deleted; registries + variants + PhaseWorkQueue). - 4.2
- Interrupt/target-death/path-failure/insufficient-resources/production-cancel/spawn-blocked paths have LAYERED defined results. - 2.3, 3.x, 4.4.2
- Identical InitialSimulationManifest + command/effect stream -> identical per-tick checksum. - 4.4 S8, 4.8, 2.17
- Extends to projectiles/tech/garrison/formations/diplomacy/more missions without core rewrite. - 4.6
- 14 phases, no Uninterruptible term, no circular id, ScenarioEffect full T+1 timing, work queue vs persistent store separated, spawn id allocated Phase 11 post-sort, MoveIntoRange correct squared formula, Damage sequential working HP, Build full slice + gate, authoritative tests no sync pathfinder fallback, RuntimeMode mutually exclusive, Replay uses InitialSimulationManifest, no dual authoritative state, stable registry/RNG/schema/scheduler ids+hashes. - throughout
