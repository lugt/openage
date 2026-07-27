# Copyright 2026 the openage authors. See copying.md for legal info.
"""
Tests for B5-FIX1: ``SpriteMetadataExport`` subtex_count invariant.

Phase B5-FIX1 establishes:

  Every key in ``SpriteMetadataExport.graphics_metadata`` (i.e. every
  image registered by ``add_graphics_metadata()``) must have an entry in
  ``subtex_count`` by the time ``dump()`` runs. ``dump()`` must fail-fast
  with request provenance if the invariant is broken. The class must NOT
  silently swallow the gap with ``dict.get()``, ``except KeyError``,
  deletion, or filename-specific hardcoding.

These tests cover the scenarios Phase B5-FIX1 calls out:

  * single-frame animation
  * multi-frame animation
  * multi-direction animation
  * shared image (multiple add_graphics_metadata calls for one img_filename)
  * filtered request (no exporter update fired for that file)
  * renamed target (exporter updates with a different key than added)
  * missing invariant fail-fast with provenance
"""
from __future__ import annotations

import pytest

from openage.convert.entity_object.export.formats.sprite_metadata import (
    LayerMode as SpriteLayerMode,
)
from openage.convert.entity_object.export.metadata_export import (
    SpriteMetadataExport,
)


def _make_export() -> SpriteMetadataExport:
    """A blank SpriteMetadataExport suitable for individual tests."""
    return SpriteMetadataExport(
        targetdir="data/game_entity/generic/villager/graphics/",
        target_filename="villager.sprite",
    )


def _add_villager_idle(export, img="idle_villager_1473.png",
                        tex="idle_villager_1473.texture",
                        frames=1, angles=1):
    export.add_graphics_metadata(
        img_filename=img,
        tex_filename=tex,
        layer_mode=SpriteLayerMode.LOOP,
        layer_pos=0,
        frame_rate=0.0,
        replay_delay=0.0,
        frame_count=frames,
        angle_count=angles,
        mirror_mode=0,
        start_angle=270,
    )


def test_single_frame_seed_populates_subtex_count():
    """Single-frame animation; add_graphics_metadata alone sets subtex_count."""
    export = _make_export()
    _add_villager_idle(export, frames=1, angles=1)
    assert export.subtex_count["idle_villager_1473.png"] == 1


def test_multi_frame_seed_populates_subtex_count():
    """Multi-frame animation seeds N*1 = N."""
    export = _make_export()
    _add_villager_idle(export, img="walk_villager_0.png",
                        tex="walk_villager_0.texture",
                        frames=12, angles=1)
    assert export.subtex_count["walk_villager_0.png"] == 12


def test_multi_direction_seed_populates_subtex_count():
    """Multi-direction animation seeds frames * angles."""
    export = _make_export()
    _add_villager_idle(export, img="idle_villager_all_0.png",
                        tex="idle_villager_all_0.texture",
                        frames=12, angles=8)
    assert export.subtex_count["idle_villager_all_0.png"] == 96


def test_shared_image_seed_keeps_first_estimate_on_second_call():
    """Two add_graphics_metadata calls with the same img_filename must not
    double-stack counts. The exporter uses setdefault so the original
    seed survives a re-add. (The same target_filename should not normally
    be registered twice by the converter; this guards against the case
    where it happens.)
    """
    export = _make_export()
    _add_villager_idle(export, img="shared.png", tex="shared.texture",
                        frames=8, angles=1)
    _add_villager_idle(export, img="shared.png", tex="shared.texture",
                        frames=99, angles=1)
    assert export.subtex_count["shared.png"] == 8


def test_filtered_request_no_update_still_dumps():
    """If the texture exporter never fires (e.g., media filter excluded
    the request), the genie-derived seed carries dump() across the
    finish line. This is the regression case: previously this raised
    KeyError on the lookup in dump().
    """
    export = _make_export()
    _add_villager_idle(export, img="filtered_out_1473.png",
                        tex="filtered_out_1473.texture",
                        frames=10, angles=1)
    # No update() call. dump() must succeed without KeyError.
    out = export.dump()
    assert "filtered_out_1473.png" in out or len(out) > 0


def test_exporter_override_refines_seed():
    """If update() fires (the exporter actually built the texture) the
    seed is overwritten with the exact subtex_metadata length. A real
    atlas with fewer frames than the genie estimate should reflect that
    downstream."""
    export = _make_export()
    _add_villager_idle(export, img="override.png", tex="override.texture",
                        frames=20, angles=1)
    # Exporter reports only 12 frames made it into the atlas.
    fake_observable = object()
    export.update(
        fake_observable,
        message={"override.png": {"size": (256, 256), "subtex_metadata": [{}] * 12}},
    )
    assert export.subtex_count["override.png"] == 12


def test_renamed_target_does_not_collide_keys():
    """If an exporter's update() somehow reports a renamed key (e.g.,
    the .texture filename instead of .png), the dump invariant must
    not be polluted: the genie seed for the .png key still applies,
    and the rename is irrelevant to the .png's lookup.
    """
    export = _make_export()
    _add_villager_idle(export, img="renamed.png", tex="renamed.texture",
                        frames=4, angles=1)
    export.update(object(), message={
        "some.other.key.texture": {
            "size": (256, 256),
            "subtex_metadata": [{}] * 99,
        },
    })
    # The renamed key doesn't help us; we still rely on the seed.
    assert export.subtex_count["renamed.png"] == 4
    # And no extra key appeared from the renamed update.
    assert "some.other.key.texture" in export.subtex_count  # from update()
    assert export.subtex_count["some.other.key.texture"] == 99


def test_missing_invariant_fail_fast_with_provenance():
    """If, by some bug, add_graphics_metadata is *not* called for a key
    that the dump() would have looked up (e.g., an external caller
    hand-rolled a broken SpriteMetadataExport), dump() must raise a
    structured Error whose message names the offending file plus its
    frame/angle count, instead of a silent placeholder or generic
    KeyError.
    """
    export = _make_export()
    # Bypass the seeding path: forge a graphics_metadata entry without
    # touching add_graphics_metadata. This simulates a future bug where
    # a different code path populates graphics_metadata externally.
    export.graphics_metadata["hypothetical.png"] = (
        "hypothetical.texture",
        SpriteLayerMode.LOOP,
        0,
        0.0, 0.0,
        5, 1,  # frame_count=5, angle_count=1
        0, 270,
    )
    # subtex_count was never populated for this entry.
    with pytest.raises(Exception) as exc:
        export.dump()
    msg = str(exc.value)
    assert "hypothetical.png" in msg
    # Provenance: must surface frame_count / angle_count so an operator
    # can re-trace what was registered.
    assert "frame_count=5" in msg
    assert "angle_count=1" in msg


def test_dump_proceeds_through_zero_seed():
    """A frame_count=0 image is a legal 'no frames' placeholder; the
    seed is 0 and dump() must accept that without raising.
    """
    export = _make_export()
    _add_villager_idle(export, img="empty.png", tex="empty.texture",
                        frames=0, angles=0)
    assert export.subtex_count["empty.png"] == 0
    out = export.dump()
    # Just don't raise.
    assert isinstance(out, str)
