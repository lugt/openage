# William Wallace Mission 01 MVP — Progress

Branch: `mvp/william-wallace`
Tracking: `IMPLEMENTATION-PLAN-v2.md` milestones M0–M8.
Gate A (M6) verifies the desktop gameplay loop on the hand-authored mission.
Gate B (M7) verifies that loop on converter-backed mission content.

## Milestone status

| Milestone | Title | Status | Notes |
|-----------|-------|--------|-------|
| **M0** | Baseline Integrity & Harness Portability | 🟡 `WIP` | 2026-08-05: all 12 completion criteria met, tests all green (scenario suite 103/103; 35-demo regression PASS). 2026-11-16: status downgraded from `VERIFIED_COMPLETE` — acceptance-harness false positives were found (a FATAL crash marker in the log with engine rc=0 still passed; the regression script printed FAIL=1 but exited 0). Cleanup in progress (Gate A automation Task 1); see the 2026-11-16 section below. |
| M1 | Visibility Spike | Desktop verified | The centered starting camera shows both villagers and the Town Center. |
| M2 | Render Breakpoint Localization | Conditional | The opening visibility issue was resolved by centering the camera on the placed entities. |
| M3 | Real-Qt Selection | Desktop verified | The OS input helper selected both villagers. The HUD displayed the enemy's owner and HP, and a right-click on ground issued no order for the enemy. |
| M4 | Real-Qt Attack Wiring | Desktop verified | The same run included a move, an attack, HP depletion and removal from simulation and rendering. |
| M5 | Player-Visible Victory | Desktop verified | The objective changed to “Enemy defeated,” “VICTORY” appeared, and the game exited with code 0. |
| M6 | Gate A Acceptance | 🟡 `WIP` | Desktop playthrough recorded below. The portable in-process Qt verifier remains pending. |
| M7 | Gate B Content | 🟡 `WIP` | The reconstruction renders converted DE2 grass. Original scenario import is pending. |
| M8 | MVP Handoff | 🔲 not started | |

## M0 outcome summary

- **Source-of-truth adjudication:** `COMMITTED_CONTENT_OVERRIDE_IS_SOURCE_OF_TRUTH`. The committed dump `docs/mvp/reports/mission1-scenario-dump/scenario.json` is the declared single source of truth (commit b97ae872 message; `assets/nyan/ww_mvp/install.sh:64-68` regenerates the runtime pack via `ScenarioDefinition.from_dump(DEFAULT_DUMP_DIR)` → `write_mission_pack`). The nyan position members (`_Pos8_8` at `mission_01.nyan:165,200`, still pre-b97ae872 (8,8/8,9/1,1)) are dead code on the production path (`install.sh:15-17`: "the legacy nyan-only path is NOT used").
- **6,4 vs 8,8:** Intentional, two-commit placement closure. b97ae872 moved (8,8/8,9/1,1)→(6,8/8,8/10,8); 59edcd69 introduced the rectangular Footprint (7,7)-(9,9) and moved villager/enemy y 8→4 to clear the footprint (TC stays 8,8). Final (6,4)/(8,8)/(10,4) is correct; nyan/IR-fixture drift is the stale copy, not the dump.
- **Files changed (M0 only):**
  - `scripts/verify_ww_mission1.sh` (+256/-65): portable timeout (GNU→Python stdlib fallback), python resolver, libopenage discovery (.so + .dylib), per-stage 6-class classification, documented 0/1/2 exit semantics, GL-context `ENV_BLOCKED` reclassification, nyan-load budget 25s→60s.
  - `openage/scenario/tests/test_p2_integration.py` (+19/-3): stale `_mission1_scenario()` fixture positions (8,8/8,9/1,1)→(6,4/8,8/10,4) + provenance comment. Strict `==` assertion at line 482 **unchanged**.
  - `docs/mvp/reports/mission1-scenario-dump/object_table.csv` (M0 cleanup sync, user-authorized 2026-08-05): deterministic generated-product sync via `from_dump`->`write_mission_pack`; strict guard confirmed only pos_x/pos_y changed across all 3 rows. sha256 `3449f0a9`->`3f27a5a7`. Non-load-bearing (production loader does not read it); 103/103 still PASS, determinism confirmed. Not committed.
