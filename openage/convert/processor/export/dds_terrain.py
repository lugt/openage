# Copyright 2026 the openage authors. See copying.md for legal info.

"""Export an individual DDS terrain texture as openage media."""

from __future__ import annotations

from inspect import getfile
from io import BytesIO
from pathlib import Path
from typing import TYPE_CHECKING

from PIL import Image

from ....util.export_cache import ExportCache
from ...entity_object.export.formats.terrain_metadata import TerrainMetadata
from ...entity_object.export.formats.texture_metadata import TextureMetadata

if TYPE_CHECKING:
    from openage.util.fslike.path import Path as ConverterPath


def export_dds_terrain(source: bytes, target_path: Path | ConverterPath, compression_level: int = 6) -> None:
    """Write a PNG and its .texture/.terrain descriptors next to it."""
    # Decode before opening any output so invalid DDS data cannot replace it.
    with Image.open(BytesIO(source), formats=["DDS"]) as image:
        rgba = image.convert("RGBA")
    width, height = rgba.size
    filename = target_path.name
    stem = Path(filename).stem

    texture = TextureMetadata("", f"{stem}.texture")
    texture.set_imagefile(filename)
    texture.set_size(width, height)
    texture.set_pxformat(cbits=False)
    texture.add_subtex(0, 0, width, height, 0, 0)

    terrain = TerrainMetadata("", f"{stem}.terrain")
    terrain.add_texture(0, f"{stem}.texture")
    terrain.add_layer(0)
    terrain.add_frame(0, 0, 0, 0)

    with target_path.open("wb") as output:
        rgba.save(output, format="PNG", compress_level=compression_level)
    for metadata in (texture, terrain):
        with (target_path.parent / metadata.filename).open("wb") as output:
            output.write(metadata.dump().encode("utf-8"))


def export_dds_terrain_cached(source: bytes, target_path: Path, compression_level: int = 6) -> bool:
    """Reuse intact terrain media until the source, exporter or compression changes."""
    version = str(compression_level).encode("ascii")
    for implementation in (__file__, getfile(TextureMetadata), getfile(TerrainMetadata)):
        version += Path(implementation).read_bytes()
    outputs = [target_path.with_suffix(extension) for extension in (".png", ".texture", ".terrain")]
    cache = ExportCache(source, outputs, version)
    if cache.valid():
        return False
    export_dds_terrain(source, target_path, compression_level)
    cache.commit()
    return True


def main() -> None:
    """Convert an individual terrain without loading the game database."""
    from argparse import ArgumentParser

    parser = ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Original terrain DDS")
    parser.add_argument("target", type=Path, help="Output .png path")
    args = parser.parse_args()
    if args.target.suffix.lower() != ".png":
        parser.error("target must end in .png")
    args.target.parent.mkdir(parents=True, exist_ok=True)
    export_dds_terrain(args.source.read_bytes(), args.target)


if __name__ == "__main__":
    main()
