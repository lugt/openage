# de2_base Full-Load Correctness and Performance — Design Spec

- Date: 2026-07-24
- Branch: `mvp/tutorial-demo`
- Owner: openage MVP tutorial
- Status: Draft for review
- Supersedes: any prior tutorial_assets modpack sketch
- Related memories: [[mvp-direction-pivot]] [[phase05f-startup-no-converter]] [[phase05e-real-villager-fqon]] [[de2-base-asset-audit]]

## 1. Background and Motivation

`bin/run --tutorial --modpacks de2_base` currently takes 5–9 minutes to reach
`load_scenario`. Profiling (commit `fd847725`) showed that ~656 `.nyan` files
load in the first 60 s — extrapolated to 5918 files ≈ 9 minutes. The same
load **crashes** at `de2_base/data/civ/slavs/slavs.nyan:813` with
`unknown name: 'ArsonResearchable'`, a converter-output bug, not a loader
bug.

Two problems are entangled:

1. **Correctness** — converter produces nyan referencing absent FQONs.
2. **Performance** — `Game::load_data` → `Database::load` per-file recursion
   and triple AST walk produces O(N²) work.

The user has forbidden both: do **not** silently rebuild on `bin/run game`,
do **not** fall back to a hand-rolled tutorial_assets modpack. The fix is a
**general compiled cache** for nyan, lazy materialization, and an offline
validator to enumerate every converter bug in one pass.

## 2. Non-Goals

- No new gameplay features (Attack/Gather/Build/Train remain out of scope).
- No mmap of existing C++ AST/Object/ObjectState memory layout.
- No msgpack / SQLite / capnproto. Hand-rolled packed POD only.
- No full-converter rebuild to fix `slavs.nyan`. Fix at the converter source,
  not in generated `.nyan`.
- No tutorial-specific asset package. The cache serves any modpack.

## 3. Acceptance Criteria

| ID | Criterion | Gate |
|----|-----------|------|
| AC-1 | `bin/run validate-nyan --modpacks de2_base` exits 0 after all converter bugs are fixed | Required for AC-2 |
| AC-2 | `bin/run prepare-nyan-cache --modpacks de2_base` produces `<asset-dir>/converted/.nyancache/<modpack>.cache` only after a successful validate pass | Required |
| AC-3 | `bin/run game --tutorial --modpacks de2_base --no-prompts` reaches `load_scenario` in ≤ 10 s cold / ≤ 5 s warm | Required |
| AC-4 | Warm startup reads no `.nyan` text; mmap only | Required |
| AC-5 | Each `.nyan` source file is parsed at most once across full load | Required |
| AC-6 | Cache invalidates on: any source hash change, manifest change, converter version change, engine nyan schema version change, cache format version change | Required |
| AC-7 | `bin/run game` with missing or invalid cache exits non-zero with `run prepare-nyan-cache --modpacks <x>` hint, does NOT silently rebuild | Required |
| AC-8 | Mission 1 lazily materializes Villager + TownCenter; unused civs/research/naval never call `create_obj_state` | Required |
| AC-9 | No `tutorial_assets/` modpack directory or FQON rewrite exists in working tree | Required |

## 4. Architecture

### 4.1 Two store backends behind one `NyanStore` interface

```
                 ┌──────────────────────┐
                 │ Game::load_data      │ (unchanged signature)
                 │ Database/View/Trans- │
                 │ action C++ API       │ (unchanged surface)
                 └──────────┬───────────┘
                            │ uses
                            ▼
                 ┌──────────────────────┐
                 │     NyanStore        │  ← new abstract base
                 └──────────┬───────────┘
                            │
            ┌───────────────┼────────────────┐
            ▼               ▼                ▼
      TextNyanStore    MmapNyanStore    OverlayNyanStore
      (existing        (new, reads       (in-memory, for
      Database/load    .nyancache)       player civ patches
      via File +                         + Transactions)
      Parser)
```

