# M0 — Baseline Integrity & Harness Portability Report

**Mission:** William Wallace Mission 01 — playable MVP
**Branch:** `mvp/william-wallace`
**Milestone:** M0 (Baseline Integrity & Harness Portability)
**Date:** 2026-08-05
**Status:** `VERIFIED_COMPLETE`
**Execution model:** decisions/planning + M0 only. No M1. No gameplay changes. No auto-commit.

---

## 1. Executive Result

**M0 status: `VERIFIED_COMPLETE`.** All 12 completion criteria are met. A trustworthy, reproducible, cross-platform-consistent Mission 01 verification baseline is established on this host, with one documented environment limit (no OpenGL context — Stage 4 `ENV_BLOCKED`). The stale `object_table.csv` content-drift found during M0 has been resolved by a user-authorized deterministic generated-product sync (§13.1).

| Dimension | Outcome |
|---|---|
| Source-of-truth adjudication | `COMMITTED_CONTENT_OVERRIDE_IS_SOURCE_OF_TRUTH` — the committed dump `scenario.json` is the declared single source of truth. |
| 6,4 vs 8,8 | Intentional, two-commit placement closure (b97ae872 → 59edcd69). Final (6,4)/(8,8)/(10,4) is correct; the nyan members and the old IR test fixture were stale. |
| Determinism | Pack regenerates byte-identical across two runs (4/4 files). |
| Scenario test suite | 103/103 PASS. |
| `verify_ww_mission1.sh` | Stages 1/2/3 PASS; Stage 4 `ENV_BLOCKED` (no GL context); exit 2. |
| Harness portability | macOS false-failures eliminated (portable timeout, python resolver, `.so`+`.dylib` discovery, GL/env reclassification). |
| Scope | `verify_ww_mission1.sh` (+256/-65), `test_p2_integration.py` (+19/-3), and `object_table.csv` (deterministic sync, §13.1) changed. No gameplay/sim/renderer/presenter/input/converter/trigger/content-SoT changes. User WIP untouched. |

**M1 entry condition: satisfied.**

---

## 2. Execution Contract

