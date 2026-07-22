# Copyright 2020-2023 the openage authors. See copying.md for legal info.
#
# pylint: disable=too-many-locals
"""
Convert media information to metadata definitions and export
requests. Subroutine of the main DE2 processor.
"""
from __future__ import annotations
import json
import os
import typing
from pathlib import Path

from ....entity_object.export.formats.sprite_metadata import LayerMode
from ....entity_object.export.media_export_request import MediaExportRequest
from ....entity_object.export.metadata_export import SpriteMetadataExport
from ....entity_object.export.metadata_export import TextureMetadataExport
from openage.log import info
from ....value_object.read.media_types import MediaType

if typing.TYPE_CHECKING:
    from openage.convert.entity_object.conversion.aoc.genie_object_container\
        import GenieObjectContainer


# Default location of the Wwise mapping JSON produced by
# openage/convert/tool/wwise/mapping.py. Override via OPENAGE_WWISE_MAP_PATH.
DEFAULT_WWISE_MAP_PATH = "/tmp/openage_wwise_mapping.json"


def _load_wwise_mapping(path: str | None = None) -> dict[str, list[int]]:
    """Load the {wwise_sound_id: [wem_id, ...]} JSON cache.

    Returns {} if the file is missing or invalid (the caller should then
    fall back to building it via openage.convert.tool.wwise.mapping.build_mapping).
    """
    path = path or os.environ.get("OPENAGE_WWISE_MAP_PATH", DEFAULT_WWISE_MAP_PATH)
    if not os.path.isfile(path):
        return {}
    try:
        with open(path) as f:
            data = json.load(f)
        return data.get("wwise_sound_id_to_wem_ids", {})
    except (OSError, json.JSONDecodeError):
        return {}


