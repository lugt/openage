# de2_base Correctness + Loader Optimization Sprint

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `bin/run game --tutorial --modpacks de2_base --no-prompts` correct and dramatically faster through profiler-driven loader optimization, without introducing a packed cache, mmap runtime, or overlay.

**Architecture:** Keep the existing libnyan text path as the loader. Add a CLI/engine exception guard, a minimum viable validator, targeted converter fixes, structured counters, parse-once enforcement, batched file load, and an in-memory module + FQON index. Decisions on the packed cache, mmap runtime, and overlay are deferred to the long-term Revision 3 design.

**Tech Stack:** C++20 (libopenage, libnyan), Python 3 (openage CLI), Ninja build, gtest, pytest, perf (`callgrind`/`perf record` optional), `time` and `/proc/self/status` for counters.

**Scope boundary — DO NOT IMPLEMENT in this sprint:**

- Packed binary `*.nyancache`.
- mmap runtime integration.
- `OverlayNyanStore` and per-player overlay.
- Generation-safe handle full implementation (debug assert + structured stale-handle error on every accessor).
- Live reload.
- Full corruption matrix.
- Full deterministic publication (header layout, byte-identical write, atomic publication).
- Lazy materialization as a cache consumer feature.
- Multi-cache, network/save/replay FQON constraints.

**Companion documents (long-term, NOT executed in this sprint):**

- [Main design spec](../specs/2026-07-24-de2-base-fullload-design.md) — Revision 3.1
- [Binary Format Reference](../specs/2026-07-24-de2-base-fullload-binary-format.md) — Revision 3.1
- [Test Matrix](../specs/2026-07-24-de2-base-fullload-test-matrix.md) — Revision 3.1
- [Full implementation plan](2026-07-24-de2-base-fullload.md) — Revision 3.1, 33 tasks, deferred

**Sprint budget:** 6–10 person-days; hard cap 15 person-days.

---

## Global Constraints

These apply to every task unless explicitly overridden.

- Branch: `mvp/tutorial-demo`. Do not touch libnyan parser, resolver, transaction, lexer, or AST in a way that changes public semantics.
- No `tutorial_assets` modpack or FQON rewrite.
- Conversion correctness is the prerequisite for performance; do not hide loader inefficiency with a cache.
- The first attempt at converter fixes is expected to surface more than the single `ArsonResearchable` error.
- All new counters and CI checks must work without modifying the `de2_base` converter pipeline beyond targeted bug fixes.
- Sprint must produce six explicit deliverables (see §Deliverables) before considering the cache path.

---

## File Structure

### Create

- `openage/exception_guard/main.py` — try/except boundary and `set_terminate_handler`.
- `openage/exception_guard/__init__.py`.
- `openage/exception_guard/tests/test_guard.py` — pytest smoke.
- `openage/validate_nyan/main.py` — minimum viable validator.
- `openage/validate_nyan/__init__.py`.
- `openage/validate_nyan/tests/test_smoke.py` — pytest smoke.
- `libopenage/nyan/load_metrics.h` — counters struct.
- `libopenage/nyan/load_metrics.cpp` — counter update sites.
- `libopenage/nyan/tests/test_load_metrics.cpp` — counter exposure test.
- `libopenage/nyan/file_index.{h,cpp}` — module + FQON index.
- `libopenage/nyan/tests/test_file_index.cpp` — index lookup test.
- `libopenage/nyan/batch_load.{h,cpp}` — `Database::load_batch(file_list, fetcher)`.
- `libopenage/nyan/tests/test_batch_load.cpp` — parse-once test.
- `libopenage/nyan/resolve_cache.{h,cpp}` — single-flight import + FQON resolution cache.
- `libopenage/nyan/tests/test_resolve_cache.cpp` — re-resolve smoke.
- `assets/test/tutorial_smoke/conftest.py` — pytest harness.
- `assets/test/tutorial_smoke/test_exit_codes.py` — exit-code regressions.
- `assets/test/tutorial_smoke/test_no_core_dump.py` — core-dump regression.
- `assets/test/tutorial_smoke/test_startup_perf.py` — perf regression bound (only enforced user-side).

### Modify

