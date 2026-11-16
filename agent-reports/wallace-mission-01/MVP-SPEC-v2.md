# MVP SPEC v2 - William Wallace Mission 01

Date: 2026-08-05
Branch: `mvp/william-wallace` @ `56109051`
Authority: This spec supersedes `docs/mvp/mvp-gameplay-spec.md` (S1) for Mission 01. It is consistent with the living plan `docs/mvp/specs/WW_MISSION1_DEVELOPMENT_PLAN.md` (S2) and refines its P5/P6/P7 into two explicit gates. Derived from `current-state-audit.md` + spot verification (see `PLANNING-DECISION.md` §0).
Status: **Decision/planning only. No code/tests/build/assets modified.**

Evidence conventions: **RF** = repository fact; **SI** = strong inference; **HY** = hypothesis.

---

## 1. Executive Definition

The William Wallace Mission 01 playable MVP is a **single, end-to-end windowed play sequence**: launch the game normally, see the mission map and units, select a unit with the mouse, right-click to move, right-click the enemy to attack, watch the enemy die, see the objective complete and victory declared **in the UI**, and exit cleanly.

The MVP is split into two gates that MUST NOT be conflated:

- **Gate A - Windowed Gameplay Integration Gate** (`WW_MISSION_1_WINDOWED_INTEGRATION`): the production windowed runtime runs the full play sequence on the existing hand-written `ww_mvp` fixture. Placeholder/debug art is acceptable. This is an **integration slice**, not "Mission 01 complete".
- **Gate B - Mission 01 Content Gate** (`WW_MISSION_1_CONTENT`): the Gate A sequence runs on converter-backed Mission 01 data and media (no hand-written fixture).

`WW_MISSION_1_PLAYABLE_MVP` = Gate A PASS **and** Gate B PASS.

---

## 2. Confirmed Baseline

What is already verified (do NOT re-implement - see `OLD-PLAN-MIGRATION.md`):

- **Headless combat->victory chain (RF).** Demos 7, 9–14, 20, 35–38, 42, 46, 49 PASS. Demo 7: `enemy_removed=1 objective_finished=1`. Demo 20: data-driven victory with negative assertion (non-target removal does NOT win). Demo 38: synthetic-pointer combat -> `MissionState=VICTORY`.
- **Production load path (RF).** `bin/run --scenario --no-convert --no-prompts` loads the Mission Pack (`scenario.json`), spawns PlayerVillager/PlayerTownCenter/EnemyVillager (entity ids 0/1/2), synthesizes the `defeat_enemy` trigger from `VictoryDefinition.required_removals=["EnemyVillager"]` (`simulation.cpp:435-473`). Headless + windowed logs confirm.
- **Windowed startup (RF).** Qt window + OpenGL renderer + presenter + 3 attached entities + input subsystem initialize, alive >25s, no crash.
- **C++ victory signal chain (RF).** `TriggerRuntime -> mission_state -> MissionView -> HudQmlAdapter` (Q_PROPERTYs + `mission_changed` signal) is wired (`hud_qml_adapter.cpp:109-119`).

What is NOT verified / NOT wired (the Gate A work):

- **R1/U1:** whether entities are actually drawn on screen (predicted: yes, as placeholder - `PLANNING-DECISION.md` §0).
- **R2:** production windowed right-click issues MOVE only; ATTACK resolver is not bound (`controller.cpp:218-225`).
- **R3:** victory/objective is not player-visible (QML root is testscene; `MissionHud.qml` orphaned; `HudQmlAdapter` not bound to loaded QML).
- **R4:** real Qt pointer input has never driven selection/combat in the windowed path.
- **Drift:** committed mission pack positions (6,4/8,8/10,4) != IR/nyan (8,8/8,9/1,1); `verify_ww_mission1.sh` false-fails on macOS.

---

## 3. Why Previous Spec Is Obsolete

`docs/mvp/mvp-gameplay-spec.md` (S1) is partially obsolete:

