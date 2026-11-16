# William Wallace Mission 01 — Current-State Audit (Read-Only, Evidence-Based)

Audit date: 2026-08-05
Auditor: Repository Forensics / RTS Integration / Verification (Claude Code)
Branch audited: `mvp/william-wallace` @ `56109051`
Method: read-only inspection + existing tests + existing build. No production/test/asset code modified. No git state mutated.

Evidence conventions used below:
- **Repository fact (RF)** = confirmed directly from code/git/test output.
- **Strong inference (SI)** = multiple supporting facts.
- **Hypothesis (HY)** = not yet verified.

---

## 1. Executive Verdict

**There is NO fully playable William Wallace Mission 01 MVP today**, where "playable" is defined by the project's own gate `MISSION_1_WINDOWED_PLAYABLE` (plan §P7). That gate was never declared and has no acceptance report.

What DOES exist is a **headless-verified combat vertical slice on a hand-written 16×16 fixture** (`ww_mvp`, explicitly labelled `HAND-WRITTEN MVP FIXTURE — NOT A CONVERTER PRODUCT`). The simulation→victory chain is real and automatically tested:

- The production `bin/run --scenario` path loads the Mission Pack (`scenario.json`), spawns 3 entities (PlayerVillager, PlayerTownCenter, EnemyVillager), and synthesizes a data-driven `defeat_enemy` trigger from `VictoryDefinition.required_removals=["EnemyVillager"]` (RF: `/tmp/headless.log`, `/tmp/ww_window.log`).
- Demos 7, 9–14, 20, 35–38, 42, 46, 49 all PASS (exit 0). Demo 7 is the end-to-end headless regression (`enemy_removed=1 objective_finished=1`). Demo 38 is pointer-driven combat ending in `MissionState=VICTORY`. Demo 20 proves victory is data-driven with a negative assertion (non-target removal does NOT declare victory) (RF: `/tmp/demo*.log`).
- The windowed path starts cleanly on HEAD: Qt window + OpenGL renderer + presenter + 3 attached entities + input subsystem init, alive >25s, no crash (RF: `/tmp/ww_window.log`).

The first confirmed end-to-end **breakpoint for *playable*** is **P7 itself**: no interactive windowed combat→death→victory→UI sequence has ever been verified or signed off, and the visual rendering of the de2_base Villager/TownCenter sprites is *deferred* (29 sprite refs not yet exported by the converter — `verify_ww_mission1.sh` Stage 3). The simulation is ahead of the renderer/media/harness.

This is a **multi-area coupling gap**, dominated by **integration/harness (missing P7 acceptance) + converter (deferred media) + renderer (visuals unverified)**, not a simulation defect.

---

## 2. Repository Baseline

| Item | Value | Source |
|---|---|---|
| Repo root | `/Users/xc5/dev/openage` | `git rev-parse --show-toplevel` |
| Branch | `mvp/william-wallace` | `git branch --show-current` |
| HEAD | `5610905143b81d77bb7f40da1fb85192fd84bbde` | `git rev-parse HEAD` |
| Remote | `fork` = `git@github.com:lugt/openage.git`; `origin` = `https://github.com/SFTtech/openage` | `git remote -v` |
| Ahead of `fork/mvp/william-wallace` | 52 commits | `git status` |
| Diff vs `master` | 368 files, +67548 / −263 | `git diff --stat master...HEAD` |
| Worktrees | `mvp/william-wallace` (main); `wip/output-checks` at `/Users/xc5/dev/openage-output-checks` | `git worktree list` |
| Other branches | `mvp-demo`, `backup/p4.1-event-driven-objective`, `backup/p4.1.1-runtime-stabilization`, `fix/macos-build-and-run-plus-deadlock`, `feature/objc-exception-dump-in-terminate` | `git branch -a` |
| Tags of note | `ww-mission1-p3-lifecycle-complete` (RF: P3 lifecycle milestone) | `git tag` |

**Working tree (not cleaned — per audit rules):**
- Modified, unstaged: `doc/code/p4_4d2_p4_4f_plan.md` (1-line coordinate-doc correction), `libopenage/gamestate/system/move.cpp` (+112/−39; P4.4F dynamic-occupancy bisection/clamping work-in-progress).
- Untracked: `.claude/`, `assets/converted-old/`, `macos-version-files.txt`.
- No staged changes.

**Build/test environment:**
- `bin` → symlink to `.bin/clang++-debug-Oauto-sanitize-none` (ninja, Clang, debug, no sanitizer). `ninja -n` reports "no work to do" → **build is current for the committed HEAD** (the unstaged `move.cpp` is NOT compiled in; demos/launches reflect HEAD `56109051`).
- `bin/run` = Mach-O arm64 executable (Cythonized `run.py` with embedded interpreter → `openage.__main__.main`).
- venv at `venv/bin/python3` (Python 3.14). pytest was NOT installed in the venv (installed during audit to run P2 tests — dev-only, no production change).
- CTest: **Total Tests: 0** (`.bin/.../CTestTestfile.cmake` has only `subdirs`, no `add_test`). C++ "demos" are NOT ctest-registered; they run via `bin/run test -d gamestate.tests.simulation_demo N`.
- `assets/converted/` present; `assets/converted-old/` is a stale untracked copy.

Commands safely executable: read-only git, `ninja -n`, `bin/run test -d ...`, `bin/run --scenario [--headless]`, `scripts/*.sh` (with caveats below). `timeout(1)` is **not available on macOS** — relevant to script portability.

---

## 3. Git Change Summary

Recent commit cadence (last 40) shows a coherent P4.4A→F workstream on top of completed P2–P4.1:

