# Streaming Convert — Revised Plan: Lazy ValueMember (2026-07-21)

Supersedes the slots-only-record plan in `2026-07-20-streaming-convert.md`.
The pivot was approved after `schema_probe.py` proved a 30-MiB structural walk
and a lifecycle audit showed the consumer code is deeply coupled to the
ValueMember API (incl. recursive `.diff()`).

## Goal
Cut DE2 convert peak RSS from 6 GB+ OOM to **< 500 MB** (P2 acceptance).
P3 (< 300 MB) is stretch.

## Root cause (confirmed)
`load_gamespec` calls `EmpiresDatWrapper.read(file_data, 0, gv, dynamic_load=False)`
which **materializes all 1.85M ValueMember instances upfront** (the probe walked
them in 30 MiB; the real reader builds a Python object per instance). Extract
then wraps the already-materialized dicts by reference. DE2 never calls
`_release_genie_data`, so the tree lives for the whole run.

## Architecture
Keep the ValueMember API and all ~13 consumer files **unchanged**. Replace only
the build/storage layer:

```
empires2_x2_p1.dat
  └─ zlib decompress ──> raw (mmap'd tmpfile, OS-managed pages)
       └─ GenieSource pre-pass (probe-style walk, no ValueMembers built)
            └─ offset tables per category: id -> (byte_offset, struct_cls)
                 └─ LazyDict stores: genie_units[id] builds the ValueMember
                    subtree on first access via struct_cls.read(raw, offset, gv);
                    cached; LRU-bounded; released after _process_game_entities.
```

### Why this is safe
- `GenieStructure.read(raw, offset, gv)` is **self-contained per struct**: dynamic
  lengths resolve from the struct's own already-read fields; `passed_args` /
  `offset_to` resolve within the single read. Verified from `genie_structure.py`.
- `dataset.genie_units[id]` returns a `GenieUnitObject` whose `members` build
  lazily — `obj["m"].value` and `a.diff(b)` work unchanged (both units alive in
  the same line's scope).
- Per-line release is **not** safe (dead/projectile/transform units have no
  line of their own and are cross-referenced). Memory is bounded by **lazy build
  + LRU**, not eager release.
- The dat already stores `Civ.unit_offsets` (int32 per-civ unit file offsets);
  the reader ignores them for seeking (sequential read + `TODO` comment). We
  leverage them for per-civ-unit lazy access.

## Invariants
1. No genie record's ValueMember subtree exists in the Python heap except the
   ones currently cached (LRU-bounded) or in an active `with`/line scope.
2. Persistent indices hold handles (`id -> byte_offset`), never Python object
   references.
3. `raw` is mmap'd (OS page cache), not held as a Python `bytes` in the heap.
4. Consumer code (`obj["m"].value`, `.diff()`, `__getitem__`) is unchanged.

## Tasks
- **T1 — GenieSource + offset tables.** `value_object/read/source.py`. Probe-
  style walker (mirrors the proven `schema_probe.Probe`) recording, per category,
  `id -> (offset, struct_cls)`: base units (civs[0].units, key=id0), unit_headers,
  graphics, sounds, techs/researches, terrains, effect_bundles, terrain_
  restrictions, player_colors, civs, per-civ-unit overrides ((civ_idx, id0)),
  connections. mmap raw. Validate: counts == probe counts.
- **T2 — LazyMember (fix DynamicLoader).** `value_object/read/dynamic_loader.py`.
  Build-on-first-access + **cache** (not unload-per-access bug). Add dict
  protocol (`keys`/`values`/`items`/`__contains__`) from the struct schema
  (cheap, no materialization). Reuse for all categories.
- **T3 — Lazy stores + extraction rewrite.** `genie_units` etc. become
  `LazyDict`s; `extract_*` populate from offset tables (no `.value` calls).
  `genie_units[id]` -> `GenieUnitObject` with LazyMember members. Cross-refs
  resolve through the same lazy store.
- **T4 — Backend switch + DE2 wiring.** `OPENAGE_CONVERTER_BACKEND=legacy|lazy`
  in `load_gamespec`/`driver.py`; default `legacy` until T6 passes.
- **T5 — Cache mgmt + measure.** LRU on lazy builds; release after
  `_process_game_entities`. Measure peak RSS; tune LRU size.
- **T6 — Acceptance.** DE2 convert under lazy backend: RSS < 500 MB; nyan
  output byte-identical to a golden run (`golden_diff`).
- **T7 — budget_gate + report.** `--memory-budget-test` + `budget_gate.py`;
  report to `docs/superpowers/reports/phase1.txt`.

## Non-goals (deferred)
- Retiring ValueMember entirely (was the old plan's mechanism, not a requirement).
- Media (sprite/sound file) streaming (P3 stretch).
