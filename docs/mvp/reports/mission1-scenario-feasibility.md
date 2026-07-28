# William Wallace Mission 1 — Scenario Source Feasibility (P1)

**Phase:** P1 — Original Mission 1 Scenario feasibility spike  
**Branch:** `mvp/william-wallace`  
**Date:** 2026-07-28  
**Companion spec:** `docs/mvp/specs/WW_MISSION1_SCENARIO_RUNTIME_SPEC.md`  
**Companion plan:** `docs/mvp/specs/WW_MISSION1_DEVELOPMENT_PLAN.md`

This report records the evidence used to choose one of the four paths in plan
§P1.2 (A existing importer, B existing low-level parser, C source-specific
offline extractor, D source unavailable).

---

## P1.1 — Source data + parser inventory

Search performed on 2026-07-28 against `HEAD = fbb23fcf...`.

### Source data on this host

| Probe | Result | Command |
|---|---|---|
| DE2 install at `~/.steam/.../Age2HD` | **NOT FOUND** | `ls /home/gtx/.steam /home/gtx/Games ~/.local/share/Steam /mnt/games /opt/aoe` |
| `empires2.exe` / `empires2_de.exe` anywhere | **NOT FOUND** | `find / -maxdepth 5 -name empires2.exe -o -name ages2.dat` |
| `empires2_x2_p1.dat` anywhere | **NOT FOUND** | searched in install locations above |
| `*.scn` / `*.scx` / `*.cpn` files | **NOT FOUND** | `find /home/gtx/dev/games/openage -maxdepth 8 -name "*.scn" -o -name "*.scx" -o -name "*.cpn"` (excluding `.bin`) |
| `resources/_common/dat/campaigns/*` | **NOT FOUND** | not searched in install location (install location is empty) |

### Parser code in the repository

| Probe | Result | Path |
|---|---|---|
| `openage/convert` reads `empires2_x2_p1.dat`? | **YES** | `cfg/converter/games/de2/version_hashes.toml` lists `paths = ["resources/_common/dat/empires2_x2_p1.dat"]` |
| `openage/convert` reads any campaign / scenario / `campaigns_x_x.dat`? | **NO** | `grep -rE "campaigns_x\|campaign.*dat\|CampaignReader" openage/ --include="*.py" --include="*.cpp" --include="*.h" --include="*.pyx"` returns zero matches |
| Existing SCX/SCN/campaign parser? | **NO** | `grep -rE "scx\|scn\|genie_scenario\|campaign_parser\|ScxFile" libopenage/ openage/ --include="*.py" --include="*.cpp" --include="*.h" --include="*.pyx"` returns zero matches |
| C++ trigger / condition / effect runtime? | **NO** | `find libopenage -iname "*trigger*.cpp" -o -iname "*trigger*.h"` returns zero matches; `libopenage/gamestate/demo/ww_mission1_regression.cpp` uses no trigger language; `libopenage/gamestate/objective/objective_controller.cpp` polls state directly (per H0.5 report) |
| DE2 converter sub-processors | **5 total** | `ls openage/convert/processor/conversion/de2/` = `ability_subprocessor.py`, `civ_subprocessor.py`, `media_subprocessor.py`, `modpack_subprocessor.py`, `nyan_subprocessor.py`, `tech_subprocessor.py`, `upgrade_attribute_subprocessor.py`, `upgrade_resource_subprocessor.py`. **None** reads campaign / scenario data. |
| `ww_mvp` modpack origin | **HAND-AUTHORED** | `assets/converted/ww_mvp/campaign/william_wallace/mission_01.nyan` header explicitly states `HAND-WRITTEN MVP FIXTURE - NOT A CONVERTER PRODUCT`. Source = `assets/nyan/ww_mvp/campaign/william_wallace/mission_01.nyan`. Installed by `assets/nyan/ww_mvp/install.sh`. |
| `engine.util.scenario` nyan schema | **EXISTS** (openage-native) | `assets/converted/engine/util/scenario.nyan` is openage's own schema for `Scenario`. Used by `libopenage/gamestate/api/scenario.cpp` to query `Scenario.size`, `Scenario.terrain_chunks`, `Scenario.initial_entities`. |
| `engine` trigger / condition nyan | **NONE FOUND** | `find assets/converted/engine -iname "*trigger*"` returns zero matches. The repository does not yet ship a trigger / condition / effect nyan language. |