- P2 (scenario IR/loader): `2453f310`…`e917feb6` — Mission 1 IR, mission-pack writer/reader, production loader, terrain snapshot.
- P3 (lifecycle): `596279f3`…`8059a009` — attack damage via discrete effects, generic death, deferred removal.
- P4.1–P4.3 (trigger runtime): `ef344dff`…`6aa99aa4` — event-driven objective, `TriggerInstance`/`TriggerRuntime`, conditions/effects, data-driven victory, wired into `GameSimulation` production path.
- P4.4A–F (selection/pathfinding/occupancy/formation): `2f9e9a1c`…`56109051` — `SelectedEntityView`/`MissionView`, `SelectionModel`, `HudQmlAdapter`, `Footprint`/`StaticOccupancyGrid`/`PlacementValidator`, `ContextActionResolver`/`PointerEvent`, `AttackApproach`, `FormationPlanner`, `MovementReservationManager`, swept-AABB, `DynamicOccupancy`, world-space selection overlay.

File-churn hotspot (last 15 commits): `gamestate/demo/tests.cpp` (5), `demo/CMakeLists.txt` (4), pathfinding/occupancy headers (swept_aabb, static_occupancy_grid, dynamic_occupancy, placement_validator), `presenter/viewmodel/selection_*`, `renderer/stages/hud/render_stage*`, `move.cpp`, `simulation.cpp`, `map.{h,cpp}`. This is **net-new capability scaffolding (each commit adds a demo + impl), not pure churn** (SI). However the same `move.cpp`/occupancy area is modified across multiple recent commits AND has uncommitted WIP — see §10.

Notable: the unstaged `move.cpp` reworks P4.4F block-handling from "log+continue past blocker" to "bisect to a safe sub-target and halt". This is unfinished work on the same region as commits `3ea4d9d7`, `429fd37e`, `56109051`.

---

## 4. Historical Specs and Plans

| ID | File | Phase/Date | Goal | Self-stated status | Still applicable? | Relation to code |
|---|---|---|---|---|---|---|
| S1 | `docs/mvp/mvp-gameplay-spec.md` | MVP gameplay spec (references commit `62051247`, 2026-07-22 era) | Single-player AoE-style playable demo; Mission 1 "First Strike": select/move/attack/death/victory; win = kill all scripted enemies | Claims to be **current execution authority**; explicitly freezes M0 determinism + rts-foundation-spec | **Partially stale** — see §11 | Diverges from impl: spec says tick-driven + economy (gather wood) + `TutorialTrigger` (wood≥50); impl is event-driven + combat-only + `TriggerRuntime` (defeat-enemy) |
| S2 | `docs/mvp/specs/WW_MISSION1_DEVELOPMENT_PLAN.md` | P0–P7 plan | Phased plan; exit labels `MISSION_1_HEADLESS_SEMANTIC` (P6), `MISSION_1_WINDOWED_PLAYABLE` (P7) | Living | **Yes (primary)** | P0–P4 done; P5 partial/blocked; P6/P7 not declared |
| S3 | `docs/mvp/specs/WW_MISSION1_SCENARIO_RUNTIME_SPEC.md` | Scenario runtime spec | Scenario IR + trigger language contract | Living | Yes | Mirrors P2/P4 impl |
| S4 | `docs/mvp/reports/mission1-scenario-feasibility.md` | P1, 2026-07-28 | Source-data feasibility | **Path D = BLOCKED** | Yes (blocker record) | Original scenario unimportable; `ww_mvp` is hand-authored reconstruction |
| S5 | `docs/superpowers/plans/2026-07-24-william-wallace-mvp-handoff.md` / `...-progress.md` | 2026-07-24 handoff | Migration/handoff notes | Historical | Reference only | Captures mid-P3 state |
| S6 | `docs/superpowers/plans/2026-07-25-mission1-attack-command.md` | 2026-07-25 | Attack-command plan | Historical | Reference only | Realized as P4.4D `ContextActionResolver`/`AttackApproach` |
| S7 | `doc/code/p4_4d2_p4_4f_plan.md` | P4.4D.2/F, current | Selection overlay + player color + multi-unit formation + dynamic collision (4 problems, 3 tasks) | Task 1–3 "complete" with `WINDOWED_MULTI_UNIT_MANUAL_ACCEPTANCE_PENDING` | **Yes (active)** | Commits `789cf203`…`56109051` + unstaged `move.cpp` |
| S8 | `.superpowers/sdd/progress.md` | M0 determinism harness, 2026-07-22 | SimLoop/checksum/replay | **All tasks pending; paused per user** | **Frozen** (S1 §10 freezes it) | No code; abandoned direction |
| S9 | `docs/superpowers/specs/2026-07-22-rts-gameplay-foundation-design.md` + `...m0-determinism-harness.md` | Long-term netcode/determinism | Frozen reference | Frozen | Not current | Not implemented |
| S10 | `docs/future/headless-simulation-roadmap.md` | Future | Headless roadmap | Reference | — | — |
| S11 | `.superpowers/sdd/p4_4d2_p4_4f_plan/progress.md` | SDD ledger for S7 | Task tracking | Task 1–3 done (Task 3 review skipped "due to budget") | Yes | Matches commits |

**Inheritance/conflict:** S1 (gameplay spec) is the declared authority but is **stale**: it predates P2–P4 and describes a tick-driven economy with `TutorialTrigger`, while the actual implementation is event-driven combat-only with `TriggerRuntime`. S2 (dev plan) is the de-facto living plan and matches the code. S7 is the active sub-plan. S8/S9 are explicitly frozen. The feasibility report S4 records the hard blocker (original scenario import = Path D BLOCKED).

---

## 5. Previous Plan Verification Matrix

Plan of record: **S2 (`WW_MISSION1_DEVELOPMENT_PLAN.md`)** phases P0–P7, plus active sub-plan **S7 (P4.4D.2/F)**.