- **Mission 01 *goal* is still valid** (S1 §8 corrected M1 to a combat closed loop: select/move/attack/death/victory; win = clear enemies). KEEP.
- **Architecture is obsolete.** S1 §7.6 specifies a tick-driven `MissionDirector` with `TutorialTrigger` step-predicate polling + scripted enemy army on a timetable. The implementation replaced this with an **event-driven** `TriggerRuntime` + `defeat_enemy` synthesis from `required_removals` (P4.1 removed polling; `objective_poll_count=0`). S1's `MissionDirector`/`TutorialTrigger`/tick-director are DELETE_OBSOLETE.
- **Economy is out of scope.** S1 §8 Mission 2-5 (gather wood, build, train, advance, Stirling) and S1 §9.3 economy GUI are EXPLICIT NON-GOALS for this MVP. Mission 01 is combat-only (`MissionWood amount=0`).
- **GUI acceptance (S1 §9.3)** - "QtQuick primitives, not depending on de2_base graphics" - is a useful Gate A principle and is retained: Gate A does NOT require correct de2_base art.
- **CLI drift.** S1 §9.1 uses `--tutorial --tutorial-scenario-fqon` (deprecated; `main.py:304` maps it to `--scenario`). The canonical entry is `bin/run --scenario`.

S2 (`WW_MISSION1_DEVELOPMENT_PLAN.md`) remains the living plan; this spec refines S2's P5/P6/P7 into Gate A/B and corrects the P7.5 acceptance to require real Qt input and player-visible UI (not just "starts and stays alive").

---

## 4. Gate A Definition

**Gate A - Windowed Gameplay Integration Gate.**

The production windowed runtime must demonstrate, in one continuous run on the `ww_mvp` fixture, the full chain:

1. `bin/run --scenario --no-convert --no-prompts` launches a Qt window (no crash).
2. The 16×16 grass map renders.
3. The 3 entities render at their world positions (placeholder art acceptable).
4. A **real Qt pointer event** (synthesized in-process via `QMouseEvent` / `QTest`, NOT a direct simulation API call) selects the player villager; selection is observable.
5. A real Qt right-click on empty ground issues a MoveCommand; the villager moves.
6. A real Qt right-click on the enemy issues an **AttackCommand** (requires wiring R2); the villager approaches and attacks.
7. The enemy takes damage, dies, and is removed (verified via simulation state + removal log).
8. The `TriggerRuntime` receives the real removal and completes the objective.
9. `MissionState` declares victory (`reason='enemy_removed'`).
10. Victory/objective completion is **player-visible in the window** (requires wiring R3).
11. The window closes cleanly (no hang/crash/leak-as-failure).

**Pass label:** `WW_MISSION_1_WINDOWED_INTEGRATION`.
**MUST NOT be called:** converter-backed, content-complete, or playable MVP.

---

## 5. Gate B Definition

**Gate B - Mission 01 Content Gate.**

The Gate A windowed chain (items 1-11) must run on Mission 01 content where:

- The scenario data (map, placements, victory definition) comes from a **supported converter path** (or an explicitly-supported content pipeline), not the hand-written `ww_mvp` fixture.
- The required entities and media (villager/town_center sprites, terrain) are produced by the converter or an explicitly-supported pipeline.
- The `source_status` of the content is no longer `BLOCKED` for the Mission 01 subset.

Gate B does NOT require all 29 deferred de2_base sprites or full DDS terrain - only the minimum Mission 01 needs (one villager sprite set, one town_center sprite set, grass terrain).

**Pass label:** `WW_MISSION_1_CONTENT`.

---

## 6. Player-visible Behavior

A player who launches Mission 01 sees:

- A grass-covered 16×16 map in an isometric view.
- A player villager, a player town center, and an enemy villager on the map.
- Clicking the player villager highlights it (selection overlay).
- Right-clicking empty ground moves the villager there.
- Right-clicking the enemy villager orders an attack; the player villager approaches and repeatedly attacks.
- The enemy villager's health decreases; at 0 it is removed from the map.
- On enemy removal, an objective completes and a **victory state is shown in the UI** (e.g., "Mission complete" / objective panel update).
- The player can close the window for a clean exit.

Non-behaviors (explicit): no resource gathering, no building, no training, no enemy AI/counterattack (enemy is passive), no multi-mission flow, no save/load, no audio/drama.

---

## 7. Functional Requirements

