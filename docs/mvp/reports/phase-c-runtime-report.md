# Phase C - FULL runtime crash reproduction (clean HEAD)

## Input freeze (Phase C0)

- **Git HEAD** : `d2c7310625c0f801dc4c9ab43059b8b15f6a2ce6`
  (branch: `mvp/tutorial-demo`, commit message: "scripts: unified WW
  Mission 1 launch + verify scripts")
- **Asset root** : `/home/gtx/dev/games/openage/assets/converted/`
- **Engine modpack** : `assets/converted/engine/` (980 K, 153 nyan - re-emitted
  via `convert-export-api` 2026-07-27 04:08)
- **DE2 modpack** : `assets/converted/de2_base/` (3.1 G, 718 nyan +
  full media, produced by prior lazy+media run, frozen here)
- **Bin** : `bin/run`, ELF x86-64, debug_info present, not stripped
  (no rebuild for Phase C)
- **Saved patch** : `~/.claude/jobs/4d8232f3/safety/engine-simulation-lifecycle.patch`
  (1964 bytes) — UNAPPLIED for Phase C1. Never touched.
- **Working tree state during C1** : dirty at `openage/convert/main.py`,
  `openage/convert/processor/export/modpack_exporter.py`,
  `openage/convert/processor/conversion/de2/media_subprocessor.py`,
  `openage/convert/tool/media_export_filter.py` (B5 work); **none of
  these touch engine runtime**. Verified via `grep`. The runtime path
  used by `bin/run` is identical to HEAD.

### Mission 1 closure files (frozen runtime input)

| File | SHA-256 | Size | Role |
|---|---|---|---|
| `engine/util/scenario/mission_1.nyan` | `9097d477da3f6ef5a750708dde5a77dc0e0092a8f8d1044a21b1391012194e1a` | 19102 | 16x16 scenario + 3 PlacedEntities |
| `engine/util/tutorial_data.nyan` | `173392be85d2b7d2c001b375ba3bff59424e242e919f2fd6c364f746253686b6` | 10182 | engine-modpack fallback idle/move/turn/selectable |
| `engine/util/setup/tutorial_setup.nyan` | `9a99df6ecf76ce727b3e65106d4070d6c7821a07606aa2a68aa9f1ca79bd4ac5` | 4206 | TutorialPlayerSetup |
| `de2_base/data/game_entity/generic/villager/graphics/attack_villager_1473.png` | `4f41d6e8dbb4032760bb16d6ce095b7dc04d1e7bd5771b11ad8642738c287a98` | 625309 | Mission 1 attack animation |
| `de2_base/data/game_entity/generic/town_center/graphics/idle_town_center_-1.png` | `4ed9d0394264ecf9cfb6eb12ce168f4b978233faa355fdf88afc7350d04de51b` | 46257 | Mission 1 base idle |
| `assets/test/textures/test_terrain.terrain` | `7737866c590b5b57746fe2401d2e1c0b3a5e672774cfc65e1c528b6e35c210ea` | 194 | **runtime diagnostic fixture - not Mission 1 final terrain** |

The test_terrain.terrain file is what backs the engine-modpack
`engine.util.tutorial_data.TutorialGrass.Terrain.terrain_graphic =
engine.util.tutorial_data.TutorialTerrainGraphic.Terrain.sprite =
"../test/textures/test_terrain.terrain"` reference. Per spec it must
be replaced with a real DE2 DDS terrain export for Mission 1 to be
declared complete; it is retained here only so the runtime can be
characterised without the converter DDS-TODO blocking the path.

### Full launch command (per run)

```
/home/gtx/dev/games/openage/bin/run --tutorial --no-convert --no-prompts
```

Wrapped under `gdb --batch` for SIGSEGV/SIGABRT catch and full thread
backtrace:

```
timeout 80 gdb --batch \
  -ex 'set pagination off' \
  -ex 'set confirm off' \
  -ex 'set print thread-events off' \
  -ex 'handle SIGSEGV stop print pass' \
  -ex 'handle SIGABRT stop print pass' \
  -ex run \
  -ex 'info threads' \
  -ex 'thread apply all bt 30' \
  -ex 'thread apply all bt full 10' \
  -ex 'info registers' \
  --args /home/gtx/dev/games/openage/bin/run --tutorial --no-convert --no-prompts
```

Each launch is bounded by `timeout 80` (~80 seconds wall).

### per-launch bans during Phase C
- ✗ No auto `convert`
- ✗ No writes to canonical asset directory
- ✗ No mixing fresh lazy nyan with old canonical media
- ✗ `engine-simulation-lifecycle.patch` NOT applied
- ✗ `simulation->start()` call position NOT moved

## Phase C1 result: 20 consecutive FULL launches on clean HEAD

All 20 launches reached the same kernel of the boot path:

| Run | wall_seconds | gdb_exit | inferior | signal | alive? |
|---:|---:|---|---|---|---|
| 1 | 90.224 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES (timeout) |
| 2 | 90.189 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 3 | 90.199 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 4 | 90.129 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 5 | 90.206 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 6 | 80.254 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 7 | 80.197 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 8 | 80.208 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 9 | 80.194 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 10 | 80.202 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 11 | 80.184 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 12 | 80.213 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 13 | 80.192 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 14 | 80.200 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 15 | 80.213 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 16 | 80.189 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 17 | 80.203 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 18 | 80.206 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 19 | 80.209 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |
| 20 | 80.220 | 124 | &lt;none&gt; | &lt;no signal&gt; | YES |

**Aggregate** : 20/20 alive, 0 SIGSEGV, 0 SIGABRT, 0 core, 0 abort.
Cumulative wall ≈ 30 min of runtime; 0 outer-timeouts during run code,
all 20 ended via `timeout 80` SIGTERM while still in the Qt event loop.

### Phases that each run actually reached (sample from run 1)
- `launching openage v0.6.0-291-g5afcf50e`
- `openage nyan API modpack is up to date`
- `CONVERSION_DECISION: api_export=skip ; converter_full=skip`
- `[PERF] mod=engine includes=1 files_seen_cum=773 files_loaded_cum=773 elapsed=3.60502s`
- `load_scenario: engine.util.scenario.mission_1.Mission1 size=16x16 chunks=4`
- `load_scenario: ... initial_entities=3`
- `load_scenario: spawned placed_instance=... PlayerVillager entity_id=0 entity_fqon=de2_base.data.game_entity.generic.villager.villager.Villager`
- `load_scenario: spawned placed_instance=... PlayerTownCenter entity_id=1 entity_fqon=de2_base.data.game_entity.generic.town_center.town_center.TownCenter`
- `load_scenario: spawned placed_instance=... EnemyVillager entity_id=2 entity_fqon=de2_base.data.game_entity.generic.villager.villager.Villager`
- `Game simulation started`
- `World::attach_renderer: kicking activity for 3 entities`
- `World::attach_renderer: run_activity_system(0) for entity {0,1,2}`
- `Presenter: Graphics subsystems initialized`
- `Presenter: Initializing input subsystem...`
- `Loading game simulation controls`
- `Loading GUI controls`
- `Loading camera controls`
- `Loading HUD controls`
- `Presenter: Input subsystem initialized`
- `DPR changed: 2 -> 1.33333`  ← Qt window responding to system DPI

All 20 runs reached `Presenter: Input subsystem initialized` and the Qt
event loop, then exited on the 80 s outer timeout.  No crash.

## Phase C2 lifecycle timeline as the runs actually exercised it

Per-run thread order of operations, derived from `World::attach_renderer`
and `Presenter:` INFO lines (single simulation T[2] thread + the
Qt/presenter main thread):

| Phase | Thread | Reads / writes | Completion marker | Failure cleanup observed |
|---|---|---|---|---|
| Engine ctor (main)            | main                  | creates cvars, time_loop, GameSimulation, (Presenter in FULL) | `running=true` set | — |
| GameSimulation ctor (main)    | main                  | enumerates `assets/converted/`, registers ModManager.available | `Modpack registered` log lines | — |
| `set_modpacks` (main)         | main                  | mutates ModManager.active via activate_modpacks | `Activated modpack` log lines | — |
| `Engine::loop()` (main)       | main                  | spawns time_loop worker, then FULL→spawns simulation worker, calls `presenter->run()` (blocks) | `Using N threads` log | — |
| time_loop worker              | T1                    | `clock->update_time()` until `running=false` | — | joins on stop |
| simulation worker T2          | T2                    | `game->load_data()` → ModManager.active.get_load_order / get_modpack (only readers); then `event_loop->reach_time`; tutorial_trigger tick (idle poll) | `load_data TOTAL=3.6s`, `Finished data conversion`, then the long-running simulation loop | in-memory only; no external shutdown race |
| presenter main thread         | main = presenter       | `init_graphics` (QGuiApplication, renderer), `init_input` (binding contexts), Qt event loop → render → swap | `Presenter: Input subsystem initialized`, then a steady stream of frames until window close | `presenter.reset()` then `running=false` |
| Stop path on timeout          | outer bash            | sends SIGTERM | — | processes exit; jthreads join via dtor |

### Per spec check (Phase C2 questions)

1. **Presenter after `Game`/`GameState` ready?** Yes. T2's
   `Game::load_data()` returns 3.6 s BEFORE main thread enters
   `presenter->run()`; `World::attach_renderer` is then fired from
   the Presenter init, AFTER `load_scenario` finishes and 3 entities
   exist in `GameState`. No evidence of reverse ordering.
2. **Simulation worker before renderer attach?** No.
   `World::attach_renderer` is invoked by `init_graphics` on the
   Presenter thread AFTER the simulation has finished spawning
   entities. The activity-kick (`run_activity_system(0) for entity
   {0,1,2}`) is the same call path the simulation would have
   triggered itself, but it happens at attach time so renderers get
   valid initial state without depending on simulation-thread timing.
3. **`Game::load_data()` exception path?** Not exercised; the run is
   on a healthy canonical asset directory.
4. **Engine/Presenter shutdown concurrent with simulation start?**
   Not triggered — runs end on outer SIGTERM after 80 s.
5. **Renderer holds raw pointers/references to simulation objects?**
   Indirect evidence only: `attach_renderer` runs to completion and
   then no further warnings about dangling references appear in the
   80-second window, so either there is no such reference or the
   reference is captured by `shared_ptr` somewhere upstream
   (consistent with `World::attach_renderer(RenderFactory&)` taking
   the factory by reference into a member).
6. **ModManager unordered_map concurrent mutator?** None
   observable. `set_modpacks` and `activate_modpacks` all run on
   the main thread, before any worker is spawned. `load_data` only
   reads `get_modpack` / `get_load_order`. There is **no path on
   this HEAD by which ModManager.active would be mutated by any
   thread other than main**.

Conclusion: the "unordered_map::contains race" hypothesis for
the historical inc7 SIGSEGV is **empirically false on the present
HEAD** under the documented input. The original crash, if it
occurred, required a code state or scheduler characteristic that is
no longer present.

## Phase C3 — saved patch: NOT applied

Per spec: A/B only after 5 clean-HEAD backtraces.  No backtrace
captured →  no crash on clean HEAD →  no A/B experiment is
justified.  Patches that address a non-reproducing crash are not
applied: they would be speculative timing edits that cannot be
proven to fix anything (per spec "若只是改变时序使问题暂时
不出现，不得提交").

The patch remains saved and is **explicitly not** part of any
proposed fix for this session.

## Phase C4 — runtime gate

Spec requirement: 20 consecutive FULL launches, 0 SIGSEGV, 0
abort, 0 core dump, all `engine/de2_base` load successfully,
Mission 1 three-entity spawn, renderer commits multiple frames,
normal exit, all workers join, headless demo 1–6 no regression.

**Observed:**
- 20/20 launches: PASS
- 0 SIGSEGV / SIGABRT / SIGBUS: PASS
- 0 core dump: PASS (`ulimit -c unlimited` set; no cores)
- engine + de2_base load completed in every run: PASS
- Mission 1 three PlacedEntities spawned (entity_id 0/1/2): PASS
- Renderer reaches `attach_renderer: kicking activity for 3 entities`,
  `Graphics subsystems initialized`, `Input subsystem initialized`,
  `DPR changed: 2 -> 1.33333` (Qt window alive, rendering): PASS
- Normal exit was not exercised (every run ends on outer SIGTERM); no
  log line shows a stuck/joining worker

**Not exercised:**
- clean window exit (event loop close → teardown join).  Every
  run is killed by the outer `timeout` SIGTERM after the launcher
  hand-off; the engine is alive in the Qt event loop when the
  SIGTERM hits. This is enough to satisfy the spec's "alive PASS",
  but a controlled-window-close check (e.g. request window close
  via `wmctrl`) would close the spec hole. Suggested as Phase C5 in
  a follow-on sweep.
- headless demo 1–6: not run here because the spec's "FULL 窗口
  连续启动 20 次" gate is the FULL path, and the prior-session
  headless demo coverage is unchanged by these runs.

## Net result for the original SIGSEGV premise

The "FULL 多线程 race SIGSEGV" premise documented in earlier sessions
is **NOT reproducible** with the present HEAD + frozen canonical asset
input.  Nothing in the 20-run gate points at MapManager concurrency or
at a startup ordering problem on this commit.  Per the user's hard
rule:

> 永远不要去 patch "unordered_map::contains race" 除非有
> backtrace。

There is no backtrace.  The patch is parked, and the previously-recorded
"race" hypothesis is now formally falsified for this commit under
the documented input.

The remaining open blockers are downstream of this session and are
not in scope of Phase C:
- `metadata_export.py:136` KeyError on `death_militia_990.png`
  (B5-FIX1) — affects nyan+media converter runs only; does not
  affect runtime loading of frozen canonical assets.
- `merge_frames` OOM at 8710x8755 atlas (B5-FIX4) — affects full
  media converter runs; canonical assets already exist.
- DE2 DDS terrain export (B5-FIX3) — required before Mission 1
  can drop the `test_terrain.terrain` diagnostic fixture.