`NyanStore` is **not** a wholesale replacement of `Database`. It is an
adapter layer that owns the read paths. `Database`, `View`,
`Transaction`, `Object`, `ObjectState`, `Member`, `Value*` keep their
existing surface; the loader modifies which `State::objects` map and
`MetaInfo` get populated.

### 4.2 Read-path mapping

| Existing API                    | TextNyanStore                       | MmapNyanStore                        |
|---------------------------------|--------------------------------------|--------------------------------------|
| `Database::load(file, fetcher)` | call existing impl                   | no-op (cache already holds it)       |
| `Database::new_view()`          | unchanged                            | unchanged                            |
| `View::get_object(fqon)`        | resolves from MetaInfo + State       | resolves from packed index → mmapped ObjectState |
| `View::get_raw(fqon)`           | existing                             | returns ptr into mmapped region      |
| `View::new_transaction(t)`      | unchanged (writes hit `Database::state`) | writes go to `OverlayNyanStore` (mmap region stays read-only) |

`OverlayNyanStore` is always constructed alongside `MmapNyanStore`. It
chains: overlay first, fall back to mmap base. `Transaction::commit`
appends new `ObjectState`s into the overlay's map; the mmap base is
never written. The overlay exists whether or not any transaction
currently has uncommitted state — it's the canonical "current view"
fronting the immutable cache.

### 4.3 Loader flow (game startup)

```
bin/run game --tutorial --modpacks de2_base
  │
  ├─ ModManager loads manifest(s) → modpack list
  ├─ For each modpack:
  │   └─ open <modpack>.cache if present and metadata valid
  │       ├─ valid → MmapNyanStore populates MetaInfo + State from cache
  │       └─ missing/invalid → fail-fast with "run prepare-nyan-cache" hint
  │
  ├─ Database constructed with NyanStore registry
  ├─ Game::load_data: now just verifies the cache covers all modules in
  │   the manifest (cheap; no parse, no walk)
  │
  ├─ Game::load_scenario(<mission fqon>)
  │   ├─ View::get_object(<mission fqon>) → triggers overlay-on-mmap resolve
  │   ├─ For each PlacedEntity.entity → View::get_object(entity_fqon)
  │   │   triggers MmapNyanStore::materialize_object(fqon)
  │   │   which walks ObjectState.inheritance ONCE, caches result
  │   └─ TerrainChunkLayout.tile_fqons → same lazy materialization
  │
  └─ Universe::attach_renderer (unchanged)
```

### 4.4 Prepare flow (offline)

```
bin/run prepare-nyan-cache --modpacks de2_base
  │
  ├─ 1. Build full dependency closure (engine + de2_base modules)
  ├─ 2. Run existing TextNyanStore::Database::load for ALL files in one pass
  │       (NOT per-file recursion; see §6.1 batched loader)
  ├─ 3. Run validate-nyan logic; collect every error
  │       ├─ any error → write report, exit non-zero, NO cache file written
  │       └─ all clean → continue
  ├─ 4. Walk State::objects + MetaInfo, serialize via CacheBuilder to
  │       packed POD records (see §5)
  ├─ 5. Atomic write: write to <modpack>.cache.tmp, fsync, rename
  └─ 6. Print summary: objects=N, members=M, bytes=K, build_time=Ts
```

## 5. Cache File Format — `*.nyancache`

### 5.1 Header (page-aligned, 4096 bytes)

```
offset  size   field
0       4      magic = 'NYNC'                (0x4E594E43 LE)
4       4      format_version = 0x00010000   (1.0)
8       8      created_unix_ms
16      8      converter_version             (string-table offset; 0 = N/A)
24      8      engine_schema_version         (string-table offset)
32      32     source_merkle_root            (BLAKE3 of sorted source hashes)
64      8      total_objects
72      8      total_members
80      8      total_values
88      8      section_directory_offset
96      8      section_directory_size
104     8      string_table_offset
112     8      string_table_size
120     4088   reserved (zero-padded)
4088    8      header_crc32c
```