### Summary of the inventory

1. **No DE2 install on host.** The host this report was produced on is a
   graphics-less tty session with no Steam / GOG / Xbox install of AoE2 DE.
   `doc/media_convert.md` documents install paths under
   `~/.steam/.../Age2HD` etc. None of those paths are populated here.
2. **No SCX/SCN/campaign parser in repo.** The repository contains
   `libopenage` (runtime) and `openage/convert` (DE2 `.dat` → nyan) but
   **nothing that ingests `campaigns_x_x.dat` or per-mission scenario
   files**.
3. **No trigger runtime.** `libopenage/gamestate/objective/objective_controller.cpp`
   polls state every event batch (H0.5 TEMPORARY DEBT, see
   `docs/mvp/reports/phase-h0.5-driver-report.md` §3) and there is no
   `Trigger` / `Condition` / `Effect` C++ class hierarchy.
4. **The current Mission 1 is hand-authored.** This is documented in the
   file header and confirmed by `assets/nyan/ww_mvp/install.sh`. It
   references real `de2_base` entities but its tiles, placements, and
   trigger list are hand-written. It is correctly labelled in the runtime
   logs (`Scenario kind: William Wallace Mission 1 MVP reconstruction`,
   see `docs/mvp/reports/baseline-reality-audit.md` §P0.5).

---

## P1.2 — Path selection

### Evaluated paths

| Path | Available? | Notes |
|---|---|---|
| **A — Existing importer** | **NO** | The repository has no campaign importer. The `openage/convert` pipeline ingests `empires2_x2_p1.dat` only. |
| **B — Existing low-level parser, no converter** | **NO** | Same as A. There is no low-level parser for `campaigns_x_x.dat` or per-mission scenario files. |
| **C — Source-specific offline extractor** | **POSSIBLE in principle** | A new Python extractor that reads `campaigns_x_x.dat` + per-mission scenario data and emits a normalized Scenario IR is feasible but requires (a) source data the user must legally obtain and supply, (b) implementation of the AoE2 DE campaign binary format, (c) implementation of an `engine.util.trigger.*` nyan language. None of these exist today. |
| **D — Source unavailable or unsupported** | **YES** | Source data is not on this host. The repo has no parser for it. Per spec §14 stop condition "legal/source data required for the original scenario is unavailable", this is the evidence-backed blocker. |

### Decision

**Path D — `MISSION_1_SCENARIO_IMPORTABLE = BLOCKED`.**

The `ww_mvp` modpack remains a **clearly-labelled hand-authored
reconstruction** of Mission 1. It is not, and cannot be in this
environment, an import of the original AoE2 DE scenario data.

### What unblocks Path C

For the blocker to be lifted, three preconditions must all hold:

1. **Source availability.** User supplies (a copy of) the DE2 install
   containing `resources/_common/dat/empires2_x2_p1.dat` AND
   `resources/_common/dat/campaigns/william_wallace_*.dat` (or
   equivalent), placed on this host in a known location. The
   `doc/media_convert.md` install paths are the natural targets.
2. **Format support.** Implementation of:
   - `openage/convert/processor/conversion/de2/campaign_subprocessor.py`
     for `campaigns_x_x.dat`;
   - `openage/convert/processor/conversion/de2/scenario_subprocessor.py`
     for per-mission scenario files;
   - corresponding nyan objects under `engine.util.trigger.*` (Trigger,
     Condition variants, Effect variants — at least the candidates listed
     in spec §7);
   - C++ `libopenage/gamestate/trigger/*` runtime with dependency-driven
     reevaluation (spec §6.5).
