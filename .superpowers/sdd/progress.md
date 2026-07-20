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
- [pending] T6 acceptance: RSS < 500MB + golden nyan diff
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
