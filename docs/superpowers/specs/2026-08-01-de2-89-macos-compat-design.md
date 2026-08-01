# DE2 8.9 macOS Compatibility — Design

**Date:** 2026-08-01
**Branch:** `mvp/william-wallace` @ `3cae3948`
**Scope:** AoE2DE 8.4 ↔ 8.9 DAT layout compatibility, metadata-only.
**Out of scope:** P4, Scenario Runtime, Gameplay Lifecycle, Mission 1, media conversion.

## Motivation

The macOS Steam install of AoE2DE ships data under `~/.../AoE2DE/AgeOfEmpires2Data/` whose
primary `.dat` (`empires2_x2_p1.dat`, 11,076,546 B) carries the Genie header string
`"VER 8.9"`. The Ubuntu-verified reference data is `"VER 8.4"` (BfG). The macOS data is
genuine DE2 stock; the difference is the build shipped on that platform, not a re-pack
or third-party fork. We need a minimum, scoped compatibility spike so macOS contributors
can develop against their own data, without regressing the Ubuntu-verified path and
without widening scope into P4 or media conversion.

Hard constraints from the user:

- No bypassing via installing older DE2 / AoC / HD.
- No committing `.dat`, campaign, scenario, or any other proprietary bytes.
- Detection must come from the dat header, not from `sys.platform`.
- Version branching must be centralized in a layout profile; reader code may not
  consult `game_version.update_number` for DAT layout decisions.
- The 8.4 (BfG) and 8.9 outputs must converge on the same normalized Genie/nyan
  data model. Scenario IR, Mission Pack, GameState, and Event systems are
  untouched. The eager backend is not revived; lazy/mmap stays.
- Atomic commits, no automatic progression to P4.

## Audit Summary (pre-design)

| File under `resources/_common/dat/` | Size (B) | SHA-256 (first 32 hex) | Header version |
| --- | --- | --- | --- |
| `empires2_x2_p1.dat` | 11,076,546 | `ce3530df36cf0b333a9751cb0ff94460` | `VER 8.9` (post-decrypt) |

The first 256 bytes of the file are binary (Genie header is XOR-obfuscated); the
version string is read after decryption in `GenieSource._read` near
`source.py:280-312`. No proprietary content is reproduced in this spec or the
report.

Working tree at the start of the spike contains **four** modified files relevant
to 8.9 plus one **untracked** orthogonal file (`output_checks.py`, preserved at
`/tmp/de2-89-spike-stash/output_checks.py` and excluded from this spike's
commits).

## Known Schema Differences (8.4 → 8.9)

Working-tree evidence identifies five structural differences between the two
layouts. Each is described by the *layout shape* (length / presence /
enumeration), not by a per-version boolean, so future versions can compose
profiles by field-level diff:

| # | Struct / field | 8.4 (BfG) shape | 8.9 shape | Source of evidence |
| --- | --- | --- | --- | --- |
| 1 | `UnitCommand` trailing bytes | none | +2 bytes (int16, observed -1) appended at end of record | `unit.py:98-107` working-tree comment |
| 2 | `DamageGraphic` for buildings (unit_type=80) | absent from `UnitObject` | fixed 3-element array, 5 B/record, leading `int8` (reordered vs. legacy record) | `unit.py:209-239` `DamageGraphic89` |
| 3 | `UnitObject.wwise_dying_sound_id` (4 B uint32) | present | removed (-4 B) | `unit.py:578-591` working-tree comment |
| 4 | `MissileUnit` trailer between `area_of_effect_special` and `projectile_arc` | none | 16 B unknown opaque | `unit.py:942-958` working-tree comment |
| 5 | `LivingUnit` trailer after `pierce_armor_displayed` | none | 47 B unknown opaque | `unit.py:1079-1090` working-tree comment |

Differences 1, 3 are decoded; differences 2, 4, 5 are *only* sized opaque
records. The reader is expected to consume the bytes and advance the cursor;
the unknown bytes are not interpreted into the normalized model in this spike.

The 8.4 / BfG baseline is itself an extension of the base Genie DE2 layout
(`update_number="BfG"`, +16/+25 byte tails in some UnitCommand/Task records
already captured by the pre-existing 8.4 reader). This design migrates *only
the five differences above* into the profile system; unrelated BfG/DLC
edition-specific branches elsewhere in the converter are out of scope and
remain as inline `update_number` checks.

## Architecture

### New module: `openage/convert/value_object/read/media/datfile/de2_profile.py`

