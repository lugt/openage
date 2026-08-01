# Copyright 2026 the openage authors. See copying.md for legal info.
"""
Versioned DE2 DAT layout profiles.

The DE2 (.dat) reader must handle two shipped layout variants:

  * ``VER 8.4``  -- BfG (Battle for Greece) baseline, currently verified
                    on the Ubuntu install.
  * ``VER 8.9``  -- 2025+ AoE2DE update, observed on the macOS install.

The differences are described by *layout shape* (length, presence,
enumeration), not by per-version booleans, so future variants can
compose new profiles by field-level diff.  Only ``resolve_de2_dat_profile``
is allowed to map a post-decrypt ``"VER x.y"`` header string to a
profile; reader code consults the returned profile and never the raw
header or ``game_version.update_number``.

The pre-existing BfG DLC trailers (16 B ``bfg_projectile_unknown``,
25 B ``bfg_living_unknown``) are *BfG DLC* layout, not part of the
8.4 <-> 8.9 diff set; they remain inline in the reader for now and
may move into a future ``bfg_dlc`` sub-profile.
"""
from __future__ import annotations

from dataclasses import dataclass


# Building damage-graphic layout identifier.
#   "absent" -- no per-building damage graphic array (8.4 / pre-8.9)
#   "v2"     -- fixed 3-element array, 5 B per record, reordered fields (8.9)
DAMAGE_GRAPHIC_LAYOUT_ABSENT = "absent"
DAMAGE_GRAPHIC_LAYOUT_V2 = "v2"


class UnsupportedDatProfileError(RuntimeError):
    """
    Raised when a ``"VER x.y"`` header string does not map to a known
    DE2 layout profile.  We never silently fall back to a wrong layout
    because the result would be a silently-corrupt read.
    """


@dataclass(frozen=True)
class De2DatLayoutProfile:
    """
    Layout shape of a DE2 ``empires2_x2_p1.dat`` for a given
    post-decrypt ``"VER x.y"`` header.  Fields describe *shape*;
    reader code branches on them.
    """

    # Symbolic id used in logs and tests.  Distinct from
    # ``GameVersion.update_number`` so the two can evolve separately.
    profile_id: str

    # Number of opaque trailer bytes appended to every ``UnitCommand``
    # record at the end.  0 for 8.4 (no per-command trail); 2 for 8.9
    # (one int16, observed value -1).
    unit_command_trailer_bytes: int

    # ``DAMAGE_GRAPHIC_LAYOUT_*``.  8.4 has no per-building array;
    # 8.9 inserts a fixed 3-element, 5 B/record array on buildings.
    building_damage_graphic_layout: str

    # Whether the 4-field ``UnitObject`` wwise sound block still
    # includes ``wwise_dying_sound_id`` as its fourth uint32.  8.4
    # yes; 8.9 no (the field was removed, -4 B).
    has_wwise_dying_sound_id: bool

    # Opaque trailer bytes between ``area_of_effect_special`` and
    # ``projectile_arc`` on ``MissileUnit``.  0 for 8.4; 16 for 8.9.
    missile_trailer_bytes: int

    # Opaque trailer bytes appended at the end of every
    # ``LivingUnit`` (and its ``BuildingUnit`` subclass) record.
    # 0 for 8.4; 47 for 8.9.
    living_unit_trailer_bytes: int


# Base profile.  Same shape as the 8.4 baseline on the five known
# diffs, but kept as a distinct instance so a future variant that
# differs from 8.4 on the *base* fields (and from 8.9 on the 8.9
# fields) can split cleanly.
DE2_BASE_PROFILE = De2DatLayoutProfile(
    profile_id="DE2_BASE",
    unit_command_trailer_bytes=0,
    building_damage_graphic_layout=DAMAGE_GRAPHIC_LAYOUT_ABSENT,
    has_wwise_dying_sound_id=True,
    missile_trailer_bytes=0,
    living_unit_trailer_bytes=0,
)


# ``VER 8.4`` / BfG.  All five diffs match the base on the Ubuntu
# reference install.
DE2_84_PROFILE = De2DatLayoutProfile(
    profile_id="DE2_84",
    unit_command_trailer_bytes=0,
    building_damage_graphic_layout=DAMAGE_GRAPHIC_LAYOUT_ABSENT,
    has_wwise_dying_sound_id=True,
    missile_trailer_bytes=0,
    living_unit_trailer_bytes=0,
)


# ``VER 8.9`` / 2025+ AoE2DE update, observed on the macOS install.
DE2_89_PROFILE = De2DatLayoutProfile(
    profile_id="DE2_89",
    unit_command_trailer_bytes=2,
    building_damage_graphic_layout=DAMAGE_GRAPHIC_LAYOUT_V2,
    has_wwise_dying_sound_id=False,
    missile_trailer_bytes=16,
    living_unit_trailer_bytes=47,
)


# Map from post-decrypt ``"VER x.y"`` strings to profile.  Kept
# narrow: only the strings we have actually seen in a known install
# are mapped; everything else raises.
_VERSION_TO_PROFILE: dict[str, De2DatLayoutProfile] = {
    # 8.4 baseline (BfG).  The reader also keeps the legacy
    # ``update_number="BfG"`` string label, but the layout decision
    # is made here.
    "VER 8.4": DE2_84_PROFILE,
    # 8.9 2025+ update.  Observed on the macOS Steam install.
    "VER 8.9": DE2_89_PROFILE,
}


def resolve_de2_dat_profile(version_header: str) -> De2DatLayoutProfile:
    """
    Map a post-decrypt Genie version string to a
    :class:`De2DatLayoutProfile`.

    The resolver is the *only* function that maps a header string to
    a profile; reader code consults the returned profile, not the raw
    header.  Unknown versions raise :class:`UnsupportedDatProfileError`
    so the caller does not silently fall back to a wrong layout.

    :param version_header: The post-decrypt version string, e.g.
                           ``"VER 8.4"`` or ``"VER 8.9"``.
    :raises UnsupportedDatProfileError: if the header is not in the
                                        known-version table.
    """
    if not isinstance(version_header, str):
        raise UnsupportedDatProfileError(
            f"de2 profile resolver expects str, got "
            f"{type(version_header).__name__}")
    profile = _VERSION_TO_PROFILE.get(version_header)
    if profile is None:
        raise UnsupportedDatProfileError(
            f"de2 dat layout profile unknown for version header "
            f"{version_header!r}; known: {sorted(_VERSION_TO_PROFILE)}")
    return profile