- **Test results:** scenario suite 103/103 PASS; P2 integration 12/12 PASS; `test_regenerated_dump_matches_committed_p1_dump` PASS; demos 7/9/10-14/20/35-38/42/46/49 rc=0 PASS; determinism — pack regenerates byte-identical (4/4 files); committed `object_table.csv` synced to SoT (was stale pre-b97ae872; report §13.1).
- **Platform limit:** headless host has no OpenGL 3.3 context → Stage 4 (`ENV_BLOCKED`, exit 2). Engine loads + spawns correctly; only GL rendering is unverifiable here. Must be verified on a GL-capable host before Gate B.
- **M1 entry condition:** ✅ satisfied (M0 `VERIFIED_COMPLETE`).

## 2026-11-16 harness truthfulness update (Gate A automation Task 1)

- **Why M0 was downgraded:** the acceptance harness reported success on runs that were actually crashes. `run_ww_mission1.sh` propagated only `PIPESTATUS[0]`, so an engine that printed `FATAL: terminate has been called` / `uncaught exception` / `SIGSEGV` (libopenage/error/handlers.cpp) but still exited 0 passed verification; `p4_4d_regression.sh --regression` printed `FAIL=1` but exited 0.
- **Fix (working tree, uncommitted):** new shared checker `scripts/lib/ww_mission1_log_check.sh` (`ww_mission1_log_has_fatal <log>`: 0 = crash marker found — `FATAL: terminate has been called`, `uncaught exception`, or `SIGSEGV` not on a `139=SIGSEGV` exit-code help line); `run_ww_mission1.sh` and `verify_ww_mission1.sh` (stage 4) now classify such logs as `ENGINE_FAIL` even when rc=0, and both fail fast (exit 2 / exit 1 with a `cannot source` diagnostic) if the checker itself cannot be sourced — an unguarded failed `source` would silently disable crash detection; `p4_4d_regression.sh --regression` now exits 1 when any demo prints no PASS marker. `OPENAGE_RUN_BIN` was added as a test-only hook (default remains `$REPO/bin/run`; the normal user command is unchanged).
- **Tests:** `scripts/tests/test_ww_mission1_verification.py` (13 tests) covers: clean log incl. help text = no crash; each real crash marker with rc=0 = failure; clean rc=0 still passes; one demo without PASS = whole regression exits non-zero; all-PASS = exit 0; a missing/unreadable shared checker aborts both launch scripts loudly (exit 2 / exit 1 with a `cannot source` diagnostic) instead of silently passing. RED (11 failed) captured before implementation, GREEN after (11, then 13 after the review-fix pass). Post-change real regression re-run (2026-11-16): `scripts/p4_4d_regression.sh --regression` → `PASS=35 FAIL=0`, exit 0.
- At this checkpoint, demo 51 covered input bindings and three-entity desktop evidence was pending. The later desktop run is recorded below.

## 2026-11-16 desktop playthrough and source audit

- The production launcher `bash scripts/run_ww_mission1.sh` loaded the mission HUD and rendered all three starting entities. Native mouse input selected the enemy and then controlled the player villager through move and attack. The enemy disappeared from the map, the objective changed to “Enemy defeated,” and the victory panel was shown before exit 0. Local evidence is in `logs/ww_mission1_desktop_20261116/` (`session.log`, `initial.png`, `enemy-selected.png`, `victory.png`).
- The local AoE2 DE installation contains the original `resources/_common/campaign/cam1.aoe2campaign.gpv` archive and `cam1.json`, which contains seven intro/outro entries. The active `ww_mvp` scenario is still hand-authored. Its two generic entity definitions reference 29 unique sprite files, of which 10 are absent in the current converted asset directory. The original grass DDS exists at `resources/_common/terrain/textures/2x/g_grs.dds`. The converted grass definition references a `texture_grass.terrain` that is absent, so the current mission continues to render its test terrain.

## 2026-11-16 DE2 terrain and scenario maps