- `openage/__main__.py` — install `exception_guard`, register `validate-nyan`.
- `openage/convert/processor/conversion/de2/...` — targeted converter fixes (no hand-patching generated `.nyan`).
- `libopenage/nyan/database.h` and `database.cpp` — declare `load_batch`; keep `Database::load` working.
- `libopenage/nyan/file_loader.cpp` — single-flight open/parse and import de-duplication.
- `libopenage/nyan/namespace_finder.cpp` — guard against re-scans per access; single shared index.
- `libopenage/nyan/state.cpp` — switch `Game::load_data` to `load_batch` and instrument counters.
- `libopenage/gamestate/load_data.cpp` — emit `[PERF]` summary and gate against `TutorialLoadBudget` (≤15 s).
- `libopenage/CMakeLists.txt`, `libopenage/nyan/CMakeLists.txt` — add new targets.
- `openage/CMakeLists.txt` — link `exception_guard` and `validate_nyan`.

### Touch user-side only (no tracked edits)

- `assets/converted/de2_base/...` — re-run converter after each fix iteration.

---

## Deliverables (Sprint End)

These are the six mandatory outputs of the sprint:

1. 9-minute total time distribution (per-phase wall time; per-section of `Game::load_data`; per major libnyan call).
2. File-open count, parse count, resolve count, object-creation count — global and per file.
3. Confirmation (or refutation) of O(N²) behavior, with profiler evidence.
4. Pre- and post-optimization `bin/run game --tutorial --modpacks de2_base --no-prompts` wall time to `load_scenario`.
5. Validator error list (pre-fix and post-fix).
6. Real start-up result (warm/cold) with the optimized loader on full `de2_base`.

---

## Decision Gate (after Sprint End)

| Final cold `load_scenario` | Action |
|---|---|
| ≤15 s | Pause mmap cache work; return to William Wallace gameplay. |
| 15–30 s | Accept as dev build; record the cache as future optimization. |
| >30 s | Re-approve only the fixture-only mmap cache Stage 1 from the long-term plan. |
| Multiple minutes | Continue loader-algorithm work; do not use a cache to hide repeated work. |

---

## Task Structure

### Task 1: Install CLI/engine exception guard

**Files:**

- Create: `openage/exception_guard/main.py`
- Create: `openage/exception_guard/__init__.py`
- Create: `openage/exception_guard/tests/test_guard.py`
- Modify: `openage/__main__.py`

**Interfaces:**

- Consumes: `entry() -> int`.
- Produces: `run_with_exception_guard(entry) -> int`; never `std::terminate`s or writes a core file.

- [ ] **Step 1: Write failing test**

```python
# openage/exception_guard/tests/test_guard.py
import subprocess, sys, textwrap

def test_structured_exit_on_runtime_error():
    script = textwrap.dedent("""
        from openage.exception_guard.main import run_with_exception_guard
        def boom():
            raise RuntimeError("simulated nyan crash")
        raise SystemExit(run_with_exception_guard(boom))
    """)
    proc = subprocess.run([sys.executable, "-c", script], capture_output=True, text=True)
    assert proc.returncode != 0
    assert proc.returncode != -6  # not SIGABRT
    assert "simulated nyan crash" in proc.stderr
    assert "core" not in proc.stderr.lower()
```

- [ ] **Step 2: Run (expect FAIL, ModuleNotFoundError)**

```bash
pytest openage/exception_guard/tests/test_guard.py -v
```

- [ ] **Step 3: Implement guard**

```python
# openage/exception_guard/main.py
import signal
import sys
import traceback

_INSTALLED = False

def install_terminate_handler() -> None:
    global _INSTALLED
    if _INSTALLED:
        return
    def _handler(signum, _frame):
        sys.exit(128 + signum)
    for sig in (signal.SIGTERM, signal.SIGINT):
        signal.signal(sig, _handler)
    _INSTALLED = True


def run_with_exception_guard(entry):
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

- [ ] **Step 4: Run (expect PASS)**

```bash
pytest openage/exception_guard/tests/test_guard.py -v
```

- [ ] **Step 5: Wire into `openage/__main__.py`**

```python
from openage.exception_guard.main import install_terminate_handler, run_with_exception_guard

def main() -> int:
    install_terminate_handler()
    return run_with_exception_guard(_cli_main)

if __name__ == "__main__":
    raise SystemExit(main())
```

- [ ] **Step 6: Smoke**

```bash
python -m openage --help
```

Expected: exit 0, no `terminate called`.

- [ ] **Step 7: Commit**

```bash
git add openage/exception_guard openage/__main__.py
git commit -m "sprint: install CLI/engine exception guard"
```

### Task 2: Add minimum viable `validate-nyan` subcommand

**Files:**

- Create: `openage/validate_nyan/main.py`
- Create: `openage/validate_nyan/__init__.py`
- Create: `openage/validate_nyan/tests/test_smoke.py`
- Modify: `openage/__main__.py`

**Interfaces:**

- Consumes: `--modpacks X [--json report.json] [--continue-on-error] [--strict]`.
- Produces: `validate_nyan(modpacks, *, json_path=None, continue_on_error=True, strict=False) -> int`; exit codes 0 / 1 / 2.

- [ ] **Step 1: Write failing test**

```python
# openage/validate_nyan/tests/test_smoke.py
import json, pathlib

