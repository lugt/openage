# PLANNING DECISION — William Wallace Mission 01 MVP

Date: 2026-08-05
Branch: `mvp/william-wallace` @ `56109051`
Inputs: `agent-reports/wallace-mission-01/current-state-audit.md` + targeted spot verification (Qt input path, renderer entity rendering, QML HUD wiring).
Status: **Decision/planning only. No code, tests, build, or assets modified. No git mutation.**

This document records the engineering decisions behind `MVP-SPEC-v2.md` and `IMPLEMENTATION-PLAN-v2.md`. It is the authority for the chosen route; the spec and plan inherit from it.

---

## 0. Spot-verification refinements to the audit (HEAD-consistent)

The audit is consistent with HEAD. Three refinements (not contradictions) were established by reading code the audit flagged as UNKNOWN/unverified — they shape the plan:

- **R1 (audit U1 → resolved as likely-pass).** Entities render as a **placeholder** (`missing.png`) when de2_base sprites are absent: `AssetManager::request_animation` (`asset_manager.cpp:36-56`) catches the missing-file error and returns the placeholder set at `presenter.cpp:142-143` (`test_missing.sprite` → `test_missing.texture` → `missing.png`, all present). Terrain renders via a hardcoded `../test/textures/test_terrain.terrain` (`terrain_factory.cpp:25`, files present). No crash. → The render pipeline produces a draw call per entity at its world position. **"Visible" for Gate A = render pipeline draws the entity (placeholder acceptable); correct villager art is Gate B.**
- **R2 (audit row 11 → refined from "unverified" to "unwired").** The ATTACK resolver `input::resolve_context_action` (`semantic_action.cpp:99-224`) is **only called by demos**. The production right-click binding (`controller.cpp:218-225`) is still the legacy **always-MOVE** binding; the `controller.cpp:1-16` header comment claiming "P4.4D right-click goes through resolve_context_action" is **doc/code drift**. → A real windowed right-click on the enemy currently issues MOVE (walk toward), NOT AttackCommand. **M4 is a wiring task, not just verification.** (The audit's "demo 38" evidence is valid but demo 38 uses the synthetic path; the windowed gap is real.)
- **R3 (audit U2 → resolved).** QML root is hardcoded to `assets/test/qml/main.qml` (`presenter.cpp:223-238`) — a trivial colored-rectangle test scene. `MissionHud.qml` is orphaned (no references). The C++ signal chain `TriggerRuntime → mission_state → MissionView → HudQmlAdapter` (Q_PROPERTYs + `mission_changed`/`selected_changed` signals, `hud_qml_adapter.cpp:109-119`) IS wired, but `HudQmlAdapter` is **not registered as a QML context property** and no loaded QML binds to it. → Victory is computable and signal-propagated in C++ but **never reaches the screen**. **M5 is a bounded "last QML hop" wiring task.**

Additional enablers confirmed (lower risk than the audit implied):
- **Screenshot readback already exists**: `GlRenderer::display_into_data()` (`renderer.cpp:127-143`, `glReadnPixels`). One line at `presenter.cpp:510-512` after the render pass writes a PNG. `ScreenshotManager` exists but is unwired. → M1 screenshot capture is trivial.
- **In-process Qt input injection is feasible**: `InputManager::process(const QEvent&)` is the sole entry, Qt-decoupled. Proven precedent at `presenter.cpp:358-395` (`OPENAGE_DEBUG_AUTOSPAWN` constructs a `QMouseEvent` and calls `input_manager->process()`). Two paths: (A) `QTest::mouseClick(qt_window)` via `Window::get_qt_window()` (`window.h:137`) — full production path incl. event filter; (B) construct `QMouseEvent` + `InputManager::process()` — simplest, still production. **No OS-level automation needed.**
- **Sim advances by wall clock** on its own worker thread (`engine.cpp:178-180`, `clock.cpp:55-60`); `reach_time()` walks the full combat→victory chain in one call (`simulation.cpp:293-310`). A test injects input then **waits for natural victory**; `Clock::set_speed` can accelerate. No manual stepping.
- **Camera** defaults to `look_at_coord(scene3{10,10,0})` (`presenter.cpp:147`), zoom 1.0; 16×16 map is in view. Edge entities (6,4)/(10,4) may be at the margin → M1 fallback is a 1-line center/zoom adjust at `presenter.cpp:147`.
- **Offscreen (`QT_QPA_PLATFORM=offscreen`) is NOT guaranteed** for the world GL context (`context.cpp:104-109` throws if invalid; never tested offscreen). → Use a **real (optionally hidden) window + `display_into_data()` readback**. macOS: real window. Linux CI: Xvfb.

---

## 1. Recommended route

**Option 3 — Hybrid risk-gated.** Confirm with a minimal U1 visibility spike (M1) before committing to either converter work or full Qt-input automation. R1 makes it very likely U1 passes (placeholder render), so the expected path is M1 → M3 → M4 → M5 → M6 (Gate A) → M7 (Gate B) → M8, with M2 (media unblock) only triggered if M1 fails.

---

## 2. Route comparison

| Dimension | Option 1 Converter-first | Option 2 Windowed-integration-first | Option 3 Hybrid risk-gated (RECOMMENDED) |
|---|---|---|---|
| Time to first observable progress | Worst — converter media work (DDS terrain FIX-3, atlas OOM FIX-4) is large and unbounded before any new evidence | Good — window launch + screenshot in hours | Best — U1 screenshot in hours, and it is the single load-bearing unknown |
| Risk reduction | Removes B2/B3 media risk first but ignores the bigger integration unknowns (R2 ATTACK-unwired, R3 QML-unwired) | Removes integration unknowns but bets U1 passes without evidence | Removes the **largest** unknown (U1) first, then sequences the rest by evidence |
| Scope | Largest (full converter sub-project) | Medium (Qt input + QML + harness) | Smallest first step; grows only if evidence demands |
| False-success risk | High — could ship "converter done" while windowed play still broken | Medium — could declare "playable" on fixture with placeholder art | Lowest — each milestone has a falsifiable signal before the next starts |
| Converter dependency | Maximal, upfront | Minimal (Gate A uses existing `ww_mvp`) | None until M7; M1 explicitly defers converter |
| Renderer dependency | Deferred | Front-loaded | Front-loaded but bounded (readback + camera only) |
| Automation difficulty | Same end state | Must build Qt-injection harness anyway | Same harness, but built only after U1 proves rendering works |
| Architectural contamination | High (converter changes ripple) | Low-Medium (ATTACK binding + QML root are intended product code, not scaffolding) | Lowest — M1/M2 changes are isolated and Gate-A-labelled |
| Rollback | Hardest (converter outputs are sticky) | Easy (revert binding/QML) | Easiest per-step (each milestone is independently revertible) |
| Final MVP path | Reaches Gate B but may still fail Gate A late | Reaches Gate A cleanly; Gate B still needed | Reaches Gate A with least waste; Gate B follows naturally |

**Why not Option 1:** R1 shows entities already render as placeholder; the converter is **not** on the critical path to a visible, interactive slice. Front-loading DDS-terrain/atlas work (FIX-3/FIX-4) is large, unbounded, and does not resolve R2 (ATTACK-unwired) or R3 (QML-unwired), which are the actual Gate A blockers. It also risks another ineffective loop in the converter region (the project's historical churn area).

**Why not Option 2 (pure):** It is close to Option 3 but skips the U1 spike. Without U1, M3-M5 could be built against an invisible scene and only discover a render pipeline bug late. The U1 spike is hours of work and eliminates the largest unknown; skipping it is false economy.

---

## 3. Gate definitions

### Gate A — Windowed Gameplay Integration Gate
Proves the **production windowed runtime** end-to-end on the existing hand-written `ww_mvp` fixture:
`bin/run --scenario` → Qt window → renderer/presenter → visible entities (placeholder art acceptable) → **real Qt pointer input** → selection → player command → simulation combat → enemy removal → TriggerRuntime → objective completion → victory → **player-visible UI feedback** → clean exit.

Pass label: **`WW_MISSION_1_WINDOWED_INTEGRATION`**.
MUST NOT be called "converter-backed", "content-complete", or "playable MVP".

### Gate B — Mission 01 Content Gate
Proves the Gate A windowed chain runs on **converter-backed Mission 01 content**: Mission 01 data and required media come from a supported converter path (or explicitly-supported content pipeline), not the hand-written `ww_mvp` fixture. Gate A's chain must pass on that content.

Pass label: **`WW_MISSION_1_CONTENT`**.

### Final MVP
**`WW_MISSION_1_PLAYABLE_MVP`** = Gate A PASS **and** Gate B PASS, with documented reproduction, regression tests, and a tech-debt list.

---

## 4. Critical path

```
M0 (baseline integrity + harness portability)
  └─ M1 (U1 visibility spike)  ← first risk-removal experiment
        ├─ if FAIL → M2 (minimal visual unblock) → back to M1
        └─ if PASS → M3 (real Qt input → selection)
              └─ M4 (real Qt input → combat; incl. wire ATTACK binding)
                    └─ M5 (objective/victory → player-visible UI)
                          └─ M6 (Gate A acceptance + evidence package)
                                └─ M7 (converter-backed content; Gate B)
                                      └─ M8 (final MVP signoff)
```

M2 is conditional (only if M1 fails). D0 runs in M0; D1 in M1; D2 in M3; D3 in M4; D4 in M5; D5 in M7.

---

## 5. First milestone to execute

**M0 — Baseline Integrity and Harness Portability.**
It is a prerequisite for trustworthy evidence in every later milestone (the macOS harness false-fails today; the IR/pack/dump drift must be adjudicated before any "did it pass" judgment). It changes **no gameplay behavior**. See `IMPLEMENTATION-PLAN-v2.md` M0.

---

## 6. First risk-removal experiment

**The U1 visibility spike (M1 / decision gate D1):**
Launch `bin/run --scenario --no-convert --no-prompts` at a fixed window size (1024×768), fixed camera (`presenter.cpp:147`), fixed spawn (`scenario.json`); after N rendered frames, call `renderer->display_into_data().store(path)` (one line at `presenter.cpp:510-512`); inspect the PNG.

- **Question answered:** Are the 3 entities drawn at their world positions (placeholder art counts), or is the scene empty/broken?
- **Why first:** R1 predicts PASS. If PASS, the entire converter sub-project is off the Gate A critical path and M2 is skipped. If FAIL, the screenshot localizes the first visual breakpoint (camera / render-object / placeholder-parse) far more cheaply than any other investigation. This single fact reframes the whole plan.
- **Must complete before any large converter work** (M7) — hard rule.

---

## 7. Top three risks

1. **R2 — ATTACK is unwired in the production windowed path.** The most consequential plan-shaping fact. Mitigation: M4 wires `resolve_context_action` into the right-click binding (`controller.cpp:218-225`) with an automated Qt-injection test asserting AttackCommand (not MOVE) is dispatched on a hostile right-click. Risk of re-introduction: the doc/code drift must be fixed in lockstep (update the controller header comment to match the code).
2. **Ineffective-loop relapse in `move.cpp` / occupancy.** The audit flagged repeated edits to `move.cpp` + uncommitted WIP with no test pinning the new bisect-halt behavior. Mitigation: the "two-strikes stop rule" (see §9) and a requirement that any `move.cpp` change in M0-M6 land with a demo pinning the new contract; the unstaged WIP must be resolved (commit or revert) at M0 before new work.
3. **False-success in the harness** — declaring Gate A on log lines or screenshots alone, or by directly calling simulation APIs instead of Qt input. Mitigation: the spec's hard constraints (no direct sim API input, no direct victory trigger, no log-as-UI, no screenshot-as-sole-evidence, no fixture-as-converter-backed) and the per-milestone failure signals.

---

## 8. Old tasks that should NOT continue

- **Do not re-implement any VERIFIED_COMPLETE simulation capability** (demos 7, 9–14, 20, 35–38, 42, 46, 49; P3 lifecycle; P4.1–P4.3 trigger/victory; P4.4A–D selection/input-resolver). The headless combat→victory chain is done. Gate A *verifies these through the windowed path*, it does not rebuild them.
- **Do not pursue the tick-driven `MissionDirector` / `TutorialTrigger` / gather-wood economy** (old spec S1 §7.6, §8 Mission 2-5). The event-driven `TriggerRuntime` + `defeat_enemy` synthesis is the production design. Economy is an explicit non-goal.
- **Do not start the M0 determinism harness / `sim/` / replay / lockstep** (S8/S9, frozen). Explicit non-goal.
- **Do not front-load the full deferred-media list** (all 29 de2_base sprites, DDS terrain). M7 handles only the minimum Mission 01 content; "fix all deferred media" is explicitly not a milestone.
- **Do not build a new GUI automation framework.** Reuse in-process `QMouseEvent` injection (presenter.cpp:358-395 precedent) + existing `display_into_data()` readback.
- **Do not regenerate the committed dump just to make the P2.6 test pass** without the D0 source-of-truth adjudication.

---

## 9. Two-strikes stop rule (ineffective-loop breaker)

For any milestone, if **two consecutive attempts produce no new verified evidence** (same region edited, no new passing test/screenshot/gate signal), then:
1. Stop editing that region.
2. Write a one-paragraph failure analysis naming the missing observation.
3. Run the **minimal falsifiable experiment** for that observation before resuming.
4. If the second strike is in `move.cpp`/occupancy or the converter, escalate to a design note before further edits.

This directly targets the audit's "Suspected Ineffective Loops" (§10 of the audit).

---

## 10. Do we have enough information to start M0?

**Yes.** M0 needs only: the drift adjudication (audit §6 + this doc R1-R3 give the facts to decide), the macOS harness portability fixes (audit §6 verify.log), and resolving the unstaged `move.cpp` WIP (commit or revert). No unknown blocks M0. M1's U1 spike is ready immediately after M0 (R1 predicts the outcome; the readback insertion point is known: `presenter.cpp:510-512`).
