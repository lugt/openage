# de2_base Full-Load Correctness and Performance — Design Spec

- Date: 2026-07-24 (revised)
- Branch: `mvp/tutorial-demo`
- Owner: openage MVP tutorial
- Status: Draft for review (revision 2)
- Supersedes: any prior tutorial_assets modpack sketch; supersedes § of revision 1 per user follow-up
- Related memories: [[mvp-direction-pivot]] [[phase05f-startup-no-converter]] [[phase05e-real-villager-fqon]] [[de2-base-asset-audit]] [[de2-base-fullload-pivot]]
- Spec commit: revision 1 = `27c543cf`; revision 2 = (this commit)

## 0. Revision 2 — What Changed

User follow-up after revision 1 tightened 9 areas. Diff summary:

| § | Topic | Revision 1 | Revision 2 |
|---|-------|-----------|-----------|
| 1 | Cache scope | per-modpack `<modpack>.cache` | single dependency-closure `engine_de2_base-<closure_id>.nyancache` |
| 2 | FQON hash | splitmix64 over `std::hash` | vendored wyhash with frozen seed, hash id recorded in header, full-string equality required, fake-hash collision tests |
| 3 | Implementation order | 7 commits, cache 5th | CLI/engine exception guard → validate-nyan → fix converter → validator 0 → cache builder → mmap runtime; no cache until validator exits 0 |
| 4 | Binary ABI | header has 5 fields | full ABI conformance: magic, version, endian, alignment, overflow, file bounds, truncation, per-section CRC32C, whole-file digest, atomic rename |
| 5 | mmap lifecycle | not specified | explicit ownership, ObjectView lifetime, store-close behavior, reload invalidation, multi-thread read-only, lazy mat sync, destroy order, debug assertion in accessor |
| 6 | OverlayNyanStore | 5 lines | generation-scoped ObjectIds, FQON-only persistent refs, per-player overlay, materialized cache invalidation on commit, no per-player base copy |
| 7 | Stage 1 scope | committed to touching libnyan | Stage 1 is fixture-only: no edits to libnyan or full de2_base load path |
| 8 | Performance AC | "Mission 1 materialize < 200 objects" was hard gate | demoted to observation metric; cold/warm budgets remain gates |
| 9 | Adapter surface | 5-row table | explicit boundary: which public API is untouched, which internals reroute, parser/resolver/transaction forbidden to rewrite in MVP |

A final "Invariants" page (§13) lists the rules that cannot be violated without re-opening the design.

## 1. Background and Motivation

`bin/run --tutorial --modpacks de2_base` currently takes 5–9 minutes to
reach `load_scenario`. Profiling (commit `fd847725`) showed that ~656
`.nyan` files load in the first 60 s — extrapolated to 5918 files ≈ 9
minutes. The same load **crashes** at
`de2_base/data/civ/slavs/slavs.nyan:813` with
`unknown name: 'ArsonResearchable'`, a converter-output bug, not a loader
bug.

Two problems are entangled:

1. **Correctness** — converter produces nyan referencing absent FQONs.
2. **Performance** — `Game::load_data` → `Database::load` per-file
   recursion and triple AST walk produces near-O(N²) work.

The user has forbidden both: do **not** silently rebuild on
`bin/run game`, do **not** fall back to a hand-rolled `tutorial_assets`
modpack. The fix is a **general compiled cache** for nyan, lazy
materialization, and an offline validator to enumerate every converter
bug in one pass.

## 2. Non-Goals

- No new gameplay features (Attack/Gather/Build/Train remain out of
  scope).
- No mmap of existing C++ AST/Object/ObjectState memory layout.
- No msgpack / SQLite / capnproto. Hand-rolled packed POD only.
- No full-converter rebuild to fix `slavs.nyan`. Fix at the converter
  source, not in generated `.nyan`.
- No tutorial-specific asset package. The cache serves any modpack.
- No multi-cache dynamic composition in Stage 1. The cache file is for
  exactly one dependency closure.
- No parallel parsing in Stage 1. Re-evaluate after profiler evidence.
- No wholesale rewrite of libnyan in Stage 1. The MVP prototype is a
  pure-reader over its own format; libnyan is only consulted through its
  existing public surface.

## 3. Acceptance Criteria

| ID  | Criterion | Gate |
|-----|-----------|------|
| AC-1 | Engine process exits cleanly (no `std::terminate`, no core dump) when given any input that today crashes or aborts | Required |
| AC-2 | `bin/run validate-nyan --modpacks de2_base` exits 0 after all converter bugs are fixed | Required for AC-4 |
| AC-3 | `bin/run validate-nyan` never writes a cache file; `--json` writes a report only | Required |
| AC-4 | `bin/run prepare-nyan-cache --modpacks de2_base` produces `engine_de2_base-<closure_id>.nyancache` only after a successful validate pass | Required |
| AC-5 | If prepare fails, the previous valid cache file (if any) is untouched | Required |
| AC-6 | `bin/run game --tutorial --modpacks de2_base --no-prompts` reaches `load_scenario` in ≤ 10 s cold / ≤ 5 s warm | Required |
| AC-7 | Warm startup reads no `.nyan` text; only `mmap` + readonly `openat` on the cache | Required |
| AC-8 | Each `.nyan` source file is parsed at most once across full load (within one prepare run) | Required |
| AC-9 | Cache invalidates on: any source hash change, manifest change, converter version change, engine nyan schema version change, cache format version change, closure identity change | Required |
| AC-10 | `bin/run game` with missing or invalid cache exits non-zero with `run prepare-nyan-cache --modpacks <x>` hint, does NOT silently rebuild | Required |
| AC-11 | No `tutorial_assets/` modpack directory or FQON rewrite exists in working tree | Required |
| AC-12 | Stage 1 fixture (≈ 30–50 objects) tests run in seconds in sandbox and pass | Required |
| OBS-1 | Record but do not gate: Mission 1 materialized object count, RSS, page faults, lookup counts | Observation |