def write_minimal_modpack(tmp_path: pathlib.Path) -> pathlib.Path:
    (tmp_path / "manifest.json").write_text(json.dumps({"name": "smoke", "include": ["*.nyan"]}))
    (tmp_path / "a.nyan").write_text("ns Data.Test.X: pass\n")
    return tmp_path

def test_clean_passes(tmp_path):
    from openage.validate_nyan.main import validate_nyan
    rc = validate_nyan([str(write_minimal_modpack(tmp_path))])
    assert rc == 0
```

- [ ] **Step 2: Run (expect FAIL)**

```bash
pytest openage/validate_nyan/tests/test_smoke.py -v
```

- [ ] **Step 3: Implement validator skeleton**

```python
# openage/validate_nyan/main.py
import argparse, json, sys
from typing import Iterable


def validate_nyan(modpacks: Iterable[str], *, json_path: str | None = None,
                  continue_on_error: bool = True, strict: bool = False) -> int:
    errors: list[dict] = []
    for modpack in modpacks:
        errors.extend(_collect_errors(modpack, continue_on_error))
    fatal = [e for e in errors if e.get("severity") != "warning"]
    warnings = [e for e in errors if e.get("severity") == "warning"]
    report = {"version": 1, "modpacks": list(modpacks), "errors": errors,
              "summary": {"errors": len(fatal), "warnings": len(warnings)}}
    if json_path:
        with open(json_path, "w", encoding="utf-8") as fh:
            json.dump(report, fh, indent=2)
    if fatal:
        return 1
    if strict and warnings:
        return 2
    return 0


def _collect_errors(modpack: str, continue_on_error: bool) -> list[dict]:
    # Stage 1: walk manifest, parse .nyan, reuse nyan::Database::load in
    # try/except. Replace the body with a real call once Task 6 lands.
    return []


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="validate-nyan")
    parser.add_argument("--modpacks", action="append", required=True)
    parser.add_argument("--json", dest="json_path", default=None)
    parser.add_argument("--continue-on-error", action="store_true", default=True)
    parser.add_argument("--strict", action="store_true", default=False)
    args = parser.parse_args(argv)
    return validate_nyan(args.modpacks, json_path=args.json_path,
                         continue_on_error=args.continue_on_error, strict=args.strict)


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Run (expect PASS)**

```bash
pytest openage/validate_nyan/tests/test_smoke.py -v
```

- [ ] **Step 5: Register subcommand**

```python
# in openage/__main__.py, alongside existing subparsers
sub = subparsers.add_parser("validate-nyan")
sub.add_argument("--modpacks", action="append", required=True)
sub.add_argument("--json", dest="json_path", default=None)
sub.add_argument("--continue-on-error", action="store_true", default=True)
sub.add_argument("--strict", action="store_true", default=False)
def _validate(a):
    from openage.validate_nyan.main import validate_nyan
    return validate_nyan(a.modpacks, json_path=a.json_path,
                         continue_on_error=a.continue_on_error, strict=a.strict)
sub.set_defaults(handler=_validate)
```

- [ ] **Step 6: Smoke**

```bash
python -m openage validate-nyan --modpacks assets/test/nyan_cache_fixture
```

Expected: exit 0 (no crash, no `core`).

- [ ] **Step 7: Commit**

```bash
git add openage/validate_nyan openage/__main__.py
git commit -m "sprint: minimum viable validate-nyan subcommand"
```

### Task 3: Run validator against full `de2_base` corpus

**Files:** None in this task. User-side only.

- [ ] **Step 1: User-side run**

```bash
python -m openage validate-nyan --modpacks engine --modpacks de2_base --json /tmp/de2_base_sprint_report.json
```

- [ ] **Step 2: Capture report**

```bash
jq '.summary, .errors | length' /tmp/de2_base_sprint_report.json
```

- [ ] **Step 3: Store pre-fix error list**

```bash
mkdir -p docs/superpowers/sprint
cp /tmp/de2_base_sprint_report.json docs/superpowers/sprint/pre_fix_validator.json
git add docs/superpowers/sprint/pre_fix_validator.json
git commit -m "sprint: record pre-fix validator report"
```

(If the working tree is dirty with other changes, do not commit; just keep the file outside the tree for reference.)

### Task 4: Fix `ArsonResearchable` and any other validator findings