| Requirement | Plan description | Code evidence | Call-chain evidence | Test evidence | Runtime evidence | Status | Confidence | Missing |
|---|---|---|---|---|---|---|---|---|
| P0 baseline audit | Trusted baseline report | `docs/mvp/reports/baseline-reality-audit.md` | — | — | — | VERIFIED_COMPLETE | High | — |
| P1 source feasibility | Path decision + IR dump | `mission1-scenario-feasibility.md`; `mission1-scenario-dump/` | — | `test_p1_dump.py` | — | BLOCKED (by design) | High | Original AoE2DE source data + parser |
| P2.1–P2.5 IR+writer+loader | Map/players/placements load headlessly | `openage/scenario/{ir,writer,loader,terrain,validation}.py`; `mission_pack.{h,cpp}`; `mission_pack_loader.cpp` | writer→`scenario.json`→`read_mission_pack`→`load_mission_pack_into_game_state` | `test_p2_*.py` (102 pass) | headless log: P2.7 production path loads | VERIFIED_COMPLETE | High | — |
| P2.6 integration tests | Round-trip IR↔dump | `test_p2_integration.py` | — | **1 FAIL** (`test_regenerated_dump_matches_committed_p1_dump`: pos 8,8 vs 6,4) | — | PARTIAL | High | Committed dump positions stale vs current IR |
| P2.7 native production integration | Mission Pack into production Game | `game.cpp:264` P2.7 path; `simulation.cpp:182` derives `mission_pack_dir` | `bin/run --scenario`→`Game::load_scenario`→mission pack | demo 9 PASS | headless + windowed logs show spawn | VERIFIED_COMPLETE | High | — |
| P3.1–P3.4 lifecycle | effect→health→death→removal | `system/move.cpp`, `event/attack.cpp`, `demo/ww_mission1_lifecycle.cpp` | AttackHandler→ApplyDiscreteEffect→Health→DeathEvent→RemovalEventHandler | demos 10/11/12/13 PASS | demo 7 regression | VERIFIED_COMPLETE | High | — |
| P3.5 mandatory tests | Per-increment tests | demos 10–14 | — | PASS | — | VERIFIED_COMPLETE | High | — |
| P4.1 event-driven objective | Remove polling | `objective_controller.cpp` (polling removed, `objective_poll_count=0`); `event_driven_objective` | `game.removal`→`notify_entity_removed` | demo 14 PASS | — | VERIFIED_COMPLETE | High | — |
| P4.2 TriggerInstance+runtime | Condition/effect dispatch | `scenario/trigger/{trigger_instance,trigger_runtime,trigger_condition,trigger_effect}.cpp` | `TriggerRuntime::evaluate_trigger` | demos 16–19 PASS | — | VERIFIED_COMPLETE | High | — |
| P4.3 data-driven victory | TriggerRuntime in production | `simulation.cpp:210` ctor, `:435` `wire_trigger_data`, `:477` handler | `required_removals`→`make_defeat_enemy_trigger_ir`→`add_trigger` | demo 20 PASS (negative assertion) | headless/windowed log `trigger_count=1` | VERIFIED_COMPLETE | High | — |
| P4.4A selection viewmodel | SelectedEntityView/MissionView | `presenter/viewmodel/mission_view.*,selection_model.*` | Controller→callback→SelectionModel | demos 22–24 PASS | — | VERIFIED_COMPLETE | High | — |
| P4.4B footprint/placement | Footprint+StaticOccupancyGrid+PlacementValidator | `scenario/{footprint,static_occupancy_grid,placement_validator}.*` | — | demos 25–27 PASS | `static_obstacles=1` in log | VERIFIED_COMPLETE | High | — |
| P4.4C pathfinding | GridAStar + clearance + waypoints + TC routing | `pathfinding/`, `system/move.cpp` | `Move::find_path` | demos 28–32 PASS | windowed log: `Move::move_default` runs | VERIFIED_COMPLETE | High | — |
| P4.4D input action resolution | ContextActionResolver + PointerEvent + AttackApproach | `input/semantic_action.cpp`, `input/pointer_event.h`, `controller.cpp` | right-click→resolve MOVE/ATTACK→AttackCommand | demos 35–38 PASS | input subsystem inits in window | VERIFIED_COMPLETE | High | — |
| P4.4D.2 selection overlay | World-space outline per selected entity | `renderer/stages/hud/render_stage.*`, `assets/shaders/hud_selection_overlay.*`, `selection_overlay.*` | presenter.render→quads→hud pass | builds; demos 7/42/46/49 PASS | **windowed pixel acceptance PENDING** | IMPLEMENTED_UNVERIFIED | Med | Manual visual sign-off (`WINDOWED_MULTI_UNIT_MANUAL_ACCEPTANCE_PENDING`) |
| P4.4F multi-unit formation + dynamic collision | No overlap; formation destinations; swept AABB | `pathfinding/formation_planner.*`, `scenario/dynamic_occupancy.*`, `scenario/swept_aabb.*`, `system/move.cpp` (unstaged WIP) | GroupMoveHandler→FormationPlanner; Move::move_default swept-AABB bisect | demos 42/46/49 PASS | — | PARTIAL | Med | `move.cpp` block-handling is uncommitted WIP; Task 3 review skipped |
| P5.1–P5.4 terrain & media closure | Real Mission 1 visual asset closure | `verify_ww_mission1.sh` Stage 3 | — | Stage 3: resolved=32, missing=0, **deferred=29** | — | BLOCKED | High | 29 de2_base villager/town_center sprites not exported; DDS terrain not started (FIX-3/FIX-4) |
| P6 fresh headless semantic | `MISSION_1_HEADLESS_SEMANTIC` | — | — | demos 7/38 cover similar ground but **no P6 gate report** | — | NOT_IMPLEMENTED | High | Formal P6 scenario suite + failure budgets not run/declared |
| P7.1–P7.4 windowed scripts/smoke | Canonical launch+smoke | `scripts/run_ww_mission1.sh`, `verify_ww_mission1.sh` | `bin/run --scenario` | windowed starts clean (RF) | — | PARTIAL | High | `verify_ww_mission1.sh` Stages 1/2/4 FAIL on macOS (`timeout` missing, `.venv`/`.so` checks) — portability bugs, not engine defects |
| P7.5 manual playable acceptance | 11-item checklist; `MISSION_1_WINDOWED_PLAYABLE` | — | — | — | **no acceptance report exists** | NOT_IMPLEMENTED | High | Interactive combat→death→victory→UI never signed off |
| Player color per owner (S7 problem 4) | Same sprite, different player colors | flagged `PLAYER_COLOR_METADATA_CONVERSION_BLOCKED` in S7 | — | — | — | BLOCKED | Med | Player-color mask/palette wiring blocked on converter metadata |

