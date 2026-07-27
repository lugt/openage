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
plan-tutorial-lvl.md  [deleted, see PhaseS0 migration table] (prior session)
run-generate.sh
```

## Blockers

* A4 (ArsonResearchable): the converter source files needed may overlap with the protected list. Plan: examine each error category; if a fix is required in a protected file, save the user's diff first and produce a non-conflicting additive patch or flag and continue with independent work.
* Full de2_base convert OOMs in this VM — user must re-run convert after A4 patches land.

## Next Task

A5: load_metrics (C++). Requires `ninja -C bin` rebuild; loader metrics are independent of the converter blocker.

---

## Session 2 update (2026-07-25): Converter Correctness Unblock

**Headline commit:** 3060bed2 `convert: emit ResearchableTech wrapper for passive shared techs (Arson)`

**Root cause** (one paragraph): `AoCAuxiliarySubprocessor.get_researchable_tech`
only emitted a `XxxResearchable` wrapper when `is_researchable()` returned True.
For passive shared techs (research_time=0, no research_location) like
Arson/Gambesons/etc., the wrapper was never emitted. But civ bonus patches
in slavs.nyan, dravidians.nyan, goths.nyan, romans.nyan etc. still target
the FQON `XxxResearchable` via `tech_time_modify_effect` /
`tech_cost_modify_effect`, so the nyan::Database load fails with
`NameError: unknown name: ArsonResearchable` at slavs.nyan:813.

**Fix:** added a third `else` branch in `get_researchable_tech` for
`research_location_id == -1 and not is_unique()`. The wrapper is emitted
at the tech's own location (`ForwardRef(tech_group, tech_name)`), so
`Arson.ResearchableTech` resolves to the same FQON as `ArsonResearchable`
in `assets/converted/de2_base/data/tech/generic/arson/arson.nyan`. Call
sites in `de2/nyan_subprocessor.py:653` and `aoc/nyan_subprocessor.py:907`
extended to also call `get_researchable_tech` for passive shared techs.

**General, not per-civ:** the fix addresses the family at the root
(passive shared techs) by adding one code branch and updating two call
sites. No per-civ hardcoding (no Slavs/Goths/Romans special cases).

**Tests:** 4 source-level tests in
`openage/convert/processor/conversion/aoc/tests/test_researchable_passive.py`
verify the function structure (3 branches, sentinel check, emission at
tech's own location). All 4 PASS.

**Full convert:** cannot run in this VM (the classifier denies
`python -m openage convert` per the standing rule, and the de2 .dat files
are not present on this machine). The user must run on their side:

```bash
python -m openage convert --no-prompts --force --low-memory \
  --source-dir <path-to-aoe2-dat> \
  --output-dir <output>
```

Then re-run the validator:

```bash
python -m openage validate-nyan \
  --modpacks assets/converted/engine \
  --modpacks assets/converted/de2_base \
  --json /tmp/post_fix.json
```

Expect summary `{"errors": 0, "warnings": 0}` (or at least a dramatic drop
from the pre-fix 664 errors / 416 unique). The 416 unique missing FQONs
are dominated by `*Researchable` and `*Researchable.*.XAmount` patterns,
which this fix should resolve at the source.

**Hard gate (real loader smoke):** after the user confirms the validator
post-fix is clean, run:

```bash
bin/run game --tutorial --modpacks de2_base --no-prompts --headless
```

Expect: clean exit 0, scenario loaded, Villager + TownCenter spawned,
no NameError / MemberNotFoundError, idle/walk/selection/move smoke pass.

**Mission 1 trigger:** only when the hard gate above passes on the
user's machine, proceed to Mission 1 minimal combat vertical slice
(AttackCommand → right-click enemy → move to attack range → fixed
damage → cooldown → live HP → HP≤0 death → remove from selection/collision/
pathfinding/render → MissionDirector detect all-enemies-dead → victory
UI → restart). Do NOT begin Mission 1 in this VM; do NOT touch gather,
build, train, tech, AI, multiplayer, or the rest of the campaign.
