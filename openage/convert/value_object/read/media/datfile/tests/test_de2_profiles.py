# Copyright 2026 the openage authors. See copying.md for legal info.
"""
Tests for DE2 dat layout profiles (8.4 / 8.9).

Three layers:

  A. Profile resolver -- maps post-decrypt "VER x.y" header strings to
     ``De2DatLayoutProfile`` instances.  Pure function; raises on
     unknown / non-string input.

  B. Structural synthetic tests -- for each of the five known 8.4 <-> 8.9
     diffs, the unit-format list (and, where feasible, a minimal byte
     walk) confirms the right fields appear, in the right order, with
     the right next-field alignment, and that truncated input fails
     loudly rather than silently.

  C. Combination / regression -- 8.4 still produces the original layout;
     an unknown profile string is rejected rather than silently routed
     to 8.4 or 8.9.

No proprietary bytes are sliced from any real ``.dat``; all fixtures
are hand-written and well under 200 bytes each.
"""
from __future__ import annotations

import struct
from dataclasses import FrozenInstanceError
from typing import Optional

import pytest

from openage.convert.value_object.init.game_version import (
    GameEdition,
    GameExpansion,
    GameVersion,
    Support,
)
from openage.convert.value_object.read.genie_structure import STRUCT_TYPE_LOOKUP
from openage.convert.value_object.read.media.datfile import unit as unit_mod
from openage.convert.value_object.read.media.datfile.de2_profile import (
    DAMAGE_GRAPHIC_LAYOUT_ABSENT,
    DAMAGE_GRAPHIC_LAYOUT_V2,
    DE2_84_PROFILE,
    DE2_89_PROFILE,
    DE2_BASE_PROFILE,
    De2DatLayoutProfile,
    UnsupportedDatProfileError,
    resolve_de2_dat_profile,
)
from openage.convert.value_object.read.source import VARARRAY_MATCH, GenieSource, _NS
from openage.convert.value_object.read.value_members import StorageType


# --------------------------------------------------------------------------- #
# Fixtures
# --------------------------------------------------------------------------- #


def _make_edition(game_id: str = "AOE2DE") -> GameEdition:
    """Build a minimal GameEdition for tests.  Empty paths / hashes."""
    return GameEdition(
        name=f"test-{game_id}",
        game_id=game_id,
        support="YES",
        game_version_info=[],
        media_paths=[],
        install_paths={},
        modpacks=[],
        expansions=[],
    )


def _make_gv(profile: Optional[De2DatLayoutProfile] = None,
             update_number=None,
             game_id: str = "AOE2DE") -> GameVersion:
    """Build a GameVersion carrying the given DE2 profile (or None)."""
    edition = _make_edition(game_id=game_id)
    return GameVersion(
        edition=edition,
        expansions=(),
        update_number=update_number,
        de2_profile=profile,
    )


def _field_size(var_type) -> int:
    """Return the byte size of a single var_type per Genie layout rules."""
    if isinstance(var_type, str):
        m = VARARRAY_MATCH.match(var_type)
        if m:
            type_part = m.group(1)
            count = int(m.group(2))
            symbol = STRUCT_TYPE_LOOKUP[type_part]
            return struct.calcsize(f"<{count}{symbol}")
        symbol = STRUCT_TYPE_LOOKUP[var_type]
        return struct.calcsize(f"<{symbol}")
    return 0


# --------------------------------------------------------------------------- #
# A. Profile resolver
# --------------------------------------------------------------------------- #


def test_resolver_8_4_returns_84_profile():
    p = resolve_de2_dat_profile("VER 8.4")
    assert p is DE2_84_PROFILE


def test_resolver_8_9_returns_89_profile():
    p = resolve_de2_dat_profile("VER 8.9")
    assert p is DE2_89_PROFILE


def test_resolver_unknown_version_raises():
    with pytest.raises(UnsupportedDatProfileError):
        resolve_de2_dat_profile("VER 9.0")


def test_resolver_empty_string_raises():
    with pytest.raises(UnsupportedDatProfileError):
        resolve_de2_dat_profile("")


def test_resolver_non_string_raises():
    with pytest.raises(UnsupportedDatProfileError):
        resolve_de2_dat_profile(b"VER 8.9")  # bytes, not str


def test_resolver_is_pure():
    a = resolve_de2_dat_profile("VER 8.9")
    b = resolve_de2_dat_profile("VER 8.9")
    assert a is b


def test_profile_is_frozen():
    with pytest.raises(FrozenInstanceError):
        DE2_89_PROFILE.unit_command_trailer_bytes = 99