All offsets are section-relative (relative to the section directory base,
not absolute), so the file can be remapped at any page-aligned base.

### 5.2 Sections

| Section          | Contents                                        | Sort key        |
|------------------|--------------------------------------------------|-----------------|
| `STRINGS`        | NUL-terminated UTF-8, packed contiguously       | insertion order |
| `MODULES`        | one `ModuleRecord` per `.nyan` file              | module id       |
| `FQON_INDEX`     | one `FqonEntry` per object, sorted by fqon hash  | fqon hash (64b) |
| `FQON_PROBE`     | open-addressing probe table for FQON hash → FqonEntry idx | slot |
| `DEPS`           | import graph: `DepRecord` per (importer, importee) edge | importer, importee |
| `TYPES`          | `TypeRecord` per unique type definition          | type id         |
| `OBJECTS`        | `ObjectRecord` per object                        | object id       |
| `MEMBERS`        | `MemberRecord` per (object, member name)        | object id, name hash |
| `VALUES`         | tagged `ValueRecord` union                       | (stream of bytes, see §5.4) |
| `DIAG`           | optional source-location diagnostics             | object id       |

A `SectionDirectory` lists each section's `{name_hash, offset, size,
crc32c}`. The header references this directory, so new sections can be
added without breaking older readers (format version bumps).

### 5.3 Record layout (packed POD)

```c++
#pragma pack(push, 1)

struct ModuleRecord {
    uint32_t path_str;        // STRINGS offset
    uint32_t path_len;
    uint64_t source_sha256;   // file content hash (BLAKE3 today; field
                              //   named sha256 to leave room)
    uint32_t parent_mod;      // module id; UINT32_MAX for root
    uint32_t first_obj;       // OBJECTS index
    uint32_t obj_count;
};

struct FqonEntry {
    uint64_t fqon_hash;       // 64-bit hash of full fqon string
    uint32_t str_offset;      // STRINGS offset to original fqon text
    uint32_t object_idx;      // index into OBJECTS
};

struct ObjectRecord {
    uint32_t fqon_str;        // STRINGS offset
    uint16_t fqon_len;
    uint16_t parent_count;
    uint32_t parent_idx[8];   // OBJECTS indices (overflow → DEPS edge)
    uint32_t first_member;    // MEMBERS index
    uint16_t member_count;
    uint16_t type_idx;        // TYPES, UINT16_MAX if untyped
    uint32_t diag_idx;        // DIAG, UINT32_MAX if none
};

struct MemberRecord {
    uint32_t name_str;        // STRINGS offset
    uint16_t name_len;
    uint16_t type_idx;        // TYPES, UINT16_MAX if Any
    uint32_t first_value;     // VALUES byte offset
    uint32_t value_count;     // for set/orderedset
    uint8_t  modifier_mask;   // bit 0 = Abstract, etc. (TBD)
    uint8_t  flags;           // bit 0 = has_default, 1 = inherited
    uint16_t _pad;
};

struct ValueRecord {
    uint8_t  tag;             // 0=Int, 1=Float, 2=Text, 3=Object,
                              // 4=Set, 5=OrderedSet, 6=Dict, 7=File,
                              // 8=None
    uint8_t  flags;           // bit 0 = has_override
    uint16_t _pad;
    uint32_t payload;         // tag-specific (see §5.4)
};

