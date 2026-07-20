# Streaming Convert — Design

**Date:** 2026-07-20
**Status:** Approved verbally, pending written-spec user review → writing-plans
**Author:** brainstorming session (user + Claude)
**Applies to:** `openage convert` de2 (and the whole convert pipeline)

---

## Motivation

DE2 convert OOMs at ~10 GB on the user's machine. Manual kill clamp at 2 GB still loses the binary — the OOM is large, not borderline. Existing fix attempts (`_release_genie_data` after nyan emit) cap the post-organize growth but the **6 GB genie data graph is resident from read through organize** and dominates peak RSS.

Goal: **drastically drop peak RSS** with a streaming redesign of the convert pipeline. Stretch target **< 200 MB** peak, hard floor target **< 300 MB**, with three independently mergeable phases each with explicit budget gates.

Non-goals:

- Not rewriting convert in C++/Rust (cost/benefit unfavorable; ~50–80 MB savings vs 2–3 person-months).
- Not breaking the engine: `engine.util.X.Y` FQON hardcodes in libopenage must continue to work.
- Not introducing new external dependencies.

---

## One-line invariant

> **Any genie record exists in the Python heap only inside `with src.parse_*()` scope. Any persistent index holds handles (ID, FQON-string, mmap offset), never Python object references.**

This is the single discipline that gets us from the 6 GB ceiling to the 200 MB floor. Every other design choice flows from it.

---

## Architecture

```
.dat (zlib compressed, ~50 MB)
    │
    ▼   P1-P3 new
[reader: chunked zlib inflate → /tmp/<dat>.raw → mmap]
    │  persistent: section_offsets  (~1–2 MB)
    │  mmap itself: OS-managed, only touched pages counted toward RSS
    ▼
[GenieSource: handle-driven iter + persistent ID-summary indices]
    │  persistent: per-category ID-summary (5–15 MB total)
    │  transient:  zero (records live only inside `with`)
    ▼
[organize_nyan: single dependency-ordered pass]
    │  for record in src.units_iter():
    │      with src.parse_unit(...) inside the loop body if needed
    │  emit one nyan object per record, register to engine DB, release
    ▼
[organize_media: per-graphic stream into atlas book]
    │  decode SLP → pixel view (no Pillow.Image)
    │  paste into active atlas → flush full atlas to PNG on disk → release
    ▼
(process exits; all transient state reaped)
```

**Three ownership zones:**

| Zone | Nature | Contents | RSS |
|---|---|---|---|
| OS-managed | mmap virtual pages, only touched counted | decompressed .dat bytes | 0–30 MB active |
| Persistent (Python) | survives category release, only handles/summary | ID summary indices · section offsets · FQON-string refs | 5–30 MB |
| Transient (Python) | only during `with` scope or single-pass emit window | one record (`dataclass(slots=True, frozen=True)`) · one nyan-object wrapper | <300 B |
| Fixed overhead | interpreter + openage imports | CPython + openage module imports | 50–80 MB |

Memory peaks are achieved when transient and small persistent overlap with active mmap pages. Active Pillow atlas buffers push the media peak (~16 MB) but never accumulate into a 6 GB blob.

---

## Three phases (independently mergeable)

Each phase uses the same `GenieSource` API (no API drift between phases — every phase is a strict subset of the next). Each phase installs one budget gate. Migration between phases uses `OPENAGE_CONVERTER_BACKEND=phase1|phase2|phase3` env var to flip the entry point.