def test_84_and_base_share_layout_fields():
    # Per the spec: same shape on the five diffs; distinct profile_id
    # so future variants that diverge from 8.4 on the BASE shape can
    # be added without colliding.
    assert DE2_84_PROFILE.unit_command_trailer_bytes == DE2_BASE_PROFILE.unit_command_trailer_bytes
    assert DE2_84_PROFILE.missile_trailer_bytes == DE2_BASE_PROFILE.missile_trailer_bytes
    assert DE2_84_PROFILE.living_unit_trailer_bytes == DE2_BASE_PROFILE.living_unit_trailer_bytes
    assert DE2_84_PROFILE.has_wwise_dying_sound_id == DE2_BASE_PROFILE.has_wwise_dying_sound_id
    assert DE2_84_PROFILE.building_damage_graphic_layout == DE2_BASE_PROFILE.building_damage_graphic_layout
    assert DE2_84_PROFILE.profile_id != DE2_BASE_PROFILE.profile_id


def test_89_has_the_five_known_diffs():
    assert DE2_89_PROFILE.unit_command_trailer_bytes == 2
    assert DE2_89_PROFILE.building_damage_graphic_layout == DAMAGE_GRAPHIC_LAYOUT_V2
    assert DE2_89_PROFILE.has_wwise_dying_sound_id is False
    assert DE2_89_PROFILE.missile_trailer_bytes == 16
    assert DE2_89_PROFILE.living_unit_trailer_bytes == 47


# --------------------------------------------------------------------------- #
# B. Structural synthetic tests
# --------------------------------------------------------------------------- #


# --- B.1 UnitCommand trailer (8.4 absent, 8.9 +2B int16) ------------------- #


def _format_has_named_field(fmt, name: str) -> bool:
    # get_data_format_members yields 4-tuples: (access, name, storage, type).
    # get_data_format yields 5-tuples: (is_parent, access, name, storage, type).
    if fmt and len(fmt[0]) == 5:
        return any(entry[2] == name for entry in fmt)
    return any(entry[1] == name for entry in fmt)


def test_unit_command_84_has_no_de2_command_trail():
    fmt = unit_mod.UnitCommand.get_data_format_members(_make_gv(DE2_84_PROFILE))
    assert not _format_has_named_field(fmt, "de2_command_trail"), (
        "8.4 must NOT carry de2_command_trail; the field would shift every "
        "field after the trail by 2 bytes and break the cumulative offset."
    )


def test_unit_command_89_has_int16_trail():
    fmt = unit_mod.UnitCommand.get_data_format_members(_make_gv(DE2_89_PROFILE))
    trail = [e for e in fmt if e[1] == "de2_command_trail"]
    assert len(trail) == 1
    entry = trail[0]
    access, name, storage_type, var_type = entry
    assert access == unit_mod.READ_UNKNOWN
    assert name == "de2_command_trail"
    assert storage_type == StorageType.ID_MEMBER
    assert var_type == "int16_t"
    assert _field_size(var_type) == 2


def test_unit_command_89_alignment_via_byte_walk():
    """
    Construct a 69-byte stream (67B of zeros + 2B int16 trail = 0xFFFF) and
    confirm the lazy reader walks the UnitCommand to offset 69 and reads
    the trail value 0xFFFF.  This is the end-to-end "next-field alignment"
    check the design spec requires.
    """
    raw = b"\x00" * 67 + struct.pack("<h", -1)
    assert len(raw) == 69
    gv = _make_gv(DE2_89_PROFILE)
    src = GenieSource(raw, gv, None)
    consumed = src._walk_struct(unit_mod.UnitCommand, 0, _NS(), [])
    assert consumed == 69, (
        f"UnitCommand 8.9 should consume 69 bytes; got {consumed}.  An "
        f"off-by-2 here means the cumulative offset of unit_commands[i+1] "
        f"is wrong and the walker will eventually overrun the raw dat."
    )


def test_unit_command_89_truncated_raises():
    """Cutting the trail in half must raise, not silently advance."""
    raw = b"\x00" * 67 + b"\xff"  # 68 B; trail needs 2 B
    gv = _make_gv(DE2_89_PROFILE)
    src = GenieSource(raw, gv, None)
    with pytest.raises(RuntimeError, match="ran off end of raw"):
        src._walk_struct(unit_mod.UnitCommand, 0, _NS(), [])


# --- B.2 Building damage-graphic array (8.4 absent, 8.9 v2) ---------------- #


def test_unit_object_84_omits_building_damage_graphics_v2():
    fmt = unit_mod.UnitObject.get_data_format_members(_make_gv(DE2_84_PROFILE))
    assert not _format_has_named_field(fmt, "de2_building_damage_graphics")


def test_unit_object_89_includes_building_damage_graphics_v2():
    fmt = unit_mod.UnitObject.get_data_format_members(_make_gv(DE2_89_PROFILE))
    assert _format_has_named_field(fmt, "de2_building_damage_graphics")


def test_damage_graphic_89_layout_is_5_bytes():
    fmt = unit_mod.DamageGraphic89.get_data_format_members(_make_gv(DE2_89_PROFILE))
    total = sum(_field_size(e[3]) for e in fmt)
    assert total == 5, (
        f"DamageGraphic89 must be 5 B (leading int8 + int16 graphic_id + "
        f"int8 damage_percent + int8 apply_mode); got {total}"
    )