| ID | Requirement | Gate |
|---|---|---|
| FR-1 | Launch via `bin/run --scenario` produces a Qt window without crash | A |
| FR-2 | Map + 3 entities render at their world positions | A |
| FR-3 | Real Qt LMB selects the player villager (production input dispatch) | A |
| FR-4 | Real Qt RMB on ground issues MoveCommand and the unit moves | A |
| FR-5 | Real Qt RMB on enemy issues AttackCommand (requires R2 wiring) | A |
| FR-6 | Attack produces damage, death, removal via the verified sim chain | A |
| FR-7 | Real removal drives TriggerRuntime -> objective complete -> victory | A |
| FR-8 | Victory/objective is player-visible in the window (requires R3 wiring) | A |
| FR-9 | Clean window close / shutdown | A |
| FR-10 | Mission 01 content is converter-backed (no `ww_mvp` fixture) | B |
| FR-11 | Required villager/town_center/grass media from converter/pipeline | B |

---

## 8. Non-goals

- Gather-wood economy, general economy system, resource bars (S1 Mission 2-5).
- Unit training, building construction, age advance, tech tree.
- Enemy AI / scripted army / counterattack (enemy is a passive single villager).
- Full campaign framework, Missions 2-5, `.scx` reader.
- General trigger authoring / `engine.util.trigger.*` nyan language (the synthesized `defeat_enemy` from `required_removals` is sufficient for Gate A).
- Save/load, replay, multiplayer, determinism/lockstep (S8/S9 frozen).
- Full audio, voice-over, drama/cutscene.
- Player-color-per-owner correct palette (S7 `PLAYER_COLOR_METADATA_CONVERSION_BLOCKED`) - polish, deferred.
- All 29 deferred de2_base sprites / full DDS terrain (Gate B takes only the Mission 01 minimum).
- Faithful reproduction of the original AoE2DE Mission 01 (source import is BLOCKED - audit S4).
- Defeat-by-player-death (MVP scopes to win-only; the existing victory path suffices).

---

## 9. Runtime Path

Entry: `bin/run --scenario --no-convert --no-prompts` -> `openage.__main__.main` -> `openage/main/main.py:41` (`--scenario`) -> Engine -> `GameSimulation::initialize` (`simulation.cpp`).

Verified-connected stages (RF): startup/mode-select, asset discovery, scenario file read (`game.cpp:255` P2.7 path), mission-pack load (`mission_pack.cpp:40`), map create, entity spawn (ids 0/1/2), sim event loop, trigger synthesis (`simulation.cpp:435`), objective update (`trigger_runtime.cpp:131`), victory (`simulation.cpp:541`), clean exit.

Gate-A-specific stages requiring wiring/verification:
- **Render:** `World::attach_renderer` (`world.cpp:24`) -> `WorldRenderStage::update` (`render_stage.cpp:61`) -> `WorldObject::fetch_updates` (`object.cpp:51`). Placeholder via `AssetManager` (`asset_manager.cpp:36-56`). Screenshot via `GlRenderer::display_into_data()` (`renderer.cpp:127`), which returns a `Texture2dData` exposing `store(Path)` (`texture_data.h:70`) and `flip_y()` (`texture_data.cpp:128`) - the latter needed because `glReadnPixels` is bottom-origin.
- **Input (R2):** Qt event -> `WindowEventHandler::eventFilter` (`window_event_handler.cpp:13`) -> `GlWindow::update` (`opengl/window.cpp:116`) -> `InputManager::process` (`input_manager.cpp:131`) -> `game::Controller::process` (`controller.cpp:88`) -> binding. **Right-click binding at `controller.cpp:218-225` must route through `resolve_context_action` (`semantic_action.cpp:99`) to produce ATTACK.**
- **UI (R3):** `HudQmlAdapter` Q_PROPERTYs (`hud_qml_adapter.h:33-62`; victory-relevant: `missionLifecycle` `:56`, `missionIsVictory` `:58`, `missionVictoryReason` `:60`) must be registered as a QML context property and bound to a loaded QML (MissionHud or a Mission 1 QML). The C++ signal chain is already wired (`hud_qml_adapter.cpp:109` `refresh_mission`, subscribes `mission_state_changed` `:61-72`); only the QML context-property registration + scenario-aware QML root are missing. QML root (`presenter.cpp:223-238`) must be made scenario-aware or pointed at a Mission 1 entry.

---

## 10. Data and Media Boundaries

