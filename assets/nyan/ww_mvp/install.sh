#!/usr/bin/env bash
# =============================================================================
# Install the hand-written ww_mvp modpack (William Wallace Mission 1 MVP)
# into the runtime modpack location (assets/converted/ww_mvp/).
#
# This modpack is NOT a converter product. It is hand-authored and ships the
# Mission 1 scenario + player setup. See assets/nyan/ww_mvp/ for the source.
#
# The runtime loader (enumerate_modpacks) only scans assets/converted/, so
# this script copies the tracked source into the gitignored product dir.
#
# After copying the nyan, this script also generates the deterministic
# Mission Pack (scenario.json + object_table.csv + trigger_graph.json +
# coverage.json) under the campaign/william_wallace/ subdir. The C++
# production loader (libopenage/scenario/mission_pack_loader.cpp) reads
# these files at scenario-init time; the legacy nyan-only path is NOT
# used for the Mission 1 reconstruction.
#
# Usage:
#   bash assets/nyan/ww_mvp/install.sh [--marching | --feeding | --original-map-save FILE DDS_DIR] [--grass-dds FILE] [--road-dds FILE] [--fire-particles DIR] [--shore-fish-sld FILE]
#
# Prerequisite: the base engine modpack must already exist under
#   assets/converted/engine/ (regenerate with
#   `python -m openage convert-export-api $REPO/assets/converted` and overlay the
#   engine fixtures with `bash assets/test/nyan/testscene/install.sh`).
#   ww_mvp depends on engine (declared in modpack.toml) and its Mission 1
#   scenario imports engine.test.testscene_data.TestsceneGrass.
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# ww_mvp/ lives at <repo>/assets/nyan/ww_mvp -> repo root is 3 dirs up.
REPO="$(cd "$HERE/../../.." && pwd)"
DEST="$REPO/assets/converted/ww_mvp"
GRASS_DDS=""
ROAD_DDS=""
FIRE_PARTICLES=""
SHORE_FISH_SLD=""
MARCHING=0
FEEDING=0
ORIGINAL_SAVE=""
ORIGINAL_TEXTURES=""
ORIGINAL_SCENARIO=""
ORIGINAL_INSTALL=""
while [ "$#" -gt 0 ]; do
    case "$1" in
        --grass-dds|--road-dds|--fire-particles|--shore-fish-sld)
            if [ "$#" -lt 2 ] || [ -z "$2" ]; then
                echo "Missing path after $1" >&2
                exit 2
            fi
            case "$1" in
                --grass-dds) GRASS_DDS="$2" ;;
                --road-dds) ROAD_DDS="$2" ;;
                --fire-particles) FIRE_PARTICLES="$2" ;;
                --shore-fish-sld) SHORE_FISH_SLD="$2" ;;
            esac
            shift 2
            ;;
        --original-scenario)
            if [ "$#" -lt 3 ]; then
                echo "Expected an exported original scenario JSON and original installation directory" >&2
                exit 2
            fi
            ORIGINAL_SCENARIO="$2"
            ORIGINAL_INSTALL="$3"
            shift 3
            ;;
        --marching) MARCHING=1; shift ;;
        --feeding) FEEDING=1; shift ;;
        --original-map-save)
            if [ "$#" -lt 3 ]; then
                echo "Expected a Mission 1 save and a directory of original terrain DDS files" >&2
                exit 2
            fi
            ORIGINAL_SAVE="$2"
            ORIGINAL_TEXTURES="$3"
            shift 3
            ;;
        *)
            echo "Usage: $0 [--marching | --feeding | --original-map-save FILE DDS_DIR] [--grass-dds FILE] [--road-dds FILE] [--fire-particles DIR] [--shore-fish-sld FILE]" >&2
            exit 2
            ;;
    esac
done
if (( MARCHING + FEEDING > 1 )) || { (( MARCHING + FEEDING > 0 )) && [ -n "$ORIGINAL_SAVE" ]; }; then
    echo "Choose one scenario profile" >&2
    exit 2
fi
# Cross-platform venv python: try venv/bin/python3 (macOS layout), then .venv/bin/python3 (Linux layout).
if [ -x "$REPO/venv/bin/python3" ]; then
    PY="$REPO/venv/bin/python3"
elif [ -x "$REPO/.venv/bin/python3" ]; then
    PY="$REPO/.venv/bin/python3"
else
    echo "ERROR: venv python not found at $REPO/venv/bin/python3 or $REPO/.venv/bin/python3" >&2
    exit 1
fi

if [ ! -d "$REPO/assets/converted/engine" ]; then
    echo "ERROR: engine modpack not found at $REPO/assets/converted/engine" >&2
    echo "Run 'python -m openage convert-export-api $REPO/assets/converted' first," >&2
    echo "then 'bash assets/test/nyan/testscene/install.sh' to overlay engine fixtures." >&2
    exit 1
fi

mkdir -p "$DEST/campaign/william_wallace"

# Generate the deterministic Mission Pack that the C++ production
# loader consumes (P2.7). The script invokes the same writer the
# pytest suite uses, so the on-disk pack matches the canonical IR
# round-trip.
"$PY" - "$REPO" "$HERE" "$DEST" "$GRASS_DDS" "$ROAD_DDS" "$MARCHING" "$ORIGINAL_SAVE" "$ORIGINAL_TEXTURES" "$FIRE_PARTICLES" "$FEEDING" "$SHORE_FISH_SLD" "$ORIGINAL_SCENARIO" "$ORIGINAL_INSTALL" <<'PY'
import sys
from dataclasses import replace
from pathlib import Path
from shutil import copyfile

