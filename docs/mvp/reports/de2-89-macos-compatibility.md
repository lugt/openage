# DE2 8.9 macOS Compatibility — Findings

**Date:** 2026-08-01
**Branch:** `mvp/william-wallace` @ `bdcf18b1` (after the design spec commit)
**Spike status:** `DE2_89_COMPATIBILITY_SPIKE_COMPLETE / ADAPTATION_DEFERRED`
**Design spec:** `docs/superpowers/specs/2026-08-01-de2-89-macos-compat-design.md`

## 1. macOS environment

| Item | Value |
| --- | --- |
| Host OS | macOS (Darwin 25.3.0) |
| Steam install | `~/Library/Application Support/Steam/steamapps/common/AoE2DE/` |
| Data root | `~/Library/Application Support/Steam/steamapps/common/AoE2DE/AgeOfEmpires2Data/` |
| Primary dat | `resources/_common/dat/empires2_x2_p1.dat` |
| Dat size | 11,076,546 bytes |
| Dat SHA-256 (first 32 hex) | `ce3530df36cf0b333a9751cb0ff94460` |
| First 256 bytes | Genie header is XOR-obfuscated; version string is read after decryption |
| Post-decrypt size | 86,745,747 bytes |
| Post-decrypt first 8 bytes | `b'VER 8.9\x00'` |

No proprietary content is reproduced in this report. The full SHA-256
is logged for reproducibility but not committed; only the first 32
hex chars are recorded.

## 2. Identity of "8.4" / "8.9"

"VER 8.4" and "VER 8.9" are Genie's *post-decrypt version strings*
inside `empires2_x2_p1.dat`. They identify the *layout* the file
ships in, not a user-facing game build. They are not Genie
DAT-schema versions in a numeric sense, nor are they game build
tags or proprietary macOS markers — they are the same Genie-internal
string the converter has been reading since the BfG DLC ("VER 8.4")
was added. The macOS install simply ships the next-layout
("VER 8.9") version of the same Genie file.

The header string is read post-zlib-decompression inside
`GenieSource.from_compressed` (see source.py around the
`# Version detection must run on the decompressed bytes` block).
It is mapped to a layout profile by `resolve_de2_dat_profile`, the
only function in the converter allowed to make that mapping.

## 3. File identity (sanitized)

The relative path under the data root is `resources/_common/dat/empires2_x2_p1.dat`.
This is the same relative path the Windows and Linux DE2 installs
use; only the parent directory is platform-specific. The converter
identifies the macOS data root via a generic Steam path that does
not bind to the user's home directory name:

```toml
[AOE2DE.installpaths]
macos = [
  "~/Library/Application Support/Steam/steamapps/common/AoE2DE/AgeOfEmpires2Data",
  "~/Library/Application Support/Steam/steamapps/common/AoE2DE",
]
```

No user-specific absolute path is committed.

## 4. Pre-spike first error (working-tree evidence)

The pre-spike working tree contained five targeted `if update_number
== "8.9"` branches plus a heavy `[DEBUG 8.9]` print storm that had
been added to identify each of the five diffs. Without those
branches the first error is the "DE2 dat layout profile missing"
check inside `_de2_profile(game_version)`, raised from
`UnitCommand.get_data_format_members` at the first int16 read of the
`unit_commands` array, with the file mis-aligned by 2 bytes per
command.

## 5. Schema differences (8.4 → 8.9)

Five known diffs, all expressed as layout-shape fields in
`De2DatLayoutProfile`:

| # | Field | 8.4 (BfG) | 8.9 |
| --- | --- | --- | --- |
| 1 | `unit_command_trailer_bytes` | 0 | 2 |
| 2 | `building_damage_graphic_layout` | `"absent"` | `"v2"` |
| 3 | `has_wwise_dying_sound_id` | True | False |
| 4 | `missile_trailer_bytes` | 0 | 16 |
| 5 | `living_unit_trailer_bytes` | 0 | 47 |

Diffs 1 and 3 are decoded; diffs 2, 4, 5 are sized opaque records
that the reader consumes as `READ_UNKNOWN` and does not interpret
into the normalized model in this spike.

A sixth diff was observed during the lazy probe (see §8).

## 6. Profile design (as implemented)

```text
De2DatLayoutProfile(frozen, structural fields only):
  profile_id
  unit_command_trailer_bytes
  building_damage_graphic_layout  # "absent" | "v2"
  has_wwise_dying_sound_id
  missile_trailer_bytes
  living_unit_trailer_bytes

DE2_BASE_PROFILE, DE2_84_PROFILE, DE2_89_PROFILE  # all three
                                                   # registered
resolve_de2_dat_profile("VER x.y") -> De2DatLayoutProfile
  raises UnsupportedDatProfileError on unknown / non-string
```

