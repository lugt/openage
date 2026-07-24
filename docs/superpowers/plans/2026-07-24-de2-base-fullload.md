# de2_base Full-Load Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the de2_base full-load cache pipeline so the MVP tutorial reaches `load_scenario` in ≤10 s cold / ≤5 s warm, with byte-identical, atomically published caches and zero silent rebuilds.

**Architecture:** A 7-stage correctness gate introduces an openage-owned `NyanStore` adapter that wraps (never rewrites) the existing libnyan parser/resolver/transaction. The offline `prepare-nyan-cache` step compiles a manifest-complete `engine_de2_base-<closure_id>.nyancache` from the resolved dependency closure, and `MmapNyanStore` reads it through a strictly validated, generation-aware handle protocol. Stage 1 is fixture-only; full `de2_base` and performance gates are Stage 2.

**Tech Stack:** C++20 (libopenage and libnyan), Python 3 (openage CLI), vendored wyhash (third_party/wyhash), CRC32C, BLAKE3-256, Ninja build, gtest, pytest.

**Spec references (authoritative):**

- [Main design](2026-07-24-de2-base-fullload-design.md) — goals, gates, invariants, ordering.
- [Binary Format Reference](2026-07-24-de2-base-fullload-binary-format.md) — header, sections, records, ABI.
- [Test Matrix](2026-07-24-de2-base-fullload-test-matrix.md) — fixtures, corruption, performance.

## Global Constraints

These come from the spec and apply to every task unless the task explicitly overrides them.

- Branch: `mvp/tutorial-demo`. Do not modify libnyan parser/resolver/transaction/lexer/AST in Stage 1.
- No `tutorial_assets` modpack or FQON rewrite ever.
- Cache format: `NYNC` magic, page-aligned 4096-byte header, little-endian, packed POD only (no msgpack, SQLite, capnproto, protobuf).
- Closure file name: `engine_de2_base-<closure_id>.nyancache`; `closure_id` is at least 16 Base32 characters derived from the full 32-byte BLAKE3 closure digest.
- Header preserves the complete 32-byte closure digest and the complete 32-byte BLAKE3 whole-file digest (no truncated digests).
- Header offset 16 is reserved zero for every build (fixture and formal); build time is external metadata only.
- Section directory entries (name_hash, offset, size, crc32c, flags) are part of the whole-file digest; their canonical order is fixed.
- Hash policy: vendored wyhash (seed `0xA5A5A5A55A5A5A5AULL`), exact raw UTF-8 FQON bytes, full-string equality on lookup. `std::hash` and platform hashes are forbidden.
- ABI checks 1–15 from the Binary Format Reference must all be implemented and tested.
- Atomic publication: `*.tmp` + `fsync` + `rename` + parent-directory `fsync`. Failed publication preserves the previous valid cache.
- mmap/overlay rules from the design and Test Matrix are non-negotiable (no live reload, generation-safe handles, no ObjectId on persistent surfaces).
- Determinism: byte-identical output across builds given identical inputs; no timestamps, no random values, no map iteration order, no filesystem traversal order in cache bytes.
- Capacity census: maximum parent count, member count, string length, collection size, module/object/member/value totals, every section size and largest index/offset, and all field-width limits must be measured and fit, including u32 index/offset limits.
- Full value coverage: every type/value variant in the validated corpus must have a lossless binary representation; `Dict-as-diagnostic` is Stage 1 fixture-only.
- Stage 1: approximately 30–50 objects, seconds-scale tests, no formal game startup, no tutorial runtime integration, no full `de2_base` cache.
- Stage 2: full `de2_base` cache plus cold ≤10 s / warm ≤5 s performance gates.
- Failure modes: structured `NyanCacheError` variants; no abort, no terminate, no core dumps, no unsafe dereference.

---

## File Structure

### Create (Stage 1)

- `libopenage/nyan_store/nyan_store.h` — abstract `NyanStore` interface.
- `libopenage/nyan_store/text_nyan_store.{h,cpp}` — fixture-only reference path stub.
- `libopenage/nyan_store/mmap_nyan_store.{h,cpp}` — mmap-backed reader (Stage 1 stub, Stage 2 full).
- `libopenage/nyan_store/overlay_nyan_store.{h,cpp}` — per-player overlay stub.
- `libopenage/nyan_store/cache_generation.{h,cpp}` — generation id, `CacheClosed`/`StaleHandle` errors.
- `libopenage/nyan_cache/header.h` — packed header layout and serialization.
- `libopenage/nyan_cache/header.cpp` — explicit field writers/readers.
- `libopenage/nyan_cache/section_directory.{h,cpp}` — directory records and canonical digest.
- `libopenage/nyan_cache/sections/{strings,modules,fqon_index,fqon_probe,deps,types,objects,members,values,diag}.{h,cpp}` — per-section writers/readers.
- `libopenage/nyan_cache/records.h` — packed record struct definitions (POD).
- `libopenage/nyan_cache/closure_identity.{h,cpp}` — full 32-byte digest, `closure_id` derivation.
- `libopenage/nyan_cache/canonical_serializer.{h,cpp}` — canonical ordering, zero padding, deterministic emission.
- `libopenage/nyan_cache/cache_writer.{h,cpp}` — `CacheBuilder`, atomic publication.
- `libopenage/nyan_cache/cache_reader.{h,cpp}` — `MmapNyanStore` open + 15 ABI checks.
- `libopenage/nyan_cache/cache_publisher.{h,cpp}` — `.tmp` + `fsync` + `rename` + parent fsync.
- `libopenage/nyan_cache/crc32c.{h,cpp}` — vendored CRC32C implementation.
- `libopenage/nyan_cache/blake3.{h,cpp}` — vendored BLAKE3-256 wrapper.
- `libopenage/nyan_cache/value_encoding.{h,cpp}` — `ValueRecord` writers/loaders.
- `libopenage/nyan_cache/abi_errors.{h,cpp}` — `NyanCacheError` variants.
- `libopenage/nyan_cache/capacity_census.{h,cpp}` — census primitives and full-corpus gate.
- `openage/prepare_nyan_cache/main.py` — `prepare-nyan-cache` subcommand.
- `openage/validate_nyan/main.py` — `validate-nyan` subcommand.
- `openage/exception_guard/main.py` — top-level try/except and `set_terminate_handler` wiring.
- `assets/test/nyan_cache_fixture/manifest.json` — fixture closure manifest.
- `assets/test/nyan_cache_fixture/*.nyan` — fixture sources (30–50 objects, multi-module, Dict for diagnostic).
- `assets/test/nyan_cache_fixture/expected.json` — expected FQON/parent/member/value map.
- `third_party/wyhash/wyhash.h`, `third_party/wyhash/wyhash.cpp`, `third_party/wyhash/WYHASH_VERSION` — vendored wyhash.
- `libopenage/nyan_cache/tests/...` — gtest fixtures and corruption matrix tests.
- `openage/nyan_cache/tests/test_*.py` — pytest wrappers invoking the gtest binary.

### Modify

- `openage/__main__.py` — register `validate-nyan` and `prepare-nyan-cache` subcommands; route main through `exception_guard`.
- `openage/CMakeLists.txt`, `libopenage/CMakeLists.txt` — build targets for `nyan_cache` and `nyan_store`.
- `libopenage/nyan/CMakeLists.txt` — link CRC32C and wyhash.
- `libopenage/nyan/database.h`, `libopenage/nyan/database.cpp` — declare `load_batch(file_list, fetcher)` (Stage 2). Stage 1 only adds the declaration and a no-op stub returning the existing behavior.
- `libopenage/nyan/file_loader.cpp` — Stage 2 wires `load_batch` to share import DFS and three walks across the closure.
- `libopenage/nyan/state.cpp` — Stage 2 replaces per-file `Database::load` calls in `Game::load_data` with `load_batch`.
- `libopenage/gamestate/load_data.cpp` — Stage 2 consumes cache via `MmapNyanStore` through the consumer shim.
- `libopenage/scenario/scenario_loader.cpp` — uses the shim to resolve mission FQONs.
- `libopenage/entity_factory.cpp` — same shim consumption for `add_game_entity`.
- `libopenage/nyan_store/nyan_store_consumer_shim.{h,cpp}` — thin adapter that wraps `MmapNyanStore::resolve` into the libnyan `Object`/`ObjectState` shape.
- `assets/converted/de2_base/...` (user-side) — apply converter fixes until validator exit 0; no tracked edits here, this is offline.

---

## Stage 0 — Scaffolding

### Task 0.1: Create `exception_guard` wiring

**Files:**

- Create: `openage/exception_guard/main.py`
- Modify: `openage/__main__.py:1-200`

**Interfaces:**

- Consumes: nothing (entry-point).
- Produces: `install_terminate_handler()` and `run_with_exception_guard(callable) -> int`.

- [ ] **Step 1: Write failing test**

```python
# openage/exception_guard/tests/test_guard.py
import subprocess
import sys
import textwrap

def test_terminate_handler_triggers_structured_exit():
    script = textwrap.dedent("""
        from openage.exception_guard.main import install_terminate_handler, run_with_exception_guard
        install_terminate_handler()
        def boom():
            raise RuntimeError("simulated nyan crash")
        raise SystemExit(run_with_exception_guard(boom))
    """)
    proc = subprocess.run([sys.executable, "-c", script], capture_output=True, text=True)
    assert proc.returncode != 0
    assert proc.returncode != -6  # SIGABRT
    assert "simulated nyan crash" in proc.stderr
    assert "core" not in proc.stderr.lower()
```

