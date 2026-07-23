# Copyright 2015-2024 the openage authors. See copying.md for legal info.

"""
Main engine entry point for openage.
"""
from __future__ import annotations
import typing

from ..log import info

if typing.TYPE_CHECKING:
    from argparse import ArgumentParser


def init_subparser(cli: ArgumentParser):
    """ Initializes the parser for game-specific args. """
    cli.set_defaults(entrypoint=main)

    cli.add_argument(
        "--gl-debug", action='store_true',
        help="throw exceptions directly from the OpenGL calls")

    cli.add_argument(
        "--headless", action='store_true',
        help="run without displaying graphics")

    cli.add_argument(
        "--tutorial", action='store_true', dest='tutorial',
        help="launch in tutorial mode: a tutorial PlayerSetup FQON and "
             "scenario FQON are forwarded to the engine so the C++ "
             "simulation can wire up the tutorial slot instead of the "
             "hardcoded empty / debug defaults.")

    cli.add_argument(
        "--tutorial-player-setup-fqon", type=str,
        default="engine.util.setup.tutorial_setup.TutorialPlayerSetup",
        dest='tutorial_player_setup_fqon',
        help="FQON of the nyan PlayerSetup used for the tutorial slot. "
             "Defaults to the engine modpack's TutorialPlayerSetup so "
             "`--tutorial` alone is enough to run the campaign tutorial. "
             "Pass an empty string to keep the slot as a debug/observer "
             "with no civ patches or starting resources. Combined with "
             "--tutorial; ignored otherwise.")

    cli.add_argument(
        "--tutorial-scenario-fqon", type=str,
        default="engine.util.scenario.mission_1.Mission1",
        dest='tutorial_scenario_fqon',
        help="FQON of the nyan Scenario loaded by the simulation on "
             "start. Defaults to the engine modpack's Mission1 (16x16 "
             "grass map, 1 villager + 1 town center; teaches gather wood). "
             "Pass an empty string to keep the simulation's hardcoded "
             "procedural terrain. Combined with --tutorial; ignored "
             "otherwise.")

    cli.add_argument(
        "--modpacks", nargs="+", type=str,
        help="list of modpacks to load")

    cli.add_argument(
        "--no-prompts", action='store_false', dest='show_prompts',
        help="disable user prompts (e.g. the asset-conversion prompt); "
             "no conversion is attempted and, when no modpacks are "
             "specified, the first available modpack is selected "
             "automatically")

    cli.add_argument(
        "--window-size", nargs=2, type=int, default=[1024, 768],
        metavar=('WIDTH', 'HEIGHT'),
        help="Initial window size in pixels")

    cli.add_argument(
        "--vsync", action='store_true',
        help="Enable vertical synchronization")

    cli.add_argument(
        "--window-mode", choices=["fullscreen", "borderless", "windowed"], default="windowed",
        help="Set the window mode")


def main(args, error):
    """
    Makes sure that the assets have been converted,
    and jumps into the C++ main method.
    """
    # pylint: disable=too-many-locals
    del error  # unused

    # we have to import stuff inside the function
    # as it depends on generated/compiled code
    from .main_cpp import run_game
    from .. import config
    from ..assets import get_asset_path
    from ..convert.main import convert_assets
    from ..convert.service.init.api_export_required import api_export_required
    from ..convert.service.init.changelog import check_updates
    from ..convert.service.init.modpack_search import enumerate_modpacks, query_modpack
    from ..convert.tool.api_export import export_api
    from ..convert.tool.subtool.acquire_sourcedir import wanna_convert
    from ..cppinterface.setup import setup as cpp_interface_setup
    from ..cvar.location import get_config_path
    from ..util.fslike.union import Union

    # initialize libopenage
    cpp_interface_setup(args)

    info("launching openage %s", config.VERSION)
    info("compiled by %s", config.COMPILER)

    if config.DEVMODE:
        info("running in DEVMODE")

    # create virtual file system for data paths
    root = Union().root

    # mount the assets folder union at "assets/"
    asset_path = get_asset_path(args.asset_dir)
    root["assets"].mount(asset_path)

    # mount the config folder at "cfg/"
    root["cfg"].mount(get_config_path(args.cfg_dir))
    args.cfg_dir = root["cfg"]

    # ensure that the openage API is present
    if api_export_required(asset_path):
        # export to assets folder
        converted_path = asset_path / "converted"
        converted_path.mkdirs()
        export_api(converted_path)

    # check if modpacks need to be converted
    if args.show_prompts and wanna_convert():
        convert_assets(asset_path, args)

    available_modpacks = enumerate_modpacks(asset_path / "converted")
    playable_modpacks = {
        name: version
        for name, version in available_modpacks.items()
        if name != "engine"
    }
    if len(playable_modpacks) == 0:
        info("No modpacks have been found")
        if not args.modpacks:
            info("Starting bare 'engine' mode")
            args.modpacks = ["engine"]

    # check if the converted modpacks are up to date
    check_updates(playable_modpacks, args.cfg_dir / "converter" / "games")

    # pass modpacks to engine
    #
    # Tutorial mode uses the self-contained 'engine' modpack unless the user
    # explicitly requested other modpacks. Tutorial data lives entirely under
    # engine.util.tutorial_data / engine.util.scenario.mission_1 and does not
    # depend on de2_base. Loading de2_base here would instead abort startup:
    # its converter-generated civ data (e.g. burgundians.nyan:597 references
    # the unresolved name 'HusbandryResearchable.HusbandryCost.FoodAmount')
    # contains nyan name-resolution errors that pre-date this tutorial work.
    if getattr(args, "tutorial", False) and not args.modpacks:
        info("Tutorial mode: using 'engine' modpack (self-contained tutorial data)")
        args.modpacks = ["engine"]

    if args.modpacks:
        # ensure that specified modpacks are available
        for modpack in args.modpacks:
            if modpack not in available_modpacks:
                raise FileNotFoundError(
                    f"Modpack '{modpack}' not found in {asset_path / 'converted'}")

        args.modpacks = [modpack.encode("utf-8") for modpack in args.modpacks]

    elif args.show_prompts:
        args.modpacks = [query_modpack(list(playable_modpacks.keys())).encode("utf-8")]

    else:
        # prompts disabled and no modpacks specified: select the first
        # available modpack automatically (or fall back to bare engine mode)
        proposals = sorted(playable_modpacks.keys())
        if proposals:
            info("No modpacks specified; selecting %s", proposals[0])
            args.modpacks = [proposals[0].encode("utf-8")]
        else:
            info("No modpacks available; starting bare 'engine' mode")
            args.modpacks = ["engine".encode("utf-8")]

    # Pass window parameters to engine
    args.window_args = {
        "width": args.window_size[0],
        "height": args.window_size[1],
        "vsync": args.vsync,
        "window_mode": args.window_mode,
    }

    # Normalise tutorial CLI args. ``args.tutorial`` is the bool flag;
    # the two FQON strings are always present (defaulted to the engine
    # modpack's TutorialPlayerSetup / Tutorial1 above) and the C++ side
    # decides whether to use them based on the bool. Pass --tutorial
    # alone to use the bundled tutorial, or override the FQONs to swap
    # to a different scenario.
    args.tutorial = bool(getattr(args, "tutorial", False))
    args.tutorial_player_setup_fqon = str(
        getattr(args, "tutorial_player_setup_fqon", "") or "")
    args.tutorial_scenario_fqon = str(
        getattr(args, "tutorial_scenario_fqon", "") or "")

    # start the game, continue in main_cpp.pyx!
    return run_game(args, root)
