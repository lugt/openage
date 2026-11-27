"""Build a terrain-only Mission Pack from the original DE2 Mission 1 save."""

from dataclasses import replace

from openage.convert.processor.export.dds_terrain import export_dds_terrain_cached
from openage.scenario.de2_save_map import read_wallace_map
from openage.scenario.ir import MapDefinition


# Terrain filenames from the local DE2 empires2_x2_p1.dat.
TERRAIN_DDS = {
    0: "g_grs", 1: "g_wtr", 2: "g_bch", 3: "g_ds3", 4: "g_sha",
    5: "g_for", 6: "g_des", 7: "g_fm1", 9: "g_gr3", 10: "g_for",
    11: "g_ds2", 12: "g_gr2", 14: "g_pal", 19: "g_for", 20: "g_for",
    22: "g_wt2", 23: "g_wt3", 24: "g_rd1", 25: "g_rd2", 27: "g_ds2",
    79: "g_bch", 107: "g_beach_wet", 108: "g_gravel_wet",
}
WATER = {1, 22, 23}


def original_map(template, source, textures, destination):
    """Keep each terrain ID distinct, including those sharing a DDS."""
    data = read_wallace_map(source.read_bytes())
    return replace(template, id="ww_mission1_original_terrain",
                   map=convert_terrain(data, textures, destination), placements=[],
                   victory=replace(template.victory, required_removals=()))


def convert_terrain(data, textures, destination, terrain_definitions=None, graphics_dir=None):
    """Reuse the original terrain export for scenario grids and saved grids."""
    tiles = data["terrain_ids"]
    used = sorted(set(tiles))
    filenames = dict(TERRAIN_DDS)
    water = set(WATER)
    for tile in sorted(set(used) - filenames.keys()):
        if terrain_definitions is None or tile < 0 or tile >= len(terrain_definitions):
            raise ValueError(f"Terrain {tile} requires the exported original DAT terrain catalog")
        record = terrain_definitions[tile]
        filename = record['name_2']
        if not filename or '/' in filename or '\\' in filename:
            raise ValueError(f"Unsupported DAT terrain definition {tile}")
        filenames[tile] = filename
        if record['is_water'] in (1, 2, 4):
            water.add(tile)

    originals = {name: (textures / f"{name}.dds").read_bytes()
                 for name in sorted({filenames[tile] for tile in used})}
    graphics = graphics_dir or (destination / "original_terrain_graphics")
    graphics.mkdir(parents=True, exist_ok=True)
    for name, image in originals.items():
        export_dds_terrain_cached(image, graphics / f"{name}.png")

    lines = [
        "!version 0.2.0",
        "import de2_base.data.terrain.grass.grass as grass",
        "import de2_base.data.terrain.water.water as water",
        "import engine.util.graphics as graphics",
    ]
    for tile in used:
        parent = "water.Water" if tile in water else "grass.Grass"
        lines.extend((
            "",
            f"Tile{tile}({parent}):",
            "    Terrain.terrain_graphic = TileTexture",
            "",
            "    TileTexture(graphics.Terrain):",
            f'        Terrain.sprite = "./original_terrain_graphics/{filenames[tile]}.terrain"',
        ))
    (destination / "original_terrain.nyan").write_text("\n".join(lines) + "\n", encoding="utf-8")

    prefix = "ww_mvp.campaign.william_wallace.original_terrain"
    return MapDefinition(data["width"], data["height"],
                         [f"{prefix}.Tile{tile}" for tile in tiles], data["elevation"])