The M0 contract (read from the user's directive, restated before any edit) bound this execution to:

1. **Execute M0 only.** Stop on completion. Do not auto-enter M1.
2. **Do not touch user pre-existing changes.** No clean/stash/reset/overwrite of `libopenage/gamestate/system/move.cpp` (staged, +111/-38) or `doc/code/p4_4d2_p4_4f_plan.md` (unstaged, +1/-1).
3. **Do not fix the `controller.cpp` ATTACK gap under M0.**
4. **M0 edit prohibitions** (unless the adjudication proves a committed artifact is itself stale *and* the update is a deterministic generated-product sync): gameplay behavior, simulation, `TriggerRuntime`, renderer, presenter, Qt input, command resolver, converter media-export behavior, Mission 01 product content, entity spawn behavior.
5. **Anti-fake-fix ban:** no deleting failing tests; no relaxing assertions so both 6,4 and 8,8 pass; no weakening snapshot field checks; no `|| true`; no unconditionally marking a failing stage PASS; no skip-all-on-macOS; no auto-installing pytest/deps; no copying committed dump over IR (or vice-versa) without adjudicating provenance; no editing entity spawn to match a snapshot; no editing gameplay results to match golden.
6. **No third speculative edit.** If uncertain, mark a blocker; do not update a snapshot to make a test pass.
7. **No golden auto-overwrite in the default test run.**
8. **No M1. No gameplay changes. No auto-commit.**

The two permitted edit surfaces were the verification harness and the test fixture (stale-copy reconciliation), the latter only after a provenance adjudication.

---

## 3. Repository Baseline

Snapshot at M0 start (unchanged by M0 except the three files in §11):

- **Branch:** `mvp/william-wallace`
- **HEAD:** `56109051` — "gamestate: register DynamicOccupancy at spawn + independent removal observer"
- **Key dump-touching commits (most recent first):**
  - `59edcd69` — "gamestate: add rectangular Footprint + StaticOccupancyGrid + PlacementValidator" (refined dump `scenario.json` villager/enemy y 8→4; introduced Footprint (7,7)-(9,9))
  - `b97ae872` — "scene: close Mission 1 placements + macOS launch-script fixes" (moved placements 8,8/8,9/1,1 → 6,8/8,8/10,8; **declares** "The dump scenario.json … is the single source of truth; install.sh regenerates the converted pack on the next run.")
  - `02f5467c` — "scenario: emit chunk boundaries in Mission Pack"
  - `b132ba74` — "scenario: define normalized Scenario IR"
  - `2453f310` — "scenario: dump and validate Mission 1 reconstruction coverage"
- **Gitignored:** `assets/converted/` (`assets/.gitignore:2` `/converted/`) — the runtime pack is generated, not committed.
- **User WIP present at M0 start (untouched):** `libopenage/gamestate/system/move.cpp` (staged), `doc/code/p4_4d2_p4_4f_plan.md` (unstaged).

---

## 4. Artifact Provenance (制品溯源)

The Mission 01 artifact chain, traced end to end:

```
nyan source:  assets/nyan/ww_mvp/campaign/william_wallace/mission_01.nyan
                           │  (legacy nyan-only path; positions _Pos8_8 = 8,8/8,9/1,1)
                           ▼
install.sh:   assets/nyan/ww_mvp/install.sh
                :54-55  cp nyan + modpack.toml -> assets/converted/ww_mvp   (legacy material copied)
                :64-68  from openage.scenario.dumps import DEFAULT_DUMP_DIR
                        ir = ScenarioDefinition.from_dump(DEFAULT_DUMP_DIR)   ← PRODUCTION PATH
                        write_mission_pack(ir, .../assets/converted/ww_mvp/campaign/william_wallace)
                :15-17  comment: "the legacy nyan-only path is NOT used" on the production path
                           ▼
truth source:  docs/mvp/reports/mission1-scenario-dump/scenario.json   (COMMITTED; declared SoT)
                :  produced_at_commit = fbb23fc (ancestor of 59edcd69; regenerated at fbb23fc, committed in 59edcd69)
                           ▼
IR:            openage/scenario/ir.py:488  ScenarioDefinition.from_dump(dump_dir)
                           ▼
writer:        openage/scenario/writer.py:331  write_mission_pack(...)
                  :365  _write_object_table_csv(...)   → object_table.csv
                  + scenario.json / trigger_graph.json / coverage.json
                           ▼
runtime pack:  assets/converted/ww_mvp/campaign/william_wallace/*.json|csv   (gitignored, regenerated)
                           ▼
C++ loader:    libopenage/scenario/mission_pack_loader.cpp  (reads scenario.json at scenario-init)
```

**Provenance findings:**

- **P1.** The production path is `from_dump(DEFAULT_DUMP_DIR)` → `write_mission_pack`. `install.sh:64-68` is the authoritative generator. (`DEFAULT_DUMP_DIR` = `docs/mvp/reports/mission1-scenario-dump`, `openage/scenario/dumps.py:39`.)
- **P2.** The nyan position members (`mission_01.nyan:165` `_Pos8_8`, `:200` `PlacedEntity.position = _Pos8_8`, comments `:189-191` still 8,8/8,9/1,1) are **dead code on the production path** — `install.sh:15-17` states the legacy nyan-only path is NOT used; the C++ loader reads the regenerated pack, not the nyan placements.
- **P3.** Commit `b97ae872` explicitly declares the committed `scenario.json` the single source of truth and states `install.sh` regenerates the converted pack from it.
- **P4.** `assets/converted/ww_mvp/` is gitignored — it is a deterministic build product, never the truth source.
- **P5.** The committed dump directory is **internally inconsistent** (see §6, §13): `scenario.json` has final positions (6,4)/(8,8)/(10,4) but the sibling `object_table.csv` retains stale pre-b97ae872 positions (8,8)/(8,9)/(1,1).

---

## 5. Source-of-Truth Decision (真值来源裁决)

**Adjudication: `COMMITTED_CONTENT_OVERRIDE_IS_SOURCE_OF_TRUTH`.**

The committed dump `scenario.json` is the source of truth for Mission 01 placements. The IR is a faithful loader of that dump; the nyan members are dead code on the production path.

**Evidence matrix:**

| Claim | Evidence |
|---|---|
| Dump `scenario.json` is declared SoT | `b97ae872` commit message: "The dump scenario.json … is the single source of truth; install.sh regenerates the converted pack on the next run." |
| Production generates from the dump | `install.sh:64-68`: `from_dump(DEFAULT_DUMP_DIR)` → `write_mission_pack`. |
| nyan positions are dead code | `install.sh:15-17`: "the legacy nyan-only path is NOT used"; nyan still holds pre-b97ae872 `_Pos8_8` (8,8/8,9/1,1). |
| IR `from_dump` is a strict superset loader | `ir.py:488-490` comment: "The P2 IR is a strict superset of the P1 [dump]". |
| Runtime pack is a deterministic product | `assets/.gitignore:2` ignores `/converted/`; pack regenerates byte-identical (§9). |

**Rejected alternatives:**

- `IR_IS_SOURCE_OF_TRUTH` — would roll back the intentional P4.4 fixture design (b97ae872/59edcd69), break demo 9 position assertions, and constitute a gameplay/fixture-content edit forbidden in M0. Rejected.
- `UPSTREAM_SCENARIO_IS_SOURCE_OF_TRUTH` — Path D (regenerate dump from an upstream scenario) is blocked: there is no upstream scenario source for `ww_mvp` (it is a hand-authored fixture, not a converter product). Rejected.
- `UNRESOLVED_BLOCKER` — not warranted; provenance is fully traceable (P1–P4). Rejected.

**Consequence for the stale IR test fixture:** the `_mission1_scenario()` fixture in `test_p2_integration.py` was a pre-b97ae872 copy (positions 8,8/8,9/1,1). Because the dump is the SoT, the fixture — not the dump — was stale. Reconciling the fixture to the dump (§11) is the correct, SoT-respecting action and is **not** a fake fix: the strict `==` position assertion (`test_p2_integration.py:482`) is unchanged.

---

## 6. 6,4 vs 8,8 Evidence (差异裁决)

**Conclusion: the (6,4)/(8,8)/(10,4) placements are correct and intentional. The 8,8/8,9/1,1 values are stale residue (nyan dead code + old IR fixture + stale `object_table.csv`). No value was picked arbitrarily; the evidence matrix resolves every occurrence.**

### 6.1 Position evolution (git-proven)

| Commit | PlayerVillager | PlayerTownCenter | EnemyVillager | Notes |
|---|---|---|---|---|
| `2453f310`/`b132ba74`/`02f5467c` | 8,8 | 8,9 | 1,1 | original placements; matches nyan `_Pos8_8` |
| `b97ae872` | 6,8 | 8,8 | 10,8 | "close Mission 1 placements"; villager↔enemy distance ~10→4 tiles; TC between them; declares dump SoT |
| `59edcd69` | 6,4 | 8,8 | 10,4 | rectangular Footprint (7,7)-(9,9); villager/enemy y 8→4 to clear the footprint; TC stays 8,8 |
| **HEAD (current)** | **6,4** | **8,8** | **10,4** | committed dump `scenario.json` |

`b97ae872` shrank the villager↔enemy distance from ~10 to 4 tiles and placed the TC between them so Move/Attack must route around it. `59edcd69` then introduced the rectangular TC Footprint (7,7)-(9,9); with y=8 the villager/enemy spawn tiles would fall *inside* the footprint, so y was moved to 4 to clear it. Both steps are deliberate and consistent.

### 6.2 Where each stale 8,8/8,9/1,1 occurrence lives

| Location | Value | Status | M0 action |
|---|---|---|---|
| `scenario.json` (committed dump) | 6,4/8,8/10,4 | **correct (SoT)** | none |
| `mission_01.nyan:165,189-191,200` (`_Pos8_8`) | 8,8/8,9/1,1 | dead code on prod path | none (out of M0 scope; nyan is not the SoT) |
| `test_p2_integration.py` `_mission1_scenario()` (pre-M0) | 8,8/8,9/1,1 | **stale fixture** | **reconciled to dump** (§11) |
| `object_table.csv` (committed dump sibling) | 8,8/8,9/1,1 (pre-sync) | **stale derived artifact** | **synced** to 6,4/8,8/10,4 during M0 cleanup (§13.1) |

### 6.3 Why the pinning test passed before M0 yet the fixture was stale

`test_regenerated_dump_matches_committed_p1_dump` (`test_p2_integration.py:438-485`) writes a fresh pack from the IR fixture and compares **only `scenario.json` structural fields** (id, MapDefinition, per-entity fqon/PlayerId/**position**) against the committed `scenario.json`. With the stale fixture (8,8/8,9/1,1) the position assertion failed (`{'x':8,'y':8} != {'x':6,'y':4}`). Reconciling the fixture to the SoT dump (6,4/8,8/10,4) makes the assertion pass with the strict `==` intact. The test does **not** read `object_table.csv`, which is why the §6.2 CSV drift is undetected by it.

---

## 7. Golden Update Protocol

The committed dump `scenario.json` is a **golden** artifact (source of truth). This M0 establishes the following protocol so goldens are never silently overwritten:

1. **Golden = committed `docs/mvp/reports/mission1-scenario-dump/scenario.json`** (and, after the §13 sync, its sibling derived files). The runtime pack under `assets/converted/` is a non-golden deterministic product.
2. **Tests must not auto-overwrite golden in the default run.** Confirmed: `test_regenerated_dump_matches_committed_p1_dump` reads the committed golden read-only and compares; it writes only to `tmp_path`. No test in the scenario suite writes to `DEFAULT_DUMP_DIR`. The `verify_ww_mission1.sh` harness only reads the golden.
3. **Golden updates are deliberate, adjudicated, and manual.** A golden update is permitted only when (a) provenance adjudication proves the current golden is stale, and (b) the new content is a deterministic regeneration from the declared SoT. The update is performed by running the generator (`install.sh` path / `from_dump`→`write_mission_pack`) and committing the result with an adjudication record — never by hand-editing fields.
4. **Drift detection is structural, not cosmetic.** The pinning test compares fqon + PlayerId + position (not rotation/overrides/metadata). Loosening these fields is forbidden (anti-fake-fix ban). The `produced_at_commit` metadata field is explicitly exempt (differs by design).
5. **`object_table.csv` is a derived golden-sibling**, not the SoT. Its drift (§6.2, §13) is resolved by deterministic sync to the SoT, not by editing the SoT to match it.

---

## 8. Harness Portability Changes (`scripts/verify_ww_mission1.sh`)

The pre-M0 harness produced **false failures on macOS** from four portable-correctness defects. All four are fixed. Diff: +256/-65.

| Defect | Root cause | Fix |
|---|---|---|
| `timeout: command not found` → rc=127 misread as engine failure | GNU `timeout` absent on macOS; no fallback | `run_with_timeout()`: prefer `timeout`/`gtimeout`; fall back to a Python stdlib wrapper (`subprocess`+`start_new_session`+`os.killpg` SIGKILL on `TimeoutExpired` → exit 124). No external dependency. |
| Hardcoded `.venv/bin/python3` | assumed a single venv layout | `resolve_python()`: priority `$OPENAGE_PYTHON` → `python3` (PATH) → `venv/bin/python3` → `.venv/bin/python3`. Prints the resolved binary. |
| Hardcoded `.so` suffix | no `.dylib` on macOS | `find_libopenage()`: glob `.so*` **and** `.*.dylib` across known build dirs; no hardcoded suffix. |
| Stage 4 rc=1 misclassified `ENGINE_FAIL` | offscreen Qt plugin cannot create a GL 3.3 context on a headless host | `gl_err4` detection (`does not support createPlatformOpenGLContext` / `OpenGL version 3.3 is not available`); reclassifies to `ENV_BLOCKED` when the engine had already loaded + spawned. |
| Stage 2/4 budgets too tight (25s/30s) | de2_base nyan load = 874 files, ~34s on macOS | budgets raised to 60s. |
| Stage 2 evidence grep missed spawns | grepped literal `initial_entities=3` (never emitted) | grep `spawned placed_instance=ww_mvp` (actual log format); 3/3 spawns detected. |
| `loader_errors` false positives | grep `ABI`/`cannot open` matched `abi`**lity**`.nyan` | `loader_error_in()` narrowed to `dlopen|undefined symbol|mach-o, but wrong|wrong architecture|not a mach-o|image not found|libopenage.*(fail|cannot|not found)|cannot (open|load)[^[:alnum:]]*libopenage|dyld:.*lib`. loader_errors now 0. |
| Exit semantics undocumented | ad-hoc rc | Documented: **0** = pass; **1** = engine/content/harness failure; **2** = environment-blocked only. |
| Flat pass/fail | could not distinguish failure class | Per-stage 6-class classification: `PASS` / `ENGINE_FAIL` / `CONTENT_DRIFT` / `HARNESS_FAIL` / `ENV_BLOCKED` / `SKIPPED_WITH_REASON`. |

**Anti-fake-fix compliance:** the harness contains no `|| true`, no unconditional PASS, no skip-all-on-macOS. `ENV_BLOCKED` is asserted only when GL-context failure is positively detected *after* successful engine load+spawn — a genuine environment limit, not a suppression.

---

## 9. Test Results

### 9.1 `verify_ww_mission1.sh` (fresh run, 2026-08-05)

| Stage | Purpose | Result | Evidence |
|---|---|---|---|
| 1 | build/import smoke | **PASS** | python=`/opt/homebrew/bin/python3`, lib=`libopenage.0.dylib` |
| 2 | nyan database load (headless) | **PASS** | rc=124 (expected timeout-kill after load), `spawn_lines=3`, `nyan_errors=0`, `loader_errors=0` |
| 3 | media reference audit | **PASS** + `SKIPPED_WITH_REASON` | resolved=32, missing=0, excluded=2 (testscene fallbacks), deferred=29 (de2_base graphics, owned by de2_base, deferred to Gate B/converter) |
| 4 | real game startup (windowed) | **ENV_BLOCKED** | rc=1, `gl_errors=2`, but `mission_fqon=2 playersetup_fqon=2 spawn_lines=3 spawn(PV/TC/EV)=1/1/1 loader_errors=0` — engine loaded + spawned; only GL context missing |
| — | **VERIFY** | **exit 2** | `ENVIRONMENT BLOCKED — NO engine/content/harness failure` |

### 9.2 Scenario test suite (`pytest openage/scenario`)

- **103/103 PASS.**
- `test_regenerated_dump_matches_committed_p1_dump`: **PASS** (was failing pre-M0 on `{'x':8,'y':8} != {'x':6,'y':4}`).
- P2 integration: **12/12 PASS.**

### 9.3 Determinism (fresh check, 2026-08-05)

Pack regenerated twice from the `install.sh`/`from_dump` path into independent dirs; all 4 files byte-identical across runs:

| File | sha256 (identical in both runs) | Matches committed dump? |
|---|---|---|
| `scenario.json` | `0361c1bf280f850e81f048f390ef89bd607f40a1b12499f207157232b43c7a1e` | ✅ yes |
| `trigger_graph.json` | `0f01213d77ce0ef4fb5ffd859c2ff1d9ee793a8a8036c013474ece1be0c0431c` | ✅ yes |
| `coverage.json` | `f34cfb42d21342213964d52e5ba45e2f6d39de37162155dbc4a889f772d60e37` | ✅ yes |
| `object_table.csv` | `3f27a5a7e905b459994a496956acca72790f1aa0aa2999dfee2d6e2fd8884e3a` | ✅ yes (synced during M0 cleanup; was stale `3449f0a9…`, §13.1) |

**Determinism = CONFIRMED** (run1 == run2; committed `object_table.csv` now also matches the regeneration after the §13.1 sync). The pre-sync mismatch was committed-artifact staleness, **not** regeneration non-determinism — the regenerated CSV is stable across runs and consistent with `scenario.json`.

### 9.4 Demo regression (`bash` demos, rc=0 = pass)

Demos 7, 9, 10–14, 20, 35–38, 42, 46, 49: **all rc=0 PASS.** (Demo 9 position assertions match the closed placements.)

### 9.5 Harness syntax

`bash -n scripts/verify_ww_mission1.sh`: **OK.** (`shellcheck` not installed in this environment — not a blocker; syntax check passes.)

---

## 10. Failure Classification

The M0 taxonomy (6 classes) applied to every stage/result:

| Item | Class | Rationale |
|---|---|---|
| Stage 1 | `PASS` | binaries + key nyan present; python/lib resolved portably. |
| Stage 2 | `PASS` | nyan parses (0 errors), 3/3 entities spawn, 0 loader errors; rc=124 is the expected timeout-kill after a successful headless load (no main loop in headless mode). |
| Stage 3 | `PASS` + `SKIPPED_WITH_REASON` | all *active* Mission 1 media resolved; 29 de2_base graphics deferred (owned by de2_base, not yet exported — deferred to Gate B/converter, not load-bearing for Gate A placeholder rendering). |
| Stage 4 | `ENV_BLOCKED` | engine loaded + spawned 3/3; failure is purely the missing GL 3.3 context on this headless host. Not engine, not content, not harness. |
| `object_table.csv` committed drift | `CONTENT_DRIFT` —> **resolved** | non-load-bearing derived artifact stale vs SoT; synced during M0 cleanup (§13.1). |
| `test_regenerated_dump_matches_committed_p1_dump` (pre-M0) | `HARNESS_FAIL` (stale fixture) | the test itself was correct; its fixture was a pre-b97ae872 copy. Fixed by reconciling fixture to SoT. |

**Exit-code semantics verified:** the harness exited **2** (environment-only), correctly distinguishing "blocked on this host" from "engine/content/harness broke."

---

## 11. Modified Files

Three files changed, all within the M0-permitted surfaces (harness + test fixture + one deterministic generated-product sync).

### 11.1 `scripts/verify_ww_mission1.sh` (+256/-65)
Harness portability rewrite (§8). No gameplay/simulation/content logic. Adds `resolve_python()`, `find_libopenage()`, `run_with_timeout()`, `loader_error_in()`, `classify()`, 6-class per-stage classification, documented 0/1/2 exit semantics, GL `ENV_BLOCKED` reclassification, nyan-load budget 60s.

### 11.2 `openage/scenario/tests/test_p2_integration.py` (+19/-3)
Reconciled the stale `_mission1_scenario()` fixture to the source-of-truth dump:
- Positions: PlayerVillager (8,8)→(6,4); PlayerTownCenter (8,9)→(8,8); EnemyVillager (1,1)→(10,4).
- Added a 13-line provenance comment documenting that the dump is the SoT (`install.sh:64-68`, commit `b97ae872`), the nyan positions are dead code, and the fixture must not be "reconciled" back to the nyan.
- **Strict `==` position assertion at line 482 is unchanged.** No assertion relaxation.

### 11.3 `docs/mvp/reports/mission1-scenario-dump/object_table.csv` (M0 cleanup sync)
User-authorized deterministic generated-product sync (§13.1). The committed CSV held stale pre-b97ae872 positions (8,8)/(8,9)/(1,1), inconsistent with the source-of-truth `scenario.json` ((6,4)/(8,8)/(10,4)) in the same directory. Regenerated via the `install.sh`/`from_dump`->`write_mission_pack` path; a strict guard confirmed **only** `pos_x`/`pos_y` changed across all 3 rows (`scenario_object_id`/`scenario_fqon`/`player_id`/`entity_fqon`/`rotation`/`source` identical). Committed file overwritten by byte-exact `cp` (CRLF preserved, `writer.py:310` `newline=""`). sha256 `3449f0a9…` -> `3f27a5a7…`. Non-load-bearing (production loader does not read this file; `load_scenario_dump` checks ID-consistency only); zero test impact — 103/103 still PASS, determinism confirmed. Not a hand-edit and not a dump↔IR copy: the SoT (`scenario.json`) was unchanged; only the stale derived sibling was synced to it.

### 11.4 Not modified (scope compliance)
- `libopenage/gamestate/system/move.cpp` (user WIP, staged +111/-38) — **untouched**.
- `doc/code/p4_4d2_p4_4f_plan.md` (user WIP, unstaged +1/-1) — **untouched**.
- `docs/mvp/reports/mission1-scenario-dump/scenario.json` (committed golden / SoT) — **untouched**.
- No gameplay, simulation, `TriggerRuntime`, renderer, presenter, Qt input, command resolver, converter export, or entity-spawn code changed.

---

## 12. Scope Compliance

| M0 rule | Compliance |
|---|---|
| Execute M0 only, no M1 | ✅ M0 complete; M1 not started. |
| Don't touch user WIP | ✅ `move.cpp`, `p4_4d2_plan.md` byte-identical to M0 start. |
| Don't fix `controller.cpp` ATTACK gap | ✅ not touched. |
| No gameplay/sim/trigger/renderer/presenter/input/resolver/converter/content/spawn edits | ✅ only harness + test fixture changed. |
| Anti-fake-fix ban | ✅ no test deleted; no assertion relaxed (strict `==` intact); no `|| true`; no unconditional PASS; no skip-on-macOS; no dep auto-install; no dump↔IR copy without adjudication; no spawn/gameplay edit to match a snapshot. |
| No third speculative edit | ✅ harness + fixture edits evidence-driven; the `object_table.csv` sync was performed only after explicit user authorization (M0 cleanup), with a strict guard proving only positions changed. No speculative edit. |
| No golden auto-overwrite | ✅ tests read golden read-only; harness reads only. |
| No auto-commit | ✅ no `git commit`/stash/reset/checkout performed. |

**Budget note (transparency):** the harness change (+256/-65) exceeds the plan's ~80-line figure. The overrun is comments documenting portability rationale (required by the M0 contract: documented exit codes, printed python binary, library-failure distinction) plus the mandated 6-class classification + portable-timeout wrapper. The `move.cpp` resolution that the plan budget assumed was removed from this M0 per the user's narrower scope, so no gameplay lines were spent. Zero gameplay/simulation lines changed; the spirit of the budget is preserved.

---

## 13. Remaining Blockers / Findings

### 13.1 `object_table.csv` committed drift — `CONTENT_DRIFT` — **RESOLVED** (deterministic sync applied during M0 cleanup)

- **Finding:** the committed `docs/mvp/reports/mission1-scenario-dump/object_table.csv` retained pre-b97ae872 positions (8,8)/(8,9)/(1,1), inconsistent with the source-of-truth `scenario.json` (6,4)/(8,8)/(10,4).
- **Mechanism:** `object_table.csv` was last regenerated at `2453f310`/`02f5467c` (pre-b97ae872). `b97ae872` and `59edcd69` closed the `scenario.json` placements but did **not** regenerate `object_table.csv`. Its stale positions exactly match the nyan dead-code `_Pos8_8` (8,8/8,9/1,1).
- **Impact: non-load-bearing.** The production C++ loader (`mission_pack_loader.cpp`) does **not** read `object_table.csv` (the `mission_pack.h:11` mention is a doc comment). `load_scenario_dump` (`dumps.py:79-90`) reads it only for an **ID-consistency** check (passes — same 3 IDs); it does not validate CSV positions against `scenario.json`. No test failed.
- **Remediation method (permitted by M0 exception):** deterministic sync — regenerate `object_table.csv` from the IR via the `install.sh`/`from_dump`→`write_mission_pack` path and overwrite the committed file. One-command:
  ```bash
  .venv/bin/python3 -c "import pathlib,sys; sys.path.insert(0,'.'); from openage.scenario.ir import ScenarioDefinition; from openage.scenario.dumps import DEFAULT_DUMP_DIR; from openage.scenario.writer import write_mission_pack; write_mission_pack(ScenarioDefinition.from_dump(DEFAULT_DUMP_DIR), pathlib.Path('/tmp/m0_sync'))" && cp /tmp/m0_sync/object_table.csv docs/mvp/reports/mission1-scenario-dump/object_table.csv
  ```
  produces sha256 `3f27a5a7…` (consistent with `scenario.json`).
- **Resolution (user-authorized M0 cleanup):** regenerated `object_table.csv` from the IR via the `install.sh`/`from_dump`->`write_mission_pack` path. A strict guard confirmed **only** `pos_x`/`pos_y` changed across all 3 rows; `scenario_object_id`/`scenario_fqon`/`player_id`/`entity_fqon`/`rotation`/`source` identical. Committed file overwritten by byte-exact `cp` (CRLF preserved, `writer.py:310` `newline=""`). sha256 `3449f0a9…` -> `3f27a5a7…`, now consistent with `scenario.json`.
- **Post-sync verification:** `load_scenario_dump` OK (positions now 6,4/8,8/10,4); determinism confirmed (regenerated == committed, byte-identical); pinning test PASS; full scenario suite 103/103 PASS.
- **Scope compliance:** this is the M0-permitted deterministic generated-product sync (provenance adjudicated in §5; the committed derived artifact was proven stale; the update is a regeneration, not a hand-edit or a dump↔IR copy; the SoT `scenario.json` was not modified). No gameplay/sim/content-SoT change. Not committed (no auto-commit).

### 13.2 Stage 4 GL context — `ENV_BLOCKED`, **environment**, must be re-verified before Gate B

- **Finding:** no OpenGL 3.3 context on this headless macOS host (offscreen Qt plugin cannot create one). Stage 4 cannot verify windowed rendering here.
- **Impact:** engine load + spawn are verified (3/3 entities, 0 loader errors); only GL rendering is unverifiable on this host.
- **Remediation:** re-run Stage 4 on a GL-capable host (real display or `Xvfb`) before Gate B. Not an M0 failure — M0 criterion #6 (no macOS false engine-failure from missing GNU `timeout`) is satisfied; Stage 4 is correctly classified `ENV_BLOCKED`, not `ENGINE_FAIL`.

### 13.3 `shellcheck` unavailable
- Syntax verified via `bash -n` (OK). `shellcheck` not installed; not a blocker.

**No `UNRESOLVED_BLOCKER`.** All findings are either environment limits (re-verifiable elsewhere) or a non-load-bearing drift with a deterministic remedy.

---

## 14. M1 Readiness

**M1 entry condition: satisfied.**

- M0 status `VERIFIED_COMPLETE`; all 12 completion criteria met.
- Source-of-truth adjudicated (`COMMITTED_CONTENT_OVERRIDE_IS_SOURCE_OF_TRUTH`); provenance fully traced (§4).
- 6,4/8,8 resolved: (6,4)/(8,8)/(10,4) is the intentional, git-proven SoT; stale copies reconciled (fixture + `object_table.csv` synced) or documented (nyan dead code).
- Golden protocol established (§7); no golden auto-overwrite.
- Harness portable: Stages 1/2/3 PASS on macOS; Stage 4 is a pure environment limit.
- Test suite green: 103/103; pinning test PASS with strict `==`.
- Determinism confirmed (regeneration byte-identical).
- Scope clean: only harness + fixture changed; user WIP untouched; no auto-commit.

**M1 may begin** (Mission IR Provenance Pin) when the user explicitly authorizes it. The §13.1 `object_table.csv` sync (recommended M0 cleanup) has been applied; no outstanding M0 cleanup remains.

---

## 15. Evidence Appendix

### 15.1 Provenance citations
- `assets/nyan/ww_mvp/install.sh:54-55` — cp nyan + modpack.toml → `assets/converted/ww_mvp` (legacy material).
- `assets/nyan/ww_mvp/install.sh:64-68` — `from_dump(DEFAULT_DUMP_DIR)` → `write_mission_pack` (production generator).
- `assets/nyan/ww_mvp/install.sh:15-17` — "the legacy nyan-only path is NOT used" on the production path.
- `openage/scenario/dumps.py:39` — `DEFAULT_DUMP_DIR = docs/mvp/reports/mission1-scenario-dump`.
- `openage/scenario/dumps.py:60-104` — `load_scenario_dump`; reads `object_table.csv` for ID-consistency only (`:86-90`).
- `openage/scenario/ir.py:488-490` — `ScenarioDefinition.from_dump`; "P2 IR is a strict superset of the P1 [dump]".
- `openage/scenario/writer.py:303-365` — `_write_object_table_csv` + `write_mission_pack`.
- `assets/nyan/ww_mvp/campaign/william_wallace/mission_01.nyan:165,189-191,200` — `_Pos8_8` dead-code positions (8,8/8,9/1,1).
- `assets/.gitignore:2` — `/converted/` (runtime pack gitignored).
- `libopenage/scenario/mission_pack.h:11` — `object_table.csv` doc comment (not read by loader).

### 15.2 Commit provenance
- `b97ae872` — "scene: close Mission 1 placements + macOS launch-script fixes"; moves 8,8/8,9/1,1 → 6,8/8,8/10,8; **declares dump `scenario.json` the single source of truth**.
- `59edcd69` — "gamestate: add rectangular Footprint + StaticOccupancyGrid + PlacementValidator"; refines `scenario.json` villager/enemy y 8→4 (Footprint (7,7)-(9,9)).
- `fbb23fc` — "docs: import William Wallace Mission 1 development plan + scenario spec"; ancestor of `59edcd69`; the `produced_at_commit` stamped in the committed `scenario.json`.
- `56109051` — HEAD at M0.

### 15.3 Position evolution (git)
```
2453f310/b132ba74/02f5467c : (8,8)/(8,9)/(1,1)   [also object_table.csv last regenerated here]
b97ae872                   : (6,8)/(8,8)/(10,8)  [scenario.json; object_table.csv NOT regenerated]
59edcd69                   : (6,4)/(8,8)/(10,4)  [scenario.json; object_table.csv NOT regenerated]
HEAD                       : (6,4)/(8,8)/(10,4)  [committed scenario.json — SoT]
```

### 15.4 Fresh verification artifacts (2026-08-05)
- `verify_ww_mission1.sh` run log: stages 1/2/3 PASS, stage 4 `ENV_BLOCKED`, exit 2 (captured inline during M0).
- Stage 4 startup log: `logs/ww_verify_startup_20260806_003201.log` — `mission_fqon=2 playersetup_fqon=2 spawn_lines=3 spawn(PV/TC/EV)=1/1/1 loader_errors=0 gl_errors=2`.
- Determinism: two independent regenerations → byte-identical (§9.3 hashes).
- Committed dump sha256 (post-M0-cleanup): `scenario.json` `0361c1bf…`, `object_table.csv` `3f27a5a7…` (synced; was stale `3449f0a9…`), `trigger_graph.json` `0f01213d…`, `coverage.json` `f34cfb42…`.

### 15.5 Test-fixture diff summary
`test_p2_integration.py`: 3 position lines reconciled to SoT + 13-line provenance comment. Strict `==` assertion (`:482`) unchanged. Net +19/-3.

---

*End of M0 report. M0 is `VERIFIED_COMPLETE`. Stopped per contract: M1 not started; no gameplay changes; no auto-commit.*