| Item | Gate A | Gate B | Source |
|---|---|---|---|
| `ww_mvp` fixture (`mission_01.nyan`, `scenario.json`) | ALLOWED (explicitly labelled hand-written) | REMOVED | `assets/converted/ww_mvp/...` |
| `de2_base` villager/town_center nyan | ALLOWED | REQUIRED | converter |
| Villager/town_center sprites | PLACEHOLDER acceptable | REQUIRED (converter/pipeline) | `AssetManager` placeholder now; converter later |
| Grass terrain | test_terrain (hardcoded `terrain_factory.cpp:25`) ALLOWED | converter/pipeline | test asset now; converter later |
| `VictoryDefinition.required_removals=["EnemyVillager"]` | REQUIRED | REQUIRED (converter-emitted) | `scenario.json` |
| Trigger data | synthesized `defeat_enemy` (0 data-driven triggers) | converter-emitted triggers OR synthesis | `simulation.cpp:459` |
| Player color | not required (placeholder) | correct palette (deferred) | - |

**Source of truth (D0, see §15 AC-D0):** the **Scenario IR** (`openage/scenario/ir.py`) is the input; `scenario.json`/`trigger_graph.json`/`coverage.json` (committed dumps) and `assets/converted/ww_mvp/...` are **generated products**. Drift between them is a defect, not a tolerable state.

---

## 11. Input Requirements

