# Computer player design

The event-driven controller, shared action limits, legal production queries and operation-replay goldens are implemented.
The longer-term design below also describes capabilities that remain to be added; the implemented interface and current limits are listed here.
See [simulation architecture](README.md), [event scheduling](../event_system.md), [input](../input/README.md#ai), [snapshots](../snapshots.md) and [UI portability](../ui_portability.md).

## Running the controller

### Neutral wildlife

Converted original scenarios enable neutral wildlife automatically, independently of strategic player profiles.
Only Gaia-owned animals participate; ownership changes and explicit movement orders take precedence.
Birds fly between map locations and periodically leave an edge, rest outside the map and return.
Deer and herdables alternate short walks with grazing and flee nearby non-Gaia mobile units.
Only wolves initiate attacks, and they select villagers inside the source animal's sight radius without changing Gaia diplomacy.
Other predators retain their existing behavior.

The controller schedules simulation events every half second and limits movement decisions to sixteen animals per event.
It builds a spatial index of nearby people once per event instead of comparing every animal with every entity or running per rendered frame.
Ground animals use the existing collision-aware movement system; flying birds use fixed-point position curves.
Converted animal definitions preserve source movement, idle and death graphics, speed, health, collision extents and food amounts.

Optional `original_scenario.gameplay_metadata_version`, `command_categories` and per-definition `wildlife` fields describe the imported behavior.
`command_categories` maps building FQONs to `economic` or `military`.
Each `wildlife` object contains `kind` (`bird`, `prey`, `herdable` or `wolf`) and positive `speed`; the definition's existing `sight` supplies the detection range.
Older packs without these optional fields keep their previous behavior until reconverted.
The campaign launcher refreshes outdated converted missions from their cached parsed originals when available.
Wildlife decisions are deterministic for an unchanged initial entity set; loading a save rebuilds their idle decisions rather than restoring an exact AI checkpoint.

`bin/run test testing::wildlife_neighbors` checks spatial boundaries, sight limits, deterministic ties and villager filtering without assets.
`bin/run test testing::wildlife_behavior` uses converted assets to check fleeing, flight out of and back into the map, neutral wolf combat, captured herdables and cancellable building repair without a renderer.
`bin/run test -b testing::wildlife_spatial` measures index construction and 200 local queries across 14,053 synthetic people; it does not measure pathfinding or rendering.

### Strategic players

AI is disabled by default.
After preparing a scenario, pass `bin/run main --scenario --no-convert --no-prompts --ai-config /path/to/ai.json`.
The JSON file contains an optional positive match-wide `population_limit` and a `players` array:

```json
{
  "population_limit": 200,
  "players": [
    {
      "player": 1,
      "difficulty": "moderate",
      "seed": 7,
      "economy": true,
      "construction": true,
      "research": true,
      "scouting": true,
      "defense": true,
      "attacks": false,
      "retreat": true,
      "workers": 20,
      "army": 12,
      "protected_instances": []
    }
  ]
}
```

Player IDs are runtime IDs, not original scenario slot numbers; Wallace maps original player 2 to runtime player 1.
Gaia, unknown players and duplicate enabled player entries are rejected.
Each player can override `actions_per_minute`, `actions_per_second` and integer `reaction_ms`.
Preset reaction delays are 2500, 1800, 1200, 800, 600 and 400 milliseconds respectively.
The sustained/rolling action limits match the preset table below; reaction ranges in that table remain tuning proposals.
`enabled: false` omits that controller.
`max_orders` bounds retained diagnostic operation lines, not the number of orders the AI can execute.
Unknown profile fields and invalid option types fail explicitly.
These switches are resolved at startup; live UI policy editing is not implemented.
Reaction overrides must lie between 100 and 60,000 milliseconds.

The controller submits single-actor moves, attacks and gathers through the ordinary command handler and uses the registered economy handler for production and construction.
The production queue is the shared committed-resource and population ledger; no second AI stockpile is maintained.
A production query reads the same nyan prerequisites as the executing handler, which still revalidates the actual order.
Queued population uses the trained subject's population cost, including zero-population subjects, rather than counting queue entries.
The optional match cap applies to human and AI producers alike.

Workers select visible, supported resource sites using legal production-cost deficits, existing assignments, travel distance and visible danger.
Disabling fog explicitly grants the controller full map visibility, matching the game's observation rules.
Exhausted sites are reconsidered after their remembered location becomes visible.
Housing is requested near the population cap, with bounded placement candidates and a cooldown on retries.
Production uses legal unit/technology options; visible threats trigger defense or configured offense, and low-health units can retreat toward an owned building.
Scouts use a seeded, bounded search for explored/visible frontier tiles and reuse the engine's movement/pathfinding system.
Original source civilizations restrict producible subjects using the existing civilization catalog; scenario restrictions apply in addition.
This prevents foreign unique units or unrelated DLC units from appearing merely because their nyan production option exists.
Converted per-player nyan views remain the source of costs and prerequisites; the campaign importer does not yet restore every original starting technology or civilization bonus.

The implemented controller has simple per-unit tasks and shared attention limits.
It emits an initial resolved-limit log and a `[PERF] ai` summary every ten wall-clock seconds, including call count, submitted order count, total handler time and maximum handler time.
Those durations cover observation/decision/submission work; later engine events, including asynchronous movement and combat, are measured by the campaign runner's event-processing stage.
Wall-clock measurements never influence a decision.
Army formations, matchup-driven composition, advanced opening plans, live policy switches and a full legacy script interpreter remain design work.
`protected_instances` explicitly excludes named scenario actors; automatic leases for all script-controlled groups are not implemented.
Prefer conservative `attacks: false` for campaign profiles until their scripted groups have been identified.

Operation traces contain exact raw simulation time, player, sequence, command kind, actor/target, subject and fixed-point coordinates.
The test runner recreates the initial world and schedules the recorded operations using the same AI event-handler identity and exact time, with live decisions disabled.
It compares both the submitted operations and the sampled state byte-for-byte before accepting or updating a golden.
This checks deterministic command replay, not a complete simulation checkpoint.
Saving/loading with an active controller fails with an explicit diagnostic until pending AI/production events and stable runtime entity IDs can be restored; ordinary controller-free saves keep their existing behavior.

## Campaign tests

`bin/run test testing::ai_action_limits` checks budget boundaries, invalid profiles and civilization availability enforcement without game assets.
`bash scripts/run_wallace_golden.sh --no-build --jobs 2 --timings` prepares Mission 1 and Mission 2 sequentially and runs the existing controller-free cases plus four AI/replay cases.
Independent cases can run in separate processes; a case's live AI and replay run sequentially against the same frozen asset metadata.
The reported case wall time includes the second world's initialization and replay.

Mission 1 AI fixtures use the original map and two controlled militia reinforcements created through the existing scenario-effect executor.
This gives the original opening, which otherwise has no enemy mobile army, an immediate visible combat case.
The fixtures cover moderate/easiest limits and a disabled-controller control.
Mission 2 uses its original villagers and Town Center, declared resource grants and an eight-population match limit to exercise gathering, housing and production.
Explicit behavior expectations prevent an empty operation stream from passing as a successful AI test.

Add `--barbarossa` to include the original Barbarossa mission 1 map sandbox.
The locally inspected source is `5_Barbarossa_1.aoe2scenario`: 200 by 200 tiles, 14,053 entities and eight non-Gaia player slots.
Two enabled computer players exercise independent budgets, civilization restrictions and simultaneous economic decisions.
Source filename and test variant are checked before native loading, so mission number 1 cannot confuse Wallace with Barbarossa.

Full Barbarossa story execution is not supported yet.
The inspected source contains unsupported condition type 12 and effect types 0, 1, 10, 18, 41 and 91.
The sandbox exporter requires explicit `--ai-map-sandbox`, preserves source identity/hash and an audit of original trigger types, and removes all story triggers rather than executing a misleading subset.
The initial placements, resources and map remain available for controller testing.
This is a map-based AI regression, not proof of campaign completion or full original civilization fidelity.

The existing exporter accepts selected campaign entries:

```sh
venv/bin/python scripts/export_wallace_scenarios.py \
  --campaign /path/to/cam5.aoe2campaign \
  --scenarios 5_Barbarossa_1.aoe2scenario \
  --ai-map-sandbox --output logs/barbarossa-ai
```

The shell test runner restores Wallace Mission 1 after the suite exits.
New AI baselines are separate `ai_m1_*`, `ai_m2_*` and `ai_b1_*` files under `tests/golden/wallace/`.
Run without `--update-golden` first when checking an existing baseline; updates still execute behavior and operation-replay assertions.

## Evidence and scope

The original [Ensemble Studios Computer Player Strategy Builder guide](https://userpatch.aiscripters.net/CPSB.pdf) documents condition/action rules, goals, timers, strategic numbers and resource escrow.
It also describes civilization-aware availability checks and difficulty-dependent tactical behavior.
This is documentation of the scripting interface, not the complete engine implementation.

The original team's [Age of Empires II postmortem](https://www.gamedeveloper.com/design/postmortem-ensemble-studio-s-age-of-empires-ii-age-of-kings) describes a script-driven expert system and layered pathfinding.
Dave Pottinger's [July 2003 Game Developer article](https://media.gdcvault.com/GD_Mag_Archives/GDM_July_2003.pdf) discusses limitations of the Age II AI and the knowledge-base abstraction used in Age of Mythology.
That later architecture is useful design evidence, not evidence that Age II already used the same implementation.

The [AI scripting community's maintained reference](https://airef.github.io/) documents CD, HD, UserPatch and DE interfaces; its command and strategic-number lists distinguish versions.
Treat extension semantics as version-specific.
The official [DE update 37650](https://www.ageofempires.com/news/aoe2de-update-37650/) provides concrete examples of strategy and retreat improvements.

Locally installed HD scripts and the `Promisory` and `AiBuilder` directories under the original game's `resources/_common/ai/` provide additional behavior examples.
Inspect their conditions, goals and parameter choices locally; do not copy proprietary scripts into the source tree or golden fixtures.
Reproduce selected behavior with independently written rules and synthetic test maps.
Full `.per` compatibility, complete DE parity and an XS interpreter are separate projects.

## Control and information boundaries

A computer player is another producer of ordinary simulation commands.
Use this flow:

```text
visibility-filtered observation + own economy + scenario constraints
    -> knowledge and task state
    -> economic / military / scouting proposals
    -> one resource, population and attention scheduler
    -> validated ordinary command submission
    -> existing command handlers and event loop
```

Introduce `ComputerPlayerController` in the simulation, driven by scheduled events at fixed simulation times.
Use fixed-point simulation time and coordinates; do not drive decisions from render frames or wall-clock time.
Keep the initial implementation on the simulation thread.
Deterministic work quotas permit bounded incremental computation without introducing another thread or race-dependent results.
Desktop, headless and future WASM hosts consume the same controller.

The current `ICommandSource` is part of the synthetic snapshot model and does not yet represent production build, research or attack commands.
Extract a small shared production command envelope at the existing `game.send_command` boundary before integrating AI; do not create a second combat or economy implementation.
Include player, source, stable sequence, due time, actors, target and typed arguments.
UI, recorded input and AI use the same legality checks and command result interface.
Extract the existing economy availability checks into a reusable query; the command handler must still revalidate on execution.

`AiObservation` exposes all owned entities and resources, currently visible opponents and explicitly permitted allied information.
Store last-seen enemy positions with observation times and uncertainty; never keep live hidden enemy references.
Map exploration may remember terrain and previously seen resources, but cannot inspect undiscovered resources or enemy production queues.
Forgetting, reaction delay and tactical errors operate on this observation, not directly on unrestricted `GameState`.
Scenario-provided objectives are explicit inputs; they do not imply permission to inspect every opponent.

## Behavior modules

Start with a small ordered rule set and a few task states, rather than a generic rule-language interpreter or search framework.
Rules propose tasks; the scheduler decides which can execute.
Use stable rule identifiers, integer priorities and stable entity-ID tie breaking.
Each task records owner, assigned actors, target, status, reservations and its next permitted reconsideration time.
Unit tasks include idle, gather, construct, scout, assemble, defend, attack and retreat.
Hysteresis and a minimum task commitment prevent oscillation between similar targets.

### Economy and production

Estimate near-term resource demand from the selected production, construction and research plan.
For resource `r`, derive a nonnegative deficit from planned cost plus safety reserve minus stockpile and estimated income over a short planning horizon.
Allocate workers by deficit and reachable gathering yield, with integer largest-remainder allocation and stable ID ties.
Prefer idle workers before moving productive workers, and bound reassignment frequency.
Recompute when a deposit is exhausted, danger changes, population blocks training or the strategic plan changes.
Use remembered safe deposits, travel cost and capacity rather than global nearest-resource queries.

Maintain one reservation ledger across construction, production and research.
Unaccepted proposals do not spend resources; accepted queued commands reserve them once, and failed/cancelled/completed tasks release the appropriate reservation.
Read production queue commitments so that multiple buildings cannot promise the same resources or population slots.
Emergency defense can release lower-priority strategic savings, while research savings cannot starve basic recovery indefinitely.
Record rejection reasons such as insufficient resources, blocked population, unavailable technology, blocked placement and busy producer.

The authoritative simulation must expose both housing capacity and the match's population limit.
Current `PlayerEconomy` supplies housing capacity; a separate match population rule still needs implementation and enforcement for every command source.
Available slots equal `min(housing capacity, match limit) - living population - queued population`.
Use each unit's actual population cost, including zero-population units and civilization bonuses.
Build housing before reaching the cap; at the hard match limit, adjust worker/army targets instead of repeatedly ordering houses.
No AI-only override may bypass the authoritative population rule.

### Civilization and game context

Read available units, technologies, costs, bonuses and prerequisites from the player's patched nyan view and scenario restrictions.
A civilization profile expresses preferences over roles and unit lines, not a copied technology tree or hardcoded cost table.
If a preferred option is unavailable, choose a legal role substitute; if no substitute exists, report an unsupported plan and choose another objective.
Use start age, age ceiling, victory mode, resource abundance, revealed terrain, map connectivity and enabled scenario objectives as planning inputs.
Naval transport, markets and diplomacy are disabled until their underlying simulation commands are supported.
A missing feature must not silently become a successful AI order.

### Military and scouting

Use a small force state machine: defend, assemble, advance, engage, retreat and regroup.
Choose targets by known threat, strategic value, estimated reachable distance and expected losses.
Use converted damage classes, armor, range and visible force composition to estimate matchups; civilization preferences alone do not establish a counter.
Sample only a bounded set of candidate targets and positions.
Start with threat estimates around visible enemies and fortifications; add a coarse influence map only when profiling and behavior justify it.
Reuse navigation and occupancy interfaces for movement and placement.
A strategic retreat creates an ordinary move order; it must not teleport units, alter speed or bypass collisions.

Scouts explore reachable frontiers of the explored map in a stable order with seeded variation.
Do not send economic workers into a known threat simply because the resource deficit is large.
Require sustained evidence or a significant priority change before switching an army's target.
Independent unit auto-attacks and movement along an issued route are engine behavior, not new player actions.

### Campaign ownership

Keep `none`, `campaign` and `skirmish` controller modes distinct.
`none` disables strategic AI, while normal unit mechanics and explicit scenario triggers still run.
Campaign policy constrains attack regions, activation time, allowed buildings, pursuit and retreat areas, and scripted unit groups.
Script-controlled groups hold an explicit ownership lease; the planner cannot overwrite their orders until released.
An existing tutorial must not become a full skirmish merely because its hostile units have an enemy owner.

## Difficulty and human operation limits

Separate strategic competence from physical operation limits.
Use six proposed presets matching the familiar easiest/easy/moderate/hard/hardest/extreme labels.
These values are initial tuning proposals, not measurements or original-game constants.
All levels use the same visibility and economic rules, with no free resources, faster training or extra health by default.

| Preset | Effective orders/minute | Maximum in any rolling simulation second | Reaction delay | Tactical control |
| --- | ---: | ---: | --- | --- |
| Easiest | 12 | 1 | 2.0–3.5 s | basic defense, long task commitment |
| Easy | 24 | 1 | 1.5–2.5 s | simple attack groups |
| Moderate | 48 | 2 | 1.0–1.8 s | basic counters and retreat |
| Hard | 72 | 2 | 0.7–1.3 s | coordinated economy and army |
| Hardest | 108 | 3 | 0.5–0.9 s | more frequent tactical adjustments |
| Extreme | 144 | 3 | 0.35–0.7 s | strongest supported planning |

Use one shared action budget per player across all modules and unit groups.
An integer token bucket enforces the sustained rate; an additional rolling-window limit and minimum command spacing prevent saved tokens from producing an instantaneous burst.
Reaction delay is a separate seeded delay between noticing a change and submitting a response.
Pausing stops all three clocks because they use simulation time.
Do not give each unit or subsystem its own full APM allowance.

Count effective player orders, not hypothetical mouse movements or pathfinding steps.
One group move is one order, while separately targeted units, producer orders and individual queue additions consume separate action units.
Splitting one packet into many targets or queue entries must not bypass the budget.
Coalesce identical pending orders and discard obsolete proposals before charging the budget; rejected executed commands still consume attention and trigger a retry cooldown.
Limit tactical attention to a bounded number of groups, with a switching delay; combat and economy compete for that attention.
Difficulty can change planning horizon, scouting coverage, production diversity, attack timing and willingness to retreat, without granting hidden knowledge.

Proposed per-player controls are `enabled`, mode, difficulty, economy, construction, research, scouting, defense, offensive attacks, retreat and tactical micro.
Expose action-rate and reaction-delay overrides separately from the preset, and store the resolved profile in saves and test fixtures.
Changing a switch does not cancel already accepted engine commands automatically.
Pause prevents new AI decisions; disabling a module discards its unsent proposals and releases their reservations; stopping existing orders is a distinct explicit action.
Diagnostic omniscience or resource grants, if ever added, require separate visible opt-ins and separate test profiles.
These controls are proposed configuration fields, not existing CLI options.

## Determinism, tracing and restoration

Order AI decision events by fixed due time, player ID and controller sequence; define their order relative to player commands at the same time.
Sort entity and proposal iteration, use fixed-point/integer comparisons and give each player a saved seeded RNG stream.
A random draw cannot depend on unrelated players' decisions or render workload.
Bound candidate counts and search expansions deterministically; elapsed CPU time can be logged but must not select a different action.
Stagger different players' scheduled decisions while preserving fixed, reproducible due times.
Wake modules for relevant events and reconsider strategy less frequently than immediate task updates.

Save knowledge, last-seen observations, task ownership, reservations, RNG state, resolved profile, sequence, next decision times, delayed reactions, action tokens and recent action timestamps.
The operation log alone is insufficient to resume decision-making.
Do not claim full AI save/restore until production queues, pending events, stable entity IDs and scenario progress also restore correctly.
The current live save codec recreates entities and is not yet a complete event-queue checkpoint; that is a prerequisite for an exact AI continuation test.
Controller-owned runtime IDs must either survive load or be explicitly remapped as part of a validated restore.

Log compact decision reasons, proposed/accepted/rejected orders, resource reservations, population blockage, action-budget waits, candidate counts and module durations.
Summarize every configurable wall-time interval without influencing the simulation.
Use operation-count budgets for deterministic CI and repeated, build/host-specific timing baselines for performance regression checks.
An expensive AI must yield a pending computation at a fixed work limit, not consume unbounded CPU in one event.

## Golden contracts

Keep two primary families and an additional replay check:

1. Existing engine goldens explicitly run without strategic AI and preserve their current baseline.
2. AI goldens run the real controller with a fixed seed, resolved profile, scenario and external player input, then compare the AI's operation stream and sampled world state.
3. Replay the recorded AI and player operations with live AI disabled and compare the resulting world state against the decision run.

Record due time as an exact simulation-time integer, player, sequence, command kind, stable actor/target IDs and canonical typed arguments.
Do not include wall timings, pointers, localized names or unordered collections.
A proposal trace can be retained for diagnostics, but the operation golden contains submitted player actions and their acceptance result.
Replay injects commands before their handlers run and in the same ordering; it must not run a second AI controller.
Comparing a fixed command recording alone tests the engine, not AI decision quality.

Begin with one small self-contained map that forces idle-worker allocation, a population blockage, legal production and a visible enemy attack/defense decision.
Compare a small set of difficulty profiles and one module-disable override; add civilization and scarce-resource variants only where they catch a concrete behavior distinction.
Check invariants in addition to snapshots: no hidden targets, no illegal technologies, no overspending, no population overflow and no action-window violation.
Once full restoration exists, compare continuous execution with a save/load split, including the operation stream after the split.
Keep snapshots in `tests/golden/` with distinct AI prefixes and explicit profile/version metadata.
A rules/profile change may change the AI family; a command/engine change may change both families.

After AI integration, rerun all existing goldens without updating them first.
Review the first mismatch using the existing bounded diff, fix accidental changes, then deliberately update only AI baselines whose new decisions are intended.
Parallelize independent worlds, never ordering inside one simulation.

## Smallest useful implementation

First establish the shared production command boundary, availability query and authoritative population cap.
Then implement observations, the shared scheduler/action budget and one controller that gathers, produces, defends and sends a constrained attack group.
Reuse small ordered rules with JSON profile data; no new runtime dependency is required.
Add the synthetic AI golden and rate/legality invariants before enabling a campaign controller.
Expose resolved per-player controls in the launcher after their simulation semantics are tested.
Only then expand opening strategies, counters, civilizations and tactical micro.
Full legacy script interpretation, naval strategy, negotiation and learned policies remain independent extensions.
