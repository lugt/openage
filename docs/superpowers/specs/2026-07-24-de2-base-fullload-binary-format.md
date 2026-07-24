# de2_base Full-Load — Binary Format Reference

- Date: 2026-07-24 (revision 3.1)
- Parent design: [main design spec](2026-07-24-de2-base-fullload-design.md)
- Scope: canonical binary contract for `*.nyancache`

This document is the single source of truth for cache bytes. It intentionally does not repeat goals or the complete test matrix; see the [Test Matrix](2026-07-24-de2-base-fullload-test-matrix.md).

## 1. Identity and file layout

The filename is `engine_de2_base-<closure_id>.nyancache`; `closure_id` is at least 16 Base32 characters derived from the full closure digest. The header stores the complete 32-byte closure digest and is authoritative over the filename.

The cache contains every `.nyan` module declared by every manifest in the resolved dependency closure, not only Mission 1 or import-reachable modules. All declared modules and FQONs enter the index; object state remains lazy-materialized.

The file begins with a 4096-byte page-aligned header. All offsets are absolute file offsets and all multi-byte values are little-endian. Readers validate offsets and lengths before dereferencing mapped addresses.

## 2. Header

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | magic `NYNC` (`0x4E594E43` LE) |
| 4 | 2 | format version (`0x0001`) |
| 6 | 2 | endian marker (`0x4243`) |
| 8 | 4 | hash algorithm id (`1` = vendored wyhash) |
| 12 | 4 | hash algorithm version (`1`) |
| 16 | 8 | reserved zero; build time is external metadata only |
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
| 128 | 256 | closure identity record, including full 32-byte closure digest |
| 384 | 32 | whole-file BLAKE3 digest |
| 416 | 3676 | reserved, all zero |
| 4092 | 4 | header CRC32C over bytes 0–4091 |

Writers serialize fields explicitly, never through native struct layout. Offset 16 is zero for every fixture and formal build. Unknown sections are skippable only after bounds/checksum validation; unknown format versions are rejected.

## 3. Closure identity and sections

The fixed closure record contains canonical ordered modpack names, manifest identity, converter/schema versions, cache format version, and the full 32-byte closure digest. The filename is only a hint.

The section directory contains fixed-width entries:

```text
name_hash  u64
offset     u64
size       u64
crc32c     u32
flags      u32
```

Each section is within the file, non-overlapping, and aligned to at least 4 bytes; `STRINGS`, `MODULES`, `OBJECTS`, and `MEMBERS` are 8-byte aligned. Canonical directory bytes—including entry order, offsets, sizes, CRCs, and flags—are included in the complete whole-file digest together with every section payload.

| Section | Contents | Canonical order |
|---|---|---|
| `STRINGS` | packed UTF-8 strings | canonical record traversal; equal strings deduplicated |
| `MODULES` | source module records | canonical module path |
| `FQON_INDEX` | hash/string/object entries | FQON hash, then full FQON bytes |
| `FQON_PROBE` | open-addressing table | slot index |
| `DEPS` | import and overflow-parent edges | importer, importee |
| `TYPES` | type definitions | canonical type FQON |
| `OBJECTS` | object records | canonical FQON |
| `MEMBERS` | object members | object FQON, member bytes |
| `VALUES` | tagged values | canonical owner/member traversal |
| `DIAG` | source diagnostics | object, source location |

Reserved fields, alignment gaps, and padding bytes are zero and validated as zero.

## 4. Packed records and values

Records are written field-by-field with explicit `write_u8`, `write_u16`, `write_u32`, `write_u64`, and length-prefixed byte helpers. No `reinterpret_cast` serialization is permitted.