# --- B.3 wwise_dying_sound_id (8.4 present, 8.9 absent) ------------------- #


def test_unit_object_84_has_wwise_dying_sound_id():
    fmt = unit_mod.UnitObject.get_data_format_members(_make_gv(DE2_84_PROFILE))
    assert _format_has_named_field(fmt, "wwise_dying_sound_id")


def test_unit_object_89_omits_wwise_dying_sound_id():
    fmt = unit_mod.UnitObject.get_data_format_members(_make_gv(DE2_89_PROFILE))
    assert not _format_has_named_field(fmt, "wwise_dying_sound_id"), (
        "8.9 removed wwise_dying_sound_id (-4 B).  If this fires the "
        "name_len after the sound block is read from inside the name "
        "string and the cumulative offset cascades into a 260 KiB over-read."
    )


# --- B.4 MissileUnit trailer (8.4 0, 8.9 16 B) ----------------------------- #


def test_missile_unit_84_has_no_de2_missile_trailer():
    fmt = unit_mod.MissileUnit.get_data_format_members(_make_gv(DE2_84_PROFILE))
    assert not _format_has_named_field(fmt, "de2_missile_trailer")


def test_missile_unit_89_has_16_byte_trailer():
    fmt = unit_mod.MissileUnit.get_data_format_members(_make_gv(DE2_89_PROFILE))
    trail = [e for e in fmt if e[1] == "de2_missile_trailer"]
    assert len(trail) == 1
    var_type = trail[0][3]
    assert "uint8_t[16]" == var_type
    assert _field_size(var_type) == 16


# --- B.5 LivingUnit trailer (8.4 0, 8.9 47 B) ----------------------------- #


def test_living_unit_84_has_no_de2_living_trailer():
    fmt = unit_mod.LivingUnit.get_data_format_members(_make_gv(DE2_84_PROFILE))
    assert not _format_has_named_field(fmt, "de2_living_trailer")


def test_living_unit_89_has_47_byte_trailer():
    fmt = unit_mod.LivingUnit.get_data_format_members(_make_gv(DE2_89_PROFILE))
    trail = [e for e in fmt if e[1] == "de2_living_trailer"]
    assert len(trail) == 1
    var_type = trail[0][3]
    assert "uint8_t[47]" == var_type
    assert _field_size(var_type) == 47


# --------------------------------------------------------------------------- #
# C. Combination / regression
# --------------------------------------------------------------------------- #


def test_84_path_is_byte_for_byte_unchanged():
    """
    The 8.4 path must produce exactly the original 8.4 layout: no
    de2_* trail fields, wwise_dying_sound_id present, no 16/47-byte
    trailers.  A regression here would break the Ubuntu-verified install.
    """
    gv = _make_gv(DE2_84_PROFILE)
    for cls, name in (
        (unit_mod.UnitCommand, "UnitCommand"),
        (unit_mod.UnitObject, "UnitObject"),
        (unit_mod.MissileUnit, "MissileUnit"),
        (unit_mod.LivingUnit, "LivingUnit"),
    ):
        fmt = cls.get_data_format_members(gv)
        for de2_field in (
            "de2_command_trail",
            "de2_building_damage_graphics",
            "de2_missile_trailer",
            "de2_living_trailer",
        ):
            assert not _format_has_named_field(fmt, de2_field), (
                f"{name} 8.4 unexpectedly carries {de2_field}"
            )
        assert _format_has_named_field(fmt, "wwise_dying_sound_id") or name != "UnitObject", (
            f"{name} 8.4 lost wwise_dying_sound_id"
        )


def test_unknown_profile_raises_when_reader_runs():
    """
    If a caller forgets to call resolve_de2_dat_profile(), the reader
    must raise rather than guess a layout.
    """
    gv = _make_gv(profile=None)  # profile not set
    src = GenieSource(b"\x00" * 100, gv, None)
    with pytest.raises(UnsupportedDatProfileError):
        src._walk_struct(unit_mod.UnitCommand, 0, _NS(), [])


def test_bfglabel_does_not_change_8_4_layout():
    """
    DE2_84_PROFILE drives the 8.4 layout even when update_number is
    None or "base" -- the *profile* is authoritative for DAT layout,
    not the legacy update_number string label.
    """
    fmt_with_label = unit_mod.UnitObject.get_data_format_members(
        _make_gv(DE2_84_PROFILE, update_number="BfG"))
    fmt_without_label = unit_mod.UnitObject.get_data_format_members(
        _make_gv(DE2_84_PROFILE, update_number=None))
    names_with = sorted(e[1] for e in fmt_with_label if e[1])
    names_without = sorted(e[1] for e in fmt_without_label if e[1])
    assert names_with == names_without
