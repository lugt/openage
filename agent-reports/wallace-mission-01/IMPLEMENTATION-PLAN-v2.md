# Implementation Plan v2 - William Wallace Mission 01

Date: 2026-08-05
Branch: `mvp/william-wallace` @ `56109051`
Status: **Decision/planning only. No code/tests/build/assets modified. No milestone executed.**
Authority: Derived from `MVP-SPEC-v2.md` + `PLANNING-DECISION.md` + `current-state-audit.md`. Supersedes the P5/P6/P7 acceptance in `docs/mvp/specs/WW_MISSION1_DEVELOPMENT_PLAN.md` (S2) by refining them into Gate A/B and adding real-Qt-input + player-visible-UI requirements.

Critical path: **M0 -> M1 -> [M2 conditional] -> M3 -> M4 -> M5 -> M6 (Gate A) -> M7 (Gate B) -> M8.**
M2 is conditional: executed only if M1's U1 visibility spike FAILs. Bracketed in the path to denote this.

Common fields per milestone:
- Goal; User-visible outcome; Confirmed starting point; First breakpoint; Hypothesis; Scope; Non-scope; Likely files; Maximum change budget; Test strategy; Evidence; Failure signal; Stop condition; Rollback strategy; Exit criteria; New technical debt; Uncertainty (Low/Med/High).

Change budget is a hard cap (lines added/modified in production code, excluding tests/docs). Reaching the cap without exit-criteria met = STOP, not push through. Tests are exempt from the budget.

---

## M0 - Baseline Integrity & Harness Portability

- **Goal:** Establish a trustworthy, reproducible baseline before any new feature work.
- **User-visible outcome:** None (engineering milestone).
- **Confirmed starting point (RF):** Demos 7,9-14,20,35-38,42,46,49 PASS; headless load path works; windowed startup works >25s; `verify_ww_mission1.sh` Stages 1/2/4 false-FAIL on macOS (rc=127, `timeout` missing); `move.cpp` has unstaged WIP; committed dump positions `6,4` vs IR/nyan `8,8`.
- **First breakpoint:** `verify_ww_mission1.sh` must pass on macOS AND the `move.cpp` WIP must be resolved AND the position drift must have a recorded decision.
- **Hypothesis:** The baseline is sound; only the harness portability + WIP hygiene + a written drift decision block progress. No engine change needed.
- **Scope:** (a) Port `scripts/verify_ww_mission1.sh`: replace `timeout` with a portable kill-on-sleep wrapper (or `gtimeout` w/ fallback), accept `venv`/`.venv` + `.so`/`.dylib`, capture real exit code. (b) Resolve `move.cpp` WIP: either commit with a pinning regression test or revert. (c) Adjudicate D0 source-of-truth (positions `6,4` vs `8,8`): the Scenario IR (`openage/scenario/ir.py`) is the input; the committed dump (`docs/mvp/reports/mission1-scenario-dump/scenario.json`) and the production `assets/converted/ww_mvp/.../scenario.json` are generated products. Decide which is canonical - IR-as-source (then regenerate the stale product) OR an intentional product-override (then update the IR + record the override) - **record the decision in writing**, then make the generated products match the adjudicated source. Do NOT regenerate the dump merely to make the test pass without the recorded decision (per `PLANNING-DECISION.md` line 126). Add/update the drift-detection test `test_regenerated_dump_matches_committed_p1_dump` to assert product==source.
- **Non-scope:** Any new feature; any visual/input/UI wiring; converter work; test-skipping; assertion-lowering.
- **Likely files:** `scripts/verify_ww_mission1.sh`; `libopenage/gamestate/system/move.cpp` (resolve WIP only); `openage/scenario/dumps.py` (verify dump dir); `docs/mvp/reports/...` (decision note). Test: existing `test_p2_integration.py` drift test path.
- **Maximum change budget:** 80 prod lines (mostly `move.cpp` resolution + harness script). Tests/docs unbounded.
- **Test strategy:** Re-run `bin/run test --demo gamestate.tests.simulation_demo {7,9-14,20,35-38,42,46,49}` all PASS; run `verify_ww_mission1.sh` on macOS Stages 1/2/4 PASS (exit-code-correct); drift test green.
- **Evidence:** Demo PASS logs; macOS `verify_ww_mission1.sh` run log; git state showing `move.cpp` resolved; D0 decision note in `OLD-PLAN-MIGRATION.md`.
- **Failure signal:** A demo that was green turns red; harness still false-fails; `move.cpp` cannot be cleanly committed/reverted.
- **Stop condition:** Two-strikes rule - two consecutive failed attempts at a sub-item, STOP and report.
- **Rollback strategy:** All M0 changes are reversible (script edit, test add, WIP revert). No production behavior change intended.
- **Exit criteria:** (1) Demos green; (2) `verify_ww_mission1.sh` PASS on macOS; (3) `move.cpp` resolved; (4) D0 drift test + decision exist.
- **New technical debt:** None intended. If `move.cpp` is reverted, log the lost work as a tracked item.
- **Uncertainty:** Low.