#pragma pack(pop)
```

### 5.4 Value encoding

| Tag    | Encoding                                              |
|--------|--------------------------------------------------------|
| Int    | payload = STRINGS offset to ASCII; int64 in next 8 bytes |
| Float  | payload = STRINGS offset to ASCII; double in next 8 bytes |
| Text   | payload = STRINGS offset, len in next 4 bytes          |
| Object | payload = OBJECTS index                                 |
| None   | payload = 0                                             |
| File   | payload = STRINGS offset to relative path               |
| Set / OrderedSet | count in payload, followed by count × 4-byte index into appropriate container section |
| Dict   | not yet supported by openage gameplay; serialize as Text "unhandled" + DIAG warning |

`Int` and `Float` keep ASCII form in the string table so they survive
JSON diff round-trips and human inspection; runtime reuses ASCII → numeric
once at materialize time.

### 5.5 Hash strategy

- Source content hash: BLAKE3 (fast, no patent issues), 256-bit.
- FQON 64-bit hash: splitmix64 of `std::hash<std::string>(fqon)`.
- Probe table load factor ≤ 0.6; size rounded up to next power of two.
- All section CRCs: CRC32C (`__crc32c` on x86_64, software fallback
  elsewhere; not a security boundary, just integrity).

### 5.6 String table

- Section-relative offset zero is reserved as "no string".
- FQON strings are stored once even when shared across `ObjectRecord`
  and `DepRecord` (the fqon hash is the canonical key).

### 5.7 On-disk file layout (typical de2_base)

Estimated footprint once we have real numbers:
- header: 4 KiB
- section directory: ~256 B
- strings: ~6 MiB (de2_base has ≈ 5918 files, fqons, member names,
  text values)
- modules: ≈ 5918 × 32 B = 189 KiB
- fqon index + probe: ≤ 2 MiB
- objects: ≈ 20000 × ~64 B = 1.3 MiB
- members: ≈ 200000 × ~32 B = 6.4 MiB
- values: dominant for orderedset of FQONs; estimate 20 MiB
- deps: ≈ 30000 edges × 12 B = 360 KiB
- total: roughly 40 MiB, well within mmap cost.

## 6. Loader Modifications

### 6.1 Batched loader (first correctness+perf commit)

Today: `Game::load_path` calls `db->load(mod_dir + "/" + search,
fileload_func)` per `.nyan` file. Each call re-parses that file (already
parsed once when imported by another file), re-runs three AST walks, and
DFS-walks its imports.

New flow when building the cache:
- `Database::load_batch(file_list, filefetcher)` — single call, single
  import DFS shared across the whole list, three walks run once over the
  combined AST set.
- `Game::load_data` calls `load_batch` once per modpack.
- Existing `Database::load` is kept (it's still how incremental
  import-driven loads work during transaction commits); the batch path
  only short-circuits the top-level DFS duplication.

### 6.2 Counters added (commit `fd847725` already started)

Expose a `StartupCounters` accessor (`openage::gamestate::get_startup_stats`)
and extend:
- `parse_attempts_per_file[path]` (per-file map)
- `import_resolves_total`, `import_resolves_cached`
- `fqon_lookups_total`, `fqon_lookup_hits` (hits via index)
- `namespace_full_scans` (each `ast_obj_walk` pass counts one)
- `create_obj_state_calls`, `create_obj_state_unique`
- `object_state_rebuilds`

Print at end:
```
[PERF] load_done files_seen=N files_loaded=N
       parse_attempts_total=K parse_attempts_unique=K
       import_resolves_total=K import_resolves_cached=K
       fqon_lookups_total=K fqon_lookup_hits=K
       namespace_full_scans=K
       object_states_created=K object_states_unique=K
```

Assertion gates (CI tests):
- `parse_attempts_per_file[path] ≤ 1` for every path
- `create_obj_state_unique == total_objects`
- `namespace_full_scans ≤ 3` per top-level load (info → content → state)

### 6.3 Lazy materialization (post-cache)

`MmapNyanStore::get_object_state(fqon)`:
- first call → walk `ObjectRecord.parent_idx[]` chain through cache,
  merge inherited members into a fresh `ObjectState`, cache it under a
  `std::unordered_map<fqon, std::shared_ptr<ObjectState>>` keyed by FQON.
- subsequent calls → return cached pointer.

Critical invariant: the merged `ObjectState` is built once per FQON per
process. Mission 1 only materializes the small subtree reachable from its
FQON closure (Villager, TownCenter, their abilities, Grass terrain,
Size/Translation, etc.) — maybe 30–80 objects total instead of 5918.

### 6.4 Overlays for transactions

`OverlayNyanStore` keeps an `std::unordered_map<fqon, std::shared_ptr<ObjectState>>`
fronting the mmap base. `Transaction::commit` writes new states into the
overlay. `View::get_raw` consults overlay first.

## 7. Validator Subcommand

### 7.1 CLI

```
bin/run validate-nyan --modpacks de2_base [--json out.json]
                       [--continue-on-error]
                       [--strict]