## 4. Architecture

### 4.1 Single dependency-closure cache

Stage 1 produces **exactly one** cache file representing the closure of
the user-supplied modpack list resolved through the dependency graph.
For the MVP tutorial that closure is `engine + de2_base`, producing
`engine_de2_base-<closure_id>.nyancache`.

The closure is built as follows:

1. Start from the ordered user modpack list (e.g. `[engine, de2_base]`).
2. For each modpack, read its manifest; resolve its `depends` field to
   pull in additional modpacks (also ordered). Detect and reject cycles.
3. The union of all `.nyan` files reachable from any modpack's `include`
   patterns becomes the closure's module set.
4. `engine` and `de2_base` are loaded into the **same** global symbol
   namespace because nyan's import chain is module-agnostic — a nyan
   `import` resolves a FQON visible across all loaded modules.
5. If two modules in the closure declare the same FQON, the build fails
   with `DuplicateFqonError` naming both files and line numbers. No
   silent merge; no FQON rewrite.
6. Future evolution: multiple closures and a global resolver are
   out-of-scope for Stage 1. The file format and header carry enough
   metadata to support multiple closures later; no design choice locks
   out that path, but no implementation does it now.

### 4.2 Adapter boundary

The libnyan surface used by openage today:

```
class Database {
  static std::shared_ptr<Database> create();
  void load(const std::string &filename, filefetcher_t fetcher);
  std::shared_ptr<View> new_view();
  const std::shared_ptr<State> &get_state() const;
  const MetaInfo &get_info() const;
};
class View {
  Object get_object(const fqon_t &fqon);
  std::shared_ptr<Object> get_object_ptr(const fqon_t &fqon);
  std::shared_ptr<ObjectState> get_raw(const fqon_t &fqon, order_t t);
  const ObjectInfo &get_info(const fqon_t &fqon) const;
  Transaction new_transaction(order_t t);
  ...
};
class Object {
  value_int_t get_int(memberid_t, order_t) const;
  value_float_t get_float(memberid_t, order_t) const;
  std::string get_text(memberid_t, order_t) const;
  set_t get_set(memberid_t, order_t) const;
  ordered_set_t get_orderedset(memberid_t, order_t) const;
  std::string get_file(memberid_t, order_t) const;
  Object get_object(memberid_t, order_t) const;
};
class Transaction { ... };
```

**Rules for the MVP prototype**:

- The list above is **the surface that may not change**. Any other
  libnyan symbol is internal and may be wrapped.
- `NyanStore` is a new class declared in **openage** (not in libnyan).
  Its first concrete subclass in Stage 1 is `MmapNyanStore`, located at
  `libopenage/nyan_store/`. It does **not** inherit from `Database`,
  `View`, or `Object`.
- For Stage 1 the prototype coexists with the existing parser-driven
  `Game::load_data` path. Game startup with `--use-cache` (off by
  default) routes through the prototype; without that flag the existing
  path runs unchanged.
- `MmapNyanStore::resolve(fqon)` returns a `std::shared_ptr<ObjectState>`
  materialized on demand (see §6.3). It does **not** pretend to be a
  libnyan `Object`; the openage consumer code (scenario loader,
  `entity_factory::add_game_entity`) is adapted to consume
  `ObjectState`-shaped data with a thin shim that wraps it back into
  the libnyan `Object` API.
- The shim is the **only** place where Stage 1 touches libnyan. Parser,
  resolver, transaction, lexer, AST are untouched.

**Forbidden in Stage 1 MVP**:

- Rewriting libnyan's parser or lexer.
- Rewriting libnyan's `Database::load` triple walk.
- Rewriting libnyan's transaction semantics.
- Introducing a new public libnyan type beyond what is strictly needed
  to expose `ObjectState` to openage.

### 4.3 Read-path mapping (Stage 1 prototype)

```
                     ┌─────────────────────────┐
                     │ consumer code (e.g.     │
                     │ load_scenario, entity_  │
                     │ factory::add_game_      │
                     │ entity)                 │
                     └────────────┬────────────┘
                                  │ uses shim
                                  ▼
                     ┌─────────────────────────┐
                     │ libnyan Object handle   │ ← thin shim, no
                     │ (unchanged)             │   semantic change
                     └────────────┬────────────┘
                                  │ backed by
                                  ▼
                     ┌─────────────────────────┐
                     │ ObjectState (libnyan,   │ ← materialized
                     │ unchanged shape)        │   on demand by
                     └────────────┬────────────┘      │
                                  │ produced by         │
                                  ▼                    │
                     ┌─────────────────────────┐      │
                     │ openage::NyanStore      │      │
                     │  (abstract base)        │      │
                     └────────────┬────────────┘      │
                                  │                   │
                  ┌───────────────┼──────────────┐    │
                  ▼               ▼              ▼    │
            TextNyanStore   MmapNyanStore   OverlayNyanStore
            (Stage 1 not    (new, reads    (Stage 2+)
             yet used)       .nyancache)
```

In Stage 1 only `MmapNyanStore` is exercised, and only against the
fixture. `TextNyanStore` and `OverlayNyanStore` are stub classes whose
interfaces are sketched here so Stage 2 plumbing is mechanical.

### 4.4 Prepare and game flows

**Prepare** (`bin/run prepare-nyan-cache --modpacks de2_base`):

