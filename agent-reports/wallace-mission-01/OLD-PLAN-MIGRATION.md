# Old Plan Migration - William Wallace Mission 01

Date: 2026-08-05
Branch: `mvp/william-wallace` @ `56109051`
Status: **Decision/planning only. No code/tests/build/assets modified.**
Authority: Maps the historical plan of record (S2 `WW_MISSION1_DEVELOPMENT_PLAN.md` phases P0-P7 + active sub-plan S7 P4.4D.2/F + superseded S1 `mvp-gameplay-spec.md`) onto the new `MVP-SPEC-v2.md` / `IMPLEMENTATION-PLAN-v2.md` gates (A/B) and milestones (M0-M8). Grounded in `current-state-audit.md` §5 (Verification Matrix) and §8 (Capability Matrix).

**Allowed dispositions (only these):**
- `KEEP_VERIFIED` - already VERIFIED_COMPLETE; do not re-implement; carry as regression.
- `VERIFY_IN_WINDOW` - verified headless/built but NOT in the windowed path; must be re-verified in Gate A (M1-M6).
- `REDEFINE` - the old goal is valid but its acceptance/shape changed; redefine under the new gates.
- `MOVE_TO_GATE_B` - depends on converter-backed content; belongs to Gate B (M7), not Gate A.
- `DEFER` - out of MVP scope; postpone (may be a future mission / polish).
- `DELETE_OBSOLETE` - the design was replaced; remove from the active plan.
- `REPLACE` - supersede with a different mechanism that already exists.
- `BLOCKED` - hard external blocker; cannot proceed until resolved.

**Audit status legend:** VC=VERIFIED_COMPLETE, P=PARTIAL, IU=IMPLEMENTED_UNVERIFIED, B=BLOCKED, NI=NOT_IMPLEMENTED.

---

## A. S2 Phase-level migration (P0-P7)

