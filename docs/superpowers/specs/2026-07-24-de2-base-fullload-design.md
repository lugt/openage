# de2_base Full-Load Correctness and Performance — Design Spec

- Date: 2026-07-24 (revision 3.1)
- Branch: `mvp/tutorial-demo`
- Owner: openage MVP tutorial
- Status: Draft for implementation planning
- Supersedes: revision 2 of this document; no `tutorial_assets` design is retained
- Detailed references: [Binary Format Reference](2026-07-24-de2-base-fullload-binary-format.md), [Test Matrix](2026-07-24-de2-base-fullload-test-matrix.md)

## 0. Revision 3 scope

Revision 3 is documentation-only convergence. It splits the former long spec into a short design decision record plus two focused references. It does not add a new architecture, change the cache model, or authorize implementation changes to `libnyan`.

| Concern | Canonical location in revision 3 |
|---|---|
| Goals, boundaries, architecture, stages, gates | This document |
| Header, sections, records, offsets, hashes, checksums, ABI, deterministic bytes, generation safety | [Binary Format Reference](2026-07-24-de2-base-fullload-binary-format.md) |
| Fixtures, parity, corruption, invalid corpus, lifecycle, invalidation, performance, regression tests | [Test Matrix](2026-07-24-de2-base-fullload-test-matrix.md) |

## 1. Goals and non-goals

### Goals

1. Make converted `de2_base` data correct before it becomes cache input.
2. Replace repeated text loading with one dependency-closure cache and read-only mmap access.
3. Preserve existing libnyan public semantics through an openage-owned `NyanStore` adapter.
4. Materialize objects lazily and make cache generation/lifetime errors explicit.
5. Make startup fail fast when a cache is absent or stale; preparation is an explicit offline command.
6. Establish deterministic, validated, atomically published cache artifacts.

### Non-goals

- No new gameplay features.
- No `tutorial_assets` package or FQON rewrite.
- No mmap of existing C++ AST/Object/ObjectState layouts.
- No msgpack, SQLite, capnproto, protobuf, or other external cache serialization.
- No parser, lexer, resolver, transaction, AST, or full `Database` rewrite in Stage 1.
- No parallel parsing or background warming in Stage 1.
- No dynamic multi-cache composition in Stage 1.

The complete binary contract is defined only in the [Binary Format Reference](2026-07-24-de2-base-fullload-binary-format.md). The complete executable test contract is defined only in the [Test Matrix](2026-07-24-de2-base-fullload-test-matrix.md).

## 2. Dependency-closure cache

The cache represents exactly one resolved dependency closure. For the MVP invocation, the ordered roots are `engine` and `de2_base`, producing a file named `engine_de2_base-<closure_id>.nyancache`.

The preparation flow is:

1. Read manifests from the ordered user modpack list.
2. Resolve dependencies, reject cycles, and canonicalize the closure.
3. Collect **every `.nyan` module declared by every manifest in the resolved closure** into one global namespace. This is manifest-complete collection, not Mission 1 or import-reachable subset collection. All modules and all declared FQONs enter the cache index even when their objects are lazy-materialized later.
4. Reject duplicate FQON declarations with both source locations.
5. Run `validate-nyan`; any error prevents cache publication.
6. Build the packed cache using canonical ordering and the pinned hash policy.
7. Write `*.tmp`, `fsync`, atomically rename, and `fsync` the parent directory.

The filename is only a hint. Header identity, section checksums, and whole-file digest are authoritative. A failed preparation leaves the previous valid cache byte-identical. `game` never silently rebuilds a missing or invalid cache; it exits with a `prepare-nyan-cache` hint.

The closure identity includes ordered modpack/manifests, source content identity, converter version, engine schema version, and cache format version. Its binary layout and validation are specified in the [Binary Format Reference](2026-07-24-de2-base-fullload-binary-format.md).

## 3. NyanStore adapter boundary

`NyanStore` is an openage-owned abstraction. The first concrete reader is `MmapNyanStore`; `TextNyanStore` remains the reference semantic path and `OverlayNyanStore` is a later per-player overlay.

The existing libnyan public surface remains unchanged, including `Database`, `View`, `Object`, `ObjectState`, and `Transaction` APIs used by openage. Stage 1 does not rewrite parser, lexer, resolver, transaction semantics, AST, or `Database::load` internals. The adapter may wrap existing public objects and may expose cache-backed data through a thin consumer shim, but it must not claim that packed records are native libnyan objects.

