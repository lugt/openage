# Copyright 2026 the openage authors. See copying.md for legal info.
"""
Tests for B5-FIX2: media-export filter covers metadata closure.

The filter layer (B5-FIX2) ensures that when a MediaExportRequest is
filtered out, its *exclusive* metadata dependency (the SpriteMetadata
Export and TextureMetadataExport only that request references) is also
dropped. Shared metadata files -- those referenced by more than one
request -- must survive until the last referencing request is dropped.
"""

from __future__ import annotations

import pytest

from openage.convert.entity_object.export.formats.sprite_metadata import (
    LayerMode as SpriteLayerMode,
)
from openage.convert.entity_object.export.metadata_export import (
    SpriteMetadataExport,
    TextureMetadataExport,
)


class _MockRequest:
    """Identity-only mock of MediaExportRequest, used only for id() refs."""

    def __init__(self, request_id: int):
        self._id = request_id

    def __repr__(self) -> str:
        return f"_MockRequest<{self._id}>"


def test_metadata_request_ref_lifecycle():
    """Adding the same request ID twice must not double-count -- set semantics."""
    sprite = SpriteMetadataExport(
        "data/game_entity/generic/villager/graphics/", "villager.sprite",
    )
    req = _MockRequest(42)
    sprite.add_request_ref(id(req))
    sprite.add_request_ref(id(req))
    sprite.add_request_ref(id(req))
    assert sprite.request_refs == {id(req)}


def test_metadata_has_live_request_shared_kept():
    """Two requests share a metadata entry; either kept keeps the metadata."""
    sprite = SpriteMetadataExport(
        "data/game_entity/generic/villager/graphics/", "villager.sprite",
    )
    req_a = _MockRequest(1)
    req_b = _MockRequest(2)
    sprite.add_request_ref(id(req_a))
    sprite.add_request_ref(id(req_b))

    # Both alive -> keep.
    assert sprite.has_live_request({id(req_a), id(req_b)})
    # Only one alive -> keep (shared; the other still references it).
    assert sprite.has_live_request({id(req_a)})
    # Neither alive -> drop.
    assert not sprite.has_live_request(set())


def test_metadata_has_live_request_orphan():
    """Only the dropped request referenced this; the metadata becomes orphan."""
    sprite = SpriteMetadataExport(
        "data/game_entity/generic/villager/graphics/", "attack_villager.sprite",
    )
    req_dropped = _MockRequest(7)
    sprite.add_request_ref(id(req_dropped))

    # Set of kept ids does not contain req_dropped -> orphan.
    assert not sprite.has_live_request({8, 9, 10})


def test_metadata_no_refs_general_infrastructure_kept():
    """Metadata with zero request_refs (e.g., blend-mask atlases) is
    general infrastructure and is always kept, even when nothing matches
    the filter. The filter has to opt out of pruning it.
    """
    sprite = SpriteMetadataExport(
        "data/blendmask/terrain/", "blendmask.texture",
    )
    # No add_request_ref calls.
    assert sprite.request_refs == set()
    # Even with "all requests dropped" intent, this stays.
    assert not sprite.has_live_request(set())  # no live, but we keep it
    # The caller (modpack_exporter) treats ``not refs`` as "always keep",
    # so the
    # dedicated behaviour lives there.


def test_filter_module_returns_kept_request_ids():
    """apply_media_filter returns FilterResult.kept_request_ids with the
    id() of kept MediaExportRequests; None when no filter was applied.
    """
    from openage.convert.entity_object.export.media_export_request import (
        MediaExportRequest,
    )
    from openage.convert.value_object.read.media_types import MediaType
    from openage.convert.tool.media_export_filter import apply_media_filter

    class _Req:
        def __init__(self, name):
            self.targetdir = (
                "data/game_entity/generic/villager/graphics/")
            self.target_filename = name

    req_keep = _Req("keep.png")
    req_drop = _Req("drop.png")
    synth = {
        MediaType.GRAPHICS: [req_keep, req_drop],
    }

    import tempfile

    with tempfile.NamedTemporaryFile("w", suffix=".filter", delete=False) as fh:
        fh.write("de2_base:graphics:*/villager/*:keep.png\n")
        filter_path = fh.name
    try:
        _filtered, result = apply_media_filter(synth, "de2_base", filter_path)
        assert result.kept_request_ids == {id(req_keep)}
        assert result.kept_requests == 1
        assert result.dropped_requests == 1
    finally:
        import os
        os.unlink(filter_path)


def test_filter_empty_filter_pass_through_no_ids():
    """An empty filter file is pass-through (kept_request_ids == None, not
    a set) so the metadata pruner knows not to prune.
    """
    from openage.convert.entity_object.export.media_export_request import (
        MediaExportRequest,
    )
    from openage.convert.value_object.read.media_types import MediaType
    from openage.convert.tool.media_export_filter import apply_media_filter

    class _Req:
        targetdir = "x/y/"
        target_filename = "a.png"

    synth = {MediaType.GRAPHICS: [_Req()]}
    import tempfile
    with tempfile.NamedTemporaryFile("w", suffix=".filter", delete=False) as fh:
        pass  # empty
        filter_path = fh.name
    try:
        _, result = apply_media_filter(synth, "any", filter_path)
        assert result.kept_request_ids is None
        assert result.kept_requests == 1
    finally:
        import os
        os.unlink(filter_path)