**Roll-up (by phase of S2):** VERIFIED_COMPLETE = P0, P2 (excl. P2.6 dump-sync), P3, P4.1–P4.4D. PARTIAL = P2.6, P4.4D.2, P4.4F, P7.1–4. BLOCKED = P1, P5, player-color. NOT_IMPLEMENTED = P6, P7.5.

Approximate: **~55% VERIFIED_COMPLETE, ~20% PARTIAL/IMPLEMENTED_UNVERIFIED, ~25% BLOCKED/NOT_IMPLEMENTED** of the S2+S7 plan items.

---

## 6. Build and Test Evidence

| Command | Purpose | Exit | Duration | Result | Relevant output | Interpretation |
|---|---|---:|---:|---|---|---|
| `ninja -n` (in `.bin/...`) | Check build staleness | 0 | <1s | "no work to do" | — | Build current for HEAD; unstaged `move.cpp` not compiled in |
| `bin/run test -d gamestate.tests.simulation_demo 7` | H0 headless regression | 0 | ~15s | PASS | `enemy_removed=1 objective_finished=1 time_advances=13` | End-to-end combat+victory headless |
| `... 9` | P2.7 mission pack integration | 0 | ~10s | PASS | `16x16, 4 chunks, 2 players, 3 placements` | Production loader works |
| `... 10/11/12/13` | P3 lifecycle | 0 | ~10s each | PASS | damage/death/deferred-removal | Combat lifecycle verified |
| `... 14` | P4.1 event-driven objective | 0 | ~10s | PASS | `objective_poll_count=0` | Polling debt removed |
| `... 20` | P4.3 data-driven victory | 0 | ~10s | PASS | pins 1–4 PASS; `declare_victory reason='enemy_removed'` | Victory is data-driven, negative-tested |
| `... 35/36/37/38` | P4.4D input+combat | 0 | ~10s each | PASS | demo 38 `MissionState=VICTORY` | Pointer-driven combat→victory |
| `... 42/46/49` | P4.4F occupancy/formation | 0 | ~10s each | PASS | swept AABB, no-overlap | Formation + dynamic occupancy |
| `venv/bin/python -m pytest openage/scenario/tests/` | P1/P2 Python tests | 0 (pytest) | ~1s | **102 passed, 1 failed** | FAIL `test_regenerated_dump_matches_committed_p1_dump` (pos 8,8 vs 6,4) | Committed mission-pack dump stale vs current IR |
| `bin/run --scenario --headless --no-convert --no-prompts` | Production headless load | 143 (killed alive) | 30s | Loads OK, runs | `P2.7 production path`, 3 spawns, `trigger_count=1` | Production path wired (killed because it runs forever) |
| `scripts/run_ww_mission1.sh` (windowed, killed 25s) | Windowed smoke | 143 (killed alive) | 25s | Starts clean | Qt window+OpenGL+presenter+3 entities+input init | Windowed path launches on HEAD |
| `scripts/verify_ww_mission1.sh` | Staged verification | 1 | ~80s | Stages 1/2/4 FAIL, 3 PASS | Stage1 missing `.venv`/`.so`; Stage2 rc=127 (`timeout`); Stage4 rc=127 | **macOS portability bugs in the harness**, not engine defects |

**Tests NOT run:** full `bin/run test -a` (all tests) — deemed high-cost; the targeted WW-relevant demos + pytest subset were run instead. `bin/run test -a` would include renderer/pathfinding/main demos not audited here.

**Failing-test root cause (P2.6):** `DEFAULT_DUMP_DIR = docs/mvp/reports/mission1-scenario-dump` (`openage/scenario/dumps.py:39`). The committed `scenario.json` there (and the production `assets/converted/ww_mvp/campaign/william_wallace/scenario.json`) place `PlayerVillager` at (6,4); regenerating from the current IR yields (8,8) (matching the nyan `_Pos8_8`). The committed dumps predate a position change and were not regenerated. Functionally harmless (both are valid tiles) but breaks reproducibility and means **production entity positions (6,4/8,8/10,4) are stale relative to the nyan/IR (8,8/8,9/1,1)**.

---

## 7. Mission 01 Runtime Path

Entry: `bin/run --scenario --no-convert --no-prompts` → `openage.__main__.main` → `openage/main/main.py` (`--scenario` opts into scenario slot; modpacks `[engine, de2_base, ww_mvp]`; FQON `ww_mvp.campaign.william_wallace.mission_01.Mission`) → Engine → `GameSimulation::initialize` (`libopenage/gamestate/simulation.cpp`).