- `assets/nyan/ww_mvp/install.sh --grass-dds <g_grs.dds>` decodes original DE2 grass to PNG and writes both openage descriptors. The installer uses the generated graphics on subsequent installs. A clean asset directory can still install the fixture with no arguments. The normal launch script uses whichever version is installed.
- Other individual terrain DDS files can be exported with `venv/bin/python -m openage.convert.processor.export.dds_terrain <source.dds> <target.png>`. The tested water target was `assets/converted/de2_base/data/terrain/water/graphics/texture_water.png`.
- Mission Packs keep per-tile terrain and elevation in row-major order, including partial edge chunks. Terrain rendering shares elevation at chunk corners. Both the land A* and flow-field grids block water. Unit movement and mouse picking still use the flat gameplay plane.
- The desktop rendered Mission 1 with DE2 grass and an isolated 14×12 test map with DE2 water and grass. The headless regression verified the rectangular edge, water path costs and Mission 1 victory. Evidence is in `logs/ww_mission1_terrain_20261116/`. The installed Mission 1 still uses the hand-authored layout.

## 2026-11-16 Marching and Fighting MVP

- The installer accepts `--marching`. It builds a 16×16 Mission Pack from the authored tile grid in `assets/nyan/ww_mvp/marching.py`, including the road, three player-controlled militia, an English outpost, a Scottish village and two English militia. To provision its textures, pass `--grass-dds` with `g_grs.dds` and `--road-dds` with `g_rd1.dds` from the DE2 2x terrain directory. Later installs reuse the converted textures.
- Launch with `bash scripts/run_ww_mission1.sh`. The mission uses the existing Move and Attack commands. Victory requires all three hostile targets to be destroyed. The headless command `bin/run test -d gamestate.tests.simulation_demo 7` checks the installed profile. Running the installer without `--marching` restores the three-entity fixture used by the wider regression suite.
- The original scenario layout is still packaged inside `cam1.aoe2campaign.gpv`. The authored map uses the local original tutorial text as a reference. Flag progression, fog, automatic ally transfer and the scripted English counterattack are pending.

## 2026-11-17 Original terrain, player colors and combat data

- The local DE2 save identifies its source as `1_Wallace_1.aoe2scenario`. The terrain reader extracts its 120×120 grid, preserving 23 terrain IDs and elevation values. Install the terrain preview with `bash assets/nyan/ww_mvp/install.sh --original-map-save <WILLIAM-WALLACE-01.aoe2spgame> <DE2 terrain/textures/2x directory>` and use the normal launcher. The preview uses the original DDS names from `empires2_x2_p1.dat`. Saved entity placements and trigger data still need conversion. Install with `--marching` again to play the authored scenario.
- DE2 SLD conversion now preserves the player-color mask in the atlas alpha markers. The world shader shades those pixels using the owner ID. Existing converted atlases require regeneration. Local militia and outpost atlases were regenerated.
- Preplaced buildings use their converted maximum health. Attack matches effect and resistance classes from the converted DAT data. In the Marching and Fighting profile, the first militia hit takes the outpost from 500 HP to 496 HP. This uses generic DE2 stats. Scenario-specific modifiers still need to be checked against the source.
- Local evidence is in `logs/ww_mission1_original_terrain.png`, `logs/ww_mission1_original_map.json` and `logs/ww_mission1_team_colors.png`.

## 2026-11-17 Fire, scripted construction and selection

- Install the original DE2 outpost fire animations with `bash assets/nyan/ww_mvp/install.sh --marching --fire-particles <DE2 resources/_common/particles>`. Three looping fire sizes use the converted outpost thresholds of 75%, 50% and 25% HP. At death, the fire overlay gives way to the existing destruction animation.
- A `construction_seconds` placement override starts a scripted building at its foundation. Progress increases construction HP and uses the converted stage graphics. The friendly outpost in the Marching profile finishes after 12 seconds. Villager-issued building placement and resource costs are pending.
- Single clicks use the world sprite ID buffer at logical window coordinates. Selection tints the entire sprite and retains the selection indicator. The existing box selection still works by area.
- Local desktop evidence for foundation, finished building, selection and fire is in `logs/ww_fire_construction/verified/`.