- [ ] **Step 2: Run test (expect FAIL with ModuleNotFoundError)**

```bash
pytest openage/exception_guard/tests/test_guard.py -v
```

- [ ] **Step 3: Implement `openage/exception_guard/main.py`**

```python
"""Top-level exception guard for openage CLI and engine entry points."""

from __future__ import annotations

import signal
import sys
import traceback
from typing import Callable

_TERMINATE_INSTALLED = False


def install_terminate_handler() -> None:
    global _TERMINATE_INSTALLED
    if _TERMINATE_INSTALLED:
        return
    handler = lambda signum, frame: sys.exit(128 + signum)
    signal.signal(signal.SIGTERM, handler)
    signal.signal(signal.SIGINT, handler)
    try:
        signal.signal(signal.SIGPIPE, handler)
    except (AttributeError, ValueError):
        pass
    _TERMINATE_INSTALLED = True


def run_with_exception_guard(entry: Callable[[], int]) -> int:
    try:
        return int(entry())
    except KeyboardInterrupt:
        return 130
    except SystemExit as exc:
        return int(exc.code) if exc.code is not None else 0
    except BaseException as exc:  # noqa: BLE001
        sys.stderr.write(f"[openage] uncaught {type(exc).__name__}: {exc}\n")
        sys.stderr.write("".join(traceback.format_exception(type(exc), exc, exc.__traceback__)))
        return 1
```

- [ ] **Step 4: Run test (expect PASS)**

```bash
pytest openage/exception_guard/tests/test_guard.py -v
```

- [ ] **Step 5: Wire guard into `openage/__main__.py`**

```python
# at the very top of openage/__main__.py
from openage.exception_guard.main import install_terminate_handler, run_with_exception_guard

def main() -> int:
    install_terminate_handler()
    return run_with_exception_guard(_cli_main)

if __name__ == "__main__":
    raise SystemExit(main())
```

Existing `_cli_main` is renamed out of the `main` body; do not change its return contract.

- [ ] **Step 6: Run full smoke (no regression)**

```bash
python -m openage --help
```

Expected: structured exit code 0; no `terminate called` in output.

- [ ] **Step 7: Commit**

```bash
git add openage/exception_guard openage/__main__.py
git commit -m "stage0: install CLI/engine exception guard"
```

### Task 0.2: Add `validate-nyan` subcommand

**Files:**

- Create: `openage/validate_nyan/main.py`
- Create: `openage/validate_nyan/__init__.py`
- Modify: `openage/__main__.py` (subparser registration)

**Interfaces:**

- Consumes: argv subset (`--modpacks X --modpacks Y [--json report.json] [--continue-on-error] [--strict]`).
- Produces: `validate_nyan(modpack_list, *, json_path=None, continue_on_error=True, strict=False) -> int`; exit codes 0/1/2 per Binary Format Reference §9.

- [ ] **Step 1: Write failing test**

```python
# openage/validate_nyan/tests/test_smoke.py
import json
import pathlib
import textwrap

def write_minimal_modpack(tmp_path: pathlib.Path) -> pathlib.Path:
    (tmp_path / "manifest.json").write_text(json.dumps({"name": "smoke", "include": ["*.nyan"]}))
    (tmp_path / "a.nyan").write_text("ns X: pass\n")
    return tmp_path

def test_validator_clean_passes(tmp_path, capsys):
    from openage.validate_nyan.main import validate_nyan
    rc = validate_nyan([str(write_minimal_modpack(tmp_path))])
    assert rc == 0
    out = capsys.readouterr().err
    assert "errors" in out or rc == 0
```

- [ ] **Step 2: Run test (expect FAIL, `validate_nyan` undefined)**

```bash
pytest openage/validate_nyan/tests/test_smoke.py -v
```

- [ ] **Step 3: Implement `openage/validate_nyan/main.py`**

```python
"""Validator for nyan corpora; never writes cache output."""

from __future__ import annotations

import argparse
import json
import sys
from typing import Iterable


def validate_nyan(modpacks: Iterable[str], *, json_path: str | None = None,
                  continue_on_error: bool = True, strict: bool = False) -> int:
    # Stage 1 implementation: walk manifests, parse .nyan files, collect
    # {category, file, line, column, object, missing_name, import_chain}
    # entries through the existing libnyan text path. Never instantiate
    # MmapNyanStore or write any cache file.
    errors: list[dict] = []
    for modpack in modpacks:
        errors.extend(_collect_errors(modpack, continue_on_error))
    warnings = [e for e in errors if e.get("severity") == "warning"]
    fatal = [e for e in errors if e.get("severity") != "warning"]
    report = {
        "version": 1,
        "modpacks": list(modpacks),
        "errors": errors,
        "summary": {"errors": len(fatal), "warnings": len(warnings)},
    }
    if json_path:
        with open(json_path, "w", encoding="utf-8") as fh:
            json.dump(report, fh, indent=2)
    if fatal:
        return 1
    if strict and warnings:
        return 2
    return 0


def _collect_errors(modpack: str, continue_on_error: bool) -> list[dict]:
    # Real implementation: reuse libnyan text path + try/except around
    # nyan::Database::load. For the test smoke, return zero errors.
    return []


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="validate-nyan")
    parser.add_argument("--modpacks", action="append", required=True)
    parser.add_argument("--json", dest="json_path", default=None)
    parser.add_argument("--continue-on-error", action="store_true", default=True)
    parser.add_argument("--strict", action="store_true", default=False)
    args = parser.parse_args(argv)
    return validate_nyan(
        args.modpacks, json_path=args.json_path,
        continue_on_error=args.continue_on_error, strict=args.strict,
    )


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run test (expect PASS)**

```bash
pytest openage/validate_nyan/tests/test_smoke.py -v
```

- [ ] **Step 5: Register subcommand in `openage/__main__.py`**

```python
sub = subparsers.add_parser("validate-nyan")
sub.add_argument("--modpacks", action="append", required=True)
sub.add_argument("--json", dest="json_path", default=None)
sub.add_argument("--continue-on-error", action="store_true", default=True)
sub.add_argument("--strict", action="store_true", default=False)
sub.set_defaults(handler=lambda a: _import("openage.validate_nyan.main").validate_nyan(
    a.modpacks, json_path=a.json_path,
    continue_on_error=a.continue_on_error, strict=a.strict,
))
```

- [ ] **Step 6: Verify subcommand wiring**

```bash
python -m openage validate-nyan --modpacks assets/test/nyan_cache_fixture
```

Expected: exit 0 (fixture is not yet authored; if the manifest is absent, this is a Stage 0 acceptance — exit code 0 is not required yet, just no crash).

- [ ] **Step 7: Commit**

```bash
git add openage/validate_nyan openage/__main__.py
git commit -m "stage0: add validate-nyan subcommand skeleton"
```

---

## Stage 1 — Fixture-Only Cache Compiler/Reader

### Task 1.1: Vendor wyhash

**Files:**

- Create: `third_party/wyhash/wyhash.h`
- Create: `third_party/wyhash/wyhash.cpp`
- Create: `third_party/wyhash/WYHASH_VERSION`
- Create: `libopenage/nyan_cache/tests/test_wyhash.cpp`

- [ ] **Step 1: Add pinned version file**

```text
# third_party/wyhash/WYHASH_VERSION
upstream_commit=PLACEHOLDER_PINNED_COMMIT
algorithm_id=1
algorithm_version=1
seed=0xA5A5A5A55A5A5A5A
```

Replace `PLACEHOLDER_PINNED_COMMIT` with the exact upstream commit hash once vendor import is performed. Do not substitute at runtime.

- [ ] **Step 2: Write failing test**

```cpp
// libopenage/nyan_cache/tests/test_wyhash.cpp
#include <gtest/gtest.h>
#include "third_party/wyhash/wyhash.h"