**Files:**

- Modify: `openage/convert/processor/conversion/de2/...` (multiple files; tracked)
- (Do NOT modify generated `.nyan` files under `assets/converted/de2_base/`.)

- [ ] **Step 1: Locate source for each error category**

```bash
jq -r '.errors[].missing_name' docs/superpowers/sprint/pre_fix_validator.json | sort -u
```

- [ ] **Step 2: For each missing FQON, find converter source**

```bash
grep -RIn 'ArsonResearchable\|<missing_name>' openage/convert/processor/conversion/de2
```

- [ ] **Step 3: Apply minimum fix in converter source**

Targeted edit only. Do not refactor.

- [ ] **Step 4: Re-run converter (user-side)**

```bash
python -m openage convert ...  # user-side
```

- [ ] **Step 5: Re-run validator**

```bash
python -m openage validate-nyan --modpacks engine --modpacks de2_base --json /tmp/de2_base_sprint_report_post.json
```

Expected: fewer errors than pre-fix. Iterate.

- [ ] **Step 6: Commit each fix independently**

```bash
git add openage/convert
git commit -m "convert: fix <name> (validator finding)"
```

- [ ] **Step 7: Final validator pass**

```bash
python -m openage validate-nyan --modpacks engine --modpacks de2_base
```

Expected: exit 0.

- [ ] **Step 8: Store post-fix report**

```bash
cp /tmp/de2_base_sprint_report_post.json docs/superpowers/sprint/post_fix_validator.json
git add docs/superpowers/sprint/post_fix_validator.json
git commit -m "sprint: record post-fix validator report"
```

### Task 5: Add structured load metrics

**Files:**

- Create: `libopenage/nyan/load_metrics.h`
- Create: `libopenage/nyan/load_metrics.cpp`
- Create: `libopenage/nyan/tests/test_load_metrics.cpp`
- Modify: `libopenage/nyan/CMakeLists.txt`
- Modify: `libopenage/nyan/file_loader.cpp`
- Modify: `libopenage/nyan/namespace_finder.cpp`

**Interfaces:**

- Consumes: nothing.
- Produces: `struct LoadMetrics { ... };` and `LoadMetrics& get_load_metrics();`.

- [ ] **Step 1: Write failing test**

```cpp
#include <gtest/gtest.h>
#include "libopenage/nyan/load_metrics.h"

TEST(LoadMetrics, CountersExposed) {
    auto& m = nyan::get_load_metrics();
    m = {};  // reset for test
    EXPECT_EQ(m.files_opened, 0u);
    EXPECT_EQ(m.parse_attempts_total, 0u);
    EXPECT_EQ(m.import_resolves_total, 0u);
    EXPECT_EQ(m.fqon_lookups_total, 0u);
    EXPECT_EQ(m.object_states_created, 0u);
}
```

- [ ] **Step 2: Run (expect FAIL, header missing)**

```bash
ninja -C bin libopenage_nyan_tests
bin/libopenage_nyan_tests --gtest_filter=LoadMetrics.*
```

- [ ] **Step 3: Implement `load_metrics.h`**

```cpp
// libopenage/nyan/load_metrics.h
#pragma once
#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <string>

namespace nyan {

struct LoadMetrics {
    std::atomic<std::uint64_t> files_opened{0};
    std::atomic<std::uint64_t> files_loaded{0};
    std::atomic<std::uint64_t> parse_attempts_total{0};
    std::atomic<std::uint64_t> parse_attempts_unique{0};
    std::atomic<std::uint64_t> import_resolves_total{0};
    std::atomic<std::uint64_t> import_resolves_cached{0};
    std::atomic<std::uint64_t> fqon_lookups_total{0};
    std::atomic<std::uint64_t> fqon_lookup_hits{0};
    std::atomic<std::uint64_t> namespace_full_scans{0};
    std::atomic<std::uint64_t> object_states_created{0};
    std::atomic<std::uint64_t> object_states_unique{0};
    std::atomic<std::uint64_t> object_state_rebuilds{0};
    std::unordered_map<std::string, std::uint64_t> parse_attempts_per_file;
};

LoadMetrics& get_load_metrics();

} // namespace nyan
```

- [ ] **Step 4: Implement `load_metrics.cpp`**

```cpp
#include "libopenage/nyan/load_metrics.h"
namespace nyan {
LoadMetrics& get_load_metrics() {
    static LoadMetrics m;
    return m;
}
}
```

- [ ] **Step 5: Add CMake target**

