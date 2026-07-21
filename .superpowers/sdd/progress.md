# Streaming Convert - Progress Ledger

## 2026-07-21 - Pivot to lazy ValueMember (approved)

`schema_probe.py` is a working 30-MiB streaming walker (1.85M BfG instances in
18.7s). Lifecycle audit: OOM is read-phase ValueMember materialization
(`dynamic_load=False`); consumer code (~13 emitter files) is deeply coupled to
the ValueMember API incl. recursive `.diff()`. Per-line release is unsafe
(dead/projectile/transform units cross-referenced across categories). Dat
already stores `Civ.unit_offsets` (per-civ unit file offsets, unused for seeking).

**Direction:** keep ValueMember API + all consumer files unchanged; replace the
build/storage layer with mmap raw + offset tables + lazy-build-on-access + LRU.
Revised plan: docs/superpowers/plans/2026-07-21-lazy-convert.md.

## Task status
- [done 2026-07-21] T1 GenieSource + offset tables (value_object/read/source.py)
  - VALIDATED: all counts match schema_probe (unit_headers 1701, civs 54,
    sounds 760, techs 1268, terrains 200, effect_bundles 1218, terrain_
    restrictions 50, player_colors 16, connections 4/36/240/211); base units
    2319, civ-unit overrides 100435. Rebuilt unit id0=0 (91 ValueMembers) +
    graphic idx=0 (13) from recorded offsets via existing read() = byte-accurate.
- [done 2026-07-21] T2 DynamicLoader rewrite (cache + full dict protocol)
  - New API: `DynamicLoader(offset, cls, args, raw, gv, extra=None)`; `_build()`
    caches the member dict; `evict()` clears cache (preserves `extra`); full
    mapping protocol (`__getitem__`/keys/values/items/get/update/__contains__).
    `update()` writes to `extra` (survives evict) - the graft path for
    extractors adding `unit_commands` etc.
  - VALIDATED: (T2.1) lazy structurally == eager for unit 150 (107 members,
    deep-checked); (T2.2) dict protocol + first-access build; (T2.3) evict frees
    + rebuild identical; (T2.4) `add_member`/`update` extras survive evict;
    (T2.5) ConverterObject(DynamicLoader) get_member/has_member/self-diff OK;
    (T2.6) realistic base-vs-civ-override diff -> non-trivial changes
    (`garrison_capacity`, `unit_commands`) flow through DynamicLoader.
- [done 2026-07-21] T3 Lazy stores + extraction rewrite (lazy_extract.py)
  - New module `processor/conversion/lazy_extract.py`: `extract_*_lazy(src,
    full_data_set)` for every category, mirroring the DE2/AoC legacy extract
    but driving from `GenieSource` offset tables via `DynamicLoader` +
    `post_build` hooks (attacks/armors->container, unit_commands graft,
    civ units->lazy container, effects pop). Stores stay plain dicts of
    lightweight `GenieObject(members=DynamicLoader)` (no build at populate).
  - `_LazyUnitContainer(Mapping)`: civ `units` member built on first
    `civ["units"][id0]` access from `src.civ_units` (was the 124k-override
    OOM sink via eager `get_container`). `ContainerMember` now accepts any
    `Mapping` (value_members.py - dict already is one, so legacy untouched).
  - VALIDATED: all store counts match probe (units 2319, techs 1268,
    effect_bundles 1218, civs 54, graphics 13709, sounds 760, terrains 200,
    restrictions 50, connections 4/36/240/211); attacks/armors transform and
    unit_commands graft byte-match legacy eager; civ override-by-id matches
    eager; effect counts match; effects popped from bundle members. Full lazy
    extract: **RSS 219 MB / 55 MB Python heap** (legacy read phase OOM'd at
    6-10 GB).
- [done 2026-07-21] T4 backend switch OPENAGE_CONVERTER_BACKEND + DE2 wiring
  - `get_converter_backend()` in gamedata.py reads env (default ``legacy``).
    `get_gamespec` returns a `GenieSource` (via new `from_compressed`) when
    ``lazy``; `GenieSource.from_dat(dat_path, game_version=None)` and
    `from_compressed(compressed, game_version, name)` added. DE2
    `_pre_processor` branches on `isinstance(gamespec, GenieSource)` -> calls
    `extract_*_lazy` (+ `sanitize_effect_bundles`); legacy path unchanged.
    Driver's SWGB `blend_mode_count` access guarded for lazy. VALIDATED: DE2
    `_pre_processor` lazy branch populates all stores with correct counts;
    imports clean; default backend = legacy.
- [done 2026-07-21] T5 cache mgmt (global LRU on lazy builds)
  - Module-level `_LRU` (OrderedDict) in dynamic_loader.py caps how many built
    DynamicLoaders are alive at once (`OPENAGE_LAZY_CACHE_SIZE`, default 2048);
    `_build()` registers, `_enforce_lru_cap()` evicts oldest (FIFO, cache
    dropped directly to avoid re-entrancy). Civs are `pinned=True` (never
    evicted) since evicting forces re-reading ~2300 unit overrides.
    `lru_stats()`/`lru_clear()` for instrumentation. VALIDATED: cap=32 forces
    eviction + correct rebuilds; accessing all 2319 units (would pin everything
    w/o LRU) stays at 2048 built / 221 MB RSS.