```python
@dataclass(frozen=True)
class De2DatLayoutProfile:
    profile_id: str                          # "DE2_BASE" | "DE2_84" | "DE2_89"
    unit_command_trailer_bytes: int          # 0 for 8.4, 2 for 8.9
    building_damage_graphic_layout: str      # "absent" | "v2"
    has_wwise_dying_sound_id: bool           # True for 8.4, False for 8.9
    missile_trailer_bytes: int               # 0 for 8.4, 16 for 8.9
    living_unit_trailer_bytes: int           # 0 for 8.4, 47 for 8.9

DE2_BASE_PROFILE = De2DatLayoutProfile(...)
DE2_84_PROFILE   = De2DatLayoutProfile(...)   # equals BASE on the five diffs
DE2_89_PROFILE   = De2DatLayoutProfile(...)

def resolve_de2_dat_profile(version_header: str) -> De2DatLayoutProfile:
    """Map a post-decrypt version string (e.g. 'VER 8.4', 'VER 8.9') to a profile.

    Raises UnsupportedDatProfileError on unknown versions. The resolver is the
    *only* function that maps a header string to a profile; reader code consults
    the returned profile, not the raw header.
    """
```

The resolver raises rather than silently falling back, so an unknown future
version cannot accidentally parse under the wrong layout. The `update_number`
string label on `GameVersion` is kept for backward compatibility with the
rest of the converter, but the five DAT-layout fields above are **never**
re-derived from `update_number` in reader code.

### Wiring through `GameVersion`

`GameVersion` gains an optional `de2_profile: De2DatLayoutProfile | None` field.
If absent, readers raise `UnsupportedDatProfileError` rather than guessing.
The `GenieSource` constructor sets `de2_profile` from
`resolve_de2_dat_profile(versionstr)` *in addition to* setting `update_number`
("BfG" / "8.9" / "base"). `update_number` is preserved for non-DAT-layout
converter code paths (DLC handling, edition dispatch, etc.).

### Reader migration (`unit.py`, `source.py`)

- All six `if game_version.update_number == "8.9"` blocks in
  `openage/convert/value_object/read/media/datfile/unit.py` are replaced by
  profile queries. For example:
  - `if game_version.update_number == "8.9": ...v89_command_trail...`
    → `if gv.de2_profile.unit_command_trailer_bytes: data_format.append((...,StorageType.ID_MEMBER, "int16_t"))`
  - `if game_version.update_number == "8.9": ...v89_building_damage_graphics...`
    → `if gv.de2_profile.building_damage_graphic_layout == "v2": ...`
  - `if game_version.update_number != "8.9": ...wwise_dying_sound_id...`
    → `if gv.de2_profile.has_wwise_dying_sound_id: ...`
  - `if game_version.update_number == "8.9": ...v89_missile_unknown...`
    → `if gv.de2_profile.missile_trailer_bytes: ...`
  - `if game_version.update_number == "8.9": ...v89_living_trailer...`
    → `if gv.de2_profile.living_unit_trailer_bytes: ...`
- The 8.4 baseline migrates with it for the five known diffs only:
  `DE2_84_PROFILE` carries the eight (5) values that 8.4 currently
  exhibits on those five diffs (zero trailers, no 3-element building
  damage graphics, `wwise_dying_sound_id` present). The pre-existing
  BfG-specific 16 B `bfg_projectile_unknown` trailer
  (`unit.py:908`) and 25 B `bfg_living_unknown` trailer
  (`unit.py:1039`) are *BfG DLC* layout, not part of the 8.4↔8.9
  diff set, and remain inline as today per the user's "不重构仓库中
  所有 BfG、DLC 或 edition-specific 分支" instruction. A follow-up
  spike can lift them into a `bfg_dlc` sub-profile if desired.
- `DamageGraphic89` stays in `unit.py` (one file) but is gated by
  `building_damage_graphic_layout == "v2"` rather than by
  `update_number == "8.9"`.
- All `[DEBUG 8.9]` print statements in `source.py` are removed. The
  diagnostic attributes (`_dbg_path` / `_dbg_cls` / `_dbg_offset` /
  `[DEBUG 8.9]` blocks) are deleted.

### Detection (no platform coupling)

`resolve_de2_dat_profile("VER 8.4")` returns `DE2_84_PROFILE` on every
platform; `resolve_de2_dat_profile("VER 8.9")` returns `DE2_89_PROFILE` on
every platform. `sys.platform` is never read inside the converter. The
macOS install-path work in `cfg/converter/games/game_editions.toml` and
`cfg/converter/games/de2/version_hashes.toml` is generic Steam/DE2 paths
(no user-specific absolute paths) and is included in the second commit.

## Testing Strategy

Synthetic, no proprietary bytes. New file
`openage/convert/value_object/read/media/datfile/tests/test_de2_profiles.py`.

### A. Profile resolver

- `resolve_de2_dat_profile("VER 8.4")` → `DE2_84_PROFILE`
- `resolve_de2_dat_profile("VER 8.9")` → `DE2_89_PROFILE`
- `resolve_de2_dat_profile("VER 9.0")` → raises `UnsupportedDatProfileError`
  (no silent fallback)
- `resolve_de2_dat_profile` is a pure function; calling it with the same
  input twice returns the same profile; the returned profile is frozen.

### B. Structural synthetic tests

For each of the five known differences, a hand-written minimal byte fixture
is parsed via the unit-level reader (or a `GenieSource` constructed with a
fake raw byte stream) and asserted on:

- *consumed bytes* — the reader advances exactly the documented number of
  bytes for the trailer / array / missing field;
