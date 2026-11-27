# Copyright 2026+ the openage authors. See copying.md for legal info.

"""Regression tests for DE2 graphics export requests."""

from types import SimpleNamespace
from typing import TYPE_CHECKING, cast

from openage.convert.entity_object.export.metadata_export import SpriteMetadataExport
from openage.convert.processor.conversion.de2.media_subprocessor import DE2MediaSubprocessor
from openage.testing.testing import assert_value

if TYPE_CHECKING:
    from openage.convert.entity_object.conversion.aoc.genie_object_container import GenieObjectContainer

def media_requests() -> None:
    """Distinct DE2 layers survive sentinel SLP IDs and shared sprite references."""

    class Graphic(dict):
        """Small graphics records with the fields consumed by the exporter."""

        def __init__(self, identifier: int):
            super().__init__(
                (key, SimpleNamespace(value=value))
                for key, value in {
                    "filename": f"source_{identifier}",
                    "slp_id": -1,
                    "sequence_type": 0,
                    "layer": 20,
                    "frame_rate": 0.1,
                    "replay_delay": 0.0,
                    "frame_count": 1,
                    "angle_count": 1,
                }.items()
            )
            self.identifier = identifier

        def get_id(self) -> int:
            return self.identifier

    class Sprite:
        """Two sprite definitions referring to the same exported graphics."""

        def __init__(self, name: str):
            self.name = name

        def get_filename(self) -> str:
            return self.name

        def get_graphics(self) -> list[Graphic]:
            return graphics

        def resolve_graphics_location(self) -> dict[int, str]:
            return {graphic.get_id(): "shared/graphics" for graphic in graphics}

        def resolve_sprite_location(self) -> str:
            return f"units/{self.name}/graphics"

    graphics = [Graphic(11), Graphic(12)]
    dataset = SimpleNamespace(
        combined_sprites={1: Sprite("idle"), 2: Sprite("construct")},
        graphics_exports={},
        metadata_exports=[],
    )
    DE2MediaSubprocessor.create_graphics_requests(cast("GenieObjectContainer", dataset))
    assert_value(len(dataset.graphics_exports), 2)
    filenames = {request.target_filename for request in dataset.graphics_exports.values()}
    assert_value(len(filenames), 2)
    sprites = [entry for entry in dataset.metadata_exports if isinstance(entry, SpriteMetadataExport)]
    assert_value(len(sprites), 2)
    for sprite in sprites:
        assert_value(set(sprite.graphics_metadata), filenames)
        for metadata in sprite.graphics_metadata.values():
            assert_value(metadata[0].startswith("../../../shared/graphics/"), True)
        assert_value(sprite.request_refs, {id(req) for req in dataset.graphics_exports.values()})
        assert_value(sprite.dump().count("\nlayer "), 2)
        for request in dataset.graphics_exports.values():
            assert_value(sprite in request.observers, True)
