# Copyright 2020-2023 the openage authors. See copying.md for legal info.
#
# pylint: disable=too-many-arguments,too-many-locals
"""
Export requests for media metadata.
"""

from __future__ import annotations

import typing

from ....util.observer import Observer
from .data_definition import DataDefinition
from .formats.sprite_metadata import SpriteMetadata
from .formats.terrain_metadata import TerrainMetadata
from .formats.texture_metadata import TextureMetadata

if typing.TYPE_CHECKING:
    from openage.convert.entity_object.export.formats.sprite_metadata import LayerMode as SpriteLayerMode
    from openage.convert.entity_object.export.formats.terrain_metadata import LayerMode as TerrainLayerMode
    from openage.util.observer import Observable


class MetadataExport(DataDefinition, Observer):
    """
    A class for exporting metadata from another format. MetadataExports are
    observers so they can receive data from media conversion.

    Tracks the set of MediaExportRequest identifiers this metadata file
    depends on. The filter layer in ``ModpackExporter.export`` keeps a
    metadata entry iff at least one of its dependent media requests is
    still alive in the filtered set -- shared metadata survives the
    filter; orphan metadata is dropped. ``request_id`` is ``id(request)``,
    which sidesteps the pickle / circular-import problem of stashing the
    request itself.
    """

    def __init__(self, targetdir: str, target_filename: str):

        DataDefinition.__init__(self, targetdir, target_filename)
        self._request_refs: set[int] = set()

    def add_request_ref(self, request_id: int) -> None:
        """
        Add a back-reference from this metadata file to a media request
        that depends on it. Called by the media subprocessor right
        after ``MediaExportRequest.add_observer(...)`` so the ref count
        mirrors the observer wiring.
        """
        self._request_refs.add(int(request_id))

    def has_live_request(self, kept_request_ids: set[int]) -> bool:
        """
        Return True iff at least one of this metadata file's dependent
        media requests is still in ``kept_request_ids``. Used by
        ModpackExporter's filter step to drop only metadata that has
        become an orphan after the request filter ran.
        """
        return bool(self._request_refs & kept_request_ids)

    @property
    def request_refs(self) -> set[int]:
        return set(self._request_refs)

    def update(self, observable: Observable | None, message=None):
        return NotImplementedError("Interface does not implement update()")

    def __repr__(self):
        return f"MetadataExport<{type(self)}>"


class SpriteMetadataExport(MetadataExport):
    """
    Export requests for sprite definition files.
    """

    def __init__(self, targetdir, target_filename):
        super().__init__(targetdir, target_filename)

        self.graphics_metadata: dict[str, tuple] = {}
        # Pre-populated by add_graphics_metadata() from genie frame_count *
        # angle_count; optionally overridden by update() once the texture
        # atlas has actually been built. The invariant is:
        #
        #   every key in graphics_metadata must have an entry here by the
        #   time dump() runs.
        self.subtex_count: dict[str, int] = {}

    def add_graphics_metadata(
        self,
        img_filename: str,
        tex_filename: str,
        layer_mode: SpriteLayerMode,
        layer_pos: int,
        frame_rate: float | None,
        replay_delay: float | None,
        frame_count: int,
        angle_count: int,
        mirror_mode: int,
        start_angle: int = 0,
    ):
        """
        Add metadata from the GenieGraphic object.

        :param img_filename: Filename of the exported PNG file.
        :param tex_filename: Filename of the .texture file.
        :param layer_mode: Animation mode (off, once, loop).
        :param layer_pos: Layer position.
        :param frame_rate: Time spent on each frame.
        :param replay_delay: Time delay before replaying the animation.
        :param frame_count: Number of frames per angle in the animation.
        :param angle_count: Number of angles in the animation.
        :param mirror_mode: Mirroring mode (0, 1). If 1, angles above 180 degrees are mirrored.
        :param start_angle: Angle used for the first frame in the .texture file.
        """
        self.graphics_metadata[img_filename] = (
            tex_filename,
            layer_mode,
            layer_pos,
            frame_rate,
            replay_delay,
            frame_count,
            angle_count,
            mirror_mode,
            start_angle,
        )

        # Seed the subtexture count with the genie-derived estimate so
        # the dump() invariant holds even if the texture exporter never
        # ran for this graphic (filtered out, killed by a sibling OOM,
        # source SLD missing, etc.). The exporter's update() may still
        # overwrite this with the precise atlas count if the texture
        # actually got built.
        if angle_count == 0:
            angle_count = 1
        seeded_count = frame_count * angle_count
        self.subtex_count.setdefault(img_filename, seeded_count)

    def dump(self) -> str:
        """
        Creates a human-readable string that can be written to a file.
        """
        # Fail-fast invariant check: every img_filename registered via
        # add_graphics_metadata() must have a subtex_count entry by the
        # time dump() runs. Without pre-population this used to throw
        # KeyError on partial-export runs (filtered / OOM-killed) and on
        # historical death/projectile animations. The check below turns
        # the silent weakness into a structured error pointing at the
        # request that broke the invariant.
        missing = [k for k in self.graphics_metadata if k not in self.subtex_count]
        if missing:
            provenance = []
            for img in missing[:10]:
                meta = self.graphics_metadata[img]
                provenance.append(
                    f"  img_filename={img!r} tex_filename={meta[0]!r} "
                    f"frame_count={meta[5]} angle_count={meta[6]}"
                )
            more = "" if len(missing) <= 10 else f" (and {len(missing) - 10} more)"
            from ....error import Error as _SpriteDumpError
            raise _SpriteDumpError(
                f"SpriteMetadataExport invariant violated for sprite file "
                f"{self.filename!r}: {len(missing)} entry/entries missing in "
                f"subtex_count. First entries:\n" + "\n".join(provenance) + more
            )

        sprite_file = SpriteMetadata(self.targetdir, self.filename)

        tex_index = 0

        for img_filename, metadata in self.graphics_metadata.items():
            tex_filename = metadata[0]
            sprite_file.add_texture(tex_index, tex_filename)
            sprite_file.add_layer(tex_index, *metadata[1:5])

            frame_count = metadata[5]
            angle_count = metadata[6]
            mirror_mode = metadata[7]
            start_angle = metadata[8]

            if angle_count == 0:
                angle_count = 1

            degree = 0
            if start_angle and angle_count > 1:
                # set the angle of the first frame in the sprite list
                # in openage, angles are defined clockwise, with 0 being the
                # front-facing angle (i.e. the unit sprite faces the camera)
                degree = start_angle % 360

            degree_step = 360 / angle_count
            for angle_index in range(angle_count):
                mirror_from = None
                if mirror_mode:
                    if degree > 180:
                        mirrored_angle = (angle_index - angle_count) * (-1)
                        mirror_from = (start_angle + int(mirrored_angle * degree_step)) % 360

                sprite_file.add_angle(int(degree), mirror_from)

                if not mirror_from:
                    # Subtex_count is invariant-populated by
                    # add_graphics_metadata() (genie frame_count *
                    # angle_count) and optionally refined by the
                    # exporter's update() if the texture atlas was
                    # actually built. Either way, the key is present,
                    # so the KeyError the legacy TODO comment alluded to
                    # can no longer hide silently.
                    subtex_total = self.subtex_count[img_filename]
                    for frame_idx in range(frame_count):
                        subtex_index = frame_idx + angle_index * frame_count
                        if subtex_index >= subtex_total:
                            break

                        sprite_file.add_frame(frame_idx, int(degree), tex_index, tex_index, subtex_index)

                degree = (degree + degree_step) % 360

            tex_index += 1

        return sprite_file.dump()

    def update(self, observable, message=None):
        """
        Receive metdata from the graphics file export.

        :param message: A dict with frame metadata from the exported PNG file.
        :type message: dict
        """
        # Once the texture atlas is built the exporter reports the
        # exact subtex_metadata length; this refines (or, if the SLP
        # contained 0 frames, decrements) the genie-derived seed so the
        # sprite metadata reflects what was actually written.
        if message:
            for tex_filename, metadata in message.items():
                self.subtex_count[tex_filename] = len(metadata["subtex_metadata"])


