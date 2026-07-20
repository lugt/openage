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
- [pending] T3 Lazy stores + extraction rewrite (no .value materialization)
- [pending] T4 backend switch OPENAGE_CONVERTER_BACKEND + DE2 wiring
- [pending] T5 cache mgmt (LRU) + measure peak RSS
- [pending] T6 acceptance: RSS < 500MB + golden nyan diff
- [pending] T7 budget_gate + report -> docs/superpowers/reports/phase1.txt