| Old requirement/task | Audit status | Evidence | New disposition | New milestone | Reason |
|---|---|---|---|---|---|
| P0 baseline audit | VC | `docs/mvp/reports/baseline-reality-audit.md` | KEEP_VERIFIED | M0 (regression baseline) | Trusted baseline exists; re-affirm in M0. |
| P1 source feasibility (Path D import) | B (by design) | S4; `test_p1_dump.py`; original AoE2DE source unimportable (VER 8.9 dat) | BLOCKED | M7 (Gate B unblock) | Hard external blocker; original scenario import is blocked. The `ww_mvp` hand-written fixture is the Gate-A substitute (explicitly labelled, not converter-backed). |
| P2.1-P2.5 IR+writer+loader (map/players/placements) | VC | 102 pytest PASS; `mission_pack.{h,cpp}`; headless P2.7 log | KEEP_VERIFIED | M0 (regression) | Load path verified; do not re-implement. |
| P2.6 IR<->dump round-trip integration | P | `test_p2_integration.py` 1 FAIL (pos 8,8 vs 6,4) | REDEFINE | M0 (D0 adjudication) | Drift is a defect (D0). The IR is the source; the dump + production `scenario.json` are products. Adjudicate which is canonical (IR-as-source -> regenerate product; OR intentional product-override -> update IR + record override), record the decision, then make product match source. NOT a blind "regenerate to pass". Reproducibility fix, not a feature. |
| P2.7 native production integration (mission pack -> Game) | VC | `game.cpp:264`; `simulation.cpp:182`; demo 9; headless+windowed logs | KEEP_VERIFIED | M0 (regression) | Production load path verified. |
| P3.1-P3.4 lifecycle (effect->health->death->removal) | VC | demos 10/11/12/13; `event/attack.cpp`; `system/move.cpp` | KEEP_VERIFIED | M0 (regression) | Combat lifecycle verified; M4 wires input to it, does not rebuild it. |
| P3.5 mandatory per-increment tests | VC | demos 10-14 PASS | KEEP_VERIFIED | M0 (regression) | Test discipline retained. |
| P4.1 event-driven objective (remove polling) | VC | `objective_controller.cpp` (`objective_poll_count=0`); demo 14 | KEEP_VERIFIED | M0 (regression) | Polling debt removed; preserved. |
| P4.2 TriggerInstance + runtime | VC | `trigger_runtime.cpp`; demos 16-19 | KEEP_VERIFIED | M0 (regression) | Runtime verified; reused by M4/M5 victory chain. |
| P4.3 data-driven victory (TriggerRuntime in production) | VC | `simulation.cpp:435` `wire_trigger_data`; demo 20 (negative assertion) | KEEP_VERIFIED | M0 (regression) | Victory is data-driven + negative-tested; the M4 chain reuses it via a REAL removal. |
| P4.4A selection viewmodel (SelectedEntityView/MissionView) | VC | demos 22-24; `selection_model.*` | VERIFY_IN_WINDOW | M3 (D2) | Verified headless; must be re-verified driven by REAL Qt LMB in windowed path. |
| P4.4B footprint/placement (StaticOccupancyGrid) | VC | demos 25-27; `static_obstacles=1` log | KEEP_VERIFIED | M0 (regression) | Placement verified. |
| P4.4C pathfinding (GridAStar + clearance + waypoints + TC routing) | VC | demos 28-32; windowed `Move::move_default` log | VERIFY_IN_WINDOW | M3/M4 (real-Qt move) | Pathfinding verified; must be re-verified with a REAL Qt RMB move command in windowed path. |
| P4.4D input action resolution (ContextActionResolver + AttackApproach) | VC (headless) | demos 35-38; `semantic_action.cpp:99` `resolve_context_action` | REDEFINE | M4 (D3) | Resolver verified headless, BUT production windowed right-click (`controller.cpp:218-225`) is bound to MOVE only and does NOT call the resolver. M4 redefines: wire the production binding through the resolver so REAL Qt RMB yields ATTACK. (Doc/code drift at `controller.cpp:1-16` must be reconciled.) |
| P4.4D.2 selection overlay (world-space outline per selected entity) | IU | builds; demos 7/42/46/49 PASS; `WINDOWED_MULTI_UNIT_MANUAL_ACCEPTANCE_PENDING` | VERIFY_IN_WINDOW | M3 (D2) | Implemented but windowed pixel acceptance pending. Verify with REAL Qt selection + screenshot in M3. |
| P4.4F multi-unit formation + dynamic collision | P | demos 42/46/49 PASS; `move.cpp` uncommitted WIP; Task 3 review skipped | REDEFINE | M0 (resolve WIP) | WIP must be resolved (commit-with-test or revert) at M0 before new work. Formation/collision is NOT required for the single-villager MVP combat but must not regress. |
| P5.1-P5.4 terrain & media closure (real Mission 1 visual assets) | B | `verify_ww_mission1.sh` Stage 3: resolved=32, missing=0, deferred=29; DDS terrain not started | MOVE_TO_GATE_B | M7 (Gate B) | Depends on converter (29 de2_base sprites + DDS terrain). Gate A uses placeholder art. |
| P6 fresh headless semantic gate (`MISSION_1_HEADLESS_SEMANTIC`) | NI | no P6 gate report (demos 7/38 cover similar ground) | REDEFINE | M0/M6 (folded) | The headless semantic capability is already VC via demos 7,9-14,20,35-38. Redefine P6 as the regression sweep in M0 (re-affirm) + M6 (Gate A regression), not a separate gate. |
| P7.1-P7.4 windowed scripts/smoke (canonical launch + smoke) | P | `run_ww_mission1.sh` windowed starts clean (RF); `verify_ww_mission1.sh` Stages 1/2/4 FAIL on macOS (portability) | REDEFINE | M0 (harness portability) + M6 (smoke in Gate A) | Windowed launch works; harness has portability bugs (`timeout`/`.venv`/`.so`). Fix harness in M0; fold smoke into Gate A acceptance (M6). |
| P7.5 manual playable acceptance (11-item checklist; `MISSION_1_WINDOWED_PLAYABLE`) | NI | no acceptance report; interactive combat->death->victory->UI never signed off | REDEFINE | M6 (Gate A acceptance) | The old P7.5 checklist is redefined as Gate A (`WW_MISSION_1_WINDOWED_INTEGRATION`) with stricter evidence: REAL Qt input (not just "stays alive"), player-visible victory UI (not log-only), screenshot+readback corroboration. |
| Player color per owner (S7 problem 4) | B | S7 flag `PLAYER_COLOR_METADATA_CONVERSION_BLOCKED` | DEFER | - (post-MVP polish) | Polish, not MVP-required. Blocked on converter metadata; defer. |

---

## B. S1 (`mvp-gameplay-spec.md`) design-element migration

S1 is **partially stale** (predates P2-P4). Goal valid; architecture obsolete.