```text
ModuleRecord: u32 path_str, u32 path_len, source digest fields,
              u32 parent_module, u32 first_object, u32 object_count
FqonEntry:    u64 fqon_hash, u32 string_offset, u32 string_length,
              u32 object_index
ObjectRecord: u32 fqon_string, u16 fqon_length, u16 parent_count,
              u32 parent_index[8], u32 first_member, u16 member_count,
              u16 type_index, u32 diagnostic_index
MemberRecord: u32 name_string, u16 name_length, u16 type_index,
              u32 first_value, u32 value_count, u8 modifier_mask,
              u8 flags, u16 padding_zero
ValueRecord:  u8 tag, u8 flags, u16 padding_zero, u32 payload
```

Tags cover Int, Float, Text, Object, Set, OrderedSet, Dict, File, and None. Object and collection indices are generation-local. `Dict-as-diagnostic` is permitted only for the Stage 1 fixture; before full `de2_base` publication every encountered type/value variant must have a lossless binary representation, otherwise publication is blocked.

## 5. Hash, offset, alignment, and endianness

The vendored wyhash uses frozen seed `0xA5A5A5A55A5A5A5AULL`, exact raw UTF-8 FQON bytes, and recorded algorithm id/version. `std::hash` and platform-selected algorithms are forbidden. Lookup always performs full-string equality after hash probing.

Offsets are absolute. Arithmetic for `count * size`, `offset + size`, nested ranges, and all indices is checked before access. Strings remain inside `STRINGS` and are capped at 64 KiB. The capacity census must prove every fixed-width field fits, including u32 indices/offsets and u16/u32 counts.

## 6. Checksums, digest, and ABI validation

Each section has CRC32C. The header has CRC32C over bytes 0–4091. The whole-file digest is the complete 32-byte BLAKE3 over canonical section-directory bytes followed by every section payload in canonical order, excluding the header. Directory changes are therefore integrity-protected even when payload bytes are unchanged.

The reader explicitly checks, in order: magic; format version; endian marker; hash id/version; header CRC; section bounds/non-overlap; alignment; zero padding; checked arithmetic; string bounds; count bounds; truncation; per-section CRCs; and complete directory-plus-payload digest. The writer enforces `.tmp` + `fsync` + `rename` + parent-directory `fsync`.

Failures return structured `NyanCacheError` variants (`HeaderError`, `BoundsError`, `OverflowError`, `CorruptSectionError`, `WholeFileDigestError`, and related variants). No reader path aborts, terminates, segfaults, or dereferences unchecked data.

## 7. Deterministic serialization and capacity census

Identical source bytes, manifests, versions, and configuration produce byte-identical caches for every build. Canonical ordering applies to modules, objects, types, members, dependencies, diagnostics, strings, sections, and directory entries. All reserved/padding bytes and the timestamp field are zero. Build time is external log metadata only. Pointer values, process IDs, timestamps, random values, hash-map iteration order, thread scheduling, and filesystem traversal order never enter cache bytes.

Before full publication, the validator records maximum parent count, member count, string length, collection size, module/object/member/value totals, each section size and largest index/offset, and all field-width limits. Any capacity overflow or unsupported full-corpus value is a hard error. Stage 1 may use a small fixture and Dict diagnostics; full `de2_base` may not use that fallback.

## 8. mmap lifecycle and generation-safe handles

`MmapNyanStore` owns one descriptor and one read-only mapping. Views, `string_view`s, materialized objects, and handles carry the cache generation identity and are valid only while that exact generation remains open.

Live replacement/unmap is forbidden while any such object may remain alive; a new cache loads only on a later process start. Close clears lazy materialization and causes `StoreClosed`; debug builds assert and release builds return structured errors. Stale-generation handles assert in debug builds and return structured errors in release builds before any old index/mapping access. Persistent surfaces use FQON strings, never ObjectId.

## 9. Reference implementation notes

A Stage 1 fixture exercises every record/accessor with approximately 30–50 objects. Full `de2_base` size/performance are Stage 2 measurements. The [Test Matrix](2026-07-24-de2-base-fullload-test-matrix.md) defines fixture, capacity, and corruption coverage.