| Phase | What lands | Budget gate |
|---|---|---|
| **P1** Reader skeleton + `units` only on iter | `GenieSource` class, section-offset pre-walk, `units_iter()` + `units_summary()` impl, single category (units) routed through iter. Other categories stay on legacy `ValueMember` tree. | **< 1.5 GB** |
| **P2** All categories on iter; ValueMember tree retired | Civs, techs, graphics, sounds, terrains, effects, terrain_restrictions, age/tech/building/unit connections, group_refs, upgrades, unlocks, civ-boni: all routed through iter. Legacy `value_members.py` deleted. | **< 500 MB** |
| **P3** Media streaming + automated budget gates | `organize_media` switches to per-graphic atlas packing without intermediate `Pillow.Image` instances. CI wires `OPENAGE_MEMORY_BUDGET=phase3`. Atlas books compute per palette variant. | **< 200 MB** (achievable) · hard floor 300 MB |

Phase reports (`/tmp/openage_memprobe_heap.txt` from `OPENAGE_MEMPROBE_HEAP=1`) attached to each phase's PR for review.

---

## Section 2 — Reader design

**Data source**: write the decompressed `.dat` to `/tmp/<game_id>_<dat_name>[_<verstr>].raw` on first run, then `mmap.mmap(fd, 0, flags=MAP_PRIVATE)`. Skip on subsequent runs by hash-keyed cache file (decompression is cheap; ~1–2 s, ~50 MB write).

Why not mmap the compressed file directly: zlib has no random access, so we'd be implementing our own inflate state machine — no benefit.

**Section offset pre-walk** (`peek_offsets()`): one full pass over the decompressed bytes, only reading length-of-children fields, never constructing `ValueMember`. Output: a frozen `Mapping[str, SectionSpan]` where `SectionSpan = (offset, byte_length, record_count, record_sizes)`. Cost: 1–3 seconds, <2 MB resident.

**`GenieSource` API**:

```python
@dataclass(frozen=True, slots=True)
class SectionSpan:
    name: str
    offset: int
    byte_length: int
    record_count: int
    record_sizes: tuple[int, ...]   # for var-length records

@dataclass(frozen=True, slots=True)
class UnitHandle:                    # ~24 B per record
    id: int
    offset: int
    byte_length: int

@dataclass(frozen=True, slots=True)
class UnitSummary:                   # persistent; only fields really needed for cross-ref
    id: int
    name: str
    graphic_id: int
    max_speed: float                 # expand as needed; minimum projection

class GenieSource:
    # Persistent (built lazily, cached for source lifetime)
    def units_summary(self) -> Mapping[int, UnitSummary]: ...
    def unit_handle(self, id: int) -> UnitHandle: ...
    def unit_handles_iter(self) -> Iterator[tuple[UnitHandle, UnitSummary]]: ...

    # Transient (record only exists during caller's hold; release the reference to GC)
    def units_iter(self) -> Iterator[UnitRecord]: ...
    def parse_unit(self, h: UnitHandle) -> UnitRecord: ...

    @contextmanager
    def open_unit(self, id: int) -> Iterator[UnitRecord]:
        try:
            yield self.parse_unit(self.unit_handle(id))
        finally:
            pass  # exit `with` → reference dies → record reclaimed

    def release(self) -> None:  # explicit shutdown; close mmap, drop indices
        ...
```

**Record representation**: `@dataclass(slots=True, frozen=True)`. Strings decoded once from mmap sub-slices (in-memory copy). Collections are `tuple` not `list`. Cross-ref fields store `int` IDs only — resolution happens at emit time via `*_summary()` index or `open_*()` re-parse on demand.

**Failure semantics**: single record parse failure → `warn()` + skip (legacy behavior). `peek_offsets()` failure → `cli.error` with hexdump of `mmap[bad_offset:bad_offset+32]`. Stale mmap tmpfile → delete + re-decompress once before final fail.

**File layout**:

```
openage/convert/value_object/read/
    source.py        # GenieSource, SectionSpan, handle dataclasses
    peek.py          # EmpiresDat.peek_offsets()
    records/         # one dataclass per category
        unit.py
        civ.py
        tech.py
        graphic.py
        sound.py
        terrain.py
        effect.py
        ...
```

---

## Section 3 — Organize layer (single-pass + handle-only persistent)

