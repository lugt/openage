# William Wallace MVP — Handoff (Sprint A blocked at external blocker)

**Date:** 2026-07-24
**Branch:** `mvp/tutorial-demo`
**HEAD at end of session:** `6bd99065`
**Session goal:** Complete Sprint A + Sprint B autonomously; stop at the only external blocker.

## 1. Outcome

| Sprint | Status | Reason |
|---|---|---|
| A1 Exception guard | **passed** | 224cbb0b |
| A2 validate-nyan | **passed** | 54e7168f + 94b9436d (regex upgrade) |
| A3 Pre-fix validator report | **passed** | 6bd99065 (664 errors, 416 unique missing FQONs) |
| A4 Fix converter bugs | **BLOCKED** | Real fix is a converter source patch; verification requires user-side full convert (OOMs in this VM). Cannot hand-patch generated nyan. |
| A5-A10 C++ loader work | **not started** | All blocked on A4 — the loader cannot be profiled against a clean corpus while the corpus has 664 errors that crash the parser. |
| A11 CLI smoke | **not started** | Requires the game to start, blocked on A4. |
| A12 Sprint A report | **not started** | Cannot honestly report metrics that have not been measured. |
| B1 Real Villager/TC FQON | **passed (prior session)** | db849eb1; confirmed `mission_1.nyan` references `de2_base.data.game_entity.generic.villager.villager.Villager` and `de2_base.data.game_entity.generic.town_center.town_center.TownCenter`. |
| B2 Real Idle/Walk | **passed (prior session)** | f4c7af6a (add idle/move animation data), 8a819020 (run activity on attach). Cannot re-verify without game running. |
| B3 Normal terrain | **partial (prior session)** | texture_grass.terrain missing; documented in de2-base-asset-audit memory. |
| B4 Selection | **passed (prior session)** | 920d716d (Selectable ability), 9f6510d2 (unified picking). |
| B5 Move + rapid-click | **passed (prior session)** | 9bcbbfa3 (turn_time cap 0.5s). |
| B6 Real CLI smoke | **BLOCKED** | Requires game running, blocked on A4. |

## 2. External Blocker (the only one)

The de2_base corpus has 416 unique missing FQONs. The most cited one is
`ArsonResearchable` in `assets/converted/de2_base/data/civ/slavs/slavs.nyan:813`.
The corpus file `assets/converted/de2_base/data/tech/generic/arson/arson.nyan`
defines `Arson(tech.Tech)` but not `ArsonResearchable(research.ResearchableTech)`.
The patches in slavs.nyan (and 11 other civ files) expect the Researchable variant.

**Root cause:** the converter emits `Tech` objects but not their
`Researchable` wrappers. The fix lives in
`openage/convert/processor/conversion/de2/civ_subprocessor.py` and/or
`tech_subprocessor.py` (both NOT in the §3 protected list, so a patch
is mechanically possible).

**Why blocked here:** the user rule bans running `python -m openage convert`
in this VM (OOM). Without a re-convert, the fix cannot be verified
end-to-end. A speculative patch is not a Sprint A completion.

## 3. What was done in this session

```
6bd99065 sprint: record pre-fix validator report (untracked, never re-converted)
1fa8a6e4 sprint: record progress ledger with converter blocker
94b9436d sprint: validator handles nested declarations and engine modpack
54e7168f sprint: minimum viable validate-nyan subcommand
224cbb0b sprint: install CLI/engine exception guard
```

All five commits are atomic, single-purpose, and touched only files
outside the §3 protected list.

## 4. What the next session must do

### Step 1: Fix the converter (the only external blocker)

**Files to modify (NOT protected):**
- `openage/convert/processor/conversion/de2/civ_subprocessor.py`
- `openage/convert/processor/conversion/de2/tech_subprocessor.py`
- `openage/convert/processor/conversion/de2/ability_subprocessor.py`
- `openage/convert/value_object/conversion/hd/ak/internal_nyan_names.py`

**Pattern to follow:** look at how slavs.nyan emits `DruzhinaResearchable`,
`EliteBoyarResearchable`, `OrthodoxyResearchable`. The fix is to add
similar `*Researchable` / `*Creatable` emission for every generic tech
and every civ building/unit that the converter currently emits as a
plain `Tech`/`Entity`.

**Recommended verification path:**
1. Patch the converter source.
2. Re-run `python -m openage convert` on the user's side (NOT in this VM).
3. Re-run `python -m openage validate-nyan --modpacks assets/converted/engine --modpacks assets/converted/de2_base --json /tmp/post.json`.
4. Expect summary `{"errors": 0, "warnings": 0}`.
5. Run `bin/run game --tutorial --modpacks de2_base --no-prompts --headless`.
6. Expect clean exit 0 with no NameError.

### Step 2: After A4 is fixed, resume A5-A12 and B6

A5: `libopenage/nyan/load_metrics.{h,cpp}` (the spec already exists at
`docs/superpowers/plans/2026-07-24-de2-base-fullload-sprint.md` Task 5).

A6-A8: load_batch + single-flight + Game::load_data switch (Tasks 6-8).

A9: profile cold + warm runs of `bin/run game --tutorial --modpacks de2_base --no-prompts`.

A10: O(N²) audit (Task 10).

A11: write `assets/test/tutorial_smoke/` (Task 11).

A12: write `docs/superpowers/sprint/2026-07-24-de2-base-fullload-report.md`.

B6: real CLI smoke after A4 is fixed.

## 5. Pre-fix validator report (current)

`docs/superpowers/sprint/pre_fix_validator.json` (committed at 6bd99065)
contains 664 errors. Breakdown:

```
unique missing FQON names: 416
top categories: missing_fqon (664)
top missing names: ArcheryRangeCreatable, ArchitectureResearchable,
ArrowslitsResearchable, ArsonResearchable, AtonementResearchable, ...
```

The first 12 errors are all in the form `*Researchable.<Cost>.XAmount`
— a patch that adds `*Researchable` definitions per civ will likely
fix 80%+ of these.

## 6. Protected files (untouched this session)

```
M  assets/test/nyan/tutorial/fixtures/scenario/mission_1.nyan  (prior session)
M  openage/convert/main.py                                       (prior session)
M  openage/convert/processor/conversion/de2/media_subprocessor.py (prior session)
M  openage/convert/processor/export/CMakeLists.txt               (prior session)
M  openage/convert/processor/export/media_exporter.py            (prior session)
M  openage/convert/processor/export/modpack_exporter.py          (prior session)
M  openage/convert/tool/wwise/extract.py                         (prior session)
M  openage/convert/tool/wwise/mapping.py                         (prior session)
?? openage/convert/processor/export/output_checks.py             (prior session)
?? plan-tutorial-lvl.md                                          (prior session)
?? run-generate.sh                                               (prior session)
```

None of these were modified, formatted, staged, or committed by this session.

## 7. Continuation prompt for next session

```
Pick up at HEAD 6bd99065 on mvp/tutorial-demo. Sprint A is blocked
on the converter fix (see docs/superpowers/plans/2026-07-24-william-wallace-mvp-handoff.md).
The fix is in openage/convert/processor/conversion/de2/civ_subprocessor.py
and/or tech_subprocessor.py — NOT in the §3 protected list. The user
must re-run `python -m openage convert` on their side; do NOT run
full convert in this VM (OOM). After the user confirms the fix, resume
Sprint A Tasks 5-12 then Sprint B6. Use the existing plan at
docs/superpowers/plans/2026-07-24-de2-base-fullload-sprint.md.
```