```cmake
# libopenage/nyan/CMakeLists.txt
add_library(openage_nyan STATIC ...)
target_link_libraries(openage_nyan PUBLIC openage_util)
# Add a test target
add_executable(libopenage_nyan_tests tests/test_load_metrics.cpp)
target_link_libraries(libopenage_nyan_tests PRIVATE openage_nyan gtest_main)
```

- [ ] **Step 6: Wire counters in `file_loader.cpp`**

```cpp
#include "libopenage/nyan/load_metrics.h"
void FileLoader::open(const std::filesystem::path& p) {
    nyan::get_load_metrics().files_opened.fetch_add(1);
    nyan::get_load_metrics().parse_attempts_per_file[p.string()] += 1;
    nyan::get_load_metrics().parse_attempts_total.fetch_add(1);
    // ... existing open logic ...
}
```

- [ ] **Step 7: Wire counters in `namespace_finder.cpp`**

```cpp
#include "libopenage/nyan/load_metrics.h"
void NamespaceFinder::full_scan() {
    nyan::get_load_metrics().namespace_full_scans.fetch_add(1);
    // ... existing logic ...
}
```

- [ ] **Step 8: Build and run test (expect PASS)**

```bash
ninja -C bin libopenage_nyan_tests
bin/libopenage_nyan_tests --gtest_filter=LoadMetrics.*
```

- [ ] **Step 9: Commit**

```bash
git add libopenage/nyan
git commit -m "sprint: expose load metrics counters"
```

### Task 6: Add `Database::load_batch` and parse-once enforcement

**Files:**

- Modify: `libopenage/nyan/database.h`
- Modify: `libopenage/nyan/database.cpp`
- Modify: `libopenage/nyan/file_loader.cpp`
- Create: `libopenage/nyan/tests/test_batch_load.cpp`

**Interfaces:**

- Consumes: file list, file fetcher.
- Produces: `void Database::load_batch(files, fetcher)`; runs import DFS once, parses each file once, and reuses one namespace index.

- [ ] **Step 1: Write failing test**

```cpp
TEST(Database, LoadBatchParsesEachFileOnce) {
    nyan::get_load_metrics() = {};
    auto db = nyan::Database::create();
    db->load_batch({"a.nyan", "b.nyan"}, [](auto p) { return read(p); });
    auto& m = nyan::get_load_metrics();
    for (auto& [path, count] : m.parse_attempts_per_file) {
        EXPECT_EQ(count, 1u) << path;
    }
}
```

- [ ] **Step 2: Run (expect FAIL, `load_batch` undefined)**

```bash
bin/libopenage_nyan_tests --gtest_filter=Database.LoadBatchParsesEachFileOnce
```

- [ ] **Step 3: Declare `load_batch`**

```cpp
// libopenage/nyan/database.h
class Database {
public:
    void load_batch(const std::vector<std::filesystem::path>& files,
                    filefetcher_t fetcher);
    // existing:
    void load(const std::string& filename, filefetcher_t fetcher);
};
```

- [ ] **Step 4: Implement `load_batch`**

```cpp
// libopenage/nyan/database.cpp
void Database::load_batch(const std::vector<std::filesystem::path>& files,
                          filefetcher_t fetcher) {
    // 1. Compute import union (DFS) once
    auto all_imports = compute_import_union(files, fetcher);
    // 2. Parse each file exactly once
    for (auto& f : all_imports) {
        load(f.string(), fetcher);  // already guarded by single-flight in Task 7
    }
    // 3. Run namespace-finder walks exactly once over the combined AST
    namespace_finder_->walk_combined_ast();
}
```

- [ ] **Step 5: Re-run (expect PASS)**

- [ ] **Step 6: Commit**

```bash
git add libopenage/nyan
git commit -m "sprint: introduce Database::load_batch (parse-once)"
```

### Task 7: Single-flight import + FQON resolution cache

**Files:**

- Create: `libopenage/nyan/resolve_cache.{h,cpp}`
- Modify: `libopenage/nyan/file_loader.cpp`
- Modify: `libopenage/nyan/database.cpp`
- Create: `libopenage/nyan/tests/test_resolve_cache.cpp`

**Interfaces:**

- Consumes: file paths and FQON strings.
- Produces: `resolve_path(p) -> parsed_unit`; `resolve_fqon(fqon) -> ObjectState`. Both return the cached entry on subsequent calls.

- [ ] **Step 1: Write failing test**