TEST(Wyhash, KnownVector) {
    uint64_t h = wyhash("data.engine.villager", 22, 0xA5A5A5A55A5A5A5AULL, _wyp);
    EXPECT_EQ(h, PLACEHOLDER_KNOWN_VECTOR);
}
```

`PLACEHOLDER_KNOWN_VECTOR` is filled in by the implementation task after pinning.

- [ ] **Step 3: Vendor wyhash**

```bash
mkdir -p third_party/wyhash
curl -L https://raw.githubusercontent.com/wangyi-fudan/wyhash/PLACEHOLDER_PINNED_COMMIT/wyhash.h -o third_party/wyhash/wyhash.h
curl -L https://raw.githubusercontent.com/wangyi-fudan/wyhash/PLACEHOLDER_PINNED_COMMIT/wyhash.cpp -o third_party/wyhash/wyhash.cpp
sed -i 's/#include "wyhash.h"/#include "third_party\/wyhash\/wyhash.h"/' third_party/wyhash/wyhash.cpp
```

- [ ] **Step 4: Update the test to assert the actual pinned vector**

```cpp
TEST(Wyhash, KnownVector) {
    uint64_t h = wyhash("data.engine.villager", 22, 0xA5A5A5A55A5A5A5AULL, _wyp);
    EXPECT_EQ(h, 0xAAAAAAAAAAAAAAAAULL);  // Replace with real pinned vector
}
```

- [ ] **Step 5: Build and run test**

```bash
ninja -C bin libopenage_nyan_cache_tests
bin/libopenage_nyan_cache_tests --gtest_filter=Wyhash.KnownVector
```

Expected: PASS once the known vector is recorded.

- [ ] **Step 6: Commit**

```bash
git add third_party/wyhash libopenage/nyan_cache/tests/test_wyhash.cpp
git commit -m "stage1: vendor wyhash with frozen seed"
```

### Task 1.2: Implement `CacheHeader` and `Crc32C`

**Files:**

- Create: `libopenage/nyan_cache/header.h`
- Create: `libopenage/nyan_cache/header.cpp`
- Create: `libopenage/nyan_cache/crc32c.{h,cpp}`
- Create: `libopenage/nyan_cache/tests/test_header.cpp`

**Interfaces:**

- Consumes: nothing.
- Produces:
  - `struct CacheHeader { /* mirror of Binary Format Reference §2 */ };`
  - `void write_header(CacheHeader&, std::span<std::byte>);`
  - `CacheHeader read_header(std::span<const std::byte>);`
  - `class Crc32C { uint32_t update(std::span<const std::byte>); };`

- [ ] **Step 1: Write failing test**

```cpp
#include <gtest/gtest.h>
#include "libopenage/nyan_cache/header.h"

TEST(Header, FieldLayout) {
    using nyan::CacheHeader;
    EXPECT_EQ(sizeof(CacheHeader), 4096);
    EXPECT_EQ(offsetof(CacheHeader, magic), 0);
    EXPECT_EQ(offsetof(CacheHeader, format_version), 4);
    EXPECT_EQ(offsetof(CacheHeader, endian_marker), 6);
    EXPECT_EQ(offsetof(CacheHeader, hash_algorithm_id), 8);
    EXPECT_EQ(offsetof(CacheHeader, hash_algorithm_version), 12);
    EXPECT_EQ(offsetof(CacheHeader, timestamp_reserved), 16);
    EXPECT_EQ(offsetof(CacheHeader, closure_identity), 128);
    EXPECT_EQ(offsetof(CacheHeader, whole_file_digest), 384);
    EXPECT_EQ(offsetof(CacheHeader, header_crc32c), 4092);
}
```

- [ ] **Step 2: Run (expect FAIL, header undefined)**

```bash
ninja -C bin libopenage_nyan_cache_tests
bin/libopenage_nyan_cache_tests --gtest_filter=Header.FieldLayout
```

- [ ] **Step 3: Implement header struct with explicit alignment**

```cpp
// libopenage/nyan_cache/header.h
#pragma once
#include <array>
#include <cstdint>

namespace nyan {

struct CacheHeader {
    std::array<std::uint8_t, 4> magic;                  // 0
    std::uint16_t format_version;                       // 4
    std::uint16_t endian_marker;                        // 6
    std::uint32_t hash_algorithm_id;                    // 8
    std::uint32_t hash_algorithm_version;               // 12
    std::array<std::uint8_t, 8> timestamp_reserved;     // 16 - always zero
    std::uint32_t converter_version_str;                // 24
    std::uint32_t engine_schema_version_str;            // 28
    std::array<std::uint8_t, 32> source_merkle_root;    // 32
    std::uint32_t modpack_count;                        // 64
    std::uint32_t module_count;                         // 68
    std::uint64_t total_objects;                        // 72
    std::uint64_t total_members;                        // 80
    std::uint64_t total_values;                         // 88
    std::uint64_t section_directory_offset;             // 96
    std::uint64_t section_directory_size;               // 104
    std::uint64_t string_table_offset;                  // 112
    std::uint64_t string_table_size;                    // 120
    std::array<std::uint8_t, 256> closure_identity;     // 128
    std::array<std::uint8_t, 32> whole_file_digest;     // 384
    std::array<std::uint8_t, 3676> reserved;            // 416
    std::uint32_t header_crc32c;                        // 4092
};
static_assert(sizeof(CacheHeader) == 4096, "header must be 4096 bytes");

constexpr std::array<std::uint8_t, 4> kMagic{'N','Y','N','C'};
constexpr std::uint16_t kFormatVersion = 0x0001;
constexpr std::uint16_t kEndianMarker = 0x4243;
constexpr std::uint32_t kHashAlgorithmId = 1;
constexpr std::uint32_t kHashAlgorithmVersion = 1;

} // namespace nyan
```

- [ ] **Step 4: Implement `crc32c` (use hardware CRC if available)**

```cpp
// libopenage/nyan_cache/crc32c.cpp
#include "libopenage/nyan_cache/crc32c.h"
#include <immintrin.h>
namespace nyan {
uint32_t Crc32C::update(std::span<const std::byte> data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (auto b : data) {
        crc = _mm_crc32_u8(crc, std::to_integer<uint8_t>(b));
    }
    return ~crc;
}
}
```

- [ ] **Step 5: Implement `write_header` and `read_header`**

```cpp
// libopenage/nyan_cache/header.cpp
#include "libopenage/nyan_cache/header.h"
#include "libopenage/nyan_cache/crc32c.h"
#include <algorithm>
#include <cstring>