- [analysis 2026-07-21] T6 boundedness (code-level) - bounded by design; empirical run pending
  - Code-level trace of the lazy pipeline confirms the LRU bounds
    processing/export, not just extract:
    * DE2 `_pre_processor` lazy branch calls `extract_*_lazy` (un-built
      DynamicLoaders) and returns; it does NOT call the legacy
      `extract_genie_units` (which would `.value` all units = pin-all). Good.
    * `_processor` (`create_unit_lines` etc., aoc/processor.py:513) stores
      `GenieUnitObject` refs in unit_lines but only force-builds the
      transform-unit minority via `unit["transform_unit_id"].value`; line
      heads/upgrades otherwise touch connection members, not unit members.
    * `_post_processor` nyan creation (aoc/nyan_subprocessor.py,
      entity_object/conversion/aoc/genie_unit.py:215/233/257) reads every
      member via `.value` (primitives/transients) - `attacks`/`armors`/
      `unit_commands` are per-call locals, NOT stored on nyan objects. Nyan
      objects hold copied primitives + nyan-API refs + ID-based ForwardRefs,
      never genie member refs. So built genie DynamicLoaders are evictable
      once their unit_line's nyan members are emitted; the LRU caps the
      simultaneously-built count (matches T5's empirical "all 2319 units ->
      2048 built / 221 MB").
  - Conclusion: extract proven 219 MiB; processing/export bounded by
    design via the `.value`-extraction + LRU pattern. The residual risk is
    an untraced nyan member path that holds a genie ref; the budget_gate
    stage trajectory (`after nyan conversion` vs `after metadata read`) is
    the empirical tripwire - flat = bounded, spike = pin-all.
  - PENDING: the actual `--memory-budget-test` run (deferred per the
    no-full-convert-here constraint) to write docs/superpowers/reports/
    phase1.txt and confirm RSS < 500 MiB. Golden nyan diff needs a legacy
    run on a RAM-rich box (legacy OOMs locally).
- [done 2026-07-21] T7 budget_gate tooling + `--memory-budget-test` CLI
  - New module `processor/../tool/budget_gate.py`: `BudgetGate` samples RSS
    every 0.25s in a daemon thread (catches transients between stage marks),
    `mark(stage)` records RSS + bumps peak at known boundaries,
    `finish(notes=)` writes `docs/superpowers/reports/phase1.txt` with peak
    RSS, P2(<500)/P3(<300) pass/fail, stage trajectory, and lazy-LRU stats.
    Report path resolved against repo root (4 dirnames up) so it lands in
    docs/ regardless of caller CWD. `--memory-budget-test` flag added to
    `convert/main.py init_subparser`; `convert_assets` forces
    `OPENAGE_CONVERTER_BACKEND=lazy`, installs the gate on args, starts it,
    and calls `finish()` after `convert(args)`. `convert_metadata` marks
    `convert-metadata-start`, `after metadata read (gamespec)`,
    `after nyan conversion`, per-modpack `after export of <name>`,
    `after all exports`. Listed in `convert/tool/CMakeLists.txt`.
    VALIDATED: dry-run unit test (start/mark/finish) writes a well-formed
    report; argparse recognises the flag (default False); driver+main import
    clean. The actual phase1.txt is produced by running the full convert
    with the flag (deferred per the no-full-convert-here constraint).

- [done 2026-07-21] T6 acceptance: P2 PASS (458.8 MiB < 500), 717/718 nyan byte-identical to legacy
  - **RSS < 500 MiB: PASS.** Full lazy convert (BfG DE2, --no-media) with default
    OPENAGE_LAZY_CACHE_SIZE=256: peak 458.8 MiB, vs legacy read-phase OOM at
    6-10 GB. Lowered default LRU cap 2048 -> 256 in dynamic_loader.py (cap=2048
    produced 536 MiB, failing P2; 128/256 both yield 458 MiB and byte-identical
    output). Report at docs/superpowers/reports/phase1.txt.
  - **Golden nyan diff: 717/718 byte-identical to legacy.** Single diff in
    gaia.nyan: lazy emits game_setup=o{} while legacy emits 21 WesternEuropean
    AnimationWrapper entries. Root cause: gaia (civs[0]) game_setup patches not
    produced under lazy path. Tracked for follow-up; exit 0, 0 dangling refs.
  - **Fixes to get lazy pipeline running end-to-end (T6 pass fixes):**
    1. BfG detection: gamedata.py read compressed bytes (zlib header) and never
       matched "VER 8.4", so update_number="BfG" was never applied -> wrong
       struct layouts and bogus -1 tags. Moved detection into
       GenieSource.from_compressed (post-decompress).
    2. Connection keys: extract_connections_lazy used array index, consumers
       look up by the connection's own id field. Changed to key by
       dl["id"].value (build the small connection struct).
    3. unit_type injection: base units and civ-unit overrides lacked unit_type
       (array-level tag, not a struct field). Added tag to RecordHandle, capture
       in _maybe_record, inject via DynamicLoader(extra={"unit_type": IDMember}).
    4. _unit_post_build read built["unit_type"] but post_build runs before the
       extra merge; switched to loader.extra["unit_type"].value.
    5. _maybe_record (generic path) dropped passed_args; TerrainRestriction
       needs terrain_count in cls(**args). Captured _passed_args on
       base_child_values, extracted in _maybe_record into RecordHandle.args.