class TextureMetadataExport(MetadataExport):
    """
    Export requests for texture definition files.
    """

    def __init__(self, targetdir, target_filename):
        super().__init__(targetdir, target_filename)

        self.imagefile: str | None = None
        self.size: tuple[int, int] | None = None
        self.pxformat = "rgba8"
        self.cbits = True
        self.subtex_metadata: list[dict[str, int]] = []

    def add_imagefile(self, img_filename):
        """
        Add metadata from the GenieGraphic object.

        :param img_filename: Filename of the exported PNG file.
        """
        self.imagefile = img_filename

    def dump(self) -> str:
        """
        Creates a human-readable string that can be written to a file.
        """
        texture_file = TextureMetadata(self.targetdir, self.filename)

        assert self.imagefile is not None and self.size is not None
        texture_file.set_imagefile(self.imagefile)
        texture_file.set_size(self.size[0], self.size[1])
        texture_file.set_pxformat(self.pxformat, self.cbits)

        for subtex_metadata in self.subtex_metadata:
            texture_file.add_subtex(*subtex_metadata.values())

        return texture_file.dump()

    def update(self, observable: Observable | None, message: dict | None = None):
        """
        Receive metdata from the graphics file export.

        :param message: A dict with texture metadata from the exported PNG file.
        :type message: dict
        """
        if message:
            texture_metadata = message[self.imagefile]
            self.size = texture_metadata["size"]
            self.subtex_metadata = texture_metadata["subtex_metadata"]


class TerrainMetadataExport(MetadataExport):
    """
    Export requests for texture definition files.
    """

    def __init__(self, targetdir, target_filename):
        super().__init__(targetdir, target_filename)

        self.graphics_metadata: dict[str, tuple] = {}
        self.subtex_count: dict[str, int] = {}

    def add_graphics_metadata(
        self,
        img_filename: str,
        tex_filename: str,
        layer_mode: TerrainLayerMode,
        layer_pos: int,
        frame_rate: float | None,
        replay_delay: float | None,
        frame_count: int,
    ):
        """
        Add metadata from the GenieGraphic object.

        :param img_filename: Filename of the exported PNG file.
        :param tex_filename: Filename of the .texture file.
        :param layer_mode: Animation mode (off, loop).
        :param layer_pos: Layer position.
        :param frame_rate: Time spent on each frame.
        :param replay_delay: Time delay before replaying the animation.
        :param frame_count: Number of frames in the animation.
        """
        self.graphics_metadata[img_filename] = (
            tex_filename,
            layer_mode,
            layer_pos,
            frame_rate,
            replay_delay,
            frame_count,
        )

    def dump(self) -> str:
        """
        Creates a human-readable string that can be written to a file.
        """
        terrain_file = TerrainMetadata(self.targetdir, self.filename)

        tex_index = 0
        for _, metadata in self.graphics_metadata.items():
            tex_filename = metadata[0]
            terrain_file.add_texture(tex_index, tex_filename)
            terrain_file.add_layer(tex_index, *metadata[1:5])

            frame_count = metadata[5]

            for frame_idx in range(frame_count):
                subtex_index = frame_idx
                terrain_file.add_frame(frame_idx, tex_index, tex_index, subtex_index)

            tex_index += 1

        return terrain_file.dump()