```

- `--continue-on-error` (default true): never stop at first error. The
  whole point is to enumerate everything.
- `--strict`: treat warnings as errors (e.g. unused import aliases).
- `--json`: machine-readable report (see §7.3).

### 7.2 Error categories collected

| Category             | Source                                  |
|----------------------|------------------------------------------|
| `NameError`          | existing Nyan API                        |
| `MemberNotFoundError`| `Database::find_member`                   |
| `MemberTypeError`    | `Database::resolve_types`                |
| `MissingImport`      | `import X` where X is not in any namespace |
| `ManifestMissing`    | modpack manifest is absent                |
| `FqonConflict`       | `NamespaceFinder::check_object_conflict` |
| `DuplicateFile`      | same file path parsed twice               |
| `CycleInInheritance` | `linearize_new` reports cycle             |

Each error: `{category, file, line, column, object, missing_name,
import_chain}`. `import_chain` is reconstructed by walking
`DepRecord[obj]` edges back to the entry module.

### 7.3 JSON report schema

```json
{
  "version": 1,
  "modpacks": ["engine", "de2_base"],
  "duration_s": 7.31,
  "summary": {
    "files_seen": 5918, "files_loaded": 5918,
    "objects": 20000, "members": 200000,
    "errors": 1, "warnings": 0
  },
  "errors": [
    {
      "category": "NameError",
      "missing_name": "ArsonResearchable",
      "file": "data/civ/slavs/slavs.nyan",
      "line": 813, "column": 21,
      "object": "data.civ.slavs.slavs.Arson",
      "import_chain": [
        "data/civ/slavs/slavs.nyan:813",
        "data/game_entity/generic/bombard_tower/bombard_tower.nyan:42"
      ]
    }
  ]
}
```

### 7.4 Implementation notes

- Lives in `openage/validate_nyan/main.py` registered as
  `subparsers.add_parser("validate-nyan", ...)` in
  `openage/__main__.py`.
- Reuses `ModManager`, `Game::load_data` driving but **not** `Game`
  itself: validator constructs a `Database` directly, calls
  `Database::load_batch`, and wraps the existing nyan C++ error types
  with a try/catch that records and continues.
- `--json` writes the report; without it, prints human-readable summary
  with first 50 errors and a count of suppressed duplicates.
- Exit code: 0 if no errors, 1 if any error, 2 if `--strict` failed on
  warnings.

## 8. Converter Bug Fix Workflow

`bin/run validate-nyan --modpacks de2_base` must exit 0 before the cache
can be built. Fix flow:

1. Run validator → collect all `NameError`, `MemberNotFoundError`,
   `MissingImport` (especially `ArsonResearchable`).
2. For each, identify whether the missing FQON is genuinely absent from
   the source `.dat` (real converter bug) or the FQON exists but with a
   different name (typo in converter output).
3. Fix in `openage/convert/processor/conversion/de2/` source files. Never
   hand-patch generated `.nyan` files.
4. Re-run converter (offline, user-side), install converted output,
   re-run validator.
5. Iterate until validator exits 0.
6. Commit converter fix **separately** from cache machinery.

The first attempt at the converter fix is **expected** to surface more
than the single known `ArsonResearchable` error — the validator exists
exactly so we don't have to discover them one-by-one at 9 min each.

## 9. Commit Breakdown (per user directive)

1. `tools: add validate-nyan subcommand`
2. `loader: add parse/load counters and complexity diagnostics`
3. `converter: fix invalid de2_base references` (depends on #1's report)
4. `loader: cache module and FQON indexes` (in-memory only; no file)
5. `loader: add versioned compiled nyan cache`
6. `loader: add lazy object materialization`
7. `test: add correctness and startup performance gates`

Each commit is independent and individually runnable. CI verifies AC-1
through AC-9.

## 10. Tests

### Correctness (AC-1, AC-7, AC-9)

- `validator_finds_slavs_error`: corrupt `slavs.nyan` to introduce
  `FooNotARealType`; assert validator reports it with full location.
- `validator_clean_passes`: known-good minimal modpack (`engine + test
  fixtures`) exits 0.
- `missing_cache_fails_fast`: rm `<modpack>.cache`, run `game`,
  expect non-zero exit + `prepare-nyan-cache` hint in stderr.
- `no_tutorial_assets_dir`: grep working tree for
  `tutorial_assets` references; expect zero matches.

### Parse-once (AC-5)

- `parse_once_per_file`: instrument a small 3-file modpack with the
  counters, assert `parse_attempts_per_file[path] == 1` for all paths.
- `object_state_unique`: same instrumented modpack, assert
  `create_obj_state_unique == total_objects`.

### Cache integrity (AC-6)

- `header_corruption_detected`: flip a byte in the header CRC, expect
  MmapNyanStore to reject with `CacheCorruptError`.
- `section_offset_out_of_range`: set a section's offset past EOF, expect
  rejection.
- `hash_change_invalidates`: touch one source file's mtime, prepare,
  then re-run validator with cache present — expect cache rejected with
  `CacheStaleError` and `prepare-nyan-cache` hint.
- `format_version_bump_detected`: write cache with format_version = 0,
  expect loader to refuse.

### Performance (AC-3, AC-4, AC-8)

- `cold_startup_under_10s`: from clean disk, run `bin/run game
  --tutorial --modpacks de2_base --no-prompts`; measure wall time from
  process start to `load_scenario` returning. Gate ≤ 10 s.
- `warm_startup_under_5s`: same; second run; gate ≤ 5 s.
- `warm_no_text_io`: strace-equivalent (`/usr/bin/strace -e trace=openat
  -c` on a short warm run); assert zero `.nyan` opens.
- `mission1_materializes_subset`: after warm load, assert
  `materialized_objects.size() < 200` and contains the expected FQONs.

All perf gates are skipped (not failed) in sandbox environments that
don't have the converted de2_base modpack present; they are mandatory
in user-side runs.

## 11. Open Questions

1. **Cache location**: `<asset-dir>/converted/.nyancache/` is the
   working proposal. Confirm.
2. **MMAP write atomicity**: do we need a `flock` lock file to prevent
   two `prepare-nyan-cache` runs racing? Currently planned to use
   `<modpack>.cache.lock` and exit non-zero on EAGAIN.
3. **BLAKE3 dependency**: openage already pulls in some crypto deps for
   SLP/hash; verify BLAKE3 (or another fast hash) is acceptable.
   Fallback: SHA-256 via libcrypto if BLAKE3 is unavailable.

## 12. Out of Scope

- Cache for `engine` modpack generated nyan (the `api_export` small
  subset); it's already separately fast (no recursive `**`).
- Replacing `Database` wholesale. We add an adapter, not a rewrite.
- Parallelizing `Parser::parse_file`. Single-threaded is fine for the
  first pass; we revisit if profiling in §6.2 shows parse dominates.

## 13. Risks

- **Risk 1**: `MmapNyanStore` materially drifts from `TextNyanStore` in
  semantics (e.g. inheritance ordering). Mitigation: parity test in §10
  runs both stores over the same fixture and diffs every member value.
- **Risk 2**: cache file size for de2_base exceeds the estimate in §5.7
  and `mmap` becomes expensive. Mitigation: section layout is page-aligned
  so we can `madvise(MADV_RANDOM)` per section; if total > 80 MiB we
  re-evaluate.
- **Risk 3**: converter bug count balloons and the "iterate until
  validator exits 0" loop takes hours of wall time. Mitigation: validator
  output is sortable by `category` and `missing_name`; convert in bulk
  once, not per-error.