# Tutorial Campaign Design — AoE2:DE William Wallace (5 missions)

## Scope

The end goal is to implement the **complete William Wallace Learning Campaign
(5 missions)** from AoE2:DE inside `openage/`. Each phase of the plan
(`~/.claude/plans/peaceful-chasing-mango.md`) ships one piece of capability
that lets exactly **one more mission** become playable.

The 5 missions are simplified teaching versions, not historical re-creations.
The simplification axis is **which new mechanic the mission introduces** —
each mission adds 1–2 new game concepts on top of the previous one.

## Mission progression — incremental mechanics

| # | Mission name           | New mechanic introduced | Cumulative capability                                                |
|---|------------------------|--------------------------|----------------------------------------------------------------------|
| 1 | First Steps            | (none — foundation)      | 1 villager, 1 TC, gather wood; reach 50 wood                          |
| 2 | Build a Village        | House + Food economy     | + build house (pop cap), + gather food from bush                      |
| 3 | Train an Army          | Barracks + production    | + build barracks, + train militia                                     |
| 4 | First Strike           | Basic combat             | + attack command, + HP-based death, + 1 scripted enemy villager       |
| 5 | The Battle of Stirling | Full integration         | + ranged units (archer), + multiple enemy waves, + destroy-enemy-TC   |

Each mission **reuses the previous one's infrastructure** — nothing is
rewritten, only added. A mission is "complete" when its `TutorialTrigger`
factory has all its predicates wired and the simulation runs the mission
end-to-end without errors.

## Phase → mission mapping

The 5-phase plan (`peaceful-chasing-mango.md`) maps onto these missions:

| Phase | What it ships                                          | Missions unblocked |
|-------|--------------------------------------------------------|--------------------|
| A     | enums + ability decoding + Player + Live + Game deharcoding + add_player | prerequisite for everything |
| **B** | `load_scenario` + `APIScenario` + Mission 1 scenario  | **Mission 1**      |
| C     | gather + build + train + combat + scripted enemy      | Missions 2, 3, 4   |
| D     | HUD tutorial panel + key bindings                     | Missions 1–5 playable |
| E     | integration tests + balance                          | Missions 5, polish |

**Phase B is currently shipping Mission 1 only.** The infrastructure
(`load_scenario`, `APIScenario`, the per-mission trigger factory pattern)
is built once so Phase C can drop in Mission 2–4 scenarios without
touching the load path again.

## Mission 1 — "First Steps"

The smallest end-to-end mission: prove the foundation works.

### Scenario
- 1 villager at tile (8, 8)
- 1 town center at tile (8, 9)
- 4 grass tiles around the starting position (no enemies, no other units)
- Player 0 owns the villager + TC
- Starting resources: 0 wood (player must gather from zero)

### Trigger steps

| # | name           | predicate                                                  | wired? |
|---|----------------|------------------------------------------------------------|--------|
| 1 | gather_wood    | `Player::get_resource(TutorialWood) >= 50`                 | YES    |

Just one step. Mission 1's win condition is **gather 50 wood**.

### Why only 1 step?

The original 7-step linear tutorial (select → gather → house → food →
barracks → train → attack) conflates two things:
- **mechanics that work today** (just gather)
- **mechanics that need future phases** (everything else)

By splitting the campaign into 5 missions, each mission ships **only the
mechanics the player needs to complete that mission**. Mission 1 = gather.
Mission 2 = build house + gather food. Etc.

This means each phase has a concrete deliverable: Phase C must wire up
build/train/combat so that Missions 2, 3, 4 *each* light up one new step
in `create_mission_N_trigger`.

## Mission 2 — "Build a Village"

### Scenario
- 1 villager at (8, 8)
- 1 town center at (8, 9)
- 4 grass tiles (food bushes near TC, wood trees a bit further)
- Starting resources: 200 wood (so player can build a house immediately)
- No enemies

### Trigger steps

| # | name           | predicate                                                  | requires Phase C task |
|---|----------------|------------------------------------------------------------|-----------------------|
| 1 | build_house    | at least 1 player-owned House entity exists                | C2 build chain        |
| 2 | gather_food    | player villager has Gather activity running OR food > 0    | C1 gather chain        |

## Mission 3 — "Train an Army"