**Single-pass dependency-ordered walk** — topologically ordered list:

```
1. terrains
2. terrain_restrictions
3. graphics              ← needs to exist before unit can reference
4. sounds
5. effect_bundles
6. units
7. age/tech/building/unit_connections
8. techs                 ← patches forward into units
9. civs                  ← patches forward into units + techs + graphics + ...
10. group_refs / upgrades / unlocks / boni
```

**Per-category template**:

```python
for handle, summary in src.unit_handles_iter():
    nyan_obj = make_unit_nyan_object(summary)         # only summary needed
    with src.open_unit(handle.id) as unit:           # expanded only inside loop
        apply_basic_attributes(nyan_obj, unit)
    register_to_engine(nyan_obj, summary.fqon)        # C++ DB holds the real nyan obj
    # nyan_obj Python wrapper can be discarded after registration if no in-loop patch
    # persistent handle summary stays in src.units_summary() for downstream cross-ref
gc.collect()                                          # explicit at category boundary
```

**Cross-ref resolution** uses persistent `*_summary()` tables, **never** dereferences through record references:

```python
for civ_record in src.civs_iter():
    civ_nyan = make_civ_nyan(civ_record)
    apply_unit_references(civ_nyan, civ_record,
                          src.units_summary(),   # ID → summary, no record deref
                          nyan_object_index)
    register_to_engine(civ_nyan, civ_record.fqon)
```

**Forward-only patch application**: tech→unit patches need the **unit nyan object** as target. Since units emit before techs, target is reachable. **Never** fetch genie fields through a record reference — fetch only what `summary` exposes.

**Reverse-not-allowed**: later categories cannot read genie fields of already-released categories. Anything that downstream categories need must be projected into summary at emit time.

**Three persistent indices** (all handle-only, total < 30 MB):

| Index | Key | Value | RSS |
|---|---|---|---|
| `GenieSource.*_summary()` | `int(genie_id)` | `*Summary` dataclass (5–10 fields per entry) | 5–30 MB |
| `nyan_ref_by_fqon` | `str(FQON)` | `NyanRef(category, id, fqon)` — 30–80 B | 1–5 MB |
| `section_offsets` | `str(section)` | `SectionSpan` (~24 B) | <2 MB |

**No `nyan_object_index` persistent**. Nyan object Python wrappers live ≤ emit-window. After registration to engine DB they are releasable.

**Release boundary at category end**:

```python
del local_handle_loop_var
# Release only summaries USED FOR THIS CATEGORY's OWN emit, if any.
# Critically: `src.units_summary()` (or others for cross-ref from later
# categories) is NOT released here — those are persistent for downstream.
gc.collect()
```

**File-level caching for shared nyan handle**: P3 may introduce `functools.lru_cache(maxsize=64)` over `parse_*()` for short-window re-access (e.g., ten techs each iterating over the same unit). Disabled in P1 for simplicity; enabled only if profiling shows the re-parse is a bottleneck.

---

## Section 4 — Media layer (per-graphic stream)

**No Pillow Image caching**. Decode SLP frame → bytes/numpy view → paste into atlas → release. No intermediate Image object ever sits in Python heap.

```python
for graphic in src.graphics_iter():
    for slp_id in graphic.slp_ids:
        slp_bytes = load_slp_bytes(slp_id)        # bytes; one SLP, released at figure end
        for frame_idx in range(graphic.frame_count):
            pixel_view = decode_slp_frame(slp_bytes, frame_idx, palette)
            atlases.place(graphic, slp_id, frame_idx, pixel_view)   # immediate paste
            # pixel_view dies when `atlases.place` returns
```

**Atlas flush**:
- Trigger when active atlas cannot place next frame, OR soft cap (≥ 64 MB written)
- `buf = io.BytesIO(); self.active.save(buf, "PNG"); open(path, "wb").write(buf.getvalue())` — write file directly, discard bytes immediately
- **NEVER** accumulate `list[bytes]` of finished PNGs
- Reset active atlas: `self.active = Image.new("RGBA", (1024, 1024), (0,0,0,0))`