- Selection and commands MUST originate from a **real Qt pointer event** processed by the production `InputManager::process(const QEvent&)` path (`input_manager.cpp:131`).
- Acceptable injection: in-process `QMouseEvent` construction + `InputManager::process()` (proven precedent: `presenter.cpp:358-395` `OPENAGE_DEBUG_AUTOSPAWN` constructs `QMouseEvent{MouseButtonRelease,...}` and calls `input_manager->process()`; it first calls `set_mouse()` so the binding's `to_phys3` sees the right coord), OR `QTest::mouseClick(qt_window)` via `Window::get_qt_window()` (`renderer/window.h:137`, base class; returns `std::shared_ptr<QWindow>`) - the QTest route is NOT currently used in-repo, so the `QMouseEvent`+`process` route is preferred.
- **NOT acceptable:** directly calling `Controller::set_selected`, `Commander::send_command`, `game.send_command`, `resolve_context_action` from test/demo code as the Gate A evidence. (demo 38 does this for the synthetic path - it is evidence for the *sim* chain, not for the *windowed input* chain.)
- The right-click binding MUST produce ATTACK on a hostile target (R2). The doc/code drift in `controller.cpp:1-16` vs `:218-225` must be reconciled.

---

## 12. Trigger and Victory Requirements

- Victory MUST be driven by the real `TriggerRuntime` receiving a real `game.removal` event from the simulation (not a direct `declare_victory` call).
- The verified chain (`RemovalEventHandler -> TriggerRuntime::notify_entity_removed -> evaluate_trigger -> defeat_enemy -> DeclareVictory -> MissionState`, `simulation.cpp:477-494`) is the contract.
- For Gate A, the synthesized `defeat_enemy` trigger from `required_removals` is sufficient. A negative assertion (non-target removal does NOT declare victory) MUST remain passing (demo 20 contract).
- Victory MUST become player-visible (FR-8) - this is the R3 wiring.

---

## 13. Observability Requirements

- **Logs** are necessary but NOT sufficient (see hard constraints). Required log lines for Gate A: `load_scenario: P2.7 production path`, `spawned ... entity_id=0/1/2`, `wire_trigger_data: ... trigger_count=1`, `AttackHandler: invoke`, `RemovalEventHandler: target 2 removed`, `MissionState::declare_victory: reason='enemy_removed'`, clean `GameSimulation::stop`.
- **Screenshot** (`display_into_data().store()`) is required evidence for FR-2 (entities visible) but NOT sufficient on its own (see constraints).
- **QML/UI state** readback is required for FR-8: the test must assert the `HudQmlAdapter` victory Q_PROPERTY (or equivalent bound QML element) reflects victory, AND a screenshot shows the victory UI. Internal `MissionState` alone is NOT player-visible evidence.
- **Sim state** readback (entity liveness, removal) is required for FR-6/FR-7 corroboration.

---

## 14. Cross-platform Harness Requirements

- `scripts/verify_ww_mission1.sh` MUST pass on macOS and Linux. Fixes: replace `timeout(1)` with a portable kill-on-sleep wrapper (or `gtimeout` with fallback); accept both `venv/` and `.venv/`; accept both `libopenage.so*` and `libopenage.*.dylib`.
- Screenshot capture uses a **real window** + `display_into_data()` (offscreen `QT_QPA_PLATFORM` is NOT guaranteed for the world GL context - `context.cpp:104-109`). macOS: real window. Linux headless CI: Xvfb.
- The Gate A smoke MUST capture the engine's real exit code (0 = clean, 124/137 = killed-alive = smoke OK, 134/139 = crash = FAIL) without `timeout`-related false FAILs.

---

## 15. Acceptance Criteria

Each AC tags: Gate (A/B), automation (auto/semi/manual), input, production entry, success signal, failure signal, evidence artifact.

### Decision gates
- **AC-D0 (Gate -, auto):** Source-of-truth adjudicated; IR is input, dumps/pack are products; `6,4` vs `8,8` resolved with a written decision; a drift-detection test exists. Input: IR + committed dump. Entry: pytest. Success: drift test passes on the chosen canonical positions. Failure: positions still diverge without a recorded decision. Evidence: `OLD-PLAN-MIGRATION.md` D0 decision + passing `test_p2_integration.py::test_regenerated_dump_matches_committed_p1_dump`.
- **AC-D1 (Gate A, semi):** U1 screenshot shows 3 entities drawn (placeholder OK) at their positions on the grass map. Input: `bin/run --scenario`. Entry: real window. Success: PNG shows entities + terrain. Failure: empty/broken scene; localize breakpoint. Evidence: `m1-visibility.png` + render-object count log.
- **AC-D2 (Gate A, auto):** Real Qt LMB -> selection. Input: `QMouseEvent` via `InputManager::process`. Entry: production input. Success: `SelectionModel` contains villager id; selection overlay quad present. Failure: no selection. Evidence: test log + screenshot with overlay.
- **AC-D3 (Gate A, auto):** Real Qt RMB on ground -> MoveCommand + unit moves (FR-4); AND real Qt RMB on enemy -> AttackCommand -> death -> removal -> TriggerRuntime -> objective -> victory (FR-5/FR-6/FR-7). Input: `QMouseEvent`. Entry: production input + sim. Success: ground-click issues MOVE and the unit moves (keyframes); enemy-click issues ATTACK and `MissionState::declare_victory reason='enemy_removed'` via real removal. Failure: MOVE issued instead of ATTACK (R2 unwired), or ground-click issues no/wrong command, or no victory. Evidence: test log + sim state readback (move keyframes + enemy HP/removal + victory).
- **AC-D4 (Gate A, semi):** Victory player-visible. Input: D3 victory. Entry: QML/HudQmlAdapter. Success: bound QML element reflects victory + screenshot shows victory UI. Failure: victory only in C++/log. Evidence: `m5-victory-ui.png` + Q_PROPERTY readback.
- **AC-D5 (Gate B, auto+manual):** Gate A chain passes on converter-backed content. Input: converter Mission 01. Entry: `bin/run --scenario`. Success: FR-1..FR-9 pass on converter content. Failure: depends on fixture. Evidence: Gate B run logs + screenshots + `source_status != BLOCKED`.

### Gate A exit
- **AC-A1 (Gate A, semi):** One continuous windowed run performs FR-1..FR-9 via real Qt input. Evidence: full run log + `gate-a-run.mp4` or annotated screenshots + `docs/mvp/reports/mission1-windowed-acceptance.md` declaring `WW_MISSION_1_WINDOWED_INTEGRATION: PASS`.
- **AC-A2 (Gate A, auto):** Regression: demos 7,9-14,20,35-38,42,46,49 still PASS; the new Qt-input tests PASS.

### Gate B exit
- **AC-B1 (Gate B, semi):** AC-A1 passes on converter-backed content. Evidence: `docs/mvp/reports/mission1-content-acceptance.md` declaring `WW_MISSION_1_CONTENT: PASS`.

### MVP exit
- **AC-MVP1:** AC-A1 + AC-B1 + reproduction doc + regression suite + tech-debt list. Declare `WW_MISSION_1_PLAYABLE_MVP: PASS`.

### Hard constraints (bind all ACs)
1. **No direct simulation API input.** Gate A selection/commands MUST come from real Qt pointer events through `InputManager::process`. Direct `Controller`/`Commander`/`resolve_context_action` calls are test scaffolding, not Gate A evidence.
2. **No direct victory trigger.** Victory MUST come from a real `game.removal` event through `TriggerRuntime`. No test may call `declare_victory`/`set_objective_state` directly as the Gate A signal.
3. **No exception-swallowing fallbacks.** No new `catch(...)` that masks a failure as success; the existing `simulation.cpp:237` rethrow is the model.
4. **No fixture-as-converter-backed.** Gate A evidence MUST be labelled fixture-based; Gate B MUST NOT use `ww_mvp`.
5. **Screenshot is not sole evidence.** A screenshot alone never satisfies an AC; pair with state readback or log.
6. **Log is not player-visible UI.** A log line never satisfies FR-8; pair with QML/`HudQmlAdapter` readback + screenshot.
7. **Generated products must match source input.** Dumps/pack must match the IR (D0); a drift is a defect.
8. **No lowering assertions to pass.** No skipping tests, no approximating away failures, no hardcoding results.

---

## 16. Evidence Package

Gate A evidence directory: `docs/mvp/reports/mission1-windowed-acceptance/`
- `m1-visibility.png` (D1)
- `m3-selection.png` + selection readback (D2)
- `m4-combat.png` + sim state readback + victory log (D3)
- `m5-victory-ui.png` + Q_PROPERTY readback (D4)
- `gate-a-run.log` (full run)
- `gate-a-run.mp4` or annotated screenshot sequence (AC-A1)
- `mission1-windowed-acceptance.md` (declaration)

Gate B evidence: `docs/mvp/reports/mission1-content-acceptance/` + `mission1-content-acceptance.md`.

---

## 17. Technical Debt Policy

- All Gate A temporary art (placeholder) is **labelled, isolated, and has a deletion condition** = "removed when Gate B supplies the real asset". It MUST NOT be used for Gate B signoff.
- The R2 ATTACK binding and R3 QML wiring are **intended product code**, not scaffolding - they stay after Gate A.
- The unstaged `move.cpp` WIP must be resolved (commit with a pinning test, or revert) at M0 before new work.
- New tech debt incurred per milestone MUST be listed in that milestone's "New technical debt" field and tracked to a deletion condition.
- Legacy `WilliamWallaceMission01ObjectiveController` (`objective_controller.cpp`) is dormant (not on production path); do not extend it. Deletion is a separate cleanup, not a Gate A task.

---

## 18. Unknowns

- **U1 (M1 resolves):** Do entities render on screen? Predicted PASS (placeholder). If FAIL, M2 localizes.
- **U-cam:** Are edge entities (6,4)/(10,4) in the default camera view? M1 screenshot resolves; 1-line adjust fallback.
- **U-offscreen:** Does `QT_QPA_PLATFORM=offscreen` give a usable GL context? Assumed NO; use real window + Xvfb on Linux CI. M1 confirms.
- **U-attack-bind:** Exact minimal edit to route right-click through `resolve_context_action` at `controller.cpp:218-225` (the picking layer must produce a `PickResult` for the hostile entity). M4 resolves.
- **U-qml:** Whether `MissionHud.qml` is salvageable or a minimal Mission 1 QML is simpler. M5 resolves.
- **U-content (Gate B):** The exact minimum converter output for Mission 01 (which sprite set, which terrain). M7 resolves after Gate A.

---

## 19. Final MVP Exit Criteria

`WW_MISSION_1_PLAYABLE_MVP: PASS` when ALL hold:
1. Gate A PASS (`WW_MISSION_1_WINDOWED_INTEGRATION`) with the full evidence package (§16).
2. Gate B PASS (`WW_MISSION_1_CONTENT`) on converter-backed content.
3. Real Qt input drove selection + move + attack (no direct sim API).
4. Victory came from a real removal through `TriggerRuntime` (no direct trigger).
5. Victory is player-visible (not log-only).
6. D0 source-of-truth holds; no IR/product drift.
7. Regression suite (existing demos + new Qt-input tests) green.
8. Reproduction documented; tech-debt list current; cross-platform harness passes on macOS + Linux.

Otherwise declare `FAIL` or `BLOCKED` with evidence. No partial credit.