```
resolve closure
  ├─ ModManager reads manifests, computes closure_id
  ├─ dependency walk; reject cycles; reject duplicate FQONs
  ├─ emit dependency-closure metadata record
build cache
  ├─ 1. run TextNyanStore::Database::load_batch on closure (NOT per-file)
  ├─ 2. invoke validate-nyan (Stage 1: separate command; Stage 2: in-process)
  │       ├─ any error → delete .tmp, exit non-zero, **previous cache preserved**
  │       └─ all clean → continue
  ├─ 3. walk State::objects + MetaInfo; serialize via CacheBuilder
  ├─ 4. write engine_de2_base-<closure_id>.nyancache.tmp
  ├─ 5. fsync .tmp
  ├─ 6. atomic rename .tmp → final
  └─ 7. fsync parent directory
```

**Game** (`bin/run game --tutorial --modpacks de2_base --no-prompts`):

```
ModManager loads manifests → modpack list
  ├─ compute closure_id
  ├─ open engine_de2_base-<closure_id>.nyancache
  │     ├─ header valid, format version, hash id, closure identity match → MmapNyanStore
  │     └─ missing/invalid/stale → fail-fast with prepare hint
  ├─ MmapNyanStore::open populates State + MetaInfo
  ├─ Game::load_data (Stage 1: existing text path; Stage 2: cache path)
  └─ Game::load_scenario(<mission fqon>)
        ├─ View::get_object(mission fqon) via shim
        ├─ each PlacedEntity.entity fqon → materialize on first access
        └─ overlay base untouched
```

## 5. Cache File Format — `*.nyancache`

### 5.1 Identity and naming

Filename: `engine_de2_base-<closure_id>.nyancache`.

`closure_id` is a 7-character base32 short digest derived from the
closure identity (see §5.7). The full closure identity is duplicated in
the header so the filename is a hint, not a source of truth; an attacker
or accidental rename cannot trick a successful load.

### 5.2 Header (page-aligned, 4096 bytes)

```
offset  size   field
0       4      magic            = 'NYNC' (0x4E594E43 LE)
4       2      format_version   = 0x0001 (LE u16)
6       2      endian_marker    = 0x4243 ('BC' for openage byte order)
8       4      hash_algorithm_id = 1 (wyhash; see §5.5)
12      4      hash_algorithm_version = 1
16      8      created_unix_ms   (LE u64)
24      4      converter_version_str   (STRINGS offset)
28      4      engine_schema_version_str (STRINGS offset)
32      32     source_merkle_root (BLAKE3 of sorted source hashes)
64      4      modpack_count
68      4      module_count
72      8      total_objects
80      8      total_members
88      8      total_values
96      8      section_directory_offset
104     8      section_directory_size
112     8      string_table_offset
120     8      string_table_size
128     256    closure_identity_record (see §5.7; padded with zeros)
384     8      whole_file_digest   (BLAKE3 of all sections, excluding header)
392     3704   reserved (zero-padded)
4092    4      header_crc32c
```

All offset fields are **file-absolute** in Stage 1 (simpler than
section-relative; we revisit only if relocatability becomes necessary).
Readers validate every offset against the file size before dereferencing.

### 5.3 Sections

| Section       | Contents                                              | Sort key           |
|---------------|--------------------------------------------------------|--------------------|
| `STRINGS`     | NUL-terminated UTF-8 packed contiguously               | insertion order    |
| `MODULES`     | one `ModuleRecord` per `.nyan` file in closure          | module id          |
| `FQON_INDEX`  | one `FqonEntry` per object, sorted by `fqon_hash`       | fqon_hash (64-bit) |
| `FQON_PROBE`  | open-addressing probe table: `slot → FqonEntry idx`     | slot (power-of-two) |
| `DEPS`        | import graph edges: `DepRecord(importer, importee)`    | importer, importee |
| `TYPES`       | one `TypeRecord` per unique type definition             | type id            |
| `OBJECTS`     | one `ObjectRecord` per object                           | object id          |
| `MEMBERS`     | one `MemberRecord` per (object, member name)           | object id, name hash |
| `VALUES`      | tagged `ValueRecord` stream                             | (stream of bytes)  |
| `DIAG`        | optional source-location diagnostics                    | object id          |

The `SectionDirectory` (a small fixed array inside the header space at
offset 128 — see §5.7) lists each section's
`{name_hash, offset, size, crc32c}`. New sections may be added in future
format versions; readers must skip unknown sections instead of failing.

### 5.4 Record layout (packed POD, explicit serialization)

Records are written field-by-field by `CacheBuilder`. No struct is ever
`reinterpret_cast` to a byte stream.

```c++
// All multi-byte integers are little-endian.
void write_u8 (uint8_t  v);  // 1 byte
void write_u16(uint16_t v);  // 2 bytes
void write_u32(uint32_t v);  // 4 bytes
void write_u64(uint64_t v);  // 8 bytes
void write_str(std::string_view s);  // u32 length + bytes (no NUL terminator on disk)

// Field layout for each record (declared for documentation; written field-by-field):
ModuleRecord:
    u32 path_str            // STRINGS offset (0 = none)
    u32 path_len            // bytes
    u64 source_sha256_lo    // first 8 bytes of BLAKE3(file content)
    u32 source_sha256_hi    // remaining 24 bytes split into 3 u32
    u32 source_sha256_mid
    u32 source_sha256_lo2
    u32 parent_mod          // module id; UINT32_MAX for root
    u32 first_obj           // OBJECTS index
    u32 obj_count

FqonEntry:
    u64 fqon_hash           // wyhash over UTF-8 bytes, seed = WYHASH_SEED
    u32 str_offset          // STRINGS offset to canonical FQON text
    u32 str_length          // bytes
    u32 object_idx          // index into OBJECTS

ObjectRecord:
    u32 fqon_str
    u16 fqon_len
    u16 parent_count
    u32 parent_idx[8]       // OBJECTS indices; overflow → DEPS edge
    u32 first_member
    u16 member_count
    u16 type_idx            // UINT16_MAX if untyped
    u32 diag_idx            // UINT32_MAX if none

MemberRecord:
    u32 name_str
    u16 name_len
    u16 type_idx
    u32 first_value         // VALUES byte offset
    u32 value_count
    u8  modifier_mask
    u8  flags
    u16 _pad

ValueRecord:
    u8  tag                 // 0=Int, 1=Float, 2=Text, 3=Object, 4=Set,
                            // 5=OrderedSet, 6=Dict, 7=File, 8=None
    u8  flags               // bit 0 = has_override
    u16 _pad
    u32 payload             // tag-specific (see §5.6)
```