---

## M1 - Visibility Spike (Decision Gate D1)

- **Goal:** Verify entities actually render on screen (resolve U1). Decide whether placeholder rendering is sufficient for Gate A or whether M2 is needed.
- **User-visible outcome:** A screenshot showing the grass map + 3 entities (placeholder art acceptable).
- **Confirmed starting point (RF):** `World::attach_renderer` (`world.cpp:24`) attaches 3 entities; `AssetManager::request_animation` (`asset_manager.cpp:36-56`) returns placeholder on missing sprite; `display_into_data()` (`renderer.cpp:127-143`) provides glReadnPixels readback; `presenter.cpp:510-512` is an unwired insertion point after the render loop; camera `look_at_coord({10,10,0})` (`presenter.cpp:147`).
- **First breakpoint:** A screenshot captured from a real window shows non-empty scene (terrain + entity markers). If the scene is empty/broken, that is the M1 breakpoint to localize.
- **Hypothesis (HY):** Entities render as placeholder sprites and are visible; U1 PASSes on the first try.
- **Scope:** (a) Add a temporary, Gate-A-labelled screenshot hook at `presenter.cpp:510-512`: after `for (auto &pass : render_passes) renderer->render(pass);`, call `renderer->display_into_data()` (returns `Texture2dData`), `.flip_y()` (glReadnPixels is bottom-origin), then `.store(path)` to a path controlled by an env var (e.g. `OPENAGE_DEBUG_SCREENSHOT`). (b) Run `bin/run --scenario`, capture `m1-visibility.png` from a real window. (c) Inspect for terrain + 3 entity markers. (d) Check edge entities (6,4)/(10,4) are in the default camera frustum (camera `look_at_coord({10,10,0})`, `presenter.cpp:147`).
- **Non-scope:** Fixing rendering (that's M2); input; UI; combat. The screenshot hook is temporary scaffolding (deletion condition: M6 Gate A signoff, replaced by acceptance screenshot flow).
- **Likely files:** `libopenage/presenter/presenter.cpp` (screenshot hook only). Possibly a 1-line camera adjust if edges are off-frustum.
- **Maximum change budget:** 30 prod lines (screenshot hook). 1-line camera adjust if needed.
- **Test strategy:** Manual + scripted: launch, screenshot, assert PNG non-empty + entity count from render-object log. Semi-automated.
- **Evidence:** `m1-visibility.png` + render-object count log line ("kicking activity for 3 entities").
- **Failure signal:** Empty/black PNG; broken GL context; crash on readback; entities not in frustum.
- **Stop condition:** U1 resolved (PASS or FAIL) with a screenshot. If FAIL, proceed to M2.
- **Rollback strategy:** Remove the screenshot hook. No other change.
- **Exit criteria:** (1) Screenshot shows terrain + 3 entities (PASS) -> skip M2, go M3; OR (2) documented FAIL -> M2.
- **New technical debt:** Temporary screenshot hook (labelled, deletion condition M6).
- **Uncertainty:** Med (predicted PASS but never observed).

---

## M2 - Render Breakpoint Localization (CONDITIONAL)

- **Goal:** Only if M1 FAILs. Localize why the scene is empty/broken and produce a minimal fix so M3+ can proceed.
- **User-visible outcome:** A screenshot showing terrain + entities (same as M1 exit, but after a fix).
- **Confirmed starting point:** M1 FAIL artifact (empty PNG) + the render chain (`world.cpp:24` -> `render_stage.cpp:61` -> `object.cpp:46` -> `asset_manager.cpp:36`).
- **First breakpoint:** The earliest stage in the render chain that drops the entity (e.g., `WorldObject::get_required_layer_count` returns 0 when `animation_info` is null (`object.cpp:197-204`)).
- **Hypothesis:** One specific stage nulls-out the renderable (likely `animation_info` null because placeholder path isn't hit, or `fetch_updates` returns early).
- **Scope:** Trace the chain with the screenshot hook + logging; identify the nulling stage; apply the minimal fix (e.g., ensure `get_placeholder_animation()` is returned in `fetch_updates` when path is empty). If the issue is the placeholder sprite file itself (`test_missing.sprite`), verify it exists and is loadable.
- **Non-scope:** New art; converter; full rendering rewrite. M2 is a **localization + minimal-fix** milestone, not a render overhaul.
- **Likely files:** `libopenage/renderer/stages/world/object.cpp`; possibly `libopenage/renderer/resources/assets/asset_manager.cpp`; `assets/test/textures/test_missing.sprite` (verify only).
- **Maximum change budget:** 60 prod lines.
- **Test strategy:** M1 screenshot now PASSes; no regression in demos.
- **Evidence:** Before/after screenshots; root-cause note.
- **Failure signal:** Fix exceeds budget; regression in demos; root cause unclear after trace.
- **Stop condition:** Screenshot PASSes (go M3) OR two-strikes (STOP, escalate render architecture).
- **Rollback strategy:** Revert the minimal fix; keep M1 screenshot hook.
- **Exit criteria:** `m1-visibility.png` (post-fix) shows terrain + 3 entities; demos still green.
- **New technical debt:** Whatever minimal fix is applied must be tracked (e.g., "placeholder forced in fetch_updates - revisit when converter supplies real sprites").
- **Uncertainty:** High (only entered if M1 FAILs; root cause unknown).

---

## M3 - Real-Qt Selection (Decision Gate D2)

- **Goal:** Prove a real Qt LMB selects the player villager through the production input path.
- **User-visible outcome:** Clicking the villager highlights it (selection overlay quad).
- **Confirmed starting point (RF):** `InputManager::process(const QEvent&)` (`input_manager.cpp:131`) is the sole entry; `OPENAGE_DEBUG_AUTOSPAWN` precedent constructs `QMouseEvent` and calls `process` (`presenter.cpp:358-395`); `GlWindow::get_qt_window()` (`window.h:137`) exposes the Qt window for `QTest`; selection overlay rendering exists (recent commits `660bfcc2`, `01b49d1e`).
- **First breakpoint:** After injecting a `QMouseEvent(LMB press/release)` at the villager's screen position, `SelectionModel` contains the villager entity id.
- **Hypothesis (HY):** The existing selection + overlay path works end-to-end; only the injection harness is new.
- **Scope:** (a) Build a thin Qt-input test harness (NOT a new GUI automation framework): construct a `QMouseEvent` at a computed screen coordinate (from the entity's world position via the camera projection, or a hardcoded known-good pixel from M1's screenshot) and call `InputManager::process()`. **Reuse the `OPENAGE_DEBUG_AUTOSPAWN` precedent exactly**: call `input_manager->set_mouse(x,y)` BEFORE `process()` (the spawn binding's `to_phys3` reads `InputManager::mouse_position`, not the event's own coords - `presenter.cpp:358-395`). (b) Assert `SelectionModel` contains villager id 0. (c) Capture `m3-selection.png` showing the overlay.
- **Non-scope:** Attack wiring (M4); UI/victory (M5); multi-unit selection; box-select.
- **Likely files:** A new test file under `libopenage/.../tests/` (harness); no production change expected. If the picking layer needs a hook to expose screen->entity, that is in-scope but minimal.
- **Maximum change budget:** 40 prod lines (only if a picking hook is needed). Harness/test code unbounded.
- **Test strategy:** Automated Qt-input test asserting selection; screenshot corroboration.
- **Evidence:** Test PASS log; `m3-selection.png` with overlay; `SelectionModel` readback.
- **Failure signal:** No selection after injected click; picking returns wrong/no entity; crash.
- **Stop condition:** Real-Qt LMB reliably selects the villager.
- **Rollback strategy:** Remove the test harness; no production change to revert.
- **Exit criteria:** D2 PASS (selection via real Qt event; overlay visible; readback confirms id 0).
- **New technical debt:** The screen-coordinate computation (hardcoded pixel or camera-projection helper) - track until a robust picking-test helper exists.
- **Uncertainty:** Med (picking layer never driven by real Qt in windowed path before).

---

## M4 - Real-Qt Attack Wiring (Decision Gate D3)

- **Goal:** Wire the production windowed right-click to issue ATTACK on a hostile target, and prove the full combat->death->removal->victory chain runs from real Qt input.
- **User-visible outcome:** Right-clicking the enemy villager orders an attack; the enemy dies; victory is declared (in C++/log - UI is M5).
- **Confirmed starting point (RF):** `controller.cpp:218-225` binds `RightButton release -> move_entity` (always MOVE). `resolve_context_action()` (`semantic_action.cpp:99-224`) implements ATTACK (`:185-200` hostile->ATTACK) but is ONLY called by demos, not by `controller.cpp`. The doc comment `controller.cpp:1-16` claims right-click goes through `resolve_context_action` - doc/code drift. The verified combat chain (AttackHandler, RemovalEventHandler, TriggerRuntime, DeclareVictory) is PASSING in demos.
- **First breakpoint:** After injecting a real Qt RMB on the enemy, the issued command is `command_t::ATTACK` (not MOVE); the enemy takes damage and is removed; `MissionState::declare_victory reason='enemy_removed'` fires from a real removal.
- **Hypothesis (HY):** The fix is a localized binding change - route the right-click binding through `resolve_context_action` (using the picking `PickResult` to decide MOVE vs ATTACK), reusing the verified sim chain. No new sim logic.
- **Scope:** (a) Reconcile `controller.cpp:1-16` doc with `:218-225` code. (b) Change the right-click binding to call `resolve_context_action` (or equivalent) so a hostile `PickResult` yields ATTACK and empty ground yields MOVE (both branches of the resolver must be exercised - FR-4 move + FR-5 attack). (c) Reuse the M3 Qt-input harness to inject RMB on ground (assert MOVE + unit moves) and RMB on the enemy (assert ATTACK). (d) Assert ATTACK command issued, enemy HP->0, removal, TriggerRuntime objective complete, `declare_victory`. (e) Assert demo 20's negative contract still holds (non-target removal does NOT win).
- **Non-scope:** Copying demo 38's simulation logic into the Qt layer (FORBIDDEN by user constraint - M4 drives the production path, not a sim re-implementation). Enemy AI. Multi-target. Attack-move.
- **Likely files:** `libopenage/input/controller/game/controller.cpp` (binding + doc); `libopenage/input/semantic_action.cpp` (verify only); M3 test harness (extend). Possibly `libopenage/input/controller/game/controller.h`.
- **Maximum change budget:** 100 prod lines (binding reroute + doc + any picking-hook). Tests unbounded.
- **Test strategy:** Automated Qt-input combat test (RMB on enemy -> victory via real removal); regression on demos 7,20,35-38 (combat + negative contract).
- **Evidence:** Test PASS log with `AttackHandler: invoke`, `RemovalEventHandler: target 2 removed`, `declare_victory reason='enemy_removed'`; sim state readback; `m4-combat.png`.
- **Failure signal:** MOVE issued instead of ATTACK; no damage; no removal; victory not declared; negative-contract regression.
- **Stop condition:** Real-Qt RMB on ground -> MOVE -> unit moves; AND real-Qt RMB on enemy -> ATTACK -> death -> real-removal victory; negative contract intact.
- **Rollback strategy:** Revert `controller.cpp` binding change (restores MOVE-only); harness removed. Since `controller.cpp:200-225` is the verified-stable prior art, revert restores a known-good state.
- **Exit criteria:** D3 PASS (real-Qt attack chain through TriggerRuntime; demos green).
- **New technical debt:** The exact `resolve_context_action` integration shape - track for cleanup once a unified context-action API is settled.
- **Uncertainty:** Med-High (R2 is the top risk; the picking->resolver wiring specifics are unresolved U-attack-bind).

---

## M5 - Player-Visible Victory (Decision Gate D4)

- **Goal:** Make victory/objective player-visible in the window (wire R3).
- **User-visible outcome:** On enemy removal + victory, the UI shows victory (objective panel / "Mission complete").
- **Confirmed starting point (RF):** `HudQmlAdapter` has Q_PROPERTYs (`hud_qml_adapter.h:33-56`: selectedEntityId/Name/HP/attack/missionLifecycle) and the C++ signal chain is wired (`hud_qml_adapter.cpp:109-119` refresh_mission, subscribes `mission_state_changed :61-72`). BUT: `HudQmlAdapter` is NOT registered as a QML context property (`presenter.cpp:403` comment); QML root is HARDCODED to `assets/test/qml/main.qml` (`presenter.cpp:223-238`) - trivial colored rectangles, no HUD/victory; `MissionHud.qml` is orphaned.
- **First breakpoint:** After D3's victory, a bound QML element reflects victory AND a screenshot shows the victory UI.
- **Hypothesis (HY):** The work is bounded "last-hop" QML wiring: register `HudQmlAdapter` as a context property, point the QML root at a Mission 1 entry (salvage `MissionHud.qml` or write a minimal Mission 1 QML), bind the victory Q_PROPERTY to a visible element. No new C++ signal work.
- **Scope:** (a) Register `HudQmlAdapter` as a QML context property in the presenter. (b) Make QML root scenario-aware OR add a Mission 1 QML entry that the fixture/mission loads. (c) Bind `missionLifecycle`/victory Q_PROPERTY to a visible QML element (text/panel). (d) Reuse M4's victory run; assert the Q_PROPERTY readback = victory AND `m5-victory-ui.png` shows the UI.
- **Non-scope:** Full HUD (resource bars, unit cards - non-goals); a new GUI framework; QML authoring language; full campaign UI.
- **Likely files:** `libopenage/presenter/presenter.cpp` (context property registration + QML root logic); `libopenage/presenter/hud_qml_adapter.h/.cpp` (verify); a Mission 1 QML (new or salvaged `assets/.../MissionHud.qml`).
- **Maximum change budget:** 120 prod lines (registration + QML-root logic + minimal QML). Tests unbounded.
- **Test strategy:** M4 victory run + Q_PROPERTY readback assertion + screenshot assertion. Semi-automated.
- **Evidence:** `m5-victory-ui.png` + Q_PROPERTY readback showing victory; `gate-a-run.log`.
- **Failure signal:** Q_PROPERTY not bound; QML doesn't load; victory not visible; QML errors in log.
- **Stop condition:** Victory is player-visible (Q_PROPERTY + screenshot), not log-only.
- **Rollback strategy:** Revert presenter context-property + QML-root changes; restores testscene QML. The `HudQmlAdapter` C++ chain is untouched (already wired).
- **Exit criteria:** D4 PASS (victory player-visible via bound QML + screenshot).
- **New technical debt:** A salvaged/minimal Mission 1 QML - track for replacement when the full HUD is built.
- **Uncertainty:** Med (U-qml: salvage MissionHud.qml vs minimal new QML).

---

## M6 - Gate A Acceptance

- **Goal:** Demonstrate the full Gate A sequence in one continuous windowed run and declare `WW_MISSION_1_WINDOWED_INTEGRATION: PASS`.
- **User-visible outcome:** A player can launch, see the map/units, select, move, attack, kill the enemy, see victory, and exit cleanly - all in one run.
- **Confirmed starting point:** D1-D4 PASS (M1/M3/M4/M5). M2 done or skipped.
- **First breakpoint:** One continuous run performs FR-1..FR-9 via real Qt input with the full evidence package.
- **Hypothesis (HY):** With D1-D4 passing, the integrated run passes; the remaining work is evidence packaging + a regression sweep, not new feature work.
- **Scope:** (a) Run the full Gate A sequence as one continuous windowed session (launch -> select -> move -> attack -> victory visible -> clean exit). (b) Capture `gate-a-run.log`, annotated screenshots or `gate-a-run.mp4`. (c) Write `docs/mvp/reports/mission1-windowed-acceptance.md` declaring PASS with evidence pointers. (d) Remove the M1 temporary screenshot hook (deletion condition met). (e) Regression sweep: demos 7,9-14,20,35-38,42,46,49 + new Qt-input tests all green. (f) Run `verify_ww_mission1.sh` on macOS + Linux.
- **Non-scope:** Converter content (M7); new features; economy; multi-mission.
- **Likely files:** `docs/mvp/reports/mission1-windowed-acceptance.md` (new); evidence dir; remove M1 screenshot hook from `presenter.cpp`.
- **Maximum change budget:** 10 prod lines (screenshot hook removal). Docs/evidence unbounded.
- **Test strategy:** Full run + regression suite + cross-platform harness.
- **Evidence:** `gate-a-run.log` + screenshots/mp4 + `mission1-windowed-acceptance.md` (PASS) + regression logs + macOS/Linux harness logs.
- **Failure signal:** Integrated run fails any FR; regression red; harness false-fails.
- **Stop condition:** `WW_MISSION_1_WINDOWED_INTEGRATION: PASS` declared, OR two-strikes STOP.
- **Rollback strategy:** N/A (acceptance milestone; if FAIL, return to the failing D-gate milestone).
- **Exit criteria:** AC-A1 + AC-A2 met; Gate A declared PASS.
- **New technical debt:** None (screenshot hook removed).
- **Uncertainty:** Low-Med (integration of passing parts).

---

## M7 - Gate B Content (Decision Gate D5)

- **Goal:** Run the Gate A chain on converter-backed Mission 01 content; declare `WW_MISSION_1_CONTENT: PASS`.
- **User-visible outcome:** Same player-visible sequence as Gate A, but on real Mission 01 content (no `ww_mvp` fixture).
- **Confirmed starting point (RF):** Gate A PASS. Converter `source_status` for the Mission 01 subset is the blocker (audit S4: import BLOCKED on VER 8.9 dat format). The minimum content need: one villager sprite set, one town_center sprite set, grass terrain, scenario data with `required_removals=["EnemyVillager"]`.
- **First breakpoint:** Gate A's FR-1..FR-9 pass on converter-backed content with `source_status != BLOCKED` for the Mission 01 subset.
- **Hypothesis (HY):** The converter blocker is the dominant risk; once the Mission 01 minimum content is produced (converter fix or an explicitly-supported pipeline), the Gate A chain runs on it because the chain is content-agnostic.
- **Scope:** (a) Resolve the converter blocker for the Mission 01 minimum (dat format compatibility) OR establish an explicitly-supported content pipeline that is NOT the hand-written `ww_mvp` fixture. (b) Produce the minimum villager/town_center/grass media. (c) Run the Gate A chain on the new content. (d) Assert no `ww_mvp` fixture is used. (e) Write `docs/mvp/reports/mission1-content-acceptance.md`.
- **Non-scope:** All 29 deferred de2_base sprites; full DDS terrain; player-color palette; Missions 2-5.
- **Likely files:** Converter (`openage/convert/...`, `openage/scenario/...`); content pipeline; `docs/mvp/reports/mission1-content-acceptance.md`. NOT `assets/converted/ww_mvp/` (must be unused).
- **Maximum change budget:** Not bounded by lines (converter work may be substantial) - bounded by **scope**: Mission 01 minimum only. Any scope creep (extra sprites, Missions 2-5) = STOP.
- **Test strategy:** Gate A chain green on converter content; `source_status != BLOCKED`; no-fixture assertion.
- **Evidence:** `mission1-content-acceptance.md` (PASS) + Gate B run logs/screenshots + `source_status` readout.
- **Failure signal:** Converter blocker unresolvable within Mission 01 scope; Gate A chain fails on real content; `ww_mvp` still required.
- **Stop condition:** `WW_MISSION_1_CONTENT: PASS`, OR converter blocker confirmed unresolvable in-scope (escalate/defer Gate B).
- **Rollback strategy:** Gate B failure does not roll back Gate A (Gate A stands). Revert converter changes if they destabilize the converter.
- **Exit criteria:** AC-B1 met; Gate B declared PASS.
- **New technical debt:** Any converter workaround for the dat format - track to a proper fix.
- **Uncertainty:** High (converter blocker is the largest external risk; U-content).

---

## M8 - MVP Acceptance & Closure

- **Goal:** Declare `WW_MISSION_1_PLAYABLE_MVP: PASS` and close out.
- **User-visible outcome:** The William Wallace Mission 01 playable MVP is complete and reproducible.
- **Confirmed starting point:** Gate A PASS (M6) + Gate B PASS (M7).
- **First breakpoint:** All MVP exit criteria (§19 of SPEC) hold simultaneously.
- **Hypothesis (HY):** With both gates passing, closure is documentation + final regression + tech-debt accounting.
- **Scope:** (a) Verify all 8 MVP exit criteria (SPEC §19). (b) Write the reproduction doc (`docs/mvp/reports/mission1-mvp-acceptance.md`). (c) Final regression sweep (demos + Qt-input tests + cross-platform harness). (d) Consolidate the tech-debt list (per-milestone "New technical debt" items). (e) Declare `WW_MISSION_1_PLAYABLE_MVP: PASS`.
- **Non-scope:** New features; starting Mission 02; economy; polish beyond debt accounting.
- **Likely files:** `docs/mvp/reports/mission1-mvp-acceptance.md` (new); tech-debt tracker.
- **Maximum change budget:** 0 prod lines (closure milestone).
- **Test strategy:** Full regression suite green on macOS + Linux.
- **Evidence:** `mission1-mvp-acceptance.md` (PASS) + reproduction doc + regression logs + tech-debt list.
- **Failure signal:** Any MVP exit criterion unmet; regression red.
- **Stop condition:** `WW_MISSION_1_PLAYABLE_MVP: PASS` declared, OR a criterion is genuinely unmet (declare FAIL/BLOCKED with evidence - no partial credit).
- **Rollback strategy:** N/A (declaration milestone).
- **Exit criteria:** AC-MVP1 met.
- **New technical debt:** None (this milestone accounts for it, doesn't add it).
- **Uncertainty:** Low (given M6 + M7 pass).

---

## Cross-milestone rules

- **Two-strikes stop:** Two consecutive failed attempts at a milestone sub-item -> STOP and report; do not push through.
- **No test-skipping / assertion-lowering / exception-swallowing** to make a milestone pass.
- **No direct sim API / direct victory trigger** as Gate A evidence (hard constraints §15).
- **No copying demo 38 sim logic into the Qt layer** (M4 drives the production path).
- **No new GUI automation framework** (M3/M4/M5 use a thin Qt-input harness reusing `QMouseEvent`/`QTest` + `InputManager::process`).
- **No re-implementing verified sim capabilities** (the combat->victory chain is PASSING; M4 wires input to it, does not rebuild it).
- **Temporary Gate-A art is labelled/isolated/has a deletion condition** (M2 fixes, M1 hook, placeholder sprites -> removed at Gate B).
- **Change budgets are hard caps.** Hitting the cap without meeting exit criteria = STOP.