- *next-field alignment* — the byte that the next reader call reads is the
  expected synthetic value (so an off-by-N error fails this, not the trailer
  read itself);
- *parsed values* — for trailer values we can interpret (UnitCommand int16,
  DamageGraphic fields, etc.), the parsed value matches the fixture;
- *truncated input* — feeding a fixture cut short in the trailer produces a
  reader-level error (`RuntimeError` / custom `ReaderShortReadError`),
  not a silent overflow.

Test names:

- `test_unit_command_trailer_89_alignment`
- `test_unit_command_trailer_89_truncated`
- `test_building_damage_graphics_89_layout_v2`
- `test_building_damage_graphics_89_truncated`
- `test_wwise_dying_sound_id_absent_89_alignment`
- `test_missile_trailer_89_16_bytes_alignment`
- `test_missile_trailer_89_truncated`
- `test_living_unit_trailer_89_47_bytes_alignment`
- `test_living_unit_trailer_89_truncated`

Each fixture is ≤ 200 bytes of hand-written struct data; no slicing of
`empires2_x2_p1.dat` is allowed.

### C. Combination & regression

- 8.4 UnitCommand still parses with the original BfG layout (no
  spurious trailer; the next record starts where it used to).
- 8.4 UnitObject still includes the wwise dying sound id field.
- A composed 8.9 UnitObject with all five differences stacked: the next
  record after the unit parses at the expected offset (i.e. the cumulative
  size math is consistent across all five patches).
- Unknown profile string → `UnsupportedDatProfileError`, never a 8.4 or
  8.9 fallback.
- Existing export tests
  (`openage/convert/entity_object/export/tests/`,
  `openage/convert/processor/conversion/aoc/tests/`,
  `openage/convert/processor/export/tests/`) all pass unchanged.

### D. Real macOS probe (integration evidence, not a substitute)

After synthetic tests pass, run a lazy metadata-only probe against the
real macOS `empires2_x2_p1.dat`. The probe:

- uses the lazy backend (no media conversion);
- records the first error, its offset, the reader state at that point;
- does not log raw game content;
- success criterion: macOS DE2 8.9 metadata parse completes, **or** the
  walker clears all five known differences and stops at a sixth
  structurally-precise error.

A sixth difference triggers the stop condition; we report it rather than
silently patching.

## Stop Conditions (handoff triggers)

The spike stops and reports `DE2_89_COMPATIBILITY_SPIKE_COMPLETE
/ ADAPTATION_DEFERRED` if any of the following are observed:

1. The lazy probe finds a sixth structural difference that affects more
   than one core struct class (not a single localized trailer).
2. The file is not a standard DE2 `.dat` (custom compression, header that
   is not Genie, etc.).
3. The file is third-party repackaged; no reliable schema can be derived.
4. Testing requires committing or copying proprietary bytes.
5. The same struct drifts twice after fixes (i.e. two correction rounds
   still leave the reader mis-aligned).
6. Adapting the datfile would require downstream changes to Scenario IR,
   GameState, or the Event system to absorb a source-format difference.

In the stop case, the report records the macOS workflow recommendation
(use Ubuntu-synced converted assets via `--no-convert` for P4–P7) and
makes no claim that media, terrain, campaign, or full DE2 conversion is
compatible.

## Commits (atomic, in order)

1. `tools: add read-only DE2 dat compatibility probe`
   — read-only audit / probe helper (no converter behavior change).
2. `convert: introduce versioned DE2 dat layout profiles`
   — `de2_profile.py` + `GameVersion.de2_profile` field + resolver; no
   reader yet.
3. `convert: route DE2 8.4 and 8.9 layouts through profiles`
   — migrate the six `update_number == "8.9"` blocks and the related
   BfG (8.4) branches for the five diffs; remove all `[DEBUG 8.9]`
   prints; macOS install-path and version-hash config (generic paths,
   no user-specific absolute paths).
4. `test: cover DE2 8.4 and 8.9 synthetic layouts`
   — resolver + structural + combination + regression tests.
5. `docs: record macOS DE2 8.9 compatibility findings`
   — `docs/mvp/reports/de2-89-macos-compatibility.md` (audit, decisions,
   probe results, stop conditions, handoff).

Excluded from these commits:

- `openage/convert/processor/export/output_checks.py` (preserved at
  `/tmp/de2-89-spike-stash/output_checks.py`, restored as untracked).
- Any real `.dat` content or derivative dump.
- Any debug `print` left in `source.py`.

## Post-Spike Handoff

After the spike:

- If `DE2_89_METADATA_COMPATIBLE` → next step is to extend the
  compatibility to media / terrain / blend / drs (a separate spike with
  its own design doc, not in this session).
- If `DE2_89_COMPATIBILITY_SPIKE_COMPLETE / ADAPTATION_DEFERRED` → return
  to P4 on the Ubuntu-verified dataset, using `--no-convert`; the macOS
  contributor path uses Ubuntu-synced converted assets in the meantime.