class DE2MediaSubprocessor:
    """
    Creates the exports requests for media files from DE2.
    """

    @classmethod
    def convert(cls, full_data_set: GenieObjectContainer) -> None:
        """
        Create all export requests for the dataset.
        """
        cls.create_graphics_requests(full_data_set)
        cls.create_sound_requests(full_data_set)

    @staticmethod
    def create_graphics_requests(full_data_set: GenieObjectContainer) -> None:
        """
        Create export requests for graphics referenced by CombinedSprite objects.
        """
        combined_sprites = full_data_set.combined_sprites.values()
        handled_graphic_ids = set()

        for sprite in combined_sprites:
            ref_graphics = sprite.get_graphics()
            graphic_targetdirs = sprite.resolve_graphics_location()

            # Animation metadata file definiton
            sprite_meta_filename = f"{sprite.get_filename()}.sprite"
            sprite_meta_export = SpriteMetadataExport(sprite.resolve_sprite_location(),
                                                      sprite_meta_filename)
            full_data_set.metadata_exports.append(sprite_meta_export)

            for graphic in ref_graphics:
                graphic_id = graphic.get_id()
                if graphic_id in handled_graphic_ids:
                    continue

                targetdir = graphic_targetdirs[graphic_id]
                source_filename = f"{str(graphic['filename'].value)}.sld"
                target_filename = f"{sprite.get_filename()}_{str(graphic['slp_id'].value)}.png"

                export_request = MediaExportRequest(MediaType.GRAPHICS,
                                                    targetdir,
                                                    source_filename,
                                                    target_filename)
                full_data_set.graphics_exports.update({graphic_id: export_request})

                # Texture metadata file definiton
                # Same file stem as the image file and same targetdir
                texture_meta_filename = f"{target_filename[:-4]}.texture"
                texture_meta_export = TextureMetadataExport(targetdir,
                                                            texture_meta_filename)
                full_data_set.metadata_exports.append(texture_meta_export)

                # Add texture image filename to texture metadata
                texture_meta_export.add_imagefile(target_filename)

                # Add metadata from graphics to animation metadata
                sequence_type = graphic["sequence_type"].value
                if sequence_type == 0x00:
                    layer_mode = LayerMode.OFF

                elif sequence_type & 0x08:
                    layer_mode = LayerMode.ONCE

                else:
                    layer_mode = LayerMode.LOOP

                layer_pos = graphic["layer"].value
                frame_rate = round(graphic["frame_rate"].value, ndigits=6)
                if frame_rate < 0.000001:
                    frame_rate = None

                replay_delay = round(graphic["replay_delay"].value, ndigits=6)
                if replay_delay < 0.000001:
                    replay_delay = None

                frame_count = graphic["frame_count"].value
                angle_count = graphic["angle_count"].value
                # mirror_mode = graphic["mirroring_mode"].value
                sprite_meta_export.add_graphics_metadata(target_filename,
                                                         texture_meta_filename,
                                                         layer_mode,
                                                         layer_pos,
                                                         frame_rate,
                                                         replay_delay,
                                                         frame_count,
                                                         angle_count,
                                                         mirror_mode=0,
                                                         start_angle=270)

                # Notify metadata export about SMX metadata when the file is exported
                export_request.add_observer(texture_meta_export)
                export_request.add_observer(sprite_meta_export)

                handled_graphic_ids.add(graphic_id)

        # TODO: Terrain exports (DDS files)

    @staticmethod
    def create_sound_requests(full_data_set: GenieObjectContainer) -> None:
        """
        Create export requests for sounds referenced by CombinedSound objects.

        DE2 audio is in Wwise .pck soundbanks (not AoC's loose .wav + .opus
        path). The pipeline is:
          1. Build {graphic.wwise_sound_id -> [wem_id, ...]} from the .pck HIRC
             via openage.convert.tool.wwise.mapping (cached as JSON).
          2. For every distinct (wwise_sound_id, wem_id) used by a graphic,
             emit one MediaExportRequest with source_filename = str(wem_id).
          3. media_exporter.py's Wwise branch reads .wem bytes via DIDX lookup,
             decodes via vgmstream-cli, and encodes to .opus @ 96 kbps VBR.

        The .opus file is placed in the shared sounds directory (same as AoC
        so the engine's audio loader doesn't need a separate code path).
        Naming: {wwise_sound_id}_{wem_id}.opus to allow multiple wem_ids per
        event (e.g. random/sequence containers).
        """
        # Load pre-built mapping. If absent, skip (the run-level driver is
        # expected to call build_mapping before invoking convert()).
        wwise_map = _load_wwise_mapping()
        if not wwise_map:
            info("DE2 sound exports: no wwise mapping at %s; "
                 "run openage.convert.tool.wwise.mapping first to enable audio.",
                 os.environ.get("OPENAGE_WWISE_MAP_PATH", DEFAULT_WWISE_MAP_PATH))
            return

        # Collect (wwise_sound_id, wem_id) pairs from graphics. The graphic's
        # wwise_sound_id is the event_ulID in HIRC; the mapping resolves it
        # to one or more wem_ids (random/sequence containers have several).
        seen: set[tuple[int, int]] = set()
        # DE2 doesn't have a flat `graphics` dict; graphics are reached via
        # combined_sprites -> GenieGraphic -> graphic["wwise_sound_id"].
        for sprite in full_data_set.combined_sprites.values():
            for graphic in sprite.get_graphics():
                try:
                    wid = graphic["wwise_sound_id"].value
                except (KeyError, AttributeError):
                    continue
                if wid == 0:
                    continue
                wem_ids = wwise_map.get(str(wid), [])
                for wem_id in wem_ids:
                    key = (wid, wem_id)
                    if key in seen:
                        continue
                    seen.add(key)
                    targetdir = "data/game_entity/shared/sounds/"
                    source_filename = str(wem_id)
                    target_filename = f"{wid}_{wem_id}.opus"
                    export_request = MediaExportRequest(
                        MediaType.SOUNDS,
                        targetdir,
                        source_filename,
                        target_filename,
                    )
                    full_data_set.sound_exports.update(
                        {f"{wid}_{wem_id}": export_request})

        info("DE2 sound exports: %d requests from %d mapped wwise_sound_ids",
            len(full_data_set.sound_exports), len({k[0] for k in seen}))