Stage 1 is fixture-only and is not connected to formal game startup, tutorial runtime, or full `de2_base`. It uses approximately 30–50 objects and completes all tests in seconds. The exact scope and exclusions are listed in the [Test Matrix](2026-07-24-de2-base-fullload-test-matrix.md).

## 4. mmap base and overlay structure

The base store owns the file descriptor and one read-only mapping. Views and `string_view`s are valid only while that store generation is open. Close invalidates all views, clears lazy materialization, and prevents further access through structured errors; debug builds assert on stale access.

The first version forbids live replacement or unmapping while any `ObjectView`, `string_view`, `ObjectHandle`, or materialized object may still be alive. A new cache is loaded only by a subsequent process start. Every internal handle carries the cache generation identity; debug builds assert on stale handles and release builds return structured errors without dereferencing old mappings.

`OverlayNyanStore` is a later per-player diff over one immutable shared base. It stores only changed states, resolves overlay first and base second, and invalidates affected materializations after commit. `ObjectId` is generation-scoped and never crosses a persistence boundary; save files, replays, network packets, scenario data, and user-facing serialization use FQON strings only. The detailed lifecycle and overlay rules are summarized by invariants below and tested in the [Test Matrix](2026-07-24-de2-base-fullload-test-matrix.md).

## 5. Correctness gate

The mandatory order is:

1. **CLI/engine exception guard** — catch loader and CLI exceptions and return structured exit status; no terminate/core dump.
2. **`validate-nyan`** — enumerate invalid references and other corpus errors without writing cache output.
3. **Converter correctness fixes** — fix generated-source defects in converter code, never generated `.nyan` files.
4. **Full engine + `de2_base` validator exit 0** — this is the cache gate.
5. **Fixture-only cache compiler/reader** — implement and test the packed format without formal startup integration.
6. **NyanStore adapter integration** — connect cache-backed reads at the approved adapter boundary, with lifecycle and handle checks.
7. **Full `de2_base` cache + performance gates** — prepare and measure the real closure; enforce cold/warm startup budgets.

No cache is considered valid merely because it can be written. Validator exit 0, source/manifest/version identity, binary ABI validation, deterministic serialization, complete closure collection, full value-variant support, capacity census, and atomic publication are all required. The detailed error categories and corruption cases are in the [Test Matrix](2026-07-24-de2-base-fullload-test-matrix.md). Unsupported values in a validated full corpus block publication; `Dict-as-diagnostic` is permitted only in the Stage 1 fixture prototype.

## 6. Acceptance criteria

| ID | Criterion | Gate |
|---|---|---|
| AC-1 | Engine/CLI exits cleanly with structured status for inputs that previously crashed or aborted | Required |
| AC-2 | `validate-nyan --modpacks de2_base` exits 0 after converter fixes | Required for cache |
| AC-3 | Validator never writes a cache; `--json` writes only a report | Required |
| AC-4 | `prepare-nyan-cache` publishes one closure cache only after validator exit 0 | Required |
| AC-5 | Failed preparation preserves the previous valid cache byte-identically | Required |
| AC-6 | Full tutorial startup reaches `load_scenario` in ≤10 s cold and ≤5 s warm | Stage 2 gate |
| AC-7 | Warm startup reads no `.nyan` text; it uses cache mmap/read-only access | Stage 2 gate |
| AC-8 | Each source `.nyan` file is parsed at most once per preparation run | Required |
| AC-9 | Source, manifest, converter, schema, format, or closure identity changes invalidate the cache | Required |
| AC-10 | Missing/invalid cache causes fail-fast with a prepare hint and no implicit rebuild | Required |
| AC-11 | No `tutorial_assets` directory or FQON rewrite exists | Required |
| AC-12 | Stage 1 fixture contains approximately 30–50 objects and all tests complete in seconds | Required |
| OBS-1 | Mission 1 materialized count, RSS, page faults, and lookup counts are recorded, not initially gated | Observation |

## 7. References and test ownership

The [Binary Format Reference](2026-07-24-de2-base-fullload-binary-format.md) is the single source of truth for the 4096-byte header, section directory, packed records, alignment, endianness, wyhash policy, checksums/digests, ABI checks, canonical serialization, and generation-safe handles.