3. **Asset provenance guardrails.** Re-running the converter under
   `OPENAGE_CONVERTER_BACKEND=lazy` (per spec §4.2 + P0-F-2 finding in
   `baseline-reality-audit.md`).

Path C is explicitly **out of scope** for this MVP per spec §5.2:
"Mission 2–5 … Complete AoE2 scenario/campaign format support beyond
fields required by Mission 1". Mission 1 is the **only** mission we
are committing to import; all other missions stay out.

### Why this is not a silent fall-back

Per spec §11.1: "If original data cannot legally or technically be
parsed in the current environment, the result must be `BLOCKED` with
evidence; it must not be silently replaced by a hand-authored layout."

- The mission file header explicitly says `HAND-WRITTEN MVP FIXTURE - NOT
  A CONVERTER PRODUCT`.
- Runtime log says `Scenario kind: William Wallace Mission 1 MVP
  reconstruction` (verified in
  `logs/p0_baseline/headless_load.log` and the demo 7/8 logs).
- This report records the blocker with evidence (this section + P1.1
  inventory + P1.2 path matrix above).
- No spec-violating claim of "imported original Mission 1" is made.

---

## P1.3 — Feasibility dump

Per spec §6.1 / plan §P1.3, a feasibility dump is required. Because the
source is unavailable, the dump describes the **reconstruction** used
in production today, and labels every entry as either
`source: hand-author` or `source: import-blocked`.

### Required artifacts

Plan §P1.3 names the path `build/mission1-scenario-dump/...` but
`/build` is gitignored at the repo root (see `.gitignore`), so the
artifacts are placed under
`docs/mvp/reports/mission1-scenario-dump/` to keep them tracked AND
adjacent to the feasibility report. The format and schema match the
plan's spec.

- `docs/mvp/reports/mission1-scenario-dump/scenario.json` —
  `ScenarioDefinition` per spec §6.1 (id, MapDefinition,
  players, EntityPlacements, AreaDefinitions,
  TriggerDefinitions, VictoryDefinition).
- `docs/mvp/reports/mission1-scenario-dump/object_table.csv` — one
  row per `EntityPlacement` with stable `scenario_object_id`,
  `entity_fqon`, `player_id`, `pos_x`, `pos_y`, `rotation`,
  `source`.
- `docs/mvp/reports/mission1-scenario-dump/trigger_graph.json` —
  trigger/condition/effect histograms + node/edge counts (currently
  empty: zero triggers in the reconstruction).
- `docs/mvp/reports/mission1-scenario-dump/coverage.json` —
  per-plan "only implementation list the later phases may consume".
  Contains `trigger_types_present`, `condition_types_present`,
  `effect_types_present`, plus the candidate list from spec §7 for
  later phases to narrow against.

These are produced by hand from the canonical `mission_01.nyan` content;
P2 will regenerate them automatically once the IR loader exists, and
P1.4 will pin a golden diff against this hand-written baseline.

### Coverage report

```json
{
  "scenario_kind": "William Wallace Mission 1 MVP reconstruction",
  "scenario_fqon": "ww_mvp.campaign.william_wallace.mission_01.Mission",
  "player_setup_fqon": "ww_mvp.campaign.william_wallace.mission_01.PlayerSetup",
  "source_status": "BLOCKED — no DE2 install on host; no SCX/SCN/campaign parser in repo",
  "is_imported_original": false,
  "hand_authored_fields": [
    "MapDefinition.size (16x16)",
    "MapDefinition.terrain_ids (256 grass tiles, all TestsceneGrass)",
    "MapDefinition.elevation (all 0)",
    "EntityPlacement[PlayerVillager @ (8,8) → Villager]",
    "EntityPlacement[PlayerTownCenter @ (8,9) → TownCenter]",
    "EntityPlacement[EnemyVillager @ (1,1) → Villager]",
    "PlayerSetup (MissionWood = 0)",
    "VictoryDefinition (event-driven via ObjectiveController)"
  ],
  "unsupported_or_missing": [
    "Original AoE2 DE William Wallace Mission 1 scenario data",
    "campaigns_x_x.dat parser (does not exist in repo)",
    "Per-mission scenario file parser (does not exist in repo)",
    "TriggerDefinition instances (no trigger language in nyan)",
    "ConditionDefinition / EffectDefinition variants (no runtime)",
    "DiplomacyDefinition matrix (hand-reconstruction has no enemy players)",
    "MapDefinition.terrain_ids variety (all tiles identical)",
    "Elevation (all 0; source likely non-trivial)",
    "AreasDefinition (none defined)"
  ]
}
```