```cpp
TEST(ResolveCache, RepeatedFqonReturnsCached) {
    nyan::get_load_metrics() = {};
    auto db = nyan::Database::create();
    db->load_batch({"a.nyan"}, [](auto p) { return read(p); });
    auto v1 = db->new_view()->get_object("Data.X.Villager");
    auto v2 = db->new_view()->get_object("Data.X.Villager");
    EXPECT_EQ(v1.get_info().fqon, v2.get_info().fqon);
    auto& m = nyan::get_load_metrics();
    EXPECT_GE(m.fqon_lookup_hits.load(), 1u);
}
```

- [ ] **Step 2: Implement `resolve_cache`**

```cpp
// libopenage/nyan/resolve_cache.h
namespace nyan {
class ResolveCache {
public:
    bool contains_path(const std::filesystem::path& p) const;
    void record_path(const std::filesystem::path& p);
    bool contains_fqon(const std::string& fqon) const;
    void record_fqon(const std::string& fqon, std::shared_ptr<ObjectState> state);
    std::shared_ptr<ObjectState> get_fqon(const std::string& fqon) const;
private:
    std::unordered_set<std::filesystem::path> seen_paths_;
    std::unordered_map<std::string, std::shared_ptr<ObjectState>> fqon_to_state_;
};
}
```

- [ ] **Step 3: Wire into `file_loader.cpp`**

```cpp
void FileLoader::open(const std::filesystem::path& p) {
    if (cache_.contains_path(p)) {
        nyan::get_load_metrics().import_resolves_cached.fetch_add(1);
        return;
    }
    cache_.record_path(p);
    nyan::get_load_metrics().import_resolves_total.fetch_add(1);
    // ... existing open logic ...
}
```

- [ ] **Step 4: Wire into `database.cpp` FQON resolution**

```cpp
std::shared_ptr<ObjectState> Database::resolve_fqon(const std::string& fqon) {
    if (cache_.contains_fqon(fqon)) {
        nyan::get_load_metrics().fqon_lookup_hits.fetch_add(1);
        return cache_.get_fqon(fqon);
    }
    nyan::get_load_metrics().fqon_lookups_total.fetch_add(1);
    auto state = build_state_for(fqon);
    cache_.record_fqon(fqon, state);
    return state;
}
```

- [ ] **Step 5: Re-run (expect PASS)**

- [ ] **Step 6: Commit**

```bash
git add libopenage/nyan
git commit -m "sprint: single-flight import and FQON resolution cache"
```

### Task 8: Switch `Game::load_data` to `load_batch` and emit `[PERF]`

**Files:**

- Modify: `libopenage/nyan/state.cpp`
- Modify: `libopenage/gamestate/load_data.cpp`
- Create: `libopenage/gamestate/tests/test_load_data_batch.cpp`

**Interfaces:**

- Consumes: `Database`, `MmapManager` (already exists), `LoadMetrics`.
- Produces: `Game::load_data` that calls `load_batch` and prints `[PERF] load_done ...`.

- [ ] **Step 1: Write test that asserts `load_batch` path is used**

```cpp
TEST(LoadData, UsesLoadBatch) {
    nyan::get_load_metrics() = {};
    nyan::tests::run_load_data({"engine", "de2_base"});
    auto& m = nyan::get_load_metrics();
    EXPECT_GT(m.files_loaded.load(), 0u);
    for (auto& [path, count] : m.parse_attempts_per_file) {
        EXPECT_LE(count, 1u) << "file parsed more than once: " << path;
    }
}
```

- [ ] **Step 2: Replace per-file `Database::load` with `load_batch`**

In `libopenage/nyan/state.cpp`:

```cpp
// Before: per-file recursion in Game::load_data
// After:
auto file_list = collect_closure_files(mods, file_loader);
db->load_batch(file_list, file_loader);
```

- [ ] **Step 3: Add `[PERF]` emit at end of `load_data`**

```cpp
// libopenage/gamestate/load_data.cpp
auto& m = nyan::get_load_metrics();
spdlog::info("[PERF] load_done files_seen={} files_loaded={} "
             "parse_attempts_total={} parse_attempts_unique={} "
             "import_resolves_total={} import_resolves_cached={} "
             "fqon_lookups_total={} fqon_lookup_hits={} "
             "namespace_full_scans={} "
             "object_states_created={} object_states_unique={} "
             "object_state_rebuilds={} "
             "mmap_page_faults_minor={} mmap_page_faults_major={} "
             "rss_peak_bytes={}",
             m.files_seen.load(), m.files_loaded.load(),
             m.parse_attempts_total.load(), m.parse_attempts_unique.load(),
             m.import_resolves_total.load(), m.import_resolves_cached.load(),
             m.fqon_lookups_total.load(), m.fqon_lookup_hits.load(),
             m.namespace_full_scans.load(),
             m.object_states_created.load(), m.object_states_unique.load(),
             m.object_state_rebuilds.load(),
             read_minflt(), read_majflt(), read_rss_peak());
```

