"""Import the DE2 shore-fishing animation missing from the current base pack."""

from dataclasses import replace

from openage.convert.entity_object.export.formats.sprite_metadata import LayerMode, SpriteMetadata
from openage.convert.entity_object.export.formats.texture_metadata import TextureMetadata
from openage.convert.entity_object.export.texture import Texture
from openage.convert.processor.export.texture_merge import merge_frames
from openage.convert.value_object.read.media.sld import SLD
from openage.scenario.ir import EntityPlacement


# DE2 Genie fisherman task (unit 56, graphic 3154).
FRAMES = 60
DIRECTIONS = 16
FRAME_TIME = 0.09333333373069763


def install_shore_fishing(scenario, source, destination):
    """Add a pond and use the local DE2 sprite for the shore-fishing action."""
    texture = Texture(SLD(source.read_bytes()))
    frame_count = FRAMES * DIRECTIONS
    if len(texture.frames) not in (frame_count, frame_count + 1):
        raise ValueError(f"Unexpected DE2 fisherman frame count: {len(texture.frames)}")
    # The converter's SLD reader can expose a trailing sentinel frame.
    del texture.frames[frame_count:]
    merge_frames(texture)
    metadata = texture.get_metadata()
    graphics = destination / "shore_fishing_graphics"
    graphics.mkdir(exist_ok=True)
    texture.image_data.get_pil_image().save(graphics / "fish.png")
    descriptor = TextureMetadata(str(graphics), "fish.texture")
    descriptor.set_imagefile("fish.png")
    descriptor.set_size(*metadata["size"])
    descriptor.set_pxformat()
    for frame in metadata["subtex_metadata"]:
        descriptor.add_subtex(frame["x"], frame["y"], frame["w"], frame["h"], frame["cx"], frame["cy"])
    (graphics / "fish.texture").write_text(descriptor.dump(), encoding="utf-8")

    sprite = SpriteMetadata(str(graphics), "fish.sprite")
    sprite.add_texture(0, "fish.texture")
    sprite.add_layer(0, LayerMode.LOOP, position=20, time_per_frame=FRAME_TIME)
    for direction in range(DIRECTIONS):
        angle = int((270 + direction * 360 / DIRECTIONS) % 360)
        sprite.add_angle(angle)
        for index in range(FRAMES):
            sprite.add_frame(index, angle, 0, 0, direction * FRAMES + index)
    (graphics / "fish.sprite").write_text(sprite.dump(), encoding="utf-8")

    terrain = scenario.map.terrain_ids.copy()
    for x, y in ((1, 3), (2, 3), (1, 4), (2, 4)):
        terrain[y * scenario.map.width + x] = "de2_base.data.terrain.water.water.Water"
    fish = EntityPlacement("ShoreFish", "de2_base.data.game_entity.generic.shore_fish.shore_fish.Shorefish", 1, (2, 3))
    return replace(scenario, map=replace(scenario.map, terrain_ids=terrain), placements=[*scenario.placements, fish])