### Scenario
- 1 villager, 1 TC, **1 pre-built barracks** at (12, 9)
- Starting resources: 100 wood, 50 food (enough for militia training)
- No enemies

### Trigger steps

| # | name           | predicate                                  | requires |
|---|----------------|--------------------------------------------|----------|
| 1 | train_militia  | ≥3 player-owned Militia entities exist     | C3 train chain |

## Mission 4 — "First Strike"

### Scenario
- 1 villager, 1 TC, 1 barracks, **3 pre-trained militia** at (12, 9)
- 1 **enemy villager** (scripted) at (1, 1), owner_id = 1
- No starting resources

### Trigger steps

| # | name           | predicate                                  | requires |
|---|----------------|--------------------------------------------|----------|
| 1 | defeat_enemy   | no entities with owner_id != 0 are alive   | C4 combat |

## Mission 5 — "The Battle of Stirling"

### Scenario
- 1 villager, 1 TC, 1 barracks, 5 militia, **3 archers** at base
- **Enemy base**: 1 enemy TC at (14, 14), 5 enemy militia, 3 enemy archers
- 1 enemy raider that periodically attacks the player's TC (scripted)

### Trigger steps

| # | name                  | predicate                                      | requires |
|---|-----------------------|------------------------------------------------|----------|
| 1 | build_archer_range    | ≥1 ArcheryRange entity exists                  | C3       |
| 2 | defeat_enemy_army     | no alive enemy military entities               | C4       |
| 3 | destroy_enemy_tc      | no alive enemy TC entity                       | C4 + scripted |

## Implementation: per-mission trigger factory

`libopenage/gamestate/tutorial/trigger.{h,cpp}` exposes one factory per
mission:

```cpp
std::shared_ptr<TutorialTrigger> create_mission_1_trigger(GameState &);
std::shared_ptr<TutorialTrigger> create_mission_2_trigger(GameState &);
std::shared_ptr<TutorialTrigger> create_mission_3_trigger(GameState &);
std::shared_ptr<TutorialTrigger> create_mission_4_trigger(GameState &);
std::shared_ptr<TutorialTrigger> create_mission_5_trigger(GameState &);
```

Today only `create_mission_1_trigger` has wired predicates. The others
return a stub `TutorialTrigger` with `is_finished() = true` and the
"HUD: mission not implemented yet" hint — that way the campaign screen
can show progress without crashing.

## Simulation wiring

`libopenage/gamestate/simulation.cpp::start()` picks the factory by
matching the scenario FQON suffix:

```cpp
if (scenario_fqon.ends_with("mission_1.Mission1")) {
    trigger = create_mission_1_trigger(*state);
} else if (scenario_fqon.ends_with("mission_2.Mission2")) {
    trigger = create_mission_2_trigger(*state);
}
// ...
```

When all factories are wired, `GameSimulation` runs the chosen trigger's
`on_update()` from its tick loop.

## Self-contained nyan data

Until the de2_base converter is finished, the 5 missions share a single
self-contained data file: `assets/converted/engine/util/tutorial_data.nyan`.
The 5 scenarios each define their own `Mission1` … `Mission5` nyan
objects, all pointing at the same engine-modpack Tutorial* primitives.

## File layout for the 5 missions

```
assets/converted/engine/util/scenario/
    mission_1.nyan       # 1 villager + 1 TC + 4 trees, no enemies
    mission_2.nyan       # + food bushes, 200 wood start
    mission_3.nyan       # + pre-built barracks, 100 wood + 50 food
    mission_4.nyan       # + 3 militia + 1 enemy villager
    mission_5.nyan       # + archer range + enemy base + scripted raids
```

(Replace `tutorial_1.nyan` with `mission_1.nyan` in Phase B cleanup.)

## Default CLI wiring

```bash
bin/run main --tutorial --headless --no-prompts
# Defaults to:
#   tutorial-player-setup-fqon = engine.util.setup.tutorial_setup.TutorialPlayerSetup
#   tutorial-scenario-fqon    = engine.util.scenario.mission_1.Mission1
```

Override to jump to a later mission (once it lands):
```bash
bin/run main --tutorial --tutorial-scenario-fqon engine.util.scenario.mission_3.Mission3
```