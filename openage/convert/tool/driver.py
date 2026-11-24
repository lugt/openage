# Copyright 2015-2024 the openage authors. See copying.md for legal info.
#
# pylint: disable=too-many-return-statements

"""
Receives cleaned-up srcdir and targetdir objects from .main, and drives the
actual conversion process.
"""

from __future__ import annotations

import timeit
import typing

from ...log import dbg, info
from ..processor.export.modpack_exporter import ModpackExporter
from ..service.debug_info import (
    debug_execution_time,
    debug_gamedata_format,
    debug_modpack,
    debug_registered_graphics,
    debug_string_resources,
)
from ..service.read.gamedata import get_gamespec
from ..service.read.palette import get_palettes
from ..service.read.register_media import get_existing_graphics
from ..service.read.string_resource import get_string_resources

import os
import gc


def _rss_mib():
    """Read current process RSS in MiB from /proc/self/status. Cheap."""
    try:
        with open("/proc/self/status") as f:
            for line in f:
                if line.startswith("VmRSS:"):
                    return int(line.split()[1]) / 1024.0
    except OSError:
        pass
    return -1.0


def _mem_log(label: str) -> None:
    """Force GC and log current RSS so we can pinpoint peak memory growth."""
    gc.collect()
    rss = _rss_mib()
    info("MEMPROBE %-40s rss=%.1f MiB", label, rss)

if typing.TYPE_CHECKING:
    from argparse import Namespace

    from openage.convert.value_object.init.game_version import GameVersion


def convert(args: Namespace) -> None:
    """
    args must hold srcdir and targetdir (FS-like objects),
    plus any additional configuration options.
    """
    convert_metadata(args)
    # with args.targetdir[GAMESPEC_VERSION_FILENAME].open('w') as fil:
    #     fil.write(EmpiresDat.get_hash(args.game_version))

    # clean args (set by convert_metadata for convert_media)
    del args.palettes