`GameVersion` carries an optional `de2_profile` field. Reader code
in `unit.py` consults the profile via the private helper
`_de2_profile(game_version)`, which raises if no profile was set.
The `update_number` string label is preserved for non-DAT-layout
converter code paths but is **not** consulted by the five diffs.

Detection lives in `GenieSource.from_compressed` (the only
authorized mapping point). It is based on the post-decrypt header
string, **never** on `sys.platform`.

```bash
$ git grep -n 'update_number.*8\.9\b' openage/convert/
openage/convert/value_object/init/game_version.py:221:    # ``update_number`` for the five known 8.4 <-> 8.9 diffs.
openage/convert/value_object/read/source.py:318:                    update_number="8.9",
openage/convert/value_object/read/source.py:322:                     "update_number -> 8.9, de2_profile -> %s",

$ git grep -n 'DEBUG 8\.9' openage/convert/
(no output)

$ git grep -n 'sys\.platform.*darwin' openage/convert/
(no output)
```

(The remaining `update_number` references in `source.py` are the
*assignment* of the legacy label inside the resolver — they are
*not* layout checks. The comment in `game_version.py` is the doc
that explains the split. The 2 `update_number == "BfG"` checks in
`unit.py` are the BfG DLC trailers — explicitly out of scope per
the design spec.)

## 7. Modified files

| File | Change |
| --- | --- |
| `cfg/converter/games/de2/version_hashes.toml` | Add `empires2_x2_p1.dat` as alternative identifier for the macOS Feral port. |
| `cfg/converter/games/game_editions.toml` | Add `~/.../AgeOfEmpires2Data` as preferred macOS install path. |
| `openage/convert/value_object/init/game_version.py` | Add optional `de2_profile` field; document that `update_number` is no longer authoritative for DAT layout. |
| `openage/convert/value_object/read/media/datfile/de2_profile.py` | **New.** `De2DatLayoutProfile` dataclass + `DE2_BASE/84/89` instances + `resolve_de2_dat_profile` resolver. |
| `openage/convert/value_object/read/media/datfile/unit.py` | Replace six `if update_number == "8.9"` branches (and one `!= "8.9"`) with profile queries. Rename `v89_*` fields to `de2_*` (the resolver — not the field name — encodes the version). |
| `openage/convert/value_object/read/media/datfile/CMakeLists.txt` | Register `de2_profile.py` and the new tests directory. |
| `openage/convert/value_object/read/media/datfile/tests/__init__.py` | **New.** |
| `openage/convert/value_object/read/media/datfile/tests/test_de2_profiles.py` | **New.** 25 synthetic tests across resolver, structural, combination, regression. |
| `openage/convert/value_object/read/source.py` | Set `de2_profile` from `resolve_de2_dat_profile(...)` in `from_compressed`. Remove all `[DEBUG 8.9]` print statements and the `self._dbg_*` context-tracking attributes. |

Excluded from the spike commits (preserved separately):

- `openage/convert/processor/export/output_checks.py` — orthogonal
  work; preserved at `/tmp/de2-89-spike-stash/output_checks.py`
  and restored to the working tree as untracked.
- `macos-version-files.txt` — untracked pre-existing artefact,
  not part of this spike.

## 8. Tests

25 synthetic tests in
`openage/convert/value_object/read/media/datfile/tests/test_de2_profiles.py`,
all passing:

```text
A. Profile resolver (9 tests)
   - 8.4 / 8.9 / unknown / empty / non-string / purity / frozen
   - 8.4 == base shape, distinct profile_id
   - 8.9 has the five known diffs
B. Structural synthetic (10 tests)
   - 8.4 absence vs 8.9 presence for each of the 5 diffs
   - DamageGraphic89 layout is 5 B
   - end-to-end byte walk: UnitCommand 8.9 = 67 + 2 B
   - truncated 8.9 raises (no silent overflow)
C. Combination / regression (3 tests)
   - 8.4 path is byte-for-byte unchanged
   - unknown profile raises when reader runs
   - update_number label does not change 8.4 layout
D. Existing convert tests (18)
   - all 18 export / conversion tests still pass
```

The end-to-end byte walk is the strongest single check: it
constructs a 69-byte stream (67B of zeros + 2B int16 trail = -1) and
asserts that `GenieSource._walk_struct(UnitCommand, 0, ...)` consumes
exactly 69 bytes. An off-by-2 in the trailer length fails this test
immediately, not later in the cumulative offset of `unit_commands[i+1]`.

## 9. Lazy probe (real macOS dat)

