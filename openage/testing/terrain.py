# Copyright 2026+ the openage authors. See copying.md for legal info.

"""Asset-free contracts for reusable terrain conversion."""

from io import BytesIO
from pathlib import Path
from tempfile import TemporaryDirectory
from unittest.mock import patch

from PIL import Image

from ..convert.processor.export.dds_terrain import export_dds_terrain_cached
from .testing import assert_value


def cached_conversion() -> None:
    """Reuse complete output and regenerate changed sources or damaged media."""

    def dds(color: str) -> bytes:
        buffer = BytesIO()
        Image.new("RGBA", (4, 4), color).save(buffer, format="DDS")
        return buffer.getvalue()

    with TemporaryDirectory(prefix="openage-terrain-") as temporary:
        target = Path(temporary) / "grass.png"
        source = dds("red")
        assert_value(export_dds_terrain_cached(source, target), True)
        with patch(
            "openage.convert.processor.export.dds_terrain.export_dds_terrain",
            side_effect=AssertionError("An unchanged texture must not be converted again"),
        ):
            assert_value(export_dds_terrain_cached(source, target), False)
        changed = dds("blue")
        assert_value(export_dds_terrain_cached(changed, target), True)
        with Image.open(target) as image:
            assert_value(image.getpixel((0, 0)), (0, 0, 255, 255))
        target.with_suffix(".texture").unlink()
        assert_value(export_dds_terrain_cached(changed, target), True)
        target.write_bytes(b"damaged PNG")
        assert_value(export_dds_terrain_cached(changed, target), True)
        assert_value(export_dds_terrain_cached(changed, target), False)


def catalog_conversion() -> None:
    """Terrain IDs beyond Wallace resolve through the exported DAT catalog."""
    import importlib.util

    from ..config import BUILD_SRC_DIR
    from .testing import assert_raises

    source = Path(BUILD_SRC_DIR) / "assets/nyan/ww_mvp/original_map.py"
    specification = importlib.util.spec_from_file_location("original_map_contract", source)
    assert specification is not None and specification.loader is not None
    module = importlib.util.module_from_spec(specification)
    specification.loader.exec_module(module)
    with TemporaryDirectory(prefix="openage-terrain-catalog-") as temporary:
        root = Path(temporary)
        textures = root / "textures"
        destination = root / "output"
        textures.mkdir()
        destination.mkdir()
        Image.new("RGBA", (4, 4), "green").save(textures / "synthetic.dds", format="DDS")
        definitions = [{"name_2": "synthetic", "is_water": 4} for _ in range(17)]
        data = {"width": 1, "height": 1, "terrain_ids": [16], "elevation": [0]}
        with assert_raises(ValueError):
            module.convert_terrain(data, textures, destination)
        result = module.convert_terrain(data, textures, destination, definitions)
        assert_value(len(result.terrain_ids), 1)
        assert_value("Tile16(water.Water)" in (destination / "original_terrain.nyan").read_text(), True)