**Palette variant**: one `AtlasBook` per palette-set. Serial traversal of `(graphic, palette)` combinations. One active book at a time.

**Media peak RSS**: 16 MB (active atlas) + 0.5–5 MB (active SLP bytes) = **<25 MB**.

**Metadata**: handle-only `SpriteRef` (atlas file path + x/y/w/h + slp_id + frame_idx + palette_id). Written to disk per-atlas; no Python-object-pin graph.

**Existing `media_exporter.py` argument `args.jobs` (multiprocessing workers)**: serial in P1, decouples iterator pacing from worker pool. Optional async path revisited in P3 if measured needed.

---

## Section 5 — Output contract + validation

### 5.1 nyan API stability contract (NOT BREAKABLE)

| Stable (contract) | Mutable (implementation detail) |
|---|---|
| nyan class FQON (`engine.util.unit.archer.Archer`, etc.) | Physical nyan file paths and names |
| Field names (`max_speed`, `line_of_sight`, ...) | Field order within a file |
| Field types (`float`, `int`, `ObjectRef`, ...) | Patch application order (as long as target-emit ordering is preserved) |
| Engine-hardcoded FQON strings | Sprite atlas internals / pixel encoding |

### 5.2 Functional equivalence test

Mechanism:

1. Build a "golden modpack" before refactor lands (per branch).
2. Build candidate modpack from refactored code.
3. Use `nyan.api` introspection to serialize every nyan object to one-line key=value pairs keyed by FQON.
4. Compare two sets. Diff tolerance: <10 KB across all embedded timestamps / hashes.

CI step:

```bash
nyan_diff --before pre-refactor-modpack --after refactored-modpack
```

### 5.3 Engine smoke test

```bash
python -m openage game --modpack refactored-modpack --scenario tutorial_1 --ticks 1000
```

Runs as a CI step. Failure = red PR.

### 5.4 Memory budget gate

New CLI flag `--memory-budget-test`:

```python
BUDGETS = {
    "phase1": 1.5 * 1024**3,
    "phase2": 500 * 1024**2,
    "phase3": 300 * 1024**2,
}
def enforce(phase):
    peak = peak_rss_tracked_by_memprobe()
    if peak > BUDGETS[phase]:
        cli.error(f"{phase}: peak RSS {peak/1e6:.0f}MB exceeds {BUDGETS[phase]/1e6:.0f}MB; see /tmp/openage_memprobe_heap.txt")
```

CI:

```yaml
- OPENAGE_MEMORY_BUDGET=phase1 python -m openage convert --source-dir <aoe2de> --memory-budget-test --force --no-prompts
- OPENAGE_MEMORY_BUDGET=phase2 python -m openage convert ...   # when phase2 lands
- OPENAGE_MEMORY_BUDGET=phase3 python -m openage convert ...   # when phase3 lands
```

`peak_rss_tracked_by_memprobe` reads from the memprobe module's RSS sampler (already in main branch).

### 5.5 Phase entry/exit criteria

| Phase | Entry precondition | Exit criteria |
|---|---|---|
| P1 | Design doc merged | de2 convert ends cleanly · peak RSS < 1.5 GB · golden-modpack diff <10 KB · engine smoke 1000 ticks · `/tmp/openage_memprobe_heap.txt` attached to PR |
| P2 | P1 merged | de2 convert ends cleanly · peak RSS < 500 MB · `value_members` types count = 0 in heap report · golden-modpack diff <10 KB · engine smoke |
| P3 | P2 merged | de2 convert ends cleanly · peak RSS < 300 MB · `PIL.Image` types count = 0 in heap report · golden-modpack diff <10 KB · engine smoke · CI gate `OPENAGE_MEMORY_BUDGET=phase3` wired |

---