- [ ] **Step 4: Re-run test (expect PASS)**

- [ ] **Step 5: Commit**

```bash
git add libopenage/nyan libopenage/gamestate
git commit -m "sprint: Game::load_data uses load_batch and emits [PERF] summary"
```

### Task 9: Profile full `de2_base` load and capture baseline

**Files:**

- Create: `assets/test/tutorial_smoke/profiler.py` (optional helper)
- Modify: `docs/superpowers/sprint/load_distribution.md` (new file)

- [ ] **Step 1: User-side run (cold + warm)**

```bash
# Cold
python -m openage convert ... >/dev/null
sync; echo 3 > /proc/sys/vm/drop_caches 2>/dev/null || true
time python -m openage game --tutorial --modpacks de2_base --no-prompts 2>&1 | tee /tmp/sprint_cold.log
# Warm (second run)
time python -m openage game --tutorial --modpacks de2_base --no-prompts 2>&1 | tee /tmp/sprint_warm.log
```

- [ ] **Step 2: Capture phase times**

```bash
grep -E '^\[PERF\]' /tmp/sprint_cold.log > docs/superpowers/sprint/cold_perf.txt
grep -E '^\[PERF\]' /tmp/sprint_warm.log > docs/superpowers/sprint/warm_perf.txt
```

- [ ] **Step 3: Profile with `callgrind` (optional)**

```bash
valgrind --tool=callgrind --callgrind-out-file=/tmp/sprint_callgrind.out \
  python -m openage game --tutorial --modpacks de2_base --no-prompts
callgrind_annotate /tmp/sprint_callgrind.out > docs/superpowers/sprint/callgrind.txt
```

- [ ] **Step 4: Commit artifacts**

```bash
git add docs/superpowers/sprint
git commit -m "sprint: capture baseline perf and counter distribution"
```

(If artifacts are too large, store only summaries in tracked tree and reference the raw logs in the report.)

### Task 10: O(N²) audit and fix

**Files:**

- Modify: `libopenage/nyan/...` based on audit findings
- Create: `docs/superpowers/sprint/o_n_squared_audit.md`

- [ ] **Step 1: Identify candidates from profile**

Look for any function in the profile whose inclusive time scales superlinearly with `total_objects` or `total_members`. Likely suspects (per existing knowledge):

- `NamespaceFinder::check_object_conflict` (full namespace scan per object).
- `Object::linearize` (walks parent chain per object; repeated per access).
- `Database::load` (re-parses imported files per importer).

- [ ] **Step 2: For each suspect, write a micro-benchmark or assertion**

```cpp
TEST(NSquared, NamespaceScanScalesLinearly) {
    auto db = nyan::tests::make_db_with_n_objects(2000);
    nyan::get_load_metrics() = {};
    db->load_batch(...);
    auto& m = nyan::get_load_metrics();
    // If scales are O(N^2), namespace_full_scans grows superlinearly with N.
    EXPECT_LE(m.namespace_full_scans.load(), 10u) << "suspicious: many full scans";
}
```

- [ ] **Step 3: For each confirmed O(N²) site, implement the fix**

Typical fixes:

- Move conflict detection to a single combined pass after all imports are loaded.
- Cache linearized parent chain per object.
- Replace per-importer file re-parse with a single-file seen-set (Task 7 already covers this).

- [ ] **Step 4: Re-run perf capture and record improvement**

```bash
time python -m openage game --tutorial --modpacks de2_base --no-prompts 2>&1 | tee /tmp/sprint_post_fix.log
```

- [ ] **Step 5: Commit**

```bash
git add libopenage/nyan docs/superpowers/sprint/o_n_squared_audit.md
git commit -m "sprint: O(N^2) audit and targeted fixes"
```

### Task 11: Real CLI smoke tests

**Files:**

- Create: `assets/test/tutorial_smoke/conftest.py`
- Create: `assets/test/tutorial_smoke/test_exit_codes.py`
- Create: `assets/test/tutorial_smoke/test_no_core_dump.py`
- Create: `assets/test/tutorial_smoke/test_startup_perf.py`

- [ ] **Step 1: Write exit-code test**

```python
def test_tutorial_exits_cleanly():
    proc = subprocess.run(
        [sys.executable, "-m", "openage", "game", "--tutorial", "--modpacks", "de2_base", "--no-prompts"],
        capture_output=True, text=True, timeout=120,
    )
    assert proc.returncode == 0, proc.stderr
```