The probe invokes `GenieSource.from_compressed` directly, mirroring
the lazy backend. The full `openage convert` CLI hits a separate
`macOS not really supported` failure in `default_dirs.get_dir`
that is unrelated to the 8.9 spike and is out of scope.

**Result on the macOS dat:**

```text
compressed:     11,076,546 B
decompressed:   86,745,747 B
first 8 bytes:  b'VER 8.9\x00'
de2_profile:    De2DatLayoutProfile(profile_id='DE2_89', ...)
update_number:  8.9
walker progress: cleared all 5 known diffs (UnitCommand, building
                 damage graphics, wwise block, MissileUnit, LivingUnit)
```

The walker cleared **all five known diffs** (the failure would
otherwise have surfaced at the first UnitCommand's int16 trail).
It then hit a **sixth, previously-unknown structural error** at a
single, well-localized point:

```text
offset:         0x369f37
unit_type tag:  -1  (0xFFFF as int16)
next 8 bytes:   ff ff ff ff ff 00 06 00
prev 16 bytes:  01 00 00 80 3f 00 00 80 3f 00 00 80 3f ff ff ff
```

The previous 16 bytes are three IEEE-754 floats equal to 1.0
followed by `\xff\xff\xff` — i.e. the *end* of the 47-byte opaque
trailer that the 8.9 reader inserts at the tail of every
`LivingUnit`. The walker correctly read the trailer, but the next
record's `unit_type` multisubtype tag at `0x369f37` decodes to
`-1`, which is not a valid `unit_type_id` (valid tags are
10/20/25/30/40/60/70/80/90).

Per the design spec, the success criterion for this spike is
"metadata parse completes **or** the walker clears all five known
diffs and stops at a precise, well-localized structural error."
This is the second case.

The error is **localized** to a single struct class (`LivingUnit`).
The 47-byte trailer may be the wrong size, may sit at a slightly
different position within `LivingUnit`, or may apply to a subset
of `LivingUnit` records rather than all of them. The diagnostic
captures one occurrence; the actual layout of the trailer is
not decoded.

## 10. Unsupported structures

| Item | Status |
| --- | --- |
| 6th LivingUnit diff (above) | **NOT** adapted in this spike. Single-struct, well-localized, but not decoded. |
| 8.4 BfG DLC trailers (`bfg_projectile_unknown` 16 B, `bfg_living_unknown` 25 B) | NOT in the 5-diff set; left inline per "不重构仓库中所有 BfG、DLC 或 edition-specific 分支". |
| Media conversion (slp / drs / blendomatic / sounds) | Out of scope. No probe executed. |
| Terrain / campaign / scenario / Mission 1 | Untouched. |
| `openage convert` full CLI on macOS | Hits an unrelated `default_dirs.get_dir` "macOS not really supported" failure in `assets.py`. Out of scope for this spike; should be a separate issue. |
| 8.9 `data/converter` rebuild end-to-end (mmap → nyan → modpack) | Not exercised. The probe stops at metadata structure walk. |

## 11. Recommendation: continue 8.9 adaptation?

**Not in this spike.** Per the design's "不要立即继续无限修补"
rule, when a sixth diff appears we stop and report rather than
auto-patch. The 6th diff is localized to `LivingUnit` and could
plausibly be added to the profile as a more precise field (e.g.
split the trailer into *position* + *size* + *sub-struct scope*),
but doing so requires the user to make the call, since:

* the trailer may also apply differently to `BuildingUnit` (a
  `LivingUnit` subclass) than to the base `LivingUnit`,
* re-decoding 47 bytes is non-trivial and may surface further
  diffs, and
* the design's stop condition is "stop on 6th diff unless the
  user explicitly directs otherwise".

**Recommended next step (P4 macOS branch):** treat the macOS DE2
8.9 dat as a *read-only* source for development metadata purposes
(e.g., schema inspection, struct-walk debugging), and continue to
run P4 on the Ubuntu-synced converted assets via `--no-convert`.
This matches the spec's `ADAPTATION_DEFERRED` recommendation.

## 12. Does this block P4 / Mission 1 runtime?

**No.** P4 advances on the Ubuntu-verified dataset; macOS is a
secondary development target. The macOS data path for P4 stays
"install + sync converted assets + `--no-convert`" — exactly the
fallback path the spec calls for when this spike defers.

## 13. handoff

Next action: return to P4 on the Ubuntu-verified dataset.

Open follow-up (separate spike, separate design doc):

* Decode the 6th `LivingUnit` diff (size, position, applicability
  to `BuildingUnit`).
* Address the `openage convert` CLI's `default_dirs.get_dir` macOS
  failure so the full CLI can run on macOS in the future.
* Extend the spike to media (`slp` / `drs` / blendomatic / sounds)
  once metadata is fully green.