def convert_metadata(args: Namespace) -> None:
    """
    Converts the metadata part.
    """
    if not args.flag("no_metadata"):
        info("converting metadata")
        # data_formatter = DataFormatter()

    # Activate periodic memory sampler + SIGUSR1 tracemalloc dump when
    # OPENAGE_MEMPROBE=1 is set in the environment. Cheap when off.
    if os.environ.get("OPENAGE_MEMPROBE") == "1":
        from . import memprobe
        memprobe.enable()

    # --memory-budget-test installs a BudgetGate on args; mark stage
    # boundaries so the report shows the RSS trajectory. No-op without it.
    budget = getattr(args, "budget_gate", None)
    if budget is not None:
        budget.mark("convert-metadata-start")

    args.converter = get_converter(args.game_version)

    # required for player palette and color lookup during SLP conversion.
    palettes = get_palettes(args.srcdir, args.game_version)

    # store for use by convert_media
    args.palettes = palettes

    if args.flag("no_metadata"):
        return

    gamedata_path = args.targetdir.joinpath("gamedata")
    if gamedata_path.exists():
        gamedata_path.removerecursive()

    # Record time taken for each stage
    stages_time = {}

    # Read .dat
    stage_start = timeit.default_timer()
    debug_gamedata_format(args.debugdir, args.debug_info, args.game_version)
    gamespec = get_gamespec(args.srcdir, args.game_version, not args.flag("no_pickle_cache"))

    # Ground-truth heap composition right after the .dat load (the peak we want
    # to characterize). Writes /tmp/openage_memprobe_heap.txt and exits, so we
    # can iterate on shrinking the gamespec without running the full pipeline.
    if os.environ.get("OPENAGE_MEMPROBE_HEAP") == "1":
        from . import memprobe
        memprobe.dump_heap_breakdown("post-gamespec-load", exit_after=True)

    # Blending mode count (SWGB-only; the lazy backend does not expose
    # top-level members, and SWGB is not a lazy target, so fall back to None).
    from ..service.read.gamedata import get_converter_backend
    if args.game_version.edition.game_id == "SWGB" and get_converter_backend() != "lazy":
        args.blend_mode_count = gamespec[0]["blend_mode_count_swgb"].value

    else:
        args.blend_mode_count = None

    # Read strings
    string_resources = get_string_resources(args)
    debug_string_resources(args.debugdir, args.debug_info, string_resources)

    # Existing graphic IDs/filenames
    existing_graphics = get_existing_graphics(args)
    debug_registered_graphics(args.debugdir, args.debug_info, existing_graphics)

    stage_end = timeit.default_timer()
    info("Finished metadata read (%.2f seconds)", stage_end - stage_start)
    stages_time.update({"read": stage_end - stage_start})
    _mem_log("after metadata read")
    if budget is not None:
        budget.mark("after metadata read (gamespec)")

    # nyan conversion
    stage_start = timeit.default_timer()
    modpacks = args.converter.convert(gamespec, args, string_resources, existing_graphics)
    del gamespec
    gc.collect()
    _mem_log("after nyan conversion (gamespec released)")
    if budget is not None:
        budget.mark("after nyan conversion")

    stage_end = timeit.default_timer()
    info("Finished data conversion (%.2f seconds)", stage_end - stage_start)
    stages_time.update({"convert": stage_end - stage_start})

    # Export modpacks
    stage_start = timeit.default_timer()
    for modpack in modpacks:
        mod_export_start = timeit.default_timer()
        ModpackExporter.export(modpack, args)
        debug_modpack(args.debugdir, args.debug_info, modpack)

        mod_export_end = timeit.default_timer()
        info(
            "Finished export of modpack '%s' v%s (%.2f seconds)",
            modpack.info.packagename,
            modpack.info.version,
            mod_export_end - mod_export_start,
        )
        _mem_log(f"after export of {modpack.info.packagename}")
        if budget is not None:
            budget.mark(f"after export of {modpack.info.packagename}")

    stage_end = timeit.default_timer()
    info("Finished export (%.2f seconds)", stage_end - stage_start)
    stages_time.update({"export": stage_end - stage_start})
    _mem_log("after all exports")
    if budget is not None:
        budget.mark("after all exports")

    debug_execution_time(args.debugdir, args.debug_info, stages_time)

    # TODO: player palettes
    # player_palette = PlayerColorTable(palette)
    # data_formatter.add_data(player_palette.dump("player_palette"))

    # TODO: terminal color files
    # termcolortable = ColorTable(URXVTCOLS)
    # data_formatter.add_data(termcolortable.dump("termcolors"))

    # TODO: gamespec files
    # data_formatter.export(args.targetdir, ("csv",))

    if args.flag("gen_extra_files"):
        dbg("generating extra files for visualization")
        # tgt = args.targetdir
        # with tgt['info/colortable.pal.png'].open_w() as outfile:
        #     palette.save_visualization(outfile)

        # with tgt['info/playercolortable.pal.png'].open_w() as outfile:
        #     player_palette.save_visualization(outfile)


def get_converter(game_version: GameVersion):
    """
    Returns the converter for the specified game version.
    """
    game_edition = game_version.edition
    game_expansions = game_version.expansions

    if game_edition.game_id == "ROR":
        from ..processor.conversion.ror.processor import RoRProcessor

        return RoRProcessor

    if game_edition.game_id == "AOE1DE":
        from ..processor.conversion.de1.processor import DE1Processor

        return DE1Processor

    if game_edition.game_id == "AOC":
        from ..processor.conversion.aoc.processor import AoCProcessor

        return AoCProcessor

    if game_edition.game_id == "AOCDEMO":
        # treat the demo as AoC during conversion
        # TODO: maybe introduce a config parameter for this purpose?
        game_edition.game_id = "AOC"
        from ..processor.conversion.aoc_demo.processor import DemoProcessor

        return DemoProcessor

    if game_edition.game_id == "HDEDITION":
        from ..processor.conversion.hd.processor import HDProcessor

        return HDProcessor

    if game_edition.game_id == "AOE2DE":
        from ..processor.conversion.de2.processor import DE2Processor

        return DE2Processor

    if game_edition.game_id == "SWGB":
        if "SWGB_CC" in [expansion.game_id for expansion in game_expansions]:
            from ..processor.conversion.swgbcc.processor import SWGBCCProcessor

            return SWGBCCProcessor

    raise RuntimeError(f"no valid converter found for game edition {game_edition.edition_name}")