| # | Stage | Entry symbol | Main file:line | Upstream | Downstream | Connected? | Verification | Known issue |
|---|---|---|---|---|---|---|---|---|
| 1 | Startup+mode select | `main()` | `openage/main/main.py:41` (`--scenario`) | CLI args | Engine init | CONNECTED | windowed/headless logs | — |
| 2 | Asset discovery | modpack activation | `main.py:259`; `simulation.cpp` modpacks | `assets/converted/{engine,de2_base,ww_mvp}` | nyan DB | CONNECTED | log `requested modpacks=[engine,de2_base,ww_mvp]` | — |
| 3 | Scenario file read | `Game::load_scenario` | `game.cpp:255` | `mission_pack_dir` derived from FQON (`simulation.cpp:182`) | `read_mission_pack` | CONNECTED | log `P2.7 production path; reading Mission Pack` | — |
| 4 | Scenario data convert | (pre-built) | `openage/scenario/{ir,writer}.py` → `scenario.json` | IR | mission pack files | CONNECTED | pytest P2 (102 pass) | committed dump stale (1 fail) |
| 5 | Converted product load | `read_mission_pack` | `mission_pack.cpp:40` (`dir+"/scenario.json"`) | mission pack dir | `MissionPack` struct | CONNECTED | demo 9; headless log | — |
| 6 | Map create | `load_mission_pack_into_game_state` | `mission_pack_loader.cpp:121` | `pack.map` | Terrain/NavigationService | CONNECTED | log `chunks=4`, `static_obstacles=1` | — |
| 7 | Players/units/resources create | placements spawn | `mission_pack_loader.cpp:188`+ | `pack.placements` | entities id 0/1/2 | CONNECTED | log `spawned ... entity_id=0/1/2` | player 1 = observer-only (`add_player: empty PlayerSetup FQON`); MissionWood amount=0 (no economy) |
| 8 | Initial camera | presenter init | `presenter.cpp` | game state | camera | CONNECTED (SI) | windowed log `Presenter: Initializing graphics` | not separately verified |
| 9 | Input+selection | `Controller`/`SelectionModel` | `input/controller/game/controller.cpp`; `presenter/viewmodel/selection_model.cpp` | Qt events | selected ids | CONNECTED | demos 22–24; Aug 3 log `MOVE for N entities` | — |
| 10 | Move command | `Move::move_default` | `system/move.cpp:279` | `ContextActionResolver`→MoveCommand | waypoints+keyframes | CONNECTED | windowed+headless logs `Move::find_path`/`move_default` | unstaged WIP reworks block-handling |
| 11 | Attack command | `AttackHandler`/`AttackApproach` | `input/semantic_action.cpp:185`; `event/attack.cpp` | right-click hostile→AttackCommand | approach+damage | CONNECTED | demo 38 `AttackHandler invoke`; demo 37 | interactive windowed attack not signed off |
| 12 | Sim event create/exec | `EventLoop` | `gamestate/simulation.cpp` run loop | commands | state changes | CONNECTED | demo 7 `event_batches=13` | — |
| 13 | Renderer/presenter update | `World::attach_renderer` | `presenter.cpp`, `renderer/stages/hud/render_stage.cpp` | game state | frame | CONNECTED (init) | windowed log `kicking activity for 3 entities` | **entity visual rendering unverified** (de2_base sprites deferred) |
| 14 | Trigger/objective read | `wire_trigger_data` | `simulation.cpp:435` | `pack.victory.required_removals` | `TriggerRuntime` triggers | CONNECTED | log `trigger_count=1` | zero data-driven TriggerDefinitions (synthesized fallback only) |
| 15 | Objective state update | `TriggerRuntime::notify_entity_removed` | `trigger_runtime.cpp:131` | `game.removal` | mission_state | CONNECTED | demo 20 pins | — |
| 16 | Victory/defeat detect | `is_victory()` | `simulation.cpp:541` | `mission_state` | bool | CONNECTED (headless) | demo 20/38 VICTORY | defeat path only (`reason='enemy_removed'`); no defeat-by-player-death condition |
| 17 | UI hints/objective text | `mission_view`, `MissionHud.qml` | `presenter/viewmodel/mission_view.cpp`; `assets/qml/MissionHud.qml` | mission_state | QML | PARTIAL | windowed log sets QML root to `assets/test/qml/main.qml` | **QML root is testscene, NOT MissionHud**; objective-text reachability unverified; scenario description is empty (`MissionEmptyMarkup`) |
| 18 | Restart/exit/cleanup | lifecycle stop | `simulation.cpp` `start()/stop()` | window close | clean exit | CONNECTED | Aug 3 log `Window close requested... shutting down` + clean loop exit | — |

---

## 8. Capability Matrix

