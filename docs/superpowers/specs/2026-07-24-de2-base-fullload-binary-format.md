# de2_base Full-Load — Binary Format Reference

- Date: 2026-07-24 (revision 3)
- Parent design: [main design spec](2026-07-24-de2-base-fullload-design.md)
- Scope: canonical binary contract for `*.nyancache`

This document is the single source of truth for the cache bytes. It intentionally does not repeat goals, implementation stages, or the complete test matrix; see the [Test Matrix](2026-07-24-de2-base-fullload-test-matrix.md).

## 1. Identity and file layout

The filename is `engine_de2_base-<closure_id>.nyancache`. `closure_id` is a seven-character base32 digest of canonical closure metadata. The full closure identity is stored in the header and is authoritative over the filename.

The file begins with a 4096-byte page-aligned header. All offsets are absolute file offsets. All multi-byte values are little-endian. Readers validate offsets and lengths before dereferencing any mapped address.

## 2. Header

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic `NYNC` (`0x4E594E43` LE) |
| 4 | 2 | format version (`0x0001`) |
| 6 | 2 | endian marker (`0x4243`) |
| 8 | 4 | hash algorithm id (`1` = vendored wyhash) |
| 12 | 4 | hash algorithm version (`1`) |
| 16 | 8 | creation timestamp, informational only |
| 24 | 4 | converter version string offset |
| 28 | 4 | engine schema version string offset |
| 32 | 32 | source Merkle root |
| 64 | 4 | modpack count |
| 68 | 4 | module count |
| 72 | 8 | total objects |
| 80 | 8 | total members |
| 88 | 8 | total values |
| 96 | 8 | section directory offset |
| 104 | 8 | section directory size |
| 112 | 8 | string table offset |
| 120 | 8 | string table size |
| 128 | 256 | closure identity record |
| 384 | 8 | whole-file digest prefix |
| 392 | 3700 | reserved, all zero |
| 4092 | 4 | header CRC32C over bytes 0–4091 |

The header has no implicit native struct layout. Writers serialize fields explicitly. Unknown future sections are skipped only when their bounds and checksums remain valid; unknown format versions are rejected.

## 3. Closure identity

The fixed closure record contains canonical ordered modpack names, a digest of those names, manifest identity, converter/schema versions, cache format version, and reserved zero bytes. The `closure_id` is computed from canonical metadata with BLAKE3-256 and truncated to seven base32 characters. Header identity is compared to the requested closure during load. A renamed file cannot make a stale cache valid.

## 4. Section directory and sections

The section directory contains fixed-width entries:

```text
name_hash  u64
offset     u64
size       u64
crc32c     u32
flags      u32
```

Each section is within the file, does not overlap the header or another section, and begins on a 4-byte boundary. `STRINGS`, `MODULES`, `OBJECTS`, and `MEMBERS` begin on 8-byte boundaries. Section order is canonical and is also the order used for the whole-file digest.

| Section | Contents | Canonical order |
|---|---|---|
| `STRINGS` | packed UTF-8 strings | first occurrence in canonical record traversal; equal strings deduplicated |
| `MODULES` | source module records | canonical module path |
| `FQON_INDEX` | hash/string/object entries | FQON hash, then full FQON bytes |
| `FQON_PROBE` | open-addressing table | slot index |
| `DEPS` | import and overflow-parent edges | importer, importee |
| `TYPES` | type definitions | canonical type FQON |
| `OBJECTS` | object records | canonical FQON |
| `MEMBERS` | object members | object FQON, member name bytes |
| `VALUES` | tagged values | owning object/member traversal order |
| `DIAG` | source diagnostics | object, source location |

Every reserved field, alignment gap, and padding byte is written as zero and validated as zero unless a future format flag explicitly assigns it.

## 5. Packed records

Records are written field-by-field using explicit `write_u8`, `write_u16`, `write_u32`, `write_u64`, and length-prefixed byte helpers. No record is serialized through `reinterpret_cast` or native ABI layout.

```text
ModuleRecord:
  u32 path_str
  u32 path_len
  u64 source_digest_part_0
  u32 source_digest_part_1
  u32 source_digest_part_2
  u32 source_digest_part_3
  u32 parent_module
  u32 first_object
  u32 object_count

FqonEntry:
  u64 fqon_hash
  u32 string_offset
  u32 string_length
  u32 object_index

ObjectRecord:
  u32 fqon_string
  u16 fqon_length
  u16 parent_count
  u32 parent_index[8]
  u32 first_member
  u16 member_count
  u16 type_index
  u32 diagnostic_index

MemberRecord:
  u32 name_string
  u16 name_length
  u16 type_index
  u32 first_value
  u32 value_count
  u8 modifier_mask
  u8 flags
  u16 padding_zero

ValueRecord:
  u8 tag
  u8 flags
  u16 padding_zero
  u32 payload
```

