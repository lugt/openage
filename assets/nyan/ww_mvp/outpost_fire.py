"""Convert the DE2 fire atlas into damage animations for the outpost."""

from pathlib import Path
import json

from PIL import Image


def install_outpost_fire(particles: Path, graphics: Path) -> None:
    """Add a looping flame layer above the original outpost idle sprite."""
    atlas_dir = particles / "textures" / "atlases"
    metadata = json.loads((atlas_dir / "fire.json").read_text(encoding="utf-8"))
    base = (graphics / "idle_outpost.sprite").read_text(encoding="utf-8")
    with Image.open(atlas_dir / "fire.dds") as source:
        atlas = source.convert("RGBA")

    for threshold, size in ((75, "small"), (50, "medium"), (25, "large")):
        config = json.loads((particles / f"fire_{size}.json").read_text(encoding="utf-8"))
        start = config["ImageFirst"]
        frames = metadata["frames"][start:start + config["ImageCount"]]
        stem = f"outpost_fire_{threshold}"
        # DE2's atlas uses twice the resolution of the converted x1 building.
        scale = config["Scale"] / 2
        images = []
        for item in frames:
            region = item["frame"]
            x, y, w, h = (region[key] for key in ("x", "y", "w", "h"))
            if item["rotated"]:
                crop = atlas.crop((x, y, x + h, y + w)).transpose(Image.Transpose.ROTATE_90)
            else:
                crop = atlas.crop((x, y, x + w, y + h))
            images.append(crop.resize(
                (max(1, round(crop.width * scale)), max(1, round(crop.height * scale))),
                Image.Resampling.LANCZOS,
            ))

        sheet = Image.new("RGBA", (sum(frame.width for frame in images),
                                  max(frame.height for frame in images)))
        entries = []
        x = 0
        for frame in images:
            # The outpost's deck is above and slightly right of the ground anchor.
            sheet.paste(frame, (x, 0))
            entries.append(f"subtex {x} 0 {frame.width} {frame.height} {frame.width // 2 - 13} {frame.height + 70}")
            x += frame.width
        # Keep normal pixels distinct from the reserved player-color alpha markers.
        alpha = sheet.getchannel("A")
        sheet.putalpha(alpha.point([0] + [value | 1 for value in range(1, 256)]))
        sheet.save(graphics / f"{stem}.png")
        texture = ["version 1", f'imagefile "{stem}.png"', f"size {sheet.width} {sheet.height}",
                   "pxformat rgba8 cbits=False", *entries]
        (graphics / f"{stem}.texture").write_text("\n".join(texture) + "\n", encoding="utf-8")

        frame_time = (config["Duration1"] + config["Duration2"]) / (2 * len(images))
        sprite = base.replace("scalefactor", f'texture 1 "{stem}.texture"\n\nscalefactor', 1)
        sprite += f"\nlayer 1 mode=loop position=21 time_per_frame={frame_time:.6f}\n"
        sprite += "".join(f"frame {index} 0 1 1 {index}\n" for index in range(len(images)))
        (graphics / f"idle_damage_override_{threshold}_outpost.sprite").write_text(sprite, encoding="utf-8")