| Capability | Required for MVP | Current state | Evidence | Blocking defects | Temp. workaround |
|---|:---:|---|---|---|---|
| Launch Mission 01 | Yes | **Works** | windowed+headless `bin/run --scenario` logs | none | — |
| Map loads correctly | Yes | **Works** | demo 9; log `16x16, 4 chunks` | committed positions stale vs IR (6,4 vs 8,8) | none (valid tiles) |
| Key entities spawn | Yes | **Works** | log `entity_id=0/1/2` PV/TC/EV | none | — |
| Player control init | Yes | **Works** | player 0 active; demo 35 | player 1 observer-only (passive enemy) | acceptable for MVP |
| Camera usable | Yes | **Likely works** | windowed Qt+OpenGL init (SI) | not separately verified | — |
| Select units | Yes | **Works (headless)** | demos 22–24; Aug 3 log | windowed drag-select visual not signed off | — |
| Issue move command | Yes | **Works** | windowed log `MOVE for N entities` + `move_default` | unstaged `move.cpp` block-handling WIP | committed version works |
| Units move | Yes | **Works** | log `Move::find_path`/keyframes | none | — |
| Issue attack command | Yes | **Works (headless)** | demo 38 pointer-driven ATTACK→VICTORY | interactive windowed attack not verified | — |
| Basic combat + death | Yes | **Works** | demos 7/10/11/12/13 | none | — |
| Scenario trigger | Yes | **Partial** | synthesized `defeat_enemy` only; 0 data-driven triggers | no trigger nyan language; original scenario blocked | fallback synthesis from `required_removals` |
| Objective display | Yes | **Unverified** | `mission_view.cpp`/`MissionHud.qml` exist | QML root is `test/qml/main.qml`; description empty | none |
| Objective state change | Yes | **Works (headless)** | demo 14/20 | UI surfacing unverified | — |
| Victory state | Yes | **Works (headless)** | demo 20/38 `VICTORY` | interactive windowed victory not seen | — |
| Defeat state | No (MVP = win) | **Not implemented** | only `enemy_removed` victory path | no player-death defeat condition | none |
| Normal exit/restart | Yes | **Works** | Aug 3 clean shutdown | restart-in-session not verified | relaunch works |
| Error observability | Yes | **Partial** | logs; `CONVERSION_DECISION` | `verify_ww_mission1.sh` broken on macOS | run `run_ww_mission1.sh` directly |
| Min regression test | Yes | **Partial** | 18 demos PASS; 1 pytest FAIL | demos not in ctest; no P6/P7 gate | — |
| **Entity visual rendering** | Yes | **Unverified/Blocked** | 29 de2_base sprites deferred (Stage 3) | converter hasn't exported villager/town_center sprites; DDS terrain not started | none |
| Player color per owner | No (polish) | **Blocked** | S7 flag `PLAYER_COLOR_METADATA_CONVERSION_BLOCKED` | converter metadata | none |

---

## 9. First Confirmed End-to-End Breakpoint

For **headless** play: there is no breakpoint — the chain launch→load→spawn→(pointer)attack→damage→death→removal→objective→victory is VERIFIED (demos 7, 38, 20).

For **playable (windowed)** the first confirmed breakpoint is:

> **P7.5 interactive windowed acceptance was never performed.** No report `docs/mvp/reports/mission1-windowed-acceptance.md` exists; `MISSION_1_WINDOWED_PLAYABLE` was never declared. The windowed path *initializes* (window+renderer+presenter+entities+input) but the sequence *right-click enemy → visible approach/attacks → enemy visibly dies → objective/victory UI visible* has zero verified evidence.

The **first hard technical sub-breakpoint** behind that is **visual rendering of de2_base entities**: `verify_ww_mission1.sh` Stage 3 reports 29 deferred villager/town_center sprite refs ("not yet exported by the converter"). The renderer attaches to 3 entities (`World::attach_renderer: kicking activity for 3 entities`) but whether anything is actually drawn on screen for villager/town_center is **UNKNOWN** (not verified — could be placeholder/invisible). This is the single most important fact to establish before any new spec.

---

## 10. Suspected Ineffective Loops

1. **`move.cpp` / dynamic-occupancy region (commits `3ea4d9d7`, `429fd37e`, `56109051` + unstaged WIP).**
   - Repeatedly modified across 3 commits + uncommitted edits in one session.
   - Intended problem: units must not pass through dynamic blockers.
   - Why not yet verifiable progress: the unstaged WIP changes block-handling semantics (log+continue → bisect+halt) but has no corresponding automated test pinning the new behavior; Task 3's subagent review was explicitly skipped "due to budget" (S11). Demo 49 pins "no-overlap" but not the new bisect-halt contract.
   - Missing observation: a demo that asserts a blocked unit halts at the clamped sub-target and emits the BLOCKED arrival event.
   - Minimal experiment: add a demo that routes one mover into another's swept AABB and asserts `t_safe<1` halt + no overlap.

2. **Stale spec vs implementation (S1 gameplay spec).**
   - The authority spec describes tick-driven economy + `TutorialTrigger(wood≥50)`; the code is event-driven combat-only + `TriggerRuntime(defeat-enemy)`.
   - Repeatedly, commits cite "P4.1 removed polling" / "data-driven victory" while the spec still describes the old design — risk of re-introducing discarded abstractions.
   - Missing observation: no reconciliation pass; the spec's Mission 1 win condition (`gather wood`) literally cannot be met (gather unimplemented) and was silently replaced by `defeat-enemy` without spec update.
   - Minimal experiment: diff spec §Mission-1 acceptance against current `scenario.json` `VictoryDefinition` and either update the spec or record the supersession.

3. **`verify_ww_mission1.sh` harness portability.**
   - Stages 1/2/4 always FAIL on macOS (`timeout` missing; checks `.venv`/`libopenage.so` only).
   - Intended problem: staged verification.
   - Why not progress: the harness gives a FAIL signal that is an environment artifact, so real regressions hide behind it; the Aug 3 logs show someone ran `run_ww_mission1.sh` directly instead, bypassing the staged gate.
   - Minimal experiment: replace `timeout` with a portable kill-on-sleep wrapper and add `.dylib`/`venv` to the checks; re-run.

4. **Dual objective abstractions.**
   - `WilliamWallaceMission01ObjectiveController` (legacy, `objective_controller.cpp`, with `create_mission_1/2/3/4_trigger` factories and hardcoded FQON `EnemyVillager`) coexists with `TriggerRuntime` (production). Comments say "legacy ObjectiveController is no longer on the production path" but it is still compiled and its factories still exist.
   - Not a blocked loop yet, but a re-introduction risk and a confusion source for future work.

---

## 11. Technical Debt and Temporary Implementations