## Existing assets that don't need rebuilding

- `openage/convert/tool/memprobe.py` is on `master` (RSS sampler, SIGUSR1, heap breakdown hook, `snapshot()`, `dump_heap_breakdown()`). Don't reimplement.
- `modpack_subprocessor._release_genie_data(full_data_set)` (after organize_nyan) is the existing working boundary for "release genie data after consumption". P1 + P2 keep this; P2 iter reads are incremental refactors inside it.
- `RawAPIObject` already has `__slots__` (9 fields including `raw_members`, `nyan_object`). Don't touch.
- `args.jobs` for media — P3 may revisit, leave API stable.

---

## Open unknowns (resolve during P1)

- **nyan C++ Database ownership**: does `nyan::Database` retain nyan objects after Python wrapper release? Required for handle-only nyan_ref design. Verify by reading `nyan_api.NyanObject` and `nyan::Database` lifecycle.
- **mmap tmpfile concurrency**: when convert is run in subprocesses (CI, tests, `--jobs` workers), is mmap'd tmpfile safe? Read-only + `MAP_PRIVATE` should be.
- **BfG dat format** (`VER 8.4`) — already addressed in current code, don't regress.

---

## Migration risks (audit checklist before P1 lands)

- [ ] Every `obj.attr` field read in `modpack_subprocessor.py` (and the 30+ subprocessor files under `processor/conversion/`) must be either in-summary at emit time or fetched via `src.open_*(id)` inside the emit loop.
- [ ] All patch flows direction-confirmed: source always emits after target.
- [ ] `ConverterObjectGroup.raw_api_objects` lifecycle: currently cleared per category in P1 boundary; P2 confirms.
- [ ] `pregen_nyan_objects` becomes `nyan_ref_by_fqon` + transient local index. Diff in P2.
- [ ] `NoDiffMember` / `LeftMissingMember` / `RightMissingMember` carry-over until P2 finishes.
- [ ] `genie_object_container.py` field set: `genie_units.clear()` etc. call sites still work or are removed.

---

## Glossary

| Term | Definition |
|---|---|
| **handle** | A lightweight value (`int`, `(cat, id, fqon)`, or `mmap offset+length` tuple). Never a Python object. Persistent by default. |
| **summary** | Persistent dataclass (`slots=True, frozen=True`) holding the minimum fields downstream categories need from a record (typically 5–10 fields). |
| **record** | Full dataclass representing one parsed binary entry from a `.dat` section. Lives only inside `with src.parse_*()`. |
| **phasic backend flip** | `OPENAGE_CONVERTER_BACKEND=phase1\|phase2\|phase3\|legacy` env var. Defaults to `legacy` in P0, `phase1` once P1 lands, etc. |
| **budget gate** | `--memory-budget-test` runs convert and exits non-zero if peak RSS exceeds the budget tied to the active phase. |
| **golden modpack** | Reference modpack captured before a refactor lands; structural diff against candidate validates nyan API stability. |

---

## Composes with:

- `openage/convert/tool/memprobe.py` — RSS sampler, SIGUSR1, heap breakdown, snapshot hooks.
- `openage/__init__.py` `_bootstrap_build_dir()` — keeps python from running `openage` from source tree (lets us edit `openage/convert/value_object/read/source.py` in-place without rebuild during iteration).

## Success criteria for the overall effort

- DE2 convert peak RSS < 200 MB (achievable) on the user's machine, where today it OOMs at 10 GB.
- Three phase gates pass on the user's machine, in order, each with explicit budget number.
- Engine still loads the modpack and runs identically (functional equivalence + golden-modpack diff < 10 KB).
- Each phase ships with a `phase_report.txt` from `OPENAGE_MEMPROBE_HEAP=1`.
- No Python nyan_object Python wrapper is held by a long-lived index.
- No Pillow Image ever accumulates beyond the active atlas.
- No rewrite to C++/Rust required.