| Old requirement/task | Audit status | Evidence | New disposition | New milestone | Reason |
|---|---|---|---|---|---|
| S1 Mission 01 goal: select/move/attack/death/victory; win = clear enemies | VC (intent) | demos 7/38; audit §1 | KEEP_VERIFIED | M4/M6 | The goal is correct and retained as Gate A's FR-3..FR-8. |
| S1 §7.6 tick-driven `MissionDirector` | Obsolete | impl is event-driven; `objective_poll_count=0` | DELETE_OBSOLETE | - | Replaced by event-driven `TriggerRuntime` (P4.1/P4.2). Tick-driven director is dead design. |
| S1 `TutorialTrigger` step-predicate polling (wood>=50) | Obsolete | no TutorialTrigger in impl | DELETE_OBSOLETE | - | Replaced by synthesized `defeat_enemy` from `required_removals`. No tutorial/economy trigger. |
| S1 §8 gather-wood economy (Mission 2-5 scope) | Out of scope | `MissionWood amount=0`; player 1 observer-only | DEFER | - | Non-goal for this MVP (combat-only). Postpone to future missions. |
| S1 §8 build/train/advance/Stirling (Mission 2-5) | Out of scope | not implemented | DEFER | - | Non-goal; future missions. |
| S1 §9.3 economy GUI / resource bars | Out of scope | not implemented | DEFER | - | Non-goal; depends on economy (deferred). |
| S1 §9.3 "QtQuick primitives, not depending on de2_base graphics" | Valid principle | placeholder rendering path | KEEP_VERIFIED | M1/M5 (Gate A art policy) | Retained: Gate A does NOT require correct de2_base art; placeholder is acceptable. |
| S1 §9.1 `--tutorial --tutorial-scenario-fqon` CLI | Deprecated | `main.py:304` maps to `--scenario` | REPLACE | M0 (canonical entry) | Canonical entry is `bin/run --scenario`. The deprecated flag is mapped to it; do not use the deprecated form in new docs. |
| S1 §10 freeze M0 determinism + rts-foundation-spec | Frozen | S8/S9 frozen, paused per user | KEEP_VERIFIED | - (remains frozen) | Determinism/lockstep/replay remain frozen and out of MVP scope. |
| S1 `MissionDirector`/`TutorialTrigger`/tick-director (consolidated) | Obsolete | - | DELETE_OBSOLETE | - | See above; replaced by TriggerRuntime. |

---

## C. S7 (P4.4D.2/F) sub-plan migration

| Old requirement/task | Audit status | Evidence | New disposition | New milestone | Reason |
|---|---|---|---|---|---|
| S7 Task 1 selection overlay sizing/resize race | VC (committed) | commits `660bfcc2`, `01b49d1e` | KEEP_VERIFIED | M0 (regression) + M3 (windowed verify) | Fixed; verify in windowed path at M3. |
| S7 Task 2 player color per owner | B | `PLAYER_COLOR_METADATA_CONVERSION_BLOCKED` | DEFER | - | Polish; blocked on converter; defer. |
| S7 Task 3 multi-unit formation + dynamic collision | P | demos 42/46/49; `move.cpp` WIP; review skipped "due to budget" | REDEFINE | M0 (resolve WIP) | The unstaged `move.cpp` WIP is the immediate M0 hygiene item. Formation is not MVP-required (single villager) but must not regress; resolve WIP (commit-with-test or revert). |
| S7 `DynamicOccupancy` spawn registration + removal observer | VC (committed) | commit `56109051` | KEEP_VERIFIED | M0 (regression) | Recently completed; carry as regression. |

---

## D. Demo / headless-capability migration (all passing demos + all headless-only-verified capabilities)

**Rule:** Passing demos are KEEP_VERIFIED (regression). Headless-only-verified capabilities that the MVP requires in the windowed path are VERIFY_IN_WINDOW - they must be re-verified through REAL Qt input in Gate A, not re-implemented.