- **`ww_mvp` is a hand-written fixture, not the original scenario.** Header: `HAND-WRITTEN MVP FIXTURE — NOT A CONVERTER PRODUCT`. Original import = Path D BLOCKED (S4). This is *labelled debt*, not silent fallback (RF).
- **Mission is combat-only, not the spec's economy mission.** `MissionWood amount=0`; no gather/build/economy. Win = `defeat_enemy` (synthesized from `required_removals`), not `gather wood`. Spec S1 is stale (§10).
- **Zero data-driven triggers.** `trigger_graph.json` `trigger_count=0`; `coverage.json` `source_status=BLOCKED`. The single trigger is synthesized in C++ from `required_removals` (`simulation.cpp:459`). No `engine.util.trigger.*` nyan language exists. This is the documented P4 fallback, not a full trigger runtime.
- **`WilliamWallaceMission01ObjectiveController` legacy code retained** with `TODO Phase C1–C4` placeholders for missions 2–5 (dormant, not on production path).
- **`EntityFactory` warns on ~14 unhandled abilities** per villager/town_center (Restock, LineOfSight, DropSite, …). Non-fatal; entities load with partial ability support.
- **Player 1 (enemy) is observer-only** (`add_player: empty PlayerSetup FQON; creating observer-only player id=1`). Enemy is passive — no AI. Acceptable for MVP "defeat scripted enemy" but means there is no opposition behavior.
- **No defeat condition** — only `reason='enemy_removed'` victory. Player-death/defeat path not implemented (MVP scopes this out, but spec mentions failure states).
- **Empty scenario description** (`MissionEmptyMarkup`) — no in-game mission text.
- **QML root is `assets/test/qml/main.qml`** (testscene), not `MissionHud.qml`. MissionHud exists but its reachability in the windowed path is unverified.
- **Committed mission-pack dump positions stale** (P2.6 test failure).
- **`mission_01.nyan` map/entity positions are dead code** in the production path (production uses `scenario.json`; nyan positions only used in the legacy nyan-only fallback). Two sources of truth diverge.
- **Demos not in ctest**; `bin/run test -a` not run in this audit.
- **`simulation.cpp:237 catch(...)`** swallows nothing — sets lifecycle=Failed and rethrows (legitimate).

---

## 12. Blockers and Unknowns

**Blockers (hard):**
- B1: Original AoE2DE William Wallace scenario data import = **BLOCKED** (no source data on host; no `campaigns_x_x.dat`/scenario parser in repo; no trigger nyan language). `ww_mvp` is a reconstruction (S4).
- B2: de2_base villager/town_center **sprite media not exported** by the converter (29 deferred refs; FIX-3 DDS terrain, FIX-4 atlas OOM). Blocks visual rendering verification.
- B3: Player-color metadata conversion blocked (S7 flag).

**Unknowns (must be resolved before new spec):**
- U1: **Does the windowed renderer actually draw villager/town_center on screen**, or are they invisible/placeholder? (Stage 3 says sprites deferred; renderer attaches but visual output never captured.) **Highest-priority unknown.**
- U2: Is `MissionHud.qml` reachable in the windowed path, or is only `test/qml/main.qml` loaded? Does objective/victory text surface?
- U3: Does an interactive right-click on the enemy in the windowed path produce attack→death→victory (the headless pointer-driven demo 38 says yes for the input layer, but the windowed Qt→input→sim round-trip is unverified)?
- U4: Does the unstaged `move.cpp` WIP break any of the passing demos if compiled? (Not built into current binaries.)
- U5: Why is the built binary's version string `v0.6.99-41-g429fd37e` (7 commits behind HEAD) despite `ninja` reporting no work? (Likely a stale generated version file; sources match built objects per `ninja -n`.) Cosmetic, but worth confirming the binary truly reflects HEAD.

---

## 13. Recommended Next Investigation

**Vertical slice that produces an observable, falsifiable result (no new spec yet):**

1. **Establish U1 (visual rendering) first** — it is the load-bearing unknown. Capture a windowed screenshot (or headless offscreen render) of the 3 spawned entities on the 16×16 grass map. Concretely: run `bin/run --scenario --no-convert --no-prompts` with `QT_QPA_PLATFORM=offscreen` (or a short windowed run with a frame dump) and inspect whether villager/town_center render or are invisible. This single fact determines whether the MVP is "1 render-bug away from playable" or "blocked on the converter".

2. **Then run the P7.5 checklist as an automated windowed smoke** that reuses demo 38's pointer-driven combat logic but through the real Qt input path: launch window → synthesize a right-click on `EnemyVillager` (entity_id=2) → assert `MissionState::declare_victory reason='enemy_removed'` appears in the log within N seconds → kill. This converts the unverified P7.5 "right-click enemy issues attack → enemy dies → victory" into an automated pass/fail.

3. **Regenerate the committed mission-pack dump** from the current IR (fixes the P2.6 test failure and the stale production positions), or consciously pin the (6,4) positions and update the IR — decide which positions are canonical.

4. **Fix `verify_ww_mission1.sh` macOS portability** (`timeout` → portable wrapper; add `.dylib` + `venv`/`.venv`) so the staged gate stops emitting false FAILs.

5. **Reconcile spec S1 with the implementation** (or formally supersede it) before writing any new spec — otherwise a new spec inherits the tick/economy/TutorialTrigger divergence.

Do NOT start a large refactor, the DDS terrain converter, or the trigger nyan language before steps 1–2 resolve — they are the cheapest way to learn whether the existing slice is already playable.

---

## 14. Evidence Appendix