Values use tags for Int, Float, Text, Object, Set, OrderedSet, Dict, File, and None. Object references and collection entries refer to object indices that are valid only in the current cache generation. Dict is currently represented as an explicit diagnostic/unhandled value rather than causing a crash.

## 6. Hash and lookup policy

The reader and writer vendor one pinned upstream wyhash implementation. The algorithm id/version and pinned source revision are recorded in the tree and header. The seed is frozen as `0xA5A5A5A55A5A5A5AULL`. Input is the exact raw UTF-8 FQON byte sequence; no normalization, case folding, or string transformation is permitted.

The probe table is power-of-two sized with load factor ≤0.6. Lookup hashes first, probes linearly, then compares the candidate's full string bytes and length. Hash equality alone never establishes identity. `std::hash<std::string>` and platform-selected hashes are forbidden. A fake-hash test must prove that two distinct FQONs sharing a hash still resolve correctly.

## 7. Offset, alignment, and endianness rules

- All file offsets are absolute and unsigned.
- Section alignment is at least 4 bytes; the four large record sections are 8-byte aligned.
- Record padding is explicit and zero-filled.
- `count * element_size`, `offset + size`, and all nested string/value ranges use checked arithmetic before access.
- String offsets and lengths must remain inside `STRINGS`; individual strings are capped at 64 KiB.
- The endian marker and canonical byte encoding are checked at load. The reader does not silently reserialize or rehash a cache for a different host.

## 8. Checksums and digest

Each section has CRC32C in its directory entry. The header has CRC32C over all header bytes except its CRC field. The whole-file digest is BLAKE3 over the concatenation of sections in canonical directory order, excluding the header; the header stores the specified digest prefix. A mismatch returns a structured error and marks the store unusable.

## 9. ABI validation contract

The writer and reader explicitly implement these checks, in order:

1. Magic.
2. Supported format version.
3. Endian marker.
4. Hash algorithm id/version.
5. Header CRC32C.
6. Section bounds and non-overlap.
7. Section alignment.
8. Record alignment and zero padding.
9. Checked integer multiplication/addition.
10. String bounds and maximum lengths.
11. Object/member count bounds.
12. Truncation (`offset + size <= file_size`).
13. Per-section CRC32C.
14. Whole-file digest.
15. Atomic publication is enforced by the writer: `.tmp`, `fsync`, `rename`, parent-directory `fsync`.

Failures return structured variants such as `HeaderError`, `FormatVersionError`, `EndianError`, `HashAlgorithmError`, `ChecksumError`, `BoundsError`, `OverlapError`, `AlignmentError`, `OverflowError`, `StringBoundsError`, `CountError`, `TruncationError`, `CorruptSectionError`, or `WholeFileDigestError`. No reader path aborts, terminates, segfaults, or dereferences an unchecked pointer.

## 10. Deterministic serialization

For identical source bytes, ordered manifests, converter version, engine schema, cache format, and configuration, the complete cache is byte-identical. Determinism is enforced by:

- canonical module, object, type, member, dependency, diagnostic, and section ordering;
- FQON bytes and member names compared by raw UTF-8 bytes;
- deterministic first-use string table assignment after canonical traversal;
- fixed-width fields and explicit little-endian serialization;
- zero creation-time or other nondeterministic metadata in the reproducibility mode used by tests;
- all reserved and padding bytes written as zero;
- no pointer values, process IDs, hash-map iteration order, thread scheduling, or filesystem traversal order in output.

A deterministic-build test compares two independently built files byte-for-byte and also verifies that changing any declared input identity changes the closure identity.

## 11. mmap lifecycle and handle safety

`MmapNyanStore` owns one file descriptor and one read-only mapping. Mapped views, `string_view`s, and handles carry a generation identity and are valid only while that exact store generation remains open.

The first implementation forbids live reload: it must not close/unmap or replace a cache while any `ObjectView`, `string_view`, `ObjectHandle`, or materialized object can remain alive. New cache loading is a next-process-start operation. Closing a store clears lazy materialization and causes subsequent access to return `StoreClosed`; debug builds assert at every accessor.

ObjectIds are generation-scoped. Persistent surfaces use FQON strings only. Debug builds assert when a handle's generation differs from the store generation. Release builds return a structured stale-handle error before any index or pointer access. Overlay destruction precedes base close, and consumer-owned materialized objects are destroyed only after base-dependent views are gone.

## 12. Reference implementation notes

A Stage 1 fixture should exercise every record and accessor with approximately 30–50 objects. Full `de2_base` size and performance are Stage 2 measurements, not format assumptions. The [Test Matrix](2026-07-24-de2-base-fullload-test-matrix.md) defines the required fixture and corruption coverage.