The [Test Matrix](2026-07-24-de2-base-fullload-test-matrix.md) is the single source of truth for fixture composition, parity, collision, corruption, invalid corpus, exception safety, invalidation, lifecycle, regression, and performance tests. It also defines the Stage 1 scope gate.

## 8. Invariants — non-negotiable constraints

Any implementation change that violates one of these rules reopens the design.

1. **No tutorial assets**: `assets/converted/tutorial_assets/` and `tutorial_assets.*` FQONs do not exist.
2. **One closure file**: one dependency closure produces one cache file, not per-modpack files.
3. **Manifest-complete closure**: every `.nyan` module declared by every manifest in the resolved dependency closure, and every declared FQON, is present in the module/FQON index; lazy materialization does not reduce index coverage.
4. **Validator gate**: only validator exit 0 may publish; failure preserves the previous cache.
5. **Hash correctness**: vendored wyhash, frozen seed/byte-order policy, and mandatory full-string equality; never `std::hash`.
6. **Binary ABI safety**: all mandatory reader/writer checks return structured errors; no abort, terminate, or unsafe dereference.
7. **Atomic publication**: temporary file + fsync + rename + parent fsync; never in-place publication.
8. **Generation-scoped ObjectId**: object IDs are valid only within one open cache generation.
9. **FQON persistence**: persistent references use FQON strings, never ObjectId.
10. **No silent rebuild**: game fails fast on missing/invalid cache and gives the prepare command.
11. **Exception safety**: engine, CLI, and loader convert unexpected input failures to structured exit statuses without core dumps.
12. **Stage 1 isolation**: fixture prototype does not modify libnyan parser/resolver/transaction/lexer/AST, formal startup, or tutorial runtime.
13. **Single-threaded Stage 1**: no parallel parsing and no background materialization warming.
14. **FQON truth**: FQON bytes are preserved exactly; strings are stored once and referenced by offset/length.
15. **Header authority**: filename is a hint; header identity and integrity fields are authoritative.
16. **Format scope**: cache serialization uses hand-rolled packed POD, not msgpack/SQLite/capnproto/protobuf.
17. **Deterministic build**: identical source bytes, manifests, versions, and configuration produce byte-identical caches for every build, not only test mode. Objects, modules, members, strings, sections, and all padding/reserved bytes use canonical ordering and zero values where reserved; the creation timestamp field is always zero and build time is external metadata only.
18. **Complete digest coverage**: the full 32-byte closure digest and full 32-byte whole-file BLAKE3 digest are stored and validated. The whole-file digest covers the canonical section directory and every section payload; no truncated digest is authoritative.
19. **No live reload**: a cache is never replaced or unmapped while any `ObjectView`, `string_view`, `ObjectHandle`, or materialized object may remain alive. A new cache is loaded only on a later process start.
20. **Generation-safe handles**: every internal handle carries cache-generation identity; debug builds assert on stale handles and release builds return structured errors without accessing an old mapping.
21. **Full value coverage**: `Dict-as-diagnostic` is a Stage 1 fixture-only limitation. Before full `de2_base` publication, every value/type variant in the validated corpus has a lossless binary representation; unsupported values block publication.
22. **Capacity census**: the full-corpus gate measures maximum parent/member/string/collection sizes and all module/object/value totals, and rejects any count or offset that exceeds its binary field capacity, including u32 index/offset limits.

## 9. Revision 3 migration map

| Revision 2 content | Revision 3 destination |
|---|---|
| §0 revision summary | Main §0 |
| §1 background, §2 non-goals | Main §1 |
| §4.1 closure and §4.4 flows | Main §2 |
| §4.2 adapter and §4.3 mapping | Main §3 |
| §5.1–§5.8 format and ABI | Binary Format Reference |
| §5.9 size estimate | Binary Format Reference, implementation notes |
| §6.3–§6.5 lifecycle/materialization/overlay | Main §4; binary handle details in Binary Format Reference |
| §7–§8 validator and converter workflow | Main §5; executable cases in Test Matrix |
| §9 implementation order | Main §5 |
| §10 tests | Test Matrix |
| §11 Stage 1 scope | Main §3; full matrix in Test Matrix |
| §12–§13 risks | Main constraints and Test Matrix regression notes |
| Invariants 1–15 | Main §8 |
| New invariants 16–18 | Main §8; serialization/lifetime details in Binary Format Reference |