**Key files & symbols**
- `openage/main/main.py:41,259` — `--scenario` flag, modpack selection.
- `libopenage/gamestate/simulation.cpp:182-206` — `mission_pack_dir` derivation from FQON; `:210-223` TriggerRuntime ctor + `wire_trigger_data`; `:435-473` `wire_trigger_data` (read pack, synthesize `defeat_enemy` from `required_removals`); `:477-494` `register_trigger_handler` (subscribe to `game.removal`); `:541-542` `is_victory()`.
- `libopenage/gamestate/game.cpp:255-305` — `Game::load_scenario` P2.7 mission-pack path vs legacy nyan-only path.
- `libopenage/scenario/mission_pack_loader.cpp:121` — `load_mission_pack_into_game_state`; `:396-419` entity_id resolution for trigger conditions; `:451-465` required_removals logging.
- `libopenage/scenario/mission_pack.cpp:40` — `read_mission_pack(dir)` reads `dir+"/scenario.json"`.
- `libopenage/scenario/trigger/trigger_runtime.cpp:36,55,131,171` — `register_handlers`, `add_trigger`, `notify_entity_removed`, `evaluate_trigger`.
- `libopenage/gamestate/objective/objective_controller.cpp:282,312,334,390,431` — legacy `WilliamWallaceMission01ObjectiveController`, hardcoded `EnemyVillager` FQON, `create_mission_N_trigger` factories, `TODO Phase C1–C4`.
- `libopenage/gamestate/system/move.cpp:279-470` — `Move::move_default`; unstaged P4.4F bisection/clamping WIP.
- `libopenage/input/semantic_action.cpp:56,158,185` — `entity_has_attack_ability`, right-click→ATTACK resolution.
- `libopenage/input/controller/game/controller.cpp:9-15` — ATTACK→AttackCommand dispatch.
- `assets/converted/ww_mvp/campaign/william_wallace/mission_01.nyan` — hand-written fixture; 3 PlacedEntities; NO triggers/objectives in nyan; `MissionEmptyMarkup`.
- `assets/converted/ww_mvp/campaign/william_wallace/scenario.json` — `VictoryDefinition.required_removals=["EnemyVillager"]`; `TriggerDefinition` absent; positions (6,4/8,8/10,4).
- `openage/scenario/dumps.py:39` — `DEFAULT_DUMP_DIR = docs/mvp/reports/mission1-scenario-dump`.
- `docs/mvp/reports/mission1-scenario-feasibility.md` — P1 Path D BLOCKED evidence.
- `.superpowers/sdd/p4_4d2_p4_4f_plan/progress.md` — Task 1–3 status, `WINDOWED_MULTI_UNIT_MANUAL_ACCEPTANCE_PENDING`, Task 3 review skipped.

**Commits**
- `56109051` (HEAD): register DynamicOccupancy at spawn + removal observer.
- `4b06ba0a`: P4.4D.2/F SDD plan.
- `6aa99aa4`: P4.3 data-driven victory demo (20).
- `dd148e35`: wire TriggerRuntime into production path.
- `d557b0e7`: WW Mission 1 runtime baseline audit.
- `fbb23fcf`: import WW plan + scenario spec (committed dump `produced_at_commit`).
- Tag `ww-mission1-p3-lifecycle-complete`.

**Commands executed (read-only / existing tests)**
- `git rev-parse/status/log/diff/worktree list/branch/tag`
- `ninja -n` (in `.bin/clang++-debug-Oauto-sanitize-none`)
- `bin/run test -d gamestate.tests.simulation_demo {7,9,10,11,12,13,14,20,35,36,37,38,42,46,49}` — all exit 0, PASS.
- `bin/run test --list`
- `venv/bin/python -m pytest openage/scenario/tests/` — 102 passed, 1 failed.
- `bin/run --scenario --headless --no-convert --no-prompts` (killed alive @30s).
- `scripts/run_ww_mission1.sh` (killed alive @25s).
- `scripts/verify_ww_mission1.sh` — Stages 1/2/4 FAIL (macOS portability), 3 PASS.

**Test names & results**
- PASS: demos 7, 9, 10, 11, 12, 13, 14, 20, 35, 36, 37, 38, 42, 46, 49.
- FAIL: `openage/scenario/tests/test_p2_integration.py::test_regenerated_dump_matches_committed_p1_dump` (AssertionError: position {'x':8,'y':8} != {'x':6,'y':4}).
- NOT RUN: `bin/run test -a` (full suite); ctest (0 registered).

**Failure-log summaries**
- `/tmp/verify.log` Stage 1: `missing .venv/bin/python3`, `missing libopenage.so` (macOS uses `venv/` + `.dylib`).
- `/tmp/verify.log` Stage 2: `rc=127` — `timeout: command not found` on macOS.
- `/tmp/verify.log` Stage 4: `rc=127`; `mission_fqon=0 playersetup_fqon=0 initial_entities=0 spawn(PV/TC/EV)=0/0/0` (launch never ran — `timeout` missing).
- pytest failure: `assert ep["position"] == other["position"]` → `{'x':8,'y':8} != {'x':6,'y':4}` at `test_p2_integration.py:469`.

**Runtime log highlights**
- `/tmp/headless.log` & `/tmp/ww_window.log`: `load_scenario: P2.7 production path`; `scenario_kind=William Wallace Mission 1 MVP reconstruction ... players=2 placements=3 chunks=4`; `spawned ... entity_id=0/1/2`; `victory required_removal target=EnemyVillager`; `wire_trigger_data: triggers=0 (data) + 1 (synthesized); runtime trigger_count=1`; windowed: `Created Qt window with OpenGL context`, `Created OpenGL renderer`, `kicking activity for 3 entities`, `Initializing input subsystem`.
- `logs/ww_mission1_20260803_011032.log`: interactive windowed run with MOVE commands; `Window close requested by user (QEvent::Close); shutting down`; clean `GameSimulation::stop` / `Presenter: Draw loop exited`.
- `/tmp/demo20.log`: `MissionState::declare_victory: reason='enemy_removed'`; `DEMO 20: PASS`.
- `/tmp/demo7.log`: `WW MISSION 1 HEADLESS REGRESSION: PASS (enemy_removed=1 objective_finished=1 time_advances=13 event_batches=13)`.
