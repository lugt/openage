# Copyright 2026 the openage authors. See copying.md for legal info.

"""Extract terrain IDs and elevations from a local DE2 Mission 1 save."""

from argparse import ArgumentParser
import json
from pathlib import Path
from struct import error as StructError, pack, unpack_from
import zlib


SCENARIO_NAME = b"1_Wallace_1.aoe2scenario"
MAP_SIZE = 120
TILE_SIZE = 10


def read_wallace_map(compressed: bytes) -> dict:
    """Read the original tile grid without replacing unknown terrain IDs."""
    data = zlib.decompress(compressed, -15)
    if (not data.startswith(b"VER 9.4\0")
            or int.from_bytes(data[12:16], "little") != 68 << 16
            or SCENARIO_NAME not in data):
        raise ValueError("Expected a VERSION 68 William Wallace Mission 1 save")

    # The save map follows variable-sized game and player metadata. Match its
    # dimensions, walk the zone records, and confirm the repeated dimensions
    # after the tiles and obstruction tables.
    dimensions = pack("<II", MAP_SIZE, MAP_SIZE)
    tile_count = MAP_SIZE * MAP_SIZE
    matches = []
    offset = data.find(dimensions)
    while offset != -1:
        try:
            zone_count, = unpack_from("<I", data, offset + 8)
            if 0 < zone_count <= 32:
                cursor = offset + 12
                for _ in range(zone_count):
                    cursor += 2048 + tile_count * 2
                    floats, = unpack_from("<I", data, cursor)
                    cursor += 8 + floats * 4

                tile_start = cursor + 2
                tile_end = tile_start + tile_count * TILE_SIZE
                table_count, = unpack_from("<I", data, tile_end)
                cursor = tile_end + 8 + table_count * 4
                if table_count <= 32:
                    for _ in range(table_count):
                        entries, = unpack_from("<I", data, cursor)
                        cursor += 4 + entries * 8
                    if data[cursor:cursor + 8] == dimensions:
                        terrain = data[tile_start:tile_end:TILE_SIZE]
                        elevations = data[tile_start + 3:tile_end:TILE_SIZE]
                        if len(set(terrain)) > 1 and max(elevations) < 32:
                            matches.append((terrain, elevations))
        except StructError:
            pass
        offset = data.find(dimensions, offset + len(dimensions))

    if len(matches) != 1:
        raise ValueError(f"Expected one Mission 1 map, found {len(matches)}")
    terrain, elevations = matches[0]
    return {
        "scenario": SCENARIO_NAME.decode("ascii"),
        "width": MAP_SIZE,
        "height": MAP_SIZE,
        "terrain_ids": list(terrain),
        "elevation": list(elevations),
    }


def main() -> None:
    """Write the original tile grid as JSON."""
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    try:
        result = read_wallace_map(args.source.read_bytes())
    except (ValueError, zlib.error) as exc:
        parser.error(str(exc))
    args.output.write_text(json.dumps(result) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