| Old capability / demo | Audit status | Evidence | New disposition | New milestone | Reason |
|---|---|---|---|---|---|
| Demo 7 (combat->victory headless) | VC | `enemy_removed=1 objective_finished=1` | KEEP_VERIFIED | M0 (regression) | Core regression anchor. |
| Demos 9-14 (load, lifecycle, event-objective) | VC | PASS logs | KEEP_VERIFIED | M0 (regression) | Load + lifecycle + event-driven objective regression. |
| Demo 20 (data-driven victory + negative assertion) | VC | `declare_victory reason='enemy_removed'`; non-target removal does NOT win | KEEP_VERIFIED | M0 (regression) + M4 (preserve negative contract) | The negative contract MUST remain green after M4's ATTACK wiring. |
| Demos 16-19 (TriggerInstance/runtime) | VC | PASS | KEEP_VERIFIED | M0 (regression) | Runtime regression. |
| Demos 22-24 (selection viewmodel) | VC (headless) | PASS | VERIFY_IN_WINDOW | M3 (D2) | Selection verified headless; re-verify via REAL Qt LMB. |
| Demos 25-27 (footprint/placement) | VC | PASS | KEEP_VERIFIED | M0 (regression) | Placement regression. |
| Demos 28-32 (pathfinding) | VC | PASS | VERIFY_IN_WINDOW | M3/M4 (real-Qt move) | Re-verify with REAL Qt RMB move. |
| Demos 35-38 (input + combat, incl. synthetic pointer) | VC (headless) | demo 38 `MissionState=VICTORY` | VERIFY_IN_WINDOW + KEEP_VERIFIED | M4 (D3) | demo 38 validates the sim chain (KEEP as regression) but its synthetic pointer is NOT Gate A evidence. M4 re-verifies via REAL Qt RMB. (Constraint: do NOT copy demo 38 sim logic into the Qt layer.) |
| Demos 42/46/49 (occupancy/formation) | VC | PASS | KEEP_VERIFIED | M0 (regression) | Formation regression; not MVP-required but must not break. |
| Windowed startup (launch + 3 entities + input init) | VC (init only) | windowed smoke >25s clean | VERIFY_IN_WINDOW | M1 (D1 visibility) | Init verified; rendering + interaction NOT verified. |
| Entity visual rendering | Unverified/Blocked | 29 sprites deferred; `display_into_data()` unwired | VERIFY_IN_WINDOW (Gate A via placeholder) + MOVE_TO_GATE_B (real art) | M1 (placeholder) / M7 (real) | Gate A: verify placeholder rendering (M1). Gate B: converter art (M7). |
| Objective display (UI) | Unverified | `MissionHud.qml` orphaned; QML root = testscene | REDEFINE | M5 (D4) | Wire `HudQmlAdapter` as QML context property + scenario-aware QML root; make victory player-visible. |
| Victory state (windowed) | VC (headless) | demo 20/38 VICTORY | VERIFY_IN_WINDOW | M4 (C++) + M5 (UI) | C++ victory verified headless; re-verify via REAL removal in M4, surface to UI in M5. |
| Defeat state (player death) | NI | only `enemy_removed` path | DEFER | - | Non-goal (MVP = win-only). |
| Screenshot readback (`display_into_data`) | Exists, unwired | `renderer.cpp:127-143` | VERIFY_IN_WINDOW | M1 (wire) | Wire at `presenter.cpp:510-512` as temporary Gate A evidence tool (deletion condition: M6). |
| Qt input injection (`QMouseEvent` + `InputManager::process`) | Precedent exists | `presenter.cpp:358-395` `OPENAGE_DEBUG_AUTOSPAWN` | VERIFY_IN_WINDOW | M3/M4 (harness) | Reuse the precedent as a thin test harness; no new GUI automation framework. |
| `verify_ww_mission1.sh` (macOS) | P (false-fail) | Stages 1/2/4 rc=127/1 | REDEFINE | M0 (portability) | Portability bugs (`timeout`/`.venv`/`.so`), not engine defects. Fix in M0. |

---

## E. Special-handling summary (required by Task 2)

- **Tick-driven design (S1 §7.6):** DELETE_OBSOLETE. Replaced by event-driven `TriggerRuntime`. Do not revive.
- **Gather-wood economy (S1 Mission 2-5):** DEFER. Non-goal; `MissionWood amount=0`. Postpone.
- **`TutorialTrigger`:** DELETE_OBSOLETE. Replaced by synthesized `defeat_enemy` from `required_removals`. No tutorial/economy triggers.
- **P4.4D.2/F (S7):** REDEFINE. Overlay = VERIFY_IN_WINDOW (M3); `move.cpp` WIP = resolve at M0; formation = regression only (not MVP-required). Player-color = DEFER.
- **P7 scripts (P7.1-P7.4):** REDEFINE. Launch works; harness portability fixed at M0; smoke folded into Gate A (M6).
- **P7.5:** REDEFINE. Becomes Gate A (`WW_MISSION_1_WINDOWED_INTEGRATION`) at M6 with stricter evidence (REAL Qt input + player-visible UI + readback, not "stays alive").
- **P1:** BLOCKED. Original source import blocked; `ww_mvp` fixture is the Gate A substitute (labelled, not converter-backed). Unblocking is M7/Gate B.
- **P5:** MOVE_TO_GATE_B. Media closure depends on converter; Gate A uses placeholder.
- **P6:** REDEFINE. Headless semantic capability is already VC via demos; fold into M0 regression + M6 Gate A regression (not a separate gate).
- **All passing demos (7,9-14,16-19,20,22-24,25-27,28-32,35-38,42,46,49):** KEEP_VERIFIED as the M0/M6 regression suite. Do not re-implement.
- **All headless-only-verified capabilities required by the MVP (selection, move, attack, victory):** VERIFY_IN_WINDOW - re-verify through REAL Qt input in Gate A (M3/M4/M5), NOT by re-implementing the sim chain. (Constraint: do not copy demo 38 sim logic into the Qt layer; do not re-implement VERIFIED_COMPLETE sim capabilities.)