### 5.5 FQON hash policy (wyhash, vendored)

- Algorithm: **wyhash** final version 4, vendored from upstream at a
  pinned commit hash recorded in `third_party/wyhash/WYHASH_VERSION`.
  The implementation file is `third_party/wyhash/wyhash.h` plus a
  `wyhash.cpp` if the upstream is header-only; either way the file is
  copied into the openage tree, **not** pulled from a system library.
- Seed: `WYHASH_SEED = 0xA5A5A5A55A5A5A5AULL`. Frozen at compile time.
- Input: the FQON string's raw UTF-8 bytes (no normalization, no case
  folding, no canonicalization).
- Cross-platform rule: a `SerializeFqonForHash` helper writes the bytes
  in **little-endian byte order** regardless of host endianness — i.e.,
  on a big-endian host the FQON is byte-reversed before being passed
  to wyhash. This guarantees identical hashes across hosts. The
  endian_marker in the header is sanity-checked against the host at
  load time; mismatch aborts with a structured error, never silently
  rehashes.
- **Forbidden**: any use of `std::hash<std::string>`, any platform
  default hash, any compile-time-selected algorithm.
- The cache header's `hash_algorithm_id = 1, hash_algorithm_version =
  1` documents what was used. Readers refuse unknown combinations.

Lookup path (pseudocode):

```
func resolve(fqon):
    h = wyhash(fqon.utf8_bytes, seed=WYHASH_SEED)
    slot = h & (probe_size - 1)
    for i in 0..probe_size:
        idx = probe_table[slot]
        if idx == EMPTY: return NotFound
        candidate = fqon_index[idx]
        if candidate.fqon_hash == h
           and candidate.str_bytes == fqon.utf8_bytes:  // full string equality
            return ObjectRecord[object_idx]
        slot = (slot + 1) & (probe_size - 1)
    return NotFound
```

The probe table uses power-of-two size, load factor ≤ 0.6, and an
`EMPTY` sentinel (u32 sentinel = UINT32_MAX). Collision chains are
linear; correctness comes from the full-string equality check, never
from the hash alone.

### 5.6 Value encoding

| Tag    | Encoding                                                            |
|--------|--------------------------------------------------------------------|
| Int    | payload = STRINGS offset to ASCII digits; int64 in next 8 bytes      |
| Float  | payload = STRINGS offset to ASCII digits; double in next 8 bytes     |
| Text   | payload = STRINGS offset, len in next 4 bytes                       |
| Object | payload = OBJECTS index (generation-scoped; see §6.6)               |
| None   | payload = 0                                                          |
| File   | payload = STRINGS offset to relative path                            |
| Set / OrderedSet | count in payload, followed by `count × 4-byte` index into OBJECTS section |
| Dict   | unhandled by openage gameplay today; serialize as Text "unhandled" + DIAG warning |

`Int` and `Float` keep their ASCII form in the string table so they
survive JSON diff round-trips and human inspection; runtime reuses
ASCII → numeric once at materialize time.

### 5.7 Closure identity and whole-file digest

`closure_identity_record` (256 bytes) is a fixed-layout struct:

```
offset  size   field
0       16     reserved (zeros; magic-style)
16      4      modpack_count
20      64     modpack_names[N]   // NUL-separated, 64-byte slot per name
84      8      sorted_modpack_names_digest (BLAKE3-256, first 8 bytes)
92      4      manifest_digest   (BLAKE3-256, first 4 bytes, of concatenated manifests)
96      16     reserved for: converter version (8 bytes LE u64),
                            engine schema version (8 bytes LE u64)
112     16     reserved for: cache format version (8 bytes LE u64),
                            creation timestamp (8 bytes LE u64)
