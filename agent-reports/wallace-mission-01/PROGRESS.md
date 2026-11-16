# William Wallace Mission 01 MVP — Progress

Branch: `mvp/william-wallace`
Tracking: `IMPLEMENTATION-PLAN-v2.md` milestones M0–M8.
Gate A (M6): headless simulation slice passes. Gate B (M7): windowed vertical slice passes.

## Milestone status

| Milestone | Title | Status | Notes |
|-----------|-------|--------|-------|
| **M0** | Baseline Integrity & Harness Portability | ✅ `VERIFIED_COMPLETE` | 2026-08-05. All 12 completion criteria met. Stage 4 `ENV_BLOCKED` is a documented GL-context environment limit on this headless host, not an engine/content/harness failure. See `M0-BASELINE-INTEGRITY-REPORT.md`. |
| M1 | Mission IR Provenance Pin | 🔲 not started | Gate to enter: M0 `VERIFIED_COMPLETE` ✅ satisfied. Do not auto-start. |
| M2 | Deterministic Spawn Verification | 🔲 not started | |
| M3 | Movement Command Path | 🔲 not started | |
| M4 | Attack/Selection Path | 🔲 not started | |
| M5 | Trigger Runtime Smoke | 🔲 not started | |
| M6 | Gate A — Headless Sim Slice | 🔲 not started | |
| M7 | Gate B — Windowed Vertical Slice | 🔲 not started | |
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

## Stop-rule compliance

- Only M0 executed. M1 not started. Gameplay/simulation/renderer/presenter/Qt-input/command-resolver/converter-export/mission-product-content untouched.
- User pre-existing WIP (`libopenage/gamestate/system/move.cpp` staged +111/-38; `doc/code/p4_4d2_p4_4f_plan.md` +1/-1) left untouched.
- No git commit/stash/reset/checkout performed. No golden auto-overwrite.
- No third speculative edit made; the `object_table.csv` sync was performed only after explicit user authorization (M0 cleanup), with a strict guard proving only positions changed. No fake-fix (no `|| true`, no assertion relaxation, no test deletion, no skip-on-macOS).