This `coverage.json` is the **only** implementation list the later phases
may consume (per plan §P4.1). It currently contains zero trigger types
because the reconstruction has none. P4 must add the openage-native
trigger language before Mission 1 can claim any victory semantics
beyond "kill the enemy villager" (which is handled in C++ by
`objective_controller.cpp`, not by triggers).

---

## P1.4 — Extraction tests

Added as a pytest module under `openage/scenario/tests/test_p1_dump.py`
(17 tests, all passing). The module imports a shared dump-loader
(`openage/scenario/dumps.py`) and validates:

| Test | Spec §10.1 invariant |
|---|---|
| `test_map_dimensions_are_integers` | "terrain dimension and dense-array validation" |
| `test_map_dimensions_match_terrain_count` | "width/height mismatch" detection |
| `test_placement_ids_unique_and_nonempty` | "scenario-object mapping stability" |
| `test_placement_count_matches_csv` | "placement count and stable `ScenarioObjectId -> GameEntityId` mapping" |
| `test_required_placements_present` | Mission 1 specific invariant |
| `test_player_villager_and_town_center_owned_by_player_zero` | "player-unit removal does not falsely satisfy enemy objective" |
| `test_enemy_villager_is_a_distinct_owner` | same |
| `test_scenario_kind_is_reconstruction` | spec §3.2 honest labelling |
| `test_source_status_is_blocked` | spec §11.1 blocker-reporting rule |
| `test_trigger_graph_is_empty` | "scenario-object mapping stability" |
| `test_coverage_trigger_types_present_empty` | "unsupported variants fail/report clearly" |
| `test_coverage_candidate_trigger_types_match_spec_section_7` | pins the P4 candidate list to spec §7 |
| `test_loader_rejects_missing_file` | "no silent data loss" |
| `test_loader_rejects_csv_id_mismatch` | "unsupported variants fail/report clearly" |
| `test_loader_rejects_trigger_count_drift` | "no silent data loss" |
| `test_loader_rejects_non_object_root` | "no silent data loss" |
| `test_unknown_trigger_type_is_reported_not_silently_accepted` | "unsupported variants fail/report clearly" |

Run with:

```bash
.venv/bin/pytest openage/scenario/tests/test_p1_dump.py -v
```

Result on this commit: **17 passed in 0.04s**.

---

## P1 exit status

```text
MISSION_1_SCENARIO_IMPORTABLE: BLOCKED
  - reason: Path D (source unavailable in this environment + no parser
            exists in the repo even if source were available)
  - evidence: this report (P1.1 inventory + P1.2 path matrix)
  - reconstruction in use: ww_mvp.campaign.william_wallace.mission_01.Mission
                           labelled 'William Wallace Mission 1 MVP
                           reconstruction' in runtime logs
  - to unblock: see "What unblocks Path C" above (3 preconditions)
```

This satisfies the P1 plan exit criterion "either `MISSION_1_SCENARIO_
IMPORTABLE` candidate exists or a documented, evidence-backed blocker
exists" — we have the documented blocker.

Atomic commit boundary for P1: this report is the single artifact
committed at the end of P1. Suggested commit:
`docs: record Mission 1 scenario feasibility blocker (Path D)`.