## 2026-11-17 Campaign launcher and first two tutorials

- `bash scripts/run_campaign.sh` opens the desktop selector for Marching and Fighting, Feeding the Army, and the original-geometry Mission 1 preview. The same launcher accepts `--mode marching`, `--mode feeding`, and `--mode terrain` for terminal use. It installs the selected profile and starts the normal desktop renderer.
- Marching and Fighting now uses movement checkpoint triggers. At the first road bend, two allied soldiers join the player. Victory requires marching there, destroying the outpost, reaching the village checkpoint, and clearing the two English militia. Verified by the Mission 1 headless regression. The English remain preplaced, and the original counterattack, fog and complete trigger import are pending.
- Feeding the Army uses DE2 villagers, berries, trees, gold mines and a town center on an authored map. Right-click a resource with a selected villager to collect a load and return it to the town center. The HUD counts delivered food, wood and gold. Each resource must reach 50 to win. The Mission 2 regression verifies no starting credit and victory after the deliveries. The original Mission 2 layout, fog and tutorial presentation are pending.

## 2026-11-23 Full original scenario import, tech-tree catalog and economy actions

- `scripts/export_tech_trees.py` exports the full DE2 civilization and technology catalog into `logs/tech-trees`, and `scripts/export_wallace_scenarios.py` unpacks `cam1.aoe2campaign(.gpv)` to extract `1_Wallace_1.aoe2scenario` (120×120 map, 5,698 objects, 31 triggers) and `1_Wallace_2.aoe2scenario` (120×120 map, 10,130 objects, 27 triggers) into `logs/wallace-original`.
- `bash assets/nyan/ww_mvp/install.sh --original-scenario <mission-N.json> <AgeOfEmpires2Data>` converts the original terrain, placements (preserving fractional coordinates and terrain elevation), starting resources, player-disabled technologies/units/buildings (such as disabled Loom and Mission 1 villager training restriction), and original trigger graphs into a playable Mission Pack. `scripts/campaign_launcher.py` uses the original scenarios automatically when the local DE2 installation is present.
- `OriginalScenario` (`libopenage/scenario/trigger/original_scenario.cpp`) executes the original AoE2 trigger conditions and effects, including flag progression, forced technology research, mid-route reinforcements, Gaia village rescue, the English counterattack and victory chain in Mission 1, and timed tutorial villager spawns and 50 Food / 50 Wood / 50 Gold stockpile victory in Mission 2. Verified end-to-end by `bin/run test -d gamestate.tests.simulation_demo 53` for both Mission 1 and Mission 2, alongside `simulation_demo 52` (tech-tree export) and the 35-demo P4.4D regression suite.

## Stop-rule compliance (historical record, 2026-08-05)

Original M0-era compliance record, kept for history. It describes the tree
as of 2026-08-05 and is superseded for M1/M3/M4 by the milestone table and
the 2026-11-16 section above (M1/M3/M4 = WIP; demo 51 implemented but real
Qt acceptance not completed). Two statements were also corrected against the
current tree: `libopenage/gamestate/system/move.cpp` is unstaged
working-tree WIP (it was originally recorded here as "staged"), and
`doc/code/p4_4d2_p4_4f_plan.md` has since been committed in the authorized
cleanup series (baseline `eea23d0d`).

- Only M0 was executed at the time; M1 had not been started then (as of
  2026-11-16, M1/M3/M4 are WIP per the milestone table). M0 itself touched
  no gameplay/simulation/renderer/presenter/Qt-input/command-resolver/
  converter-export/mission-product-content code.
- User pre-existing WIP (`libopenage/gamestate/system/move.cpp`, +111/-38 at
  the time) left untouched.
- No git commit/stash/reset/checkout performed during M0. No golden
  auto-overwrite.
- No third speculative edit made; the `object_table.csv` sync was performed
  only after explicit user authorization (M0 cleanup), with a strict guard
  proving only positions changed. No fake-fix (no `|| true`, no assertion
  relaxation, no test deletion, no skip-on-macOS).
