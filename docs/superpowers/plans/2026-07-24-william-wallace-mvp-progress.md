# William Wallace MVP — Goal-Driven Progress Ledger

**Branch:** `mvp/tutorial-demo`
**Started:** 2026-07-24 (post-224cbb0b)
**Goal:** Complete Sprint A and Sprint B per the master plan.

## Sprint A — Correctness + Loader Baseline

| Task | Status | Commit | Notes |
|---|---|---|---|
| A1: Exception guard | passed | 224cbb0b | 2/2 tests pass; verified --help, RuntimeError, ValueError |
| A2: validate-nyan | active | – | TDD: failing test first |
| A3: Run validator on de2_base | passed | – | Pre-fix report saved to docs/superpowers/sprint/pre_fix_validator.json (666 errors, 418 unique missing FQONs, untracked per plan §3 rule) |
| A4: Fix ArsonResearchable + other validator findings | blocked | – | Sanity check: `grep ArcheryRangeCreatable assets/converted/de2_base/...` shows it is referenced (in 56 files) but never declared anywhere — the converter really is missing 416+ FQON definitions. Cannot run full convert (OOM in this VM) and cannot hand-patch generated nyan. Real fix requires a converter source patch (`openage/convert/processor/conversion/de2/civ_subprocessor.py` and others) followed by user-side re-convert. All converter source files I might need are NOT in the §3 protected list, so a patch is mechanically possible, but I cannot verify it end-to-end in this session. Per user rule, stopping at the only external blocker (full convert). |
| A5: Load metrics | pending | – | C++: libopenage/nyan/load_metrics.{h,cpp} |
| A6: load_batch | pending | – | C++: parse-once |
| A7: Resolve cache | pending | – | C++: single-flight |
| A8: Game::load_data uses load_batch | pending | – | C++: state.cpp + load_data.cpp |
| A9: Profile | pending | – | capture cold/warm |
| A10: O(N²) audit | pending | – | profiler-driven |
| A11: CLI smoke | pending | – | assets/test/tutorial_smoke/ |
| A12: Sprint A report | pending | – | docs/superpowers/sprint/2026-07-24-de2-base-fullload-report.md |

## Sprint B — Real Asset Vertical Slice

| Task | Status | Commit | Notes |
|---|---|---|---|
| B1: Real Villager/TC FQON | passed (prior session) | db849eb1 | mission_1.nyan uses real de2_base FQON |
| B2: Real Idle/Walk | partial (prior session) | – | need verification |
| B3: Normal terrain | partial | – | texture_grass.terrain missing in de2_base; will use existing presenter fallback if needed |
| B4: Selection | passed (prior session) | 920d716d | villager Selectable fix + integration test |
| B5: Move + rapid-click | passed (prior session) | 9bcbbfa3 | turn_time cap 0.5s |
| B6: Real CLI smoke | pending | – | depends on A4 (ArsonResearchable) |

## Protected Files (do NOT touch)

```
openage/convert/main.py
openage/convert/processor/conversion/de2/media_subprocessor.py
openage/convert/processor/export/CMakeLists.txt
openage/convert/processor/export/media_exporter.py
openage/convert/processor/export/modpack_exporter.py
openage/convert/processor/export/output_checks.py
openage/convert/tool/wwise/extract.py
openage/convert/tool/wwise/mapping.py
plan-tutorial-lvl.md
run-generate.sh
```

## Blockers

* A4 (ArsonResearchable): the converter source files needed may overlap with the protected list. Plan: examine each error category; if a fix is required in a protected file, save the user's diff first and produce a non-conflicting additive patch or flag and continue with independent work.
* Full de2_base convert OOMs in this VM — user must re-run convert after A4 patches land.

## Next Task

A5: load_metrics (C++). Requires `ninja -C bin` rebuild; loader metrics are independent of the converter blocker.