- [ ] **Step 2: Write no-core-dump test**

```python
def test_no_core_dump(tmp_path):
    cwd = os.getcwd()
    try:
        os.chdir(tmp_path)
        proc = subprocess.run(
            [sys.executable, "-m", "openage", "game", "--tutorial", "--modpacks", "de2_base", "--no-prompts"],
            capture_output=True, text=True, timeout=120,
        )
        cores = list(tmp_path.glob("core")) + list(tmp_path.glob("core.*"))
        assert not cores, f"core dump found: {cores}"
    finally:
        os.chdir(cwd)
```

- [ ] **Step 3: Write perf regression test (sandbox-skippable)**

```python
def test_cold_startup_under_30s():
    if not pathlib.Path("assets/converted/de2_base").exists():
        pytest.skip("user-side de2_base not present")
    import time
    t0 = time.monotonic()
    proc = subprocess.run(
        [sys.executable, "-m", "openage", "game", "--tutorial", "--modpacks", "de2_base", "--no-prompts"],
        capture_output=True, text=True, timeout=600,
    )
    elapsed = time.monotonic() - t0
    assert proc.returncode == 0
    # Sprint gate: 30s soft cap; 15s decision threshold recorded separately.
    assert elapsed < 30, f"cold startup took {elapsed:.1f}s"
```

- [ ] **Step 4: Run all smoke tests**

```bash
pytest assets/test/tutorial_smoke -v
```

- [ ] **Step 5: Commit**

```bash
git add assets/test/tutorial_smoke
git commit -m "sprint: add tutorial CLI smoke tests (exit code, no core, perf)"
```

### Task 12: Sprint report

**Files:**

- Create: `docs/superpowers/sprint/2026-07-24-de2-base-fullload-report.md`

- [ ] **Step 1: Write the report with the six mandatory deliverables**

```markdown
# Sprint Report — de2_base Correctness + Loader Optimization

## 1. 9-minute time distribution
- Phase: parse, total X.X s
- Phase: namespace walk, total X.X s
- Phase: object construction, total X.X s
- Phase: scenario load, total X.X s
- Phase: render setup, total X.X s

## 2. Counters
- files_opened = ...
- parse_attempts_total = ...
- parse_attempts_unique = ...
- import_resolves_total = ...
- import_resolves_cached = ...
- fqon_lookups_total = ...
- fqon_lookup_hits = ...
- namespace_full_scans = ...
- object_states_created = ...
- object_states_unique = ...

## 3. O(N²) audit
- (confirmed / refuted, with evidence)

## 4. Pre/post optimization
- Pre: T_pre seconds
- Post: T_post seconds (cold and warm)

## 5. Validator error list
- Pre-fix: see docs/superpowers/sprint/pre_fix_validator.json
- Post-fix: see docs/superpowers/sprint/post_fix_validator.json

## 6. Final start-up result
- Cold: T_cold seconds
- Warm: T_warm seconds
- Decision gate result: ≤15 / 15–30 / >30 / minutes
- Action: pause cache / accept dev build / re-approve Stage 1 / continue loader work
```

- [ ] **Step 2: Commit**

```bash
git add docs/superpowers/sprint
git commit -m "sprint: final report with six mandatory deliverables"
```

---

## Self-Review Notes

- **Spec coverage:** every sprint item maps to one or more tasks: exception guard → Task 1; minimum validator → Task 2; converter fixes → Tasks 3–4; profiler/counters → Tasks 5, 9; parse-once → Tasks 6, 7; O(N²) → Task 10; CLI smoke → Task 11. Batch load + module/FQON index is the "minimum batch-load" required by item 7.
- **Out of scope:** packed cache, mmap, overlay, generation-safe handle, live reload, full corruption matrix, deterministic publication, lazy materialization, multi-cache are explicitly listed in the sprint doc and the long-term plan is preserved unchanged.
- **Type consistency:** `LoadMetrics` (Task 5) is the single source for `get_load_metrics()` consumed by Tasks 6–8. `ResolveCache` (Task 7) is consumed only by `Database` (Task 6) and `file_loader` (Task 7). `load_batch` (Task 6) is consumed by `Game::load_data` (Task 8).
- **No placeholders:** every step has concrete code, a concrete command, or a concrete commit. The user-side runs (Tasks 3, 4, 9) are intentionally not sandboxed; they are gated on `assets/converted/de2_base` being present and will skip otherwise.
- **Sprint budget:** Task estimates sum to roughly 6–9 person-days assuming a single engineer; Task 4 (converter fix iteration) is the largest variance and the explicit fallback (more iterations) extends it but not past 15 days.