128     16     reserved (zeros; future extension)
144     4      reserved (zeros; CRC32C of bytes 0..143)
148     108    reserved (zeros)
```

`closure_id` (the filename suffix) is computed from the same metadata
but as a short base32 digest: `base32(BLAKE3-256(canonical_metadata))[:7]`.
Mismatch between `closure_id` and the header's `closure_identity_record`
is logged but **does not** invalidate; the header is authoritative.

`whole_file_digest` is BLAKE3-256 over the concatenation of all sections
in order (header excluded). Stored at header offset 384, verified at
load.

### 5.8 Binary ABI Conformance (mandatory for the reader and writer)

Every requirement below is implemented as an explicit check, both at
write and at read. The reader never dereferences a pointer it has not
first validated.

| # | Requirement | Check |
|---|-------------|-------|
| 1 | Magic = `NYNC` at offset 0 | read 4 bytes, reject on mismatch |
| 2 | `format_version` in supported range (currently `0x0001`) | reject |
| 3 | `endian_marker == 0x4243` matches host's wyhash canonical byte order | reject |
| 4 | `hash_algorithm_id == 1 && hash_algorithm_version == 1` | reject |
| 5 | Header CRC32C at offset 4092 equals CRC32C of bytes 0..4091 | reject |
| 6 | All section offsets and sizes are in `[0, file_size]` and do not overlap each other or the header | reject on overlap or out-of-range |
| 7 | Section alignment: each section begins on a 4-byte boundary; `STRINGS`, `MODULES`, `OBJECTS`, `MEMBERS` are 8-byte aligned | reject on misaligned read |
| 8 | Record alignment: each record is written with explicit alignment padding so readers can `memcpy` field-by-field without struct-punning | reject on padded-bytes nonzero (unless `flags` indicates padding byte) |
| 9 | Integer overflow: `count * element_size` cannot exceed the section's size | reject |
| 10 | String offsets and lengths are in `[0, strings_size]` and `[0, 64 KiB]` respectively | reject |
| 11 | Object/member counts per record are in `[0, total_objects]` / `[0, total_members]` respectively | reject |
| 12 | File truncation: every section's `offset + size ≤ file_size` | reject |
| 13 | Per-section CRC32C (in section directory) computed over the section's bytes | reject |
| 14 | Whole-file digest = BLAKE3-256 (first 8 bytes stored) over all section bytes concatenated in order | reject |
| 15 | Atomic write: `CacheBuilder` writes to `*.tmp`, calls `fsync`, then `rename(2)` to final name; finally `fsync` parent dir | enforced by the writer |

The reader checks 1–14 in order, returning the first failure as a
structured `NyanCacheError` variant (`HeaderError`, `FormatVersionError`,
`EndianError`, `HashAlgorithmError`, `ChecksumError`, `BoundsError`,
`OverlapError`, `AlignmentError`, `OverflowError`, `StringBoundsError`,
`CountError`, `TruncationError`, `CorruptSectionError`,
`WholeFileDigestError`). It never aborts, terminates, or segfaults.

### 5.9 Stage 1 file size estimate

A 50-object fixture produces a header-only test bed of ≈ 50 KiB; that
is sufficient to exercise every record and accessor path. Full de2_base
size is deferred to Stage 2 and measured, not estimated here.

## 6. Loader Semantics

### 6.1 Batched loader (Stage 2 — not in Stage 1 prototype)

Today `Game::load_data` calls `db->load(mod_dir + "/" + search,
fileload_func)` per `.nyan` file. Each call re-parses that file
(already parsed once when imported by another file), re-runs three AST
walks, and DFS-walks its imports.

Stage 2 introduces `Database::load_batch(file_list, filefetcher)` —
single call, single import DFS shared across the whole list, three
walks run once over the combined AST set. `Game::load_data` calls
`load_batch` once per closure.

Stage 1 prototype does **not** call `load_batch`. It bypasses the
existing text path entirely and reads the fixture cache directly.

### 6.2 Counters added (Stage 2)

Expose a `StartupCounters` accessor (`openage::gamestate::get_startup_stats`)
and extend:

- `parse_attempts_per_file[path]` (per-file map)
- `import_resolves_total`, `import_resolves_cached`
- `fqon_lookups_total`, `fqon_lookup_hits` (hits via index)
- `namespace_full_scans` (each `ast_obj_walk` pass counts one)
- `create_obj_state_calls`, `create_obj_state_unique`
- `object_state_rebuilds`
- `mmap_page_faults` (read from `/proc/self/status` MinFlt/MajFlt deltas)
- `rss_peak_bytes`

Print at end:

```
[PERF] load_done files_seen=N files_loaded=N
       parse_attempts_total=K parse_attempts_unique=K
       import_resolves_total=K import_resolves_cached=K
       fqon_lookups_total=K fqon_lookup_hits=K
       namespace_full_scans=K
       object_states_created=K object_states_unique=K
       mmap_page_faults_minor=X mmap_page_faults_major=Y
       rss_peak_bytes=Z
