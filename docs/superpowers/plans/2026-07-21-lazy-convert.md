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

## T6 acceptance runbook (2026-07-21)

Two acceptance halves: (1) peak RSS < 500 MiB, (2) lazy nyan output
byte-identical to a golden. Both halves need a full convert run; the
golden additionally needs a legacy run, which OOMs on this box, so the
golden is generated on a RAM-rich machine.

### (1) Peak RSS — measured locally via the budget gate

```
OPENAGE_CONVERTER_BACKEND=lazy openage convert \
    --memory-budget-test --source-dir <DE2 DAT DIR> \
    --output-dir <OUT> --no-media --no-prompts
```

`--memory-budget-test` forces the lazy backend, samples RSS every 0.25 s
in a daemon thread (catches the true peak between stage marks), records
RSS at `convert-metadata-start`, `after metadata read (gamespec)`,
`after nyan conversion`, per-modpack `after export of <name>`, and
`after all exports`, and writes `docs/superpowers/reports/phase1.txt`
with P2 (<500 MiB) / P3 (<300 MiB) verdicts. Drop `--no-media` for the
full run (media export is P3 stretch and the bigger heap driver).

Already proven without a full run (T3): the lazy EXTRACT phase runs at
**219 MiB RSS / 55 MiB Python heap** with all stores populated and counts
matching the probe (units 2319, techs 1268, civs 54, graphics 13709,
…). The open question is whether gather+emit+write stay bounded on top
of that — see the pin-all analysis before running the full pipeline.

**Pin-all analysis (code-level, 2026-07-21): bounded by design.** Tracing
the lazy pipeline: (1) DE2 `_pre_processor` lazy branch calls
`extract_*_lazy` (un-built DynamicLoaders) and returns - it does NOT call
the legacy `extract_genie_units` that would `.value` all units. (2)
`_processor`/`create_unit_lines` stores `GenieUnitObject` refs in
unit_lines but only force-builds the transform-unit minority. (3) nyan
creation reads every member via `.value` (primitives/transients) -
`attacks`/`armors`/`unit_commands` are per-call locals, not stored on
nyan objects; nyan objects hold copied primitives + nyan-API refs +
ID-based ForwardRefs, never genie member refs. So built genie loaders are
evictable once their unit_line is emitted, and the LRU caps the
simultaneously-built count (matches T5's "all 2319 units -> 2048 built /
221 MiB"). Residual risk: an untraced nyan path holding a genie ref; the
budget_gate stage trajectory (`after nyan conversion` vs `after metadata
read`) is the empirical tripwire - flat = bounded, spike = pin-all.

### (2) Golden nyan diff — needs a RAM-rich box for legacy

```
# on a machine with >= 16 GB RAM (legacy eager materialises ~1.85 M
# ValueMembers and peaks 6-10 GB):
OPENAGE_CONVERTER_BACKEND=legacy openage convert \
    --source-dir <DE2 DAT DIR> --output-dir /tmp/golden --no-media
OPENAGE_CONVERTER_BACKEND=lazy   openage convert \
    --source-dir <DE2 DAT DIR> --output-dir /tmp/lazy   --no-media
diff -r /tmp/golden /tmp/lazy          # expect: identical
```

No pre-existing golden is committed: `assets/converted/engine/` is the
API definition (157 nyan files, .dat-independent, unaffected by this
refactor) and `assets/test/converted/de2_base/` is an empty skeleton.

Correctness is already covered at the extract layer (T3): units,
attacks/armors transforms, unit_commands graft, civ override-by-id, and
effect counts all byte-match legacy eager extraction. The full-pipeline
golden diff closes the gap end-to-end (gather → emit → write) and is the
one check that needs the legacy run.