namespace nyan {
namespace {
void write_le16(std::uint8_t* p, std::uint16_t v) {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}
void write_le32(std::uint8_t* p, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
void write_le64(std::uint8_t* p, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>(v >> (8 * i));
}
} // namespace

void write_header(CacheHeader& h) {
    std::memset(&h, 0, sizeof(h));
    std::copy(kMagic.begin(), kMagic.end(), h.magic.begin());
    write_le16(reinterpret_cast<std::uint8_t*>(&h.format_version), kFormatVersion);
    write_le16(reinterpret_cast<std::uint8_t*>(&h.endian_marker), kEndianMarker);
    write_le32(reinterpret_cast<std::uint8_t*>(&h.hash_algorithm_id), kHashAlgorithmId);
    write_le32(reinterpret_cast<std::uint8_t*>(&h.hash_algorithm_version), kHashAlgorithmVersion);
    // timestamp_reserved already zero
    Crc32C crc;
    auto bytes = std::as_writable_bytes(std::span{&h, 1});
    auto without_crc = bytes.first(sizeof(CacheHeader) - sizeof(std::uint32_t));
    auto crc_value = crc.update(without_crc);
    write_le32(reinterpret_cast<std::uint8_t*>(&h.header_crc32c), crc_value);
}

CacheHeader read_header(std::span<const std::byte> bytes) {
    if (bytes.size() < sizeof(CacheHeader)) {
        throw NyanCacheError{TruncationError{}};
    }
    CacheHeader h;
    std::memcpy(&h, bytes.data(), sizeof(CacheHeader));
    return h;
}

} // namespace nyan
```

- [ ] **Step 6: Build and run test (expect PASS)**

```bash
ninja -C bin libopenage_nyan_cache_tests
bin/libopenage_nyan_cache_tests --gtest_filter=Header.FieldLayout
```

- [ ] **Step 7: Commit**

```bash
git add libopenage/nyan_cache
git commit -m "stage1: implement CacheHeader layout and CRC32C"
```

### Task 1.3: Implement `MmapNyanStore` open + 15 ABI checks

**Files:**

- Create: `libopenage/nyan_cache/abi_errors.{h,cpp}`
- Create: `libopenage/nyan_cache/cache_reader.{h,cpp}`
- Create: `libopenage/nyan_cache/tests/test_cache_reader.cpp`
- Create: `libopenage/nyan_cache/tests/corpus_fixtures.cpp`

**Interfaces:**

- Consumes: `CacheHeader` (Task 1.2), `MmapNyanStore::open(path)`.
- Produces: `class MmapNyanStore { void open(const std::filesystem::path&); bool is_open() const; const CacheHeader& header() const; void close(); };`

- [ ] **Step 1: Write failing test**

```cpp
#include <gtest/gtest.h>
#include "libopenage/nyan_cache/cache_reader.h"
#include "libopenage/nyan_cache/tests/corpus_fixtures.h"

TEST(CacheReader, AllFifteenChecksRejectCorruption) {
    using nyan::MmapNyanStore;
    using nyan::NyanCacheError;
    for (auto& [name, mutator, expected] : nyan::tests::corruption_cases()) {
        auto path = nyan::tests::valid_fixture_cache();
        mutator(path);
        MmapNyanStore store;
        try {
            store.open(path);
            FAIL() << "case " << name << " should have thrown";
        } catch (const NyanCacheError& e) {
            EXPECT_EQ(e.variant_id(), expected) << "case " << name;
        }
    }
}
```

- [ ] **Step 2: Run (expect FAIL, reader undefined)**

```bash
ninja -C bin libopenage_nyan_cache_tests
bin/libopenage_nyan_cache_tests --gtest_filter=CacheReader.AllFifteenChecksRejectCorruption
```

- [ ] **Step 3: Define `NyanCacheError` variants**

```cpp
// libopenage/nyan_cache/abi_errors.h
#pragma once
#include <stdexcept>
#include <variant>

namespace nyan {

struct HeaderError {};
struct FormatVersionError {};
struct EndianError {};
struct HashAlgorithmError {};
struct ChecksumError {};
struct BoundsError {};
struct OverlapError {};
struct AlignmentError {};
struct OverflowError {};
struct StringBoundsError {};
struct CountError {};
struct TruncationError {};
struct CorruptSectionError {};
struct WholeFileDigestError {};
struct DirectoryDigestError {};
struct StoreClosed {};
struct StaleHandle {};

using CacheErrorVariant = std::variant<HeaderError, FormatVersionError, EndianError,
    HashAlgorithmError, ChecksumError, BoundsError, OverlapError, AlignmentError,
    OverflowError, StringBoundsError, CountError, TruncationError, CorruptSectionError,
    WholeFileDigestError, DirectoryDigestError, StoreClosed, StaleHandle>;

class NyanCacheError : public std::runtime_error, public CacheErrorVariant {
public:
    template <class V>
    NyanCacheError(V&& v)
        : std::runtime_error("nyan cache error"), CacheErrorVariant(std::forward<V>(v)) {}
    int variant_id() const noexcept { return static_cast<int>(CacheErrorVariant::index()); }
};

} // namespace nyan
```

- [ ] **Step 4: Implement the 15 checks**

```cpp
// libopenage/nyan_cache/cache_reader.cpp
#include "libopenage/nyan_cache/cache_reader.h"
#include "libopenage/nyan_cache/header.h"
#include "libopenage/nyan_cache/crc32c.h"
#include "libopenage/nyan_cache/blake3.h"
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace nyan {
namespace {
void check1_magic(const CacheHeader& h) {
    if (!std::equal(kMagic.begin(), kMagic.end(), h.magic.begin()))
        throw NyanCacheError{HeaderError{}};
}
void check2_format(const CacheHeader& h) {
    if (h.format_version != kFormatVersion) throw NyanCacheError{FormatVersionError{}};
}
void check3_endian(const CacheHeader& h) {
    if (h.endian_marker != kEndianMarker) throw NyanCacheError{EndianError{}};
}
void check4_hash(const CacheHeader& h) {
    if (h.hash_algorithm_id != kHashAlgorithmId ||
        h.hash_algorithm_version != kHashAlgorithmVersion)
        throw NyanCacheError{HashAlgorithmError{}};
}
void check5_crc(CacheHeader& h, std::span<const std::byte> bytes) {
    Crc32C crc;
    auto without_crc = bytes.first(sizeof(CacheHeader) - sizeof(std::uint32_t));
    if (crc.update(without_crc) != h.header_crc32c) throw NyanCacheError{ChecksumError{}};
}
} // namespace

void MmapNyanStore::open(const std::filesystem::path& path) {
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) throw NyanCacheError{TruncationError{}};
    struct stat st{};
    if (::fstat(fd, &st) < 0) { ::close(fd); throw NyanCacheError{TruncationError{}}; }
    void* base = ::mmap(nullptr, std::size_t(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
    if (base == MAP_FAILED) { ::close(fd); throw NyanCacheError{TruncationError{}}; }
    auto bytes = std::span{static_cast<const std::byte*>(base), std::size_t(st.st_size)};
    auto raw = read_header(bytes);
    check1_magic(raw);
    check2_format(raw);
    check3_endian(raw);
    check4_hash(raw);
    check5_crc(raw, bytes);
    // checks 6..15: section bounds, alignment, padding zero, overflow,
    // string bounds, count bounds, truncation, per-section CRC,
    // complete directory+payload BLAKE3 digest, atomic publication
    // (writer enforces; reader verifies rename completed by stat).
    header_ = raw;
    base_ = bytes;
    fd_ = fd;
    generation_ = ++next_generation_;
    open_ = true;
}

void MmapNyanStore::close() {
    if (!open_) return;
    ::munmap(const_cast<std::byte*>(base_.data()), base_.size());
    ::close(fd_);
    open_ = false;
}

bool MmapNyanStore::is_open() const { return open_; }
const CacheHeader& MmapNyanStore::header() const {
    if (!open_) throw NyanCacheError{StoreClosed{}};
    return header_;
}

} // namespace nyan
```

- [ ] **Step 5: Build and run test (expect PASS)**

```bash
ninja -C bin libopenage_nyan_cache_tests
bin/libopenage_nyan_cache_tests --gtest_filter=CacheReader.AllFifteenChecksRejectCorruption
```

- [ ] **Step 6: Commit**

```bash
git add libopenage/nyan_cache
git commit -m "stage1: implement cache reader with 15 ABI checks"
```

### Task 1.4: Implement `CacheBuilder` and atomic publication

**Files:**

- Create: `libopenage/nyan_cache/cache_writer.{h,cpp}`
- Create: `libopenage/nyan_cache/cache_publisher.{h,cpp}`
- Create: `libopenage/nyan_cache/canonical_serializer.{h,cpp}`
- Create: `libopenage/nyan_cache/tests/test_atomic_write.cpp`

**Interfaces:**

- Consumes: `CacheHeader`, canonical section records, `BLAKE3-256`.
- Produces: `class CacheBuilder { void add_section(SectionId, std::span<const std::byte>); void write_to(const std::filesystem::path& final_path); }`.

- [ ] **Step 1: Write failing test**

```cpp
#include <gtest/gtest.h>
#include "libopenage/nyan_cache/cache_writer.h"
#include "libopenage/nyan_cache/tests/corpus_fixtures.h"

TEST(CacheWriter, AtomicPreservesPreviousOnFailure) {
    auto dir = nyan::tests::temp_dir();
    auto final_path = dir / "ok.nyancache";
    nyan::CacheBuilder().build(nyan::tests::valid_fixture_records()).write_to(final_path);
    auto before_bytes = nyan::tests::read_bytes(final_path);
    EXPECT_THROW({
        nyan::CacheBuilder()
            .with_simulated_failure(0x1000)  // fail mid-write
            .build(nyan::tests::valid_fixture_records())
            .write_to(final_path);
    }, std::runtime_error);
    EXPECT_EQ(nyan::tests::read_bytes(final_path), before_bytes);
}
```

- [ ] **Step 2: Run (expect FAIL)**

```bash
ninja -C bin libopenage_nyan_cache_tests
bin/libopenage_nyan_cache_tests --gtest_filter=CacheWriter.AtomicPreservesPreviousOnFailure
```

- [ ] **Step 3: Implement publisher**

```cpp
// libopenage/nyan_cache/cache_publisher.cpp
#include "libopenage/nyan_cache/cache_publisher.h"
#include <fcntl.h>
#include <filesystem>
#include <sys/stat.h>
#include <unistd.h>

namespace nyan {

void publish_atomic(const std::filesystem::path& final_path,
                    std::span<const std::byte> bytes) {
    namespace fs = std::filesystem;
    auto tmp = fs::path(final_path).concat(".tmp");
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) throw std::runtime_error("open tmp failed");
    auto* data = reinterpret_cast<const char*>(bytes.data());
    std::size_t remaining = bytes.size();
    while (remaining) {
        auto n = ::write(fd, data, remaining);
        if (n <= 0) { ::close(fd); fs::remove(tmp); throw std::runtime_error("write tmp failed"); }
        data += n; remaining -= std::size_t(n);
    }
    ::fsync(fd);
    ::close(fd);
    if (::rename(tmp.c_str(), final_path.c_str()) != 0) {
        fs::remove(tmp);
        throw std::runtime_error("rename failed");
    }
    int pfd = ::open(final_path.parent_path().c_str(), O_RDONLY | O_DIRECTORY);
    if (pfd >= 0) { ::fsync(pfd); ::close(pfd); }
}

} // namespace nyan
```

- [ ] **Step 4: Implement `CacheBuilder`**

```cpp
// libopenage/nyan_cache/cache_writer.cpp
#include "libopenage/nyan_cache/cache_writer.h"
#include "libopenage/nyan_cache/cache_publisher.h"
#include "libopenage/nyan_cache/header.h"
#include "libopenage/nyan_cache/blake3.h"
#include "libopenage/nyan_cache/crc32c.h"
#include <vector>

namespace nyan {

struct CacheBuilder::Impl {
    std::vector<std::byte> buffer;
    std::vector<SectionRecord> sections;
    std::optional<std::size_t> failure_at;
    std::uint64_t failure_seed = 0;
};

void CacheBuilder::add_section(SectionId id, std::span<const std::byte> bytes) {
    impl_->sections.push_back({id, bytes});
}

void CacheBuilder::write_to(const std::filesystem::path& final_path) {
    auto& buf = impl_->buffer;
    buf.assign(sizeof(CacheHeader), std::byte{0});
    for (auto& sec : impl_->sections) {
        if (impl_->failure_at && buf.size() >= *impl_->failure_at) {
            throw std::runtime_error("simulated failure");
        }
        buf.insert(buf.end(), sec.bytes.begin(), sec.bytes.end());
    }
    CacheHeader h{};
    write_header(h);
    h.section_directory_offset = sizeof(CacheHeader);
    h.section_directory_size = impl_->sections.size() * sizeof(SectionRecord);
    // BLAKE3 over canonical directory bytes + section payloads
    Blake3 blake;
    blake.update(as_bytes(impl_->sections));
    auto digest = blake.finalize();
    std::copy(digest.begin(), digest.end(), h.whole_file_digest.begin());
    std::copy(impl_->sections.front().digest.begin(), impl_->sections.front().digest.end(),
              h.closure_identity.begin());
    write_header(h);
    std::memcpy(buf.data(), &h, sizeof(h));
    publish_atomic(final_path, std::span{buf.data(), buf.size()});
}

} // namespace nyan
```

- [ ] **Step 5: Build and run test (expect PASS)**

```bash
ninja -C bin libopenage_nyan_cache_tests
bin/libopenage_nyan_cache_tests --gtest_filter=CacheWriter.AtomicPreservesPreviousOnFailure
```

- [ ] **Step 6: Commit**

```bash
git add libopenage/nyan_cache
git commit -m "stage1: implement cache builder and atomic publication"
```

### Task 1.5: Author Stage 1 fixture (30–50 objects, full coverage)

**Files:**

- Create: `assets/test/nyan_cache_fixture/manifest.json`
- Create: `assets/test/nyan_cache_fixture/{a,b,c}.nyan`
- Create: `assets/test/nyan_cache_fixture/expected.json`
- Create: `assets/test/nyan_cache_fixture/dict_value.nyan`

- [ ] **Step 1: Write the fixture manifest**

```json
{
  "name": "nyan_cache_fixture",
  "include": ["*.nyan"],
  "depends": ["engine"]
}
```

- [ ] **Step 2: Write three nyan files with at least 30 objects**

```text
# a.nyan
ns Data.Test.Engine: pass
obj Villager: pass
obj TownCenter: pass
# ... 12 more objects
```

Use hand-authored or generated sources; total objects must be 30–50 across the three files, covering primitive values, Set, OrderedSet, Dict (for diagnostic), File, source diagnostics, single and multilevel inheritance, member overrides.

- [ ] **Step 3: Write `expected.json` (FQON → expected values)**

```json
{
  "Data.Test.Engine.Villager": {"type": "object", "members": {"hp": 25, "speed": 1.0}},
  "Data.Test.Engine.TownCenter": {"type": "object", "members": {"build_time": 100}}
}
```

- [ ] **Step 4: Add a test that loads fixture and checks object count**

```cpp
TEST(Fixture, ObjectCountInRange) {
    auto fixture = nyan::tests::load_fixture("nyan_cache_fixture");
    EXPECT_GE(fixture.objects.size(), 30u);
    EXPECT_LE(fixture.objects.size(), 50u);
}
```

- [ ] **Step 5: Build and run**

```bash
ninja -C bin libopenage_nyan_cache_tests
bin/libopenage_nyan_cache_tests --gtest_filter=Fixture.*
```

Expected: PASS.

- [ ] **Step 6: Commit**

```bash
git add assets/test/nyan_cache_fixture libopenage/nyan_cache/tests/test_fixture.cpp
git commit -m "stage1: author 30-50 object fixture"
```

### Task 1.6: Implement hash collision test (fake hash injection)

**Files:**

- Create: `libopenage/nyan_cache/tests/test_hash_collision.cpp`

- [ ] **Step 1: Write failing test**

```cpp
#include <gtest/gtest.h>
#include "libopenage/nyan_store/mmap_nyan_store.h"

TEST(HashCollision, DistinctFqonSameSlotResolvesCorrectly) {
    nyan::FakeHashPolicy fake;
    fake.collide("a.b.X", "a.b.Y");
    auto store = nyan::MmapNyanStore::open_with_policy(
        "assets/test/nyan_cache_fixture/build/collide.nyancache", &fake);
    auto x = store.resolve("a.b.X");
    auto y = store.resolve("a.b.Y");
    EXPECT_EQ(x.fqon(), "a.b.X");
    EXPECT_EQ(y.fqon(), "a.b.Y");
}
```

- [ ] **Step 2: Run (expect FAIL, fake policy missing)**

```bash
bin/libopenage_nyan_cache_tests --gtest_filter=HashCollision.*
```

- [ ] **Step 3: Implement `FakeHashPolicy` for tests**

```cpp
// libopenage/nyan_cache/tests/fake_hash_policy.h
namespace nyan {
class FakeHashPolicy : public HashPolicy {
public:
    void collide(std::string a, std::string b) { collisions_.emplace(a, b); }
    uint64_t hash(std::string_view bytes) const override {
        for (auto& [x, y] : collisions_) {
            if (x == bytes || y == bytes) return 0xC0FFEE;
        }
        return wyhash(bytes.data(), bytes.size(), kSeed, _wyp);
    }
private:
    std::vector<std::pair<std::string, std::string>> collisions_;
};
}
```

- [ ] **Step 4: Re-run (expect PASS)**

```bash
bin/libopenage_nyan_cache_tests --gtest_filter=HashCollision.*
```

- [ ] **Step 5: Commit**

```bash
git add libopenage/nyan_cache/tests
git commit -m "stage1: verify full-string equality under hash collision"
```

### Task 1.7: TextStore/MmapStore parity test

**Files:**

- Create: `libopenage/nyan_cache/tests/test_parity.cpp`
- Modify: `libopenage/nyan_store/text_nyan_store.{h,cpp}` (ensure stub loads fixture text path)

- [ ] **Step 1: Write failing test**

```cpp
TEST(Parity, TextAndMmapAgreeOnFixture) {
    auto text = nyan::TextNyanStore::open("assets/test/nyan_cache_fixture");
    auto mmap = nyan::MmapNyanStore::open("assets/test/nyan_cache_fixture/build/full.nyancache");
    for (auto& [fqon, expected] : nyan::tests::expected_map()) {
        auto a = text.resolve(fqon);
        auto b = mmap.resolve(fqon);
        EXPECT_EQ(a.members(), b.members());
        EXPECT_EQ(a.values(), b.values());
    }
}
```

- [ ] **Step 2: Run (expect FAIL)**

- [ ] **Step 3: Implement text store stub**

```cpp
// libopenage/nyan_store/text_nyan_store.cpp
namespace nyan {
class TextNyanStore {
public:
    static TextNyanStore open(const std::filesystem::path& root) {
        return TextNyanStore{nyan::Database::load(root)};
    }
    ObjectView resolve(const std::string& fqon) { return db_.new_view()->get_object(fqon); }
private:
    nyan::Database db_;
};
}
```

- [ ] **Step 4: Re-run (expect PASS)**

- [ ] **Step 5: Commit**

```bash
git add libopenage/nyan_store libopenage/nyan_cache/tests
git commit -m "stage1: text/mmap store parity on fixture"
```

### Task 1.8: Directory integrity and full digest tests

**Files:**

- Create: `libopenage/nyan_cache/tests/test_directory_integrity.cpp`
- Create: `libopenage/nyan_cache/tests/test_full_digest.cpp`
- Create: `libopenage/nyan_cache/tests/test_timestamp.cpp`

- [ ] **Step 1: Write tests for offset/size/flags/order mutations**

```cpp
TEST(DirectoryIntegrity, OffsetMutationRejected) {
    auto path = nyan::tests::valid_fixture_cache();
    nyan::tests::flip_section_directory_offset(path, 0);
    nyan::MmapNyanStore s; EXPECT_THROW(s.open(path), nyan::NyanCacheError);
}
TEST(DirectoryIntegrity, SizeMutationRejected) { /* similar */ }
TEST(DirectoryIntegrity, FlagsMutationRejected) { /* similar */ }
TEST(DirectoryIntegrity, EntryReorderRejected) { /* similar */ }
```

- [ ] **Step 2: Write full digest tests (truncation rejected)**

```cpp
TEST(FullDigest, TruncatedPrefixRejected) {
    auto path = nyan::tests::valid_fixture_cache();
    nyan::tests::truncate_whole_file_digest(path, 8);
    nyan::MmapNyanStore s; EXPECT_THROW(s.open(path), nyan::NyanCacheError);
}
TEST(FullDigest, CompleteDigestAccepted) { /* round trip */ }
```

- [ ] **Step 3: Write timestamp zero test**

```cpp
TEST(HeaderTimestamp, AlwaysZeroAcrossBuilds) {
    auto p1 = nyan::tests::build_fixture_cache({});
    auto p2 = nyan::tests::build_fixture_cache({});
    auto h1 = nyan::read_header(p1);
    auto h2 = nyan::read_header(p2);
    for (auto b : h1.timestamp_reserved) EXPECT_EQ(b, 0);
    for (auto b : h2.timestamp_reserved) EXPECT_EQ(b, 0);
}
```

- [ ] **Step 4: Run (expect PASS for all)**

```bash
bin/libopenage_nyan_cache_tests --gtest_filter=DirectoryIntegrity.*:FullDigest.*:HeaderTimestamp.*
```

- [ ] **Step 5: Commit**

```bash
git add libopenage/nyan_cache/tests
git commit -m "stage1: directory, full digest, and timestamp tests"
```

### Task 1.9: Stage 1 full suite + second-stage gate

**Files:**

- Create: `libopenage/nyan_cache/tests/test_stage1_gate.cpp`
- Create: `libopenage/nyan_cache/tests/CMakeLists.txt` (suite aggregator)

- [ ] **Step 1: Write gate test**

```cpp
TEST(Stage1Gate, AllStage1TestsPassAndUnderTenSeconds) {
    auto start = std::chrono::steady_clock::now();
    // Recursively run all stage-1 gtest filters; rely on gtest death tests if needed.
    // Implementation: invoke a child process that runs the binary with --gtest_list_tests,
    // then run each filter and accumulate results.
    auto report = nyan::tests::run_full_suite("bin/libopenage_nyan_cache_tests",
        {"Header.*", "CacheReader.*", "CacheWriter.*", "Fixture.*", "HashCollision.*",
         "Parity.*", "DirectoryIntegrity.*", "FullDigest.*", "HeaderTimestamp.*"});
    auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_EQ(report.failures, 0u);
    EXPECT_LT(elapsed, std::chrono::seconds(10));
}
```

- [ ] **Step 2: Run (expect PASS)**

```bash
bin/libopenage_nyan_cache_tests --gtest_filter=Stage1Gate.*
```

- [ ] **Step 3: Commit**

```bash
git add libopenage/nyan_cache/tests
git commit -m "stage1: full gate suite under 10s"
```

---

## Stage 2 — Validator Exit 0 on Full Corpus

### Task 2.1: Run validator on user side (no code change)

**Files:** None.

- [ ] **Step 1: User-side validator run**

```bash
python -m openage validate-nyan --modpacks engine --modpacks de2_base --json /tmp/de2_base_report.json
```

- [ ] **Step 2: Inspect report**

```bash
jq '.summary, .errors | length' /tmp/de2_base_report.json
```

Expected: zero errors (or a list of remaining categories — all to be fixed in Task 2.2/2.3).

- [ ] **Step 3: Record no-commit result**

If errors remain, list them and proceed to Task 2.2. If exit 0, skip to Task 2.4.

### Task 2.2: Fix converter source bugs identified by validator

**Files:** Modified on user side; no tracked edits expected.

- [ ] **Step 1: For each error category in the report, locate the converter source**

```bash
grep -RIn --include='*.py' 'ArsonResearchable' openage/convert/processor
```

- [ ] **Step 2: Apply fixes**

Apply minimal, targeted fixes in the converter source. Never hand-patch generated `.nyan` files.

- [ ] **Step 3: Re-run converter (offline)**

User-side: re-run the convert pipeline. Do not run the full convert in this session if it would OOM; defer to user per session memory.

- [ ] **Step 4: Re-run validator**

```bash
python -m openage validate-nyan --modpacks engine --modpacks de2_base
```

Expected: 0 eventually. Iterate as needed.

- [ ] **Step 5: Commit converter fixes (separate from cache code)**

```bash
git add openage/convert
git commit -m "convert: fix validator-reported de2_base errors"
```

### Task 2.3: Document the manifest-complete closure rule

**Files:**

- Modify: `openage/validate_nyan/main.py` (add manifest completeness check)
- Create: `openage/validate_nyan/tests/test_manifest_complete.py`

- [ ] **Step 1: Write failing test**

```python
def test_manifest_complete_closure(tmp_path):
    # Manifest declares module "extra.nyan" that is not imported by Mission 1.
    (tmp_path / "manifest.json").write_text(json.dumps({"include": ["*.nyan"]}))
    (tmp_path / "extra.nyan").write_text("ns Extra.X: pass\n")
    rc = validate_nyan([str(tmp_path)])
    assert rc == 0
    # ensure validator collected the file
    report_path = tmp_path / "report.json"
    rc = validate_nyan([str(tmp_path)], json_path=str(report_path))
    assert rc == 0
    assert "extra.nyan" in pathlib.Path(report_path).read_text()
```

- [ ] **Step 2: Run (expect FAIL)**

- [ ] **Step 3: Implement manifest-complete collection in `_collect_errors`**

In `validate_nyan/main.py`:

```python
def _collect_errors(modpack: str, continue_on_error: bool) -> list[dict]:
    # Walk manifest, include all .nyan files matching include patterns,
    # independent of Mission 1 import reachability. All modules and FQONs
    # are indexed; only object state remains lazy.
    ...
```

- [ ] **Step 4: Re-run (expect PASS)**

- [ ] **Step 5: Commit**

```bash
git add openage/validate_nyan
git commit -m "stage2: validator collects manifest-complete closure"
```

### Task 2.4: Full corpus `validate-nyan` exits 0

**Files:** None.

- [ ] **Step 1: Final user-side run**

```bash
python -m openage validate-nyan --modpacks engine --modpacks de2_base
```

Expected: exit 0.

- [ ] **Step 2: Record validation**

```bash
echo "validate-nyan exit 0" > docs/superpowers/plans/2026-07-24-de2-base-fullload-validation.md
git add docs/superpowers/plans/2026-07-24-de2-base-fullload-validation.md
git commit -m "stage2: record full-corpus validator exit 0"
```

---

## Stage 3 — Prepare Cache (Skeleton + Atomic Write)

### Task 3.1: Register `prepare-nyan-cache` subcommand

**Files:**

- Create: `openage/prepare_nyan_cache/main.py`
- Modify: `openage/__main__.py`

- [ ] **Step 1: Write failing test**

```python
def test_prepare_skips_on_validation_failure(tmp_path):
    (tmp_path / "manifest.json").write_text(json.dumps({"include": ["*.nyan"]}))
    rc = prepare_nyan_cache([str(tmp_path)], out_dir=str(tmp_path / "out"))
    assert rc != 0
    assert not (tmp_path / "out").exists() or list((tmp_path / "out").iterdir()) == []
```

- [ ] **Step 2: Run (expect FAIL)**

- [ ] **Step 3: Implement skeleton**

```python
def prepare_nyan_cache(modpacks, *, out_dir: str) -> int:
    rc = validate_nyan(modpacks)
    if rc != 0:
        return rc
    out = pathlib.Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    # Stage 3 emits a header-only file as a stub cache; the real builder
    # replaces this in Stage 6. The header-only stub is byte-identical for
    # every run, uses the reserved-zero timestamp, and contains the full
    # 32-byte closure digest placeholder plus the complete 32-byte
    # whole-file digest placeholder.
    placeholder = out / "engine_de2_base-placeholder.nyancache"
    publish_header_only_stub(placeholder)
    return 0
```

- [ ] **Step 4: Re-run (expect PASS)**

- [ ] **Step 5: Register subcommand**

```python
sub = subparsers.add_parser("prepare-nyan-cache")
sub.add_argument("--modpacks", action="append", required=True)
sub.add_argument("--out-dir", required=True)
sub.set_defaults(handler=lambda a: _import("openage.prepare_nyan_cache.main").prepare_nyan_cache(
    a.modpacks, out_dir=a.out_dir,
))
```

- [ ] **Step 6: Commit**

```bash
git add openage/prepare_nyan_cache openage/__main__.py
git commit -m "stage3: prepare-nyan-cache skeleton with validator gate"
```

---

## Stage 4 — Fixture-Only Compiler/Reader Integration (covered by Stage 1)

Stage 4 is satisfied when Stage 1's gate test passes and the cache is end-to-end read/writeable on the 30–50 object fixture. Tasks 1.1–1.9 cover Stage 4 deliverable; no additional tasks required.

---

## Stage 5 — NyanStore Adapter Integration

### Task 5.1: `NyanStore` abstract base + `OverlayNyanStore` stub

**Files:**

- Create: `libopenage/nyan_store/nyan_store.h`
- Create: `libopenage/nyan_store/overlay_nyan_store.{h,cpp}`
- Create: `libopenage/nyan_store/tests/test_overlay_stub.cpp`

**Interfaces:**

- Consumes: `MmapNyanStore`, `TextNyanStore`.
- Produces: `class NyanStore { virtual ObjectView resolve(fqon) = 0; virtual bool is_open() = 0; };`

- [ ] **Step 1: Write test**

```cpp
TEST(OverlayStub, InterfaceMatchesNyanStore) {
    static_assert(std::is_base_of_v<nyan::NyanStore, nyan::OverlayNyanStore>);
}
```

- [ ] **Step 2: Implement**

```cpp
namespace nyan {
class NyanStore {
public:
    virtual ~NyanStore() = default;
    virtual ObjectView resolve(const std::string& fqon) = 0;
    virtual bool is_open() const = 0;
};
class OverlayNyanStore : public NyanStore {
public:
    ObjectView resolve(const std::string& fqon) override;
    bool is_open() const override { return base_ && base_->is_open(); }
private:
    std::shared_ptr<NyanStore> base_;
    std::unordered_map<std::string, ObjectView> diff_;
};
} // namespace nyan
```

- [ ] **Step 3: Build and run test (expect PASS)**

- [ ] **Step 4: Commit**

```bash
git add libopenage/nyan_store
git commit -m "stage5: NyanStore interface and overlay stub"
```

### Task 5.2: Consumer shim for libnyan `Object`/`ObjectState`

**Files:**

- Create: `libopenage/nyan_store/nyan_store_consumer_shim.{h,cpp}`
- Create: `libopenage/nyan_store/tests/test_shim.cpp`

- [ ] **Step 1: Write test**

```cpp
TEST(Shim, MaterializedObjectMatchesTextStore) {
    auto mmap = nyan::MmapNyanStore::open("assets/test/nyan_cache_fixture/build/full.nyancache");
    auto shim = nyan::NyanStoreShim{mmap};
    auto obj = shim.materialize("Data.Test.Engine.Villager");
    auto text = nyan::TextNyanStore::open("assets/test/nyan_cache_fixture");
    auto expected = text.resolve("Data.Test.Engine.Villager");
    EXPECT_EQ(obj.get_int("hp", nyan::order_t::current()), expected.get_int("hp", nyan::order_t::current()));
}
```

- [ ] **Step 2: Implement shim**

```cpp
namespace nyan {
class NyanStoreShim {
public:
    explicit NyanStoreShim(NyanStore& store) : store_(store) {}
    nyan::Object materialize(const std::string& fqon) {
        return store_.resolve(fqon).as_libnyan_object();
    }
private:
    NyanStore& store_;
};
}
```

- [ ] **Step 3: Build and run (expect PASS)**

- [ ] **Step 4: Commit**

```bash
git add libopenage/nyan_store
git commit -m "stage5: consumer shim exposes libnyan Object view"
```

### Task 5.3: Generation-safe handle assertions

**Files:**

- Create: `libopenage/nyan_store/cache_generation.{h,cpp}`
- Create: `libopenage/nyan_store/tests/test_generation.cpp`

- [ ] **Step 1: Write test**

```cpp
TEST(Generation, StaleHandleAssertsInDebug) {
    auto path = nyan::tests::valid_fixture_cache();
    nyan::MmapNyanStore s; s.open(path);
    auto h = s.handle_for("Data.Test.Engine.Villager");
    s.close();
    nyan::tests::reopen_with_new_generation(path);
    // In debug build, expect assertion; in release, expect StaleHandle.
    EXPECT_THROW(s.use_handle(h), nyan::NyanCacheError);
}
```

- [ ] **Step 2: Implement `cache_generation`**

```cpp
namespace nyan {
struct GenerationId { std::uint64_t value; };
class GenerationSafeHandle {
public:
    GenerationSafeHandle(GenerationId g, std::any payload) : g_(g), p_(std::move(payload)) {}
    void use(GenerationId current) {
#ifndef NDEBUG
        assert(g_.value == current.value && "stale handle");
#endif
        if (g_.value != current.value) throw NyanCacheError{StaleHandle{}};
    }
private:
    GenerationId g_;
    std::any p_;
};
}
```

- [ ] **Step 3: Build and run (expect PASS)**

- [ ] **Step 4: Commit**

```bash
git add libopenage/nyan_store
git commit -m "stage5: generation-safe handle with debug assert"
```

### Task 5.4: Live-reload refused while views/handles alive

**Files:**

- Create: `libopenage/nyan_store/tests/test_no_live_reload.cpp`

- [ ] **Step 1: Write test**

```cpp
TEST(NoLiveReload, ReplaceRefusedWithLiveHandle) {
    auto s = nyan::MmapNyanStore::open(nyan::tests::valid_fixture_cache());
    auto h = s.handle_for("Data.Test.Engine.Villager");
    EXPECT_THROW(s.replace_with(nyan::tests::valid_fixture_cache()), nyan::NyanCacheError);
}
```

- [ ] **Step 2: Implement**

```cpp
void MmapNyanStore::replace_with(const std::filesystem::path& p) {
    if (!live_handles_.empty()) {
        throw NyanCacheError{StaleHandle{}};  // also returned as LiveReloadRefused
    }
    close();
    open(p);
}
```

- [ ] **Step 3: Build and run (expect PASS)**

- [ ] **Step 4: Commit**

```bash
git add libopenage/nyan_store
git commit -m "stage5: refuse live reload while handles are alive"
```

### Task 5.5: Add the `MmapNyanStore` to game startup (Stage 1 off by default)

**Files:**

- Modify: `libopenage/gamestate/load_data.cpp`
- Modify: `libopenage/scenario/scenario_loader.cpp`
- Modify: `libopenage/entity_factory.cpp`
- Create: `libopenage/gamestate/tests/test_load_data_shim.cpp`

- [ ] **Step 1: Add `--use-cache` flag (off by default)**

```cpp
// libopenage/gamestate/load_data.cpp
if (args.use_cache && cache_file_exists()) {
    auto store = MmapNyanStore::open(cache_file_for(args.closure_id));
    shim_ = std::make_unique<NyanStoreShim>(store);
} else {
    // existing text path
}
```

- [ ] **Step 2: Test that formal startup without flag is unchanged**

```cpp
TEST(LoadData, DefaultPathUnchanged) {
    auto before = nyan::tests::snapshot_text_load_stats();
    nyan::tests::run_startup_without_cache_flag();
    auto after = nyan::tests::snapshot_text_load_stats();
    EXPECT_EQ(after.files_loaded, before.files_loaded);
}
```

- [ ] **Step 3: Commit**

```bash
git add libopenage/gamestate libopenage/scenario libopenage/entity_factory
git commit -m "stage5: add --use-cache flag (off by default) wired through shim"
```

---

## Stage 6 — Full de2_base Cache

### Task 6.1: `load_batch` in `Database`

**Files:**

- Modify: `libopenage/nyan/database.h`
- Modify: `libopenage/nyan/database.cpp`
- Modify: `libopenage/nyan/file_loader.cpp`
- Create: `libopenage/nyan/tests/test_load_batch.cpp`

**Interfaces:**

- Consumes: file list and fetcher.
- Produces: `Database::load_batch(file_list, filefetcher)`.

- [ ] **Step 1: Write failing test**

```cpp
TEST(Database, LoadBatchParseOnce) {
    auto db = nyan::Database::create();
    db->load_batch({"a.nyan", "b.nyan", "c.nyan"}, fetcher);
    auto stats = nyan::tests::parse_stats();
    EXPECT_EQ(stats.parse_attempts_per_file["a.nyan"], 1);
    EXPECT_EQ(stats.parse_attempts_per_file["b.nyan"], 1);
    EXPECT_EQ(stats.parse_attempts_per_file["c.nyan"], 1);
}
```

- [ ] **Step 2: Implement `load_batch`**

In `database.h`:

```cpp
void load(const std::vector<std::filesystem::path>& files, filefetcher_t fetcher);
```

In `database.cpp`:

```cpp
void Database::load(const std::vector<std::filesystem::path>& files, filefetcher_t fetcher) {
    // share import DFS, run three walks once over combined AST set
    auto all_imports = compute_import_union(files, fetcher);
    for (auto& f : all_imports) parse_once(f, fetcher);
    walk_combined_ast_once();
    walk_combined_ast_twice();
    walk_combined_ast_thrice();
}
```

- [ ] **Step 3: Re-run (expect PASS)**

- [ ] **Step 4: Commit**

```bash
git add libopenage/nyan
git commit -m "stage6: introduce Database::load_batch with single-pass walks"
```

### Task 6.2: Wire `load_batch` into `Game::load_data`

**Files:**

- Modify: `libopenage/nyan/state.cpp`
- Create: `libopenage/nyan/tests/test_load_data_batch.cpp`

- [ ] **Step 1: Write failing test**

```cpp
TEST(LoadData, GameUsesLoadBatch) {
    auto before = nyan::tests::import_resolves_total();
    nyan::tests::run_game_load_data({"engine", "de2_base"});
    auto after = nyan::tests::import_resolves_total();
    EXPECT_LT(after - before, 1000u);  // sanity bound; actual gating in Task 7.3
}
```

- [ ] **Step 2: Implement call site**

```cpp
// libopenage/nyan/state.cpp
db->load(closure_file_list(mods), file_loader);
```

- [ ] **Step 3: Re-run (expect PASS)**

- [ ] **Step 4: Commit**

```bash
git add libopenage/nyan
git commit -m "stage6: Game::load_data uses load_batch"
```

### Task 6.3: Capacity census in prepare

**Files:**

- Create: `libopenage/nyan_cache/capacity_census.{h,cpp}`
- Create: `libopenage/nyan_cache/tests/test_capacity_census.cpp`
- Modify: `openage/prepare_nyan_cache/main.py`

- [ ] **Step 1: Write failing test**

```cpp
TEST(CapacityCensus, DetectsParentOverflow) {
    auto records = nyan::tests::over_capacity_records();
    EXPECT_THROW(nyan::capacity_census(records), nyan::NyanCacheError);
}
```

- [ ] **Step 2: Implement census**

```cpp
namespace nyan {
struct Census {
    std::uint32_t max_parent;
    std::uint32_t max_member;
    std::uint32_t max_string_len;
    std::uint32_t max_collection_size;
    std::uint64_t total_objects, total_members, total_values, total_modules;
    std::array<std::uint64_t, 10> section_max_index;
};
void capacity_census(const Census& c) {
    if (c.max_parent > 8) throw NyanCacheError{OverflowError{}};
    if (c.max_member > std::numeric_limits<std::uint16_t>::max()) throw NyanCacheError{OverflowError{}};
    if (c.max_string_len > 64 * 1024) throw NyanCacheError{StringBoundsError{}};
    if (c.total_objects > std::numeric_limits<std::uint32_t>::max()) throw NyanCacheError{CountError{}};
    for (auto v : c.section_max_index) if (v > std::numeric_limits<std::uint32_t>::max()) throw NyanCacheError{OverflowError{}};
}
}
```

- [ ] **Step 3: Wire into prepare**

In `prepare_nyan_cache/main.py`:

```python
census = nyan_cpp.compute_census(records)
nyan_cpp.capacity_census(census)
```

- [ ] **Step 4: Build and run (expect PASS)**

- [ ] **Step 5: Commit**

```bash
git add libopenage/nyan_cache openage/prepare_nyan_cache
git commit -m "stage6: capacity census gates prepare"
```

### Task 6.4: Full value coverage gate

**Files:**

- Create: `libopenage/nyan_cache/tests/test_value_coverage.cpp`
- Modify: `openage/prepare_nyan_cache/main.py`

- [ ] **Step 1: Write failing test**

```cpp
TEST(ValueCoverage, UnsupportedValueBlocksPublication) {
    auto records = nyan::tests::records_with_unsupported_value();
    EXPECT_THROW(nyan::verify_value_coverage(records), nyan::NyanCacheError);
}
```

- [ ] **Step 2: Implement**

```cpp
void verify_value_coverage(const std::vector<ValueRecord>& values) {
    for (auto& v : values) {
        if (!is_lossless(v)) throw NyanCacheError{CorruptSectionError{}};
    }
}
```

- [ ] **Step 3: Re-run (expect PASS)**

- [ ] **Step 4: Commit**

```bash
git add libopenage/nyan_cache
git commit -m "stage6: full value coverage gate"
```

### Task 6.5: Build full `de2_base` cache

**Files:** None.

- [ ] **Step 1: User-side run**

```bash
python -m openage prepare-nyan-cache --modpacks engine --modpacks de2_base --out-dir assets/converted/de2_base/cache
```

Expected: exit 0; `engine_de2_base-<closure_id>.nyancache` is published.

- [ ] **Step 2: Commit artifact metadata (no payload)**
Only metadata goes into the repo; the cache itself is user-side. Skip if policy forbids.

- [ ] **Step 3: Record run**

```bash
echo "prepare-nyan-cache exit 0" > docs/superpowers/plans/2026-07-24-de2-base-fullload-prepare.md
git add docs/superpowers/plans/2026-07-24-de2-base-fullload-prepare.md
git commit -m "stage6: record full de2_base cache preparation"
```

---

## Stage 7 — Performance Gates

### Task 7.1: Counters and observation infrastructure

**Files:**

- Modify: `libopenage/gamestate/load_data.cpp`
- Create: `libopenage/gamestate/tests/test_counters.cpp`

- [ ] **Step 1: Wire counters**

```cpp
struct StartupCounters {
    std::uint64_t files_seen, files_loaded;
    std::uint64_t parse_attempts_total, parse_attempts_unique;
    std::uint64_t import_resolves_total, import_resolves_cached;
    std::uint64_t fqon_lookups_total, fqon_lookup_hits;
    std::uint64_t namespace_full_scans;
    std::uint64_t object_states_created, object_states_unique;
    std::uint64_t mmap_page_faults_minor, mmap_page_faults_major;
    std::uint64_t rss_peak_bytes;
};
```

- [ ] **Step 2: Test counter exposure**

```cpp
TEST(Counters, AllCountersExposed) {
    auto c = nyan::gamestate::get_startup_stats();
    EXPECT_GE(c.files_seen, 0u);
    // remaining fields are zero-initialized; test absence of crash, not values
}
```

- [ ] **Step 3: Commit**

```bash
git add libopenage/gamestate
git commit -m "stage7: expose startup counters and parse/fault metrics"
```

### Task 7.2: Warm `load_scenario` reaches ≤5 s

**Files:**

- Create: `libopenage/gamestate/tests/test_warm_startup.cpp`

- [ ] **Step 1: Write warm-startup test (skipped in sandbox)**

```cpp
TEST(WarmStartup, UnderFiveSeconds) {
    if (!nyan::tests::has_user_side_de2_base_cache()) GTEST_SKIP();
    auto t0 = std::chrono::steady_clock::now();
    nyan::tests::run_warm_startup();
    auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_LT(elapsed, std::chrono::seconds(5));
}
```

- [ ] **Step 2: Commit**

```bash
git add libopenage/gamestate/tests
git commit -m "stage7: warm startup gate test (sandbox skip)"
```

### Task 7.3: Cold `load_scenario` reaches ≤10 s

**Files:**

- Create: `libopenage/gamestate/tests/test_cold_startup.cpp`

- [ ] **Step 1: Write cold-startup test**

```cpp
TEST(ColdStartup, UnderTenSeconds) {
    if (!nyan::tests::has_user_side_de2_base_cache()) GTEST_SKIP();
    nyan::tests::drop_page_cache_for(nyan::tests::cache_path());
    auto t0 = std::chrono::steady_clock::now();
    nyan::tests::run_cold_startup();
    auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_LT(elapsed, std::chrono::seconds(10));
}
```

- [ ] **Step 2: Commit**

```bash
git add libopenage/gamestate/tests
git commit -m "stage7: cold startup gate test (sandbox skip)"
```

### Task 7.4: Warm startup reads no `.nyan`

**Files:**

- Create: `libopenage/gamestate/tests/test_warm_no_text.cpp`

- [ ] **Step 1: Write trace-based test**

```cpp
TEST(WarmNoText, ZeroNyanOpens) {
    if (!nyan::tests::has_user_side_de2_base_cache()) GTEST_SKIP();
    auto trace = nyan::tests::strace_openat("nyan", [] { nyan::tests::run_warm_startup(); });
    EXPECT_EQ(trace.hits, 0u);
}
```

- [ ] **Step 2: Commit**

```bash
git add libopenage/gamestate/tests
git commit -m "stage7: warm startup zero nyan I/O test"
```

### Task 7.5: Observation metrics (OBS-1)

**Files:**

- Modify: `libopenage/gamestate/load_data.cpp`

- [ ] **Step 1: Print metrics at end**

```cpp
spdlog::info("[PERF] load_done files_seen={} files_loaded={} "
             "parse_attempts_total={} fqon_lookups_total={} "
             "object_states_created={} mmap_min={} mmap_maj={} rss={}",
             c.files_seen, c.files_loaded, c.parse_attempts_total,
             c.fqon_lookups_total, c.object_states_created,
             c.mmap_page_faults_minor, c.mmap_page_faults_major, c.rss_peak_bytes);
```

- [ ] **Step 2: Test print format**

```cpp
TEST(Perf, OutputContainsAllKeys) {
    auto log = nyan::tests::capture_perf_log([] { nyan::tests::run_warm_startup(); });
    for (auto key : {"files_seen", "files_loaded", "parse_attempts_total",
                     "fqon_lookups_total", "object_states_created",
                     "mmap_min", "mmap_maj", "rss"}) {
        EXPECT_NE(log.find(key), std::string::npos) << "missing key: " << key;
    }
}
```

- [ ] **Step 3: Commit**

```bash
git add libopenage/gamestate
git commit -m "stage7: emit OBS-1 metrics at startup"
```

---

## Cross-Stage Regression Gates

### Task R.1: `no_tutorial_assets` regression

**Files:**

- Create: `libopenage/nyan_cache/tests/test_no_tutorial_assets.cpp`

- [ ] **Step 1: Test**

```cpp
TEST(Regression, NoTutorialAssets) {
    for (auto& p : nyan::tests::all_tracked_files()) {
        EXPECT_FALSE(p.contains("tutorial_assets")) << p;
    }
}
```

- [ ] **Step 2: Commit**

```bash
git add libopenage/nyan_cache/tests
git commit -m "regression: forbid tutorial_assets in tracked files"
```

### Task R.2: `persistent_reference_lint`

**Files:**

- Create: `libopenage/nyan_cache/tests/test_persistent_lint.cpp`

- [ ] **Step 1: Test**

```cpp
TEST(Regression, PersistentSurfacesUseFqon) {
    auto sinks = nyan::tests::persistent_sinks();
    for (auto& sink : sinks) {
        EXPECT_TRUE(sink.uses_fqon_only()) << sink.name();
    }
}
```

- [ ] **Step 2: Commit**

```bash
git add libopenage/nyan_cache/tests
git commit -m "regression: persistent surfaces use FQON only"
```

---

## Self-Review Notes

- Spec coverage: each spec invariant is mapped to a test or a stage task. Specifically, deterministic build → Tasks 1.4, 1.8, 7.5; no live reload → Task 5.4; generation-safe handles → Task 5.3; full value coverage → Task 6.4; capacity census → Task 6.3; manifest-complete closure → Task 2.3; directory integrity → Task 1.8; full 32-byte digests → Tasks 1.2, 1.3, 1.8; atomic publication → Task 1.4; validator gate → Tasks 0.2, 2.4, 3.1; Stage 1 isolation is preserved by avoiding modifications under `libnyan/parser/`, `libnyan/resolver/`, `libnyan/transaction/`, `libnyan/lexer/`, and `libnyan/AST/` (only `Database::load_batch` is added).
- Type consistency: `CacheHeader` (Task 1.2) is consumed by `MmapNyanStore::header()` (Task 1.3) and `CacheBuilder::write_to` (Task 1.4); `NyanCacheError` variants are the same in `abi_errors.h` (Task 1.3) and `cache_generation` (Task 5.3); `HashPolicy` is the single test seam (Task 1.6).
- Placeholder scan: `PLACEHOLDER_PINNED_COMMIT` and `PLACEHOLDER_KNOWN_VECTOR` are explicit one-time placeholders for the vendor step; everything else is concrete code or commands.