```

Stage 1 prototype observes the same metrics on the fixture but does
not gate on them.

### 6.3 Lazy materialization

`MmapNyanStore::resolve(fqon)`:

- First call: walk `ObjectRecord.parent_idx[]` chain through cache,
  merge inherited members into a fresh `ObjectState`, cache it under
  a `std::unordered_map<fqon, std::shared_ptr<ObjectState>>` keyed by
  the FQON's full string.
- Subsequent calls: return cached pointer.
- The merged `ObjectState` is built once per FQON per process; this
  is the Stage 2 performance lever and is measured but not gated in
  Stage 1.

### 6.4 mmap lifecycle semantics

- **Ownership**: `MmapNyanStore` owns one `int fd` per open cache file
  and one `void *mmap_base` of length `file_size`. Both are released
  in `MmapNyanStore::close()`.
- **`ObjectView` lifetime**: pointers returned by `MmapNyanStore::view_*(idx)`
  are valid only while the store is open. They are tagged with
  `[[nodiscard]]` and a debug-only `assert(store.is_open())` runs on
  every accessor. Releasing the store while a consumer still holds a
  pointer is a bug; debug builds detect it via a magic-word check on
  each view (`OPENAGE_NYAN_VIEW_MAGIC`), release builds do not crash.
- **`std::string_view` lifetime**: returned string views point into the
  `STRINGS` section of the mmap region; same lifetime rule applies.
- **Store-close behavior**: after `close()`, all views and string views
  become invalid. Accessors in release builds return
  `NyanCacheError::StoreClosed`; in debug builds they assert.
- **Cache reload**: `MmapNyanStore::close()` + open a new file. Old
  pointers from the previous file are invalid; consumers must re-resolve.
  Lazy materialization cache is cleared by `close()`.
- **Multi-thread read-only**: the mmap base is immutable for the
  process lifetime (no one writes to it). Multiple threads may call
  `view_*` and `resolve` concurrently without external locking.
- **Lazy materialization sync**: a single `std::shared_mutex` protects
  the `unordered_map<fqon, shared_ptr<ObjectState>>`. Reads take a
  shared lock; first-time materialization takes the unique lock once.
- **Destruction order**: `OverlayNyanStore` (if present) is destroyed
  first (its `ObjectState`s may reference base objects via
  raw pointers); `MmapNyanStore::close()` second; the consumer code's
  cached `ObjectState`s last.
- **Debug assertion in every accessor**: each `view_*`, `resolve`, and
  `string_view` function begins with
  `assert(this->is_open() && "accessor called on closed store")` and
  end with `assert(idx < this->count && "view index out of range")`.
- **Corruption handling**: any check in §5.8 that fails returns a
  structured `NyanCacheError`. The store is marked `corrupt` and
  refuses further access. It does not crash, terminate, or segfault.

### 6.5 Overlay semantics (Stage 2 design; Stage 1 only sketches)

- Each player owns an independent `OverlayNyanStore`. There is exactly
  one `MmapNyanStore` (the immutable base), shared by all players.
- `MmapNyanStore` assigns an `ObjectId` to each object. This `ObjectId`
  is stable **only within one cache generation** (one cache file load).
  After cache reload, ObjectIds are reassigned and may differ.
- Persistent references — save files, replays, network packets — use
  **FQON strings**, never `ObjectId`. The cache layer must reject any
  attempt to serialize an ObjectId to a persistent surface.
- The overlay stores only diffs: a `std::unordered_map<fqon,
  std::shared_ptr<ObjectState>>`. The base is never copied per player.
- `Transaction::commit` writes new `ObjectState`s into the player's
  overlay and **invalidates** that player's materialized-object cache
  for any fqon whose parent chain includes a modified object.
- `parent/member resolution` walks the overlay first, then the base.
  A merged result is computed once per (player, fqon) and cached.

### 6.6 Telemetry as first-class observable

Stage 1 logs: files loaded, parse attempts (per file), FQON lookups
(per call), materialized object count, RSS peak, mmap page faults.
These are observed metrics; thresholds (OBS-1) are set after Stage 1
baseline data exists, not invented now.

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

| Category               | Source                                     |
|------------------------|--------------------------------------------|
| `NameError`            | existing Nyan API                          |
| `MemberNotFoundError`  | `Database::find_member`                    |
| `MemberTypeError`      | `Database::resolve_types`                  |
| `MissingImport`        | `import X` where X is not in any namespace |
| `ManifestMissing`      | modpack manifest is absent                 |
| `FqonConflict`         | `NamespaceFinder::check_object_conflict`   |
| `DuplicateFile`        | same file path parsed twice                |
| `CycleInInheritance`   | `linearize_new` reports cycle              |
| `DuplicateFqonInClosure` | two modules declare the same FQON         |

Each error: `{category, file, line, column, object, missing_name,
import_chain}`. `import_chain` is reconstructed by walking
`DepRecord[obj]` edges back to the entry module.

### 7.3 JSON report schema

```json
{
  "version": 1,
  "modpacks": ["engine", "de2_base"],
  "closure_id": "abc1234",
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

- Lives in `openage/validate_nyan/main.py`, registered as
  `subparsers.add_parser("validate-nyan", ...)` in
  `openage/__main__.py`.
- Stage 1: validator is a separate command. It does **not** invoke the
  prepare-cache step.
- Stage 2: prepare-cache invokes validator in-process before writing
  the cache. Same code path; same error collection.
- Reuses `ModManager`; constructs a `Database` directly; calls
  `Database::load_batch` (Stage 2) or its text equivalent (Stage 1).
- Validator wraps existing nyan C++ error types with try/catch that
  records and continues. Crucially, the **first commit** in the
  implementation order (§9) wraps the engine/CLI boundary so that
  *unhandled* exceptions never terminate the process.

### 7.5 Process exit codes

- 0: no errors.
- 1: any error.
- 2: `--strict` failed on warnings.

## 8. Converter Bug Fix Workflow

`bin/run validate-nyan --modpacks de2_base` must exit 0 before any
cache is built. Fix flow:

1. Run validator → collect all `NameError`, `MemberNotFoundError`,
   `MissingImport` (especially `ArsonResearchable`).
2. For each, identify whether the missing FQON is genuinely absent from
   the source `.dat` (real converter bug) or the FQON exists but with
   a different name (typo in converter output).
3. Fix in `openage/convert/processor/conversion/de2/` source files.
   Never hand-patch generated `.nyan` files.
4. Re-run converter (offline, user-side), install converted output,
   re-run validator.
5. Iterate until validator exits 0.
6. Commit converter fix **separately** from cache machinery.

The first attempt at the converter fix is **expected** to surface more
than the single known `ArsonResearchable` error — the validator exists
exactly so we don't have to discover them one-by-one at 9 min each.

## 9. Implementation Order (revised)

The user-mandated order is enforced by commit dependencies and by a
guard: **no `prepare-nyan-cache` writes a cache file until the
validator exits 0**.

1. **`engine: catch nyan and std exceptions at CLI/engine boundary`**
   - Wrap `main`/`game`/`convert` entry points with
     `std::set_terminate_handler` and try/catch around the
     simulation/load entry points.
   - Add a regression test that triggers a known crash today
     (e.g. truncated `.nyan`, malformed FQON) and asserts the process
     exits with a structured error code, no core dump.
   - **No cache in this commit.** No NyanStore. No format spec code.

2. **`tools: add validate-nyan subcommand`**
   - Implements §7. Runs the existing text path.
   - Smoke test against the existing engine modpack only (no de2_base).
   - Smoke test against an artificially corrupted fixture to prove the
     continue-on-error path enumerates > 1 error.
   - **No cache in this commit.** Validator runs without writing.

3. **`converter: enumerate de2_base errors via validate-nyan`**
   - User-side run; produce JSON report. No code change beyond the
     converter source.
   - **No commit in openage unless a converter source bug is fixed.**

4. **`converter: fix invalid de2_base references`**
   - Fix bugs identified by step 3 in `openage/convert/.../de2/...`.
   - Until `bin/run validate-nyan --modpacks de2_base` exits 0,
     step 5 does not begin.

5. **`tools: add prepare-nyan-cache subcommand (skeleton + atomic write)`**
   - Skeleton writes a `.nyancache` file containing only the header
     (no sections yet). Atomic write + fsync + rename already in place.
   - Validator exit code is the gate; a failed prepare leaves the
     previous valid cache untouched (AC-5).

6. **`loader: cache builder + cache reader + fixture tests`**
   - Implements §5 in full.
   - Cache builder uses vendored wyhash (§5.5).
   - Reader implements all 15 checks from §5.8.
   - Tests run against the §11 Stage 1 fixture; all Stage 1 tests
     pass.

7. **`loader: lazy materialization + adapter shim`** *(Stage 2; not
   required for AC-1..AC-5)*
   - Implements §6.3 + §6.5.
   - Enables `bin/run game` to consume the cache via the shim.
   - Tests + perf measurements follow; cold/warm gates become
     enforced.

Each commit is independent and individually runnable. CI verifies
AC-1 through AC-12.

## 10. Tests

### Correctness (AC-2, AC-3, AC-5, AC-10, AC-11)

- `exception_guard_no_core_dump`: corrupt `.nyan` mid-load; expect
  process exit code ≠ 0, no `core` file written, no `terminate called`.
- `validator_clean_passes`: known-good minimal modpack exits 0.
- `validator_finds_slavs_error`: corrupt a fixture to introduce a
  missing FQON; assert validator reports it with full location and
  import chain.
- `validator_continue_on_error_enumerates`: 3 distinct errors in a
  fixture; assert all three are reported.
- `prepare_skips_on_validation_failure`: feed validator a failing
  fixture; assert no cache file is written and any previous valid
  cache is byte-identical afterwards.
- `atomic_write_leaves_previous_intact`: write a valid cache; trigger
  a failing prepare; assert the old file's mtime and content are
  unchanged.
- `missing_cache_fails_fast`: rm `<modpack>.cache`, run `game` (or the
  prepare-cache dry-run path), expect non-zero exit + prepare hint.
- `no_tutorial_assets_dir`: grep working tree for `tutorial_assets`
  references; expect zero matches.

### Stage 1 fixture coverage (AC-12)

The Stage 1 fixture exercises:

- multi-module imports
- cross-module FQON resolution
- inheritance (single + multi-level)
- primitive values (Int, Float, Text, File)
- object references as member values
- Set, OrderedSet (collection)
- Dict (logged unhandled, not crashed)
- member override
- source diagnostics (`DIAG` section populated)
- hash probe collision (injected fake hash)
- invalid FQON (validator path)
- corrupted offset/count/section/checksum (all 15 checks in §5.8)
- TextStore/MmapStore parity (Stage 1 stub interfaces)
- minimal overlay representation (sketch only)

### Parse-once and integrity (AC-8, AC-9)

- `parse_once_per_file`: instrument a small 3-file modpack; assert
  `parse_attempts_per_file[path] == 1` for all paths.
- `object_state_unique`: same modpack; assert
  `create_obj_state_unique == total_objects`.
- `header_corruption_detected`: flip a byte in the header CRC, expect
  MmapNyanStore to reject with `CacheCorruptError`.
- `section_offset_out_of_range`: set a section's offset past EOF,
  expect rejection.
- `hash_change_invalidates`: touch one source file's mtime, prepare,
  then re-run validator with cache present — expect cache rejected
  with `CacheStaleError` and prepare hint.
- `format_version_bump_detected`: write cache with `format_version =
  0`, expect loader to refuse.

### Performance gates (AC-6, AC-7)

- `cold_startup_under_10s`: from clean disk, run `bin/run game
  --tutorial --modpacks de2_base --no-prompts`; measure wall time from
  process start to `load_scenario` returning. Gate ≤ 10 s. **Stage 2
  only**; Stage 1 does not gate.
- `warm_startup_under_5s`: same; second run; gate ≤ 5 s. **Stage 2
  only.**
- `warm_no_text_io`: strace-equivalent on a short warm run; assert
  zero `.nyan` opens. **Stage 2 only.**

### Observation metrics (OBS-1)

Record but do not gate: Mission 1 materialized object count, RSS peak,
minor/major page faults, FQON lookups. After Stage 1 and Stage 2
baselines exist, set thresholds in a follow-up spec revision.

All perf gates are skipped (not failed) in sandbox environments that
don't have the converted de2_base modpack present; they are mandatory
in user-side runs.

## 11. Stage 1 Prototype Scope

Stage 1 is **fixture-only**. It produces:

- `libopenage/nyan_store/` containing `NyanStore` (abstract base),
  `MmapNyanStore` (concrete, mmap reader), and stub
  `TextNyanStore` / `OverlayNyanStore` (interfaces only).
- `openage/prepare_nyan_cache/main.py` registered as
  `subparsers.add_parser("prepare-nyan-cache", ...)`.
- Vendored `third_party/wyhash/` with a `WYHASH_VERSION` file.
- A 30–50 object fixture under `assets/test/nyan_cache_fixture/`,
  hand-written or generated from a minimal subset of the existing
  engine nyan.
- Tests under `openage/nyan_cache/tests/` (or equivalent).

Stage 1 does **not**:

- modify `Game::load_data` or any code path that loads full de2_base
  today;
- modify libnyan's parser, resolver, transaction, lexer, AST;
- introduce multi-cache composition;
- introduce a global resolver across multiple closures;
- enable `bin/run game` to consume the cache yet (that's Stage 2);
- ship cache invalidation beyond simple hash mismatches (timestamp +
  BLAKE3 in this stage; full closure identity deferred).

Stage 1 success criteria:

- All 15 checks in §5.8 implemented and tested.
- Fixture round-trip: cache builder writes, mmap reader reads,
  every accessor returns expected value.
- TextStore/MmapStore parity test passes (compare the same fixture
  read by the existing parser path and by MmapNyanStore via the shim).
- Collision test: inject a fake-hash policy that maps two distinct
  FQONs to the same probe slot, assert both resolve correctly via
  full-string equality.
- Corrupt-cache tests: header CRC wrong, section offset out of range,
  section overlap, integer overflow, truncated file — all return
  structured errors, no crash.

## 12. Out of Scope

- Cache for `engine` modpack generated nyan (the `api_export` small
  subset); it's already separately fast.
- Replacing `Database` wholesale. We add an adapter, not a rewrite.
- Parallelizing `Parser::parse_file`. Single-threaded is fine for the
  first pass; revisit only after profiling.
- Civ-patch overlays in Stage 1. Sketched in §6.5; implemented in
  Stage 2.

## 13. Risks

- **Risk 1**: `MmapNyanStore` materially drifts from `TextNyanStore`
  in semantics (e.g. inheritance ordering). Mitigation: parity test
  in §10 runs both stores over the same fixture and diffs every
  member value.
- **Risk 2**: cache file size for de2_base exceeds expectations and
  `mmap` becomes expensive. Mitigation: section layout is page-aligned
  so we can `madvise(MADV_RANDOM)` per section; if total > 80 MiB we
  re-evaluate.
- **Risk 3**: converter bug count balloons and the "iterate until
  validator exits 0" loop takes hours. Mitigation: validator output is
  sortable by `category` and `missing_name`; convert in bulk once, not
  per-error.
- **Risk 4**: wyhash upstream changes semantics in a future version.
  Mitigation: pin to a specific upstream commit, vendor the exact
  bytes, and refuse to load if `hash_algorithm_version` does not
  match.
- **Risk 5**: ObjectId leak into persistent storage via copy-paste
  from one cache generation to the next. Mitigation: explicit lint
  that any code path writing to save/replay/network surfaces uses
  FQON only; documented in §6.6.

---

# Invariants — A Page That Cannot Be Violated

The rules below are non-negotiable. Any change requires re-opening the
spec, not just amending the code.

1. **Tutorial assets**: `assets/converted/tutorial_assets/` must not
   exist. No `tutorial_assets.X` FQON may appear in any tracked file.
   Grep gate enforced by `test_no_tutorial_assets`.

2. **Cache scope**: Stage 1 produces exactly one cache file per
   dependency closure. `bin/run prepare-nyan-cache --modpacks de2_base`
   produces `engine_de2_base-<closure_id>.nyancache`, not per-modpack
   files.

3. **Validator gate**: `prepare-nyan-cache` writes a cache file only
   when the validator exits 0. Any failure leaves the previous valid
   cache byte-identical on disk.

4. **Hash policy**: FQON lookup uses vendored wyhash with frozen seed
   and frozen byte-order rule for indexing only. Every successful
   lookup must compare the full FQON string before returning. No
   `std::hash`, no platform default, no compile-time-selected
   algorithm.

5. **Binary ABI**: the writer and reader implement all 15 checks in
   §5.8. A check failure returns a structured `NyanCacheError`
   variant; never abort, terminate, or segfault.

6. **Atomic write**: `CacheBuilder` writes `*.tmp`, fsyncs, renames,
   fsyncs parent dir. No in-place writes.

7. **ObjectId lifetime**: an `ObjectId` from `MmapNyanStore` is
   valid only for the lifetime of one open cache file. Reloading
   the cache invalidates all prior ObjectIds.

8. **Persistent references use FQON**: save files, replays, network
   packets, scenario files, and any user-facing serialization use
   FQON strings, never `ObjectId`. The lint in `Risk 5` enforces
   this.

9. **No silent cache rebuild**: `bin/run game` exits non-zero with a
   prepare hint when the cache is missing or invalid. It never
   rebuilds the cache implicitly.

10. **Exception safety**: no code path in the engine, CLI, or loader
    may `std::terminate` or write a `core` file on input that today
    crashes. The CLI/engine boundary catches all exceptions and
    converts to structured exit codes.

11. **Stage 1 isolation**: the Stage 1 prototype does not modify
    libnyan parser/resolver/transaction/lexer/AST, does not modify
    `Game::load_data` or any code path that today loads full
    de2_base. All Stage 1 work is in `libopenage/nyan_store/`,
    `openage/prepare_nyan_cache/`, `third_party/wyhash/`, and the
    fixture tree.

12. **No parallel parsing in Stage 1**: `MmapNyanStore` is
    single-threaded. No reader starts worker threads to "warm"
    materialization in the background. Lazy materialization is
    on-demand.

13. **FQON truth**: nyan FQONs are immutable strings. The cache
    stores them once in the `STRINGS` section and references them
    by `(offset, length)`. No FQON rewrite, no normalization, no
    case folding.

14. **Header authoritative**: filename (`engine_de2_base-<closure_id>.nyancache`)
    is a hint. The header's `closure_identity_record` and the
    sections' own CRC32C are the source of truth. A renamed file
    cannot trick the reader.

15. **No msgpack / SQLite / capnproto / protobuf**: the cache uses
    hand-rolled packed POD records only. External serialization
    libraries are forbidden in this cache.