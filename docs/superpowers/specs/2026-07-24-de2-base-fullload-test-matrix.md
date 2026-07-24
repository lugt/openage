# de2_base Full-Load — Test Matrix

- Date: 2026-07-24 (revision 3)
- Parent design: [main design spec](2026-07-24-de2-base-fullload-design.md)
- Binary contract: [Binary Format Reference](2026-07-24-de2-base-fullload-binary-format.md)

This document is the executable verification contract. It does not redefine architecture or binary layouts.

## 1. Stage 1 scope gate

Stage 1 uses an open-source fixture of approximately 30–50 objects. All Stage 1 tests complete in seconds. Stage 1:

- does not modify `Game::load_data` or formal game startup;
- does not generate a full `de2_base` cache;
- does not modify libnyan parser, resolver, transaction, lexer, or AST;
- does not connect to tutorial runtime;
- does not implement multi-cache composition;
- does not start background workers or prewarm materialized objects.

The fixture covers multiple modules, imports, cross-module FQON references, single and multilevel inheritance, member overrides, primitive values, object references, Set, OrderedSet, Dict-as-diagnostic, File values, and source diagnostics.

## 2. Fixture and parity

| Test | Input | Expected result | Stage |
|---|---|---|---|
| `fixture_object_count` | canonical fixture | 30–50 objects; deterministic count | 1 |
| `fixture_round_trip` | fixture → writer → mmap reader | every accessor returns expected value | 1 |
| `text_mmap_parity` | same fixture through TextStore and MmapStore | every FQON, parent, member, value, and diagnostic matches | 1 |
| `multi_module_resolution` | cross-module import/reference | same target FQON in both stores | 1 |
| `inheritance_order_parity` | single and multilevel inheritance | member merge and override order matches TextStore | 1 |
| `collection_parity` | Set and OrderedSet values | membership/order semantics match | 1 |
| `dict_unhandled_is_safe` | Dict value | structured diagnostic; no crash | 1 |
| `fixture_runtime_seconds` | complete Stage 1 suite | finishes in seconds | 1 |

## 3. Hash collision and string identity

| Test | Mutation | Expected result |
|---|---|---|
| `fake_hash_collision` | inject hash policy mapping two FQONs to one hash/slot | both resolve by full-string equality |
| `same_prefix_distinct_fqon` | equal prefix, different suffix | no false hit |
| `fqon_case_sensitive` | case-only difference | remains distinct |
| `fqon_no_normalization` | distinct raw UTF-8 byte forms | remains byte-distinct |
| `unknown_hash_algorithm` | alter header id/version | structured `HashAlgorithmError` |

## 4. Corrupted cache matrix

Each corruption case starts from a known-valid fixture cache and changes one property. Every case must return the documented structured error without abort, terminate, core dump, out-of-bounds read, or stale mapping access.

| Binary check | Corruption | Expected class |
|---:|---|---|
| 1 | bad magic | `HeaderError` |
| 2 | unsupported format version | `FormatVersionError` |
| 3 | bad endian marker | `EndianError` |
| 4 | unknown hash id/version | `HashAlgorithmError` |
| 5 | flip header byte without updating CRC | `ChecksumError` |
| 6 | section outside file | `BoundsError` |
| 6 | overlapping sections | `OverlapError` |
| 7 | misaligned section | `AlignmentError` |
| 8 | nonzero padding/reserved byte | `AlignmentError` or explicit padding error |
| 9 | count × size overflow | `OverflowError` |
| 10 | string offset/length outside `STRINGS` | `StringBoundsError` |
| 10 | string exceeds maximum length | `StringBoundsError` |
| 11 | object/member count exceeds totals | `CountError` |
| 12 | truncate at every section boundary | `TruncationError` |
| 13 | corrupt each section independently | `CorruptSectionError` |
| 14 | valid section CRCs but wrong whole digest | `WholeFileDigestError` |
| 15 | fail before rename/fsync | prior cache remains byte-identical |

Additional cases:

- invalid section-directory offset and size;
- unknown section with valid bounds/checksum is skipped;
- duplicate mandatory section is rejected;
- missing mandatory section is rejected;
- object/member/value index points exactly at and beyond section end;
- malformed probe table, full probe loop, and invalid empty sentinel;
- object parent cycle encoded in cache;
- generation id mismatch before index access.

## 5. Invalid nyan corpus and validator

The validator runs against the existing text path and never writes cache output. Its report includes `{category, file, line, column, object, missing_name, import_chain}` and continues after recoverable errors.

| Test | Corpus defect | Expected result |
|---|---|---|
| `validator_clean_passes` | known-good minimal modpack | exit 0 |
| `validator_missing_name` | absent referenced FQON | `NameError` with location/chain |
| `validator_missing_member` | absent member | `MemberNotFoundError` |
| `validator_member_type` | incompatible value/member type | `MemberTypeError` |
| `validator_missing_import` | unresolved import | `MissingImport` |
| `validator_missing_manifest` | no manifest | `ManifestMissing` |
| `validator_fqon_conflict` | conflicting declaration | `FqonConflict` |
| `validator_duplicate_file` | same source parsed twice | `DuplicateFile` |
| `validator_inheritance_cycle` | parent cycle | `CycleInInheritance` |
| `validator_duplicate_fqon_closure` | duplicate FQON across closure | both source locations |
| `validator_continue_on_error` | at least three independent defects | all are reported in one run |
| `validator_json_only` | `--json report.json` | report written; no cache file |
| `validator_strict_warning` | warning under `--strict` | exit 2 |

The full `engine + de2_base` corpus must exit 0 before a full cache can be prepared.

## 6. Exception handling and process safety

| Test | Trigger | Expected result |
|---|---|---|
| `exception_guard_no_core_dump` | malformed/truncated nyan during load | nonzero structured exit; no core/terminate text |
| `exception_guard_std_exception` | injected `std::exception` at CLI boundary | structured exit |
| `exception_guard_unknown_exception` | injected unknown exception | structured generic exit |
| `cache_reader_corruption_no_crash` | each §4 corruption | error return; process remains controlled |
| `store_closed_access` | access view after close | debug assertion; release `StoreClosed` |
| `stale_generation_handle` | old handle against new/different generation | debug assertion; release structured stale-handle error |

## 7. Cache preparation and invalidation

| Test | Change/condition | Expected result |
|---|---|---|
| `prepare_skips_failed_validation` | validator exits nonzero | no publication; previous cache unchanged |
| `atomic_write_previous_intact` | failure during temporary write | old mtime/content unchanged |
| `missing_cache_fails_fast` | no matching cache | nonzero exit plus prepare hint |
| `invalid_cache_fails_fast` | ABI/integrity failure | nonzero exit plus prepare hint; no rebuild |
| `source_bytes_invalidate` | source content changes | stale cache rejected |
| `manifest_invalidate` | manifest bytes/order changes | stale cache rejected |
| `converter_version_invalidate` | converter version changes | stale cache rejected |
| `schema_version_invalidate` | engine schema changes | stale cache rejected |
| `format_version_invalidate` | format changes | stale cache rejected |
| `closure_identity_invalidate` | root/dependency closure changes | stale cache rejected |
| `filename_is_hint` | rename cache without header change | selection cannot override header identity |
| `no_live_reload` | replacement requested with live views/handles/materialized objects | replacement refused; old mapping stays live |
| `next_process_loads_new_cache` | publish new cache, restart process | new generation loads successfully |

## 8. Determinism

| Test | Procedure | Expected result |
|---|---|---|
| `deterministic_byte_identity` | build twice from identical declared inputs | files are byte-identical |
| `filesystem_order_independent` | enumerate source files in different order | identical output |
| `hash_map_order_independent` | vary insertion order | identical output |
| `padding_is_zero` | inspect all reserved/alignment bytes | all zero |
| `canonical_sections` | inspect section directory | canonical fixed ordering |
| `canonical_records` | inspect module/object/member/string records | canonical ordering and stable offsets |
| `declared_input_changes_identity` | change one source/manifest/version/config item | closure identity changes |

Timestamps or environment-specific metadata must not break the deterministic test mode.

## 9. mmap, materialization, and overlay lifecycle

| Test | Scenario | Expected result | Stage |
|---|---|---|---|
| `view_valid_while_open` | access within generation lifetime | succeeds | 1 |
| `view_invalid_after_close` | close then access | assertion/error before dereference | 1 |
| `string_view_generation_bound` | retain string view across close | assertion/error | 1 |
| `resolve_materializes_once` | resolve same FQON repeatedly | one materialization per generation | 2 |
| `concurrent_read_only` | concurrent reads after validation | same immutable values | 2 |
| `overlay_is_per_player` | two player overlays mutate same FQON differently | independent results; one shared base | 2 |
| `overlay_commit_invalidates_dependents` | modify parent/member | affected materializations refresh | 2 |
| `object_id_not_persisted` | save/replay/network serializers | use FQON; ObjectId rejected | 2 |
| `destruction_order` | destroy overlay/consumers/base | no dangling base access | 2 |

## 10. Performance gates and observations

Performance tests run only after full `engine + de2_base` validation and cache preparation. In sandboxes without converted data they are skipped, not reported as passed; user-side acceptance must run them.

| Test | Measurement | Gate |
|---|---|---|
| `cold_startup_under_10s` | process start to `load_scenario` return | ≤10 s |
| `warm_startup_under_5s` | second run to same marker | ≤5 s |
| `warm_no_text_io` | trace warm run | zero `.nyan` opens |
| `parse_once_per_file` | preparation counters | one parse attempt per source file |
| `object_state_unique` | materialization counters | no duplicate creation for same FQON/generation |
| `mission1_observations` | materialized count, RSS, minor/major faults, lookups | recorded only (OBS-1) |

## 11. Regression gates

- `no_tutorial_assets`: no directory or tracked `tutorial_assets.*` FQON.
- `game_never_prepares_cache`: formal startup cannot invoke writer/preparer.
- `stage1_no_startup_change`: fixture work leaves formal startup and tutorial runtime unchanged.
- `stage1_no_libnyan_rewrite`: no parser/resolver/transaction/lexer/AST modifications.
- `stage1_no_full_de2_cache`: Stage 1 CI cannot generate the full closure.
- `stage1_seconds_only`: fixture suite remains bounded and completes in seconds.
- `full_string_equality_required`: mutation removing equality check makes collision test fail.
- `persistent_reference_lint`: save/replay/network/scenario persistence cannot serialize generation-local ObjectId.
- `failed_prepare_preserves_cache`: all validator, serialization, checksum, disk, and rename failures preserve the last valid artifact.

## 12. Acceptance mapping

| Acceptance criterion | Primary tests |
|---|---|
| AC-1 | §6 exception/process safety |
| AC-2–AC-3 | §5 validator corpus/outputs |
| AC-4–AC-5 | §7 prepare/atomic publication |
| AC-6–AC-7 | §10 startup and I/O gates |
| AC-8 | `parse_once_per_file` |
| AC-9 | §7 identity invalidation |
| AC-10 | missing/invalid fail-fast tests |
| AC-11 | `no_tutorial_assets` |
| AC-12 | §1–§2 fixture scope and runtime |
| OBS-1 | `mission1_observations` |
| Invariant 16 | §8 deterministic build |
| Invariant 17 | `no_live_reload`, next-process test |
| Invariant 18 | stale-generation and lifecycle tests |