repo, here, dest = map(Path, sys.argv[1:4])
sys.path.insert(0, str(repo))
if sys.argv[11] or sys.argv[12]:
    sys.path.insert(0, str(repo / "bin"))
from openage.scenario.ir import ScenarioDefinition
from openage.scenario.writer import write_mission_pack

mission_dir = Path("campaign/william_wallace")
nyan_name = mission_dir / "mission_01.nyan"
nyan = (here / nyan_name).read_text(encoding="utf-8")
ir = ScenarioDefinition.from_dump(repo / "docs/mvp/reports/mission1-scenario-dump")
terrain_root = repo / "assets/converted/de2_base/data/terrain"


def prepare_terrain(name, source_path):
    terrain_dir = terrain_root / name
    png = terrain_dir / f"graphics/texture_{name}.png"
    definition = terrain_dir / f"{name}.nyan"
    if source_path:
        from openage.convert.processor.export.dds_terrain import export_dds_terrain

        if not definition.is_file():
            raise SystemExit(f"Convert the de2_base data first: {definition} is missing")
        source = Path(source_path)
        data = source.read_bytes()
        png.parent.mkdir(parents=True, exist_ok=True)
        export_dds_terrain(data, png)
        print(f"Converted DE2 terrain from {source}")
    return definition.is_file() and all(
        png.with_suffix(ext).is_file() for ext in (".png", ".texture", ".terrain")
    )


has_grass = prepare_terrain("grass", sys.argv[4])
has_road = prepare_terrain("road", sys.argv[5])
if sys.argv[6] == "1":
    if not has_grass or not has_road:
        raise SystemExit("Marching and Fighting requires grass and road media. Use --grass-dds and --road-dds.")
    sys.path.insert(0, str(here))
    from marching import marching_and_fighting

    ir = marching_and_fighting(ir)
    print("Installing Marching and Fighting")
if sys.argv[10] == "1":
    if not has_grass:
        raise SystemExit("Feeding the Army requires converted grass media. Use --grass-dds.")
    sys.path.insert(0, str(here))
    from feeding import feeding_the_army

    ir = feeding_the_army(ir)
    print("Installing Feeding the Army")
if sys.argv[6] == "1" or sys.argv[10] == "1" or sys.argv[12]:
    # Keep tutorial starting resources and use the converted Celts setup for
    # civilization bonuses, available unit lines and disabled technologies.
    nyan = nyan.replace("!version 0.2.0", "!version 0.2.0\n\nimport de2_base.data.civ.celts.celts as celts", 1)
    nyan = nyan.replace("PlayerSetup(engine.util.setup.PlayerSetup):", "PlayerSetup(celts.Celts):")
    nyan = nyan.replace("    PlayerSetup.game_setup = o{}\n", "")
if (sys.argv[10] == "1" or sys.argv[12]) and sys.argv[11]:
    sys.path.insert(0, str(here))
    from shore_fishing import install_shore_fishing

    fishing_ir = install_shore_fishing(ir, Path(sys.argv[11]), dest / mission_dir)
    if sys.argv[10] == "1":
        ir = fishing_ir
    copyfile(here / mission_dir / "shore_fishing.nyan", dest / mission_dir / "shore_fishing.nyan")
    nyan = nyan.replace("!version 0.2.0", "!version 0.2.0\n\n"
                        "import ww_mvp.campaign.william_wallace.shore_fishing as fishing", 1)
    nyan = nyan.replace("    PlayerSetup.starting_resources", "    PlayerSetup.game_setup += o{fishing.ShoreFishing}\n"
                        "    PlayerSetup.starting_resources")
else:
    (dest / mission_dir / "shore_fishing.nyan").unlink(missing_ok=True)
if sys.argv[7]:
    sys.path.insert(0, str(here))
    from original_map import original_map

    ir = original_map(ir, Path(sys.argv[7]), Path(sys.argv[8]), dest / mission_dir)
    print("Installing original Mission 1 terrain preview")
elif not sys.argv[12]:
    (dest / mission_dir / "original_terrain.nyan").unlink(missing_ok=True)
if sys.argv[12]:
    sys.path.insert(0, str(here))
    from original_scene import original_scene
    ir = original_scene(ir, Path(sys.argv[12]), Path(sys.argv[13]), dest / mission_dir, repo / "logs/tech-trees")
    print(f"Installing original scenario {ir.source_file}: {len(ir.placements)} objects, {len(ir.triggers)} triggers")
else:
    (dest / mission_dir / "original_entities.nyan").unlink(missing_ok=True)
if has_grass:
    test_grass = "engine.test.testscene_data.TestsceneGrass"
    de2_grass = "de2_base.data.terrain.grass.grass.Grass"
    ir = replace(ir, map=replace(ir.map, terrain_ids=[
        de2_grass if tile == test_grass else tile for tile in ir.map.terrain_ids
    ]))
    nyan = nyan.replace("import engine.test.testscene_data", "import de2_base.data.terrain.grass.grass")
    nyan = nyan.replace(test_grass, de2_grass)
    print(f"Mission 1 terrain: {de2_grass}")

if sys.argv[9]:
    sys.path.insert(0, str(here))
    from outpost_fire import install_outpost_fire

    install_outpost_fire(Path(sys.argv[9]), terrain_root.parent / "game_entity/generic/outpost/graphics")

copyfile(here / "modpack.toml", dest / "modpack.toml")
(dest / nyan_name).write_text(nyan, encoding="utf-8")
write_mission_pack(ir, dest / mission_dir)
PY

echo "Installed ww_mvp modpack (Mission 1 scenario + player setup) into $DEST"
echo "Mission Pack (P2.7) emitted under $DEST/campaign/william_wallace/"
echo "Validate with: bash $REPO/assets/test/nyan/testscene/validate.sh"
