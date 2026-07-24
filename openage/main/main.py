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
        "--no-convert", action='store_true', dest='no_convert',
        default=True,
        help="disable automatic converter / api-export runs. Openage will "
             "only LOAD existing converted assets; missing or outdated "
             "assets cause a fast, readable failure. Use the explicit "
             "`openage convert` subcommand (or omit --no-convert) to run "
             "the converter. Note: --no-prompts only gates the *interactive* "
             "converter prompt; --no-convert is the hard kill switch and "
             "is what production / tutorial should pass.")

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

    # PHASE: converted-asset check + conversion decision
    import time as _time_phase
    def _phase(name):
        _t0 = _time_phase.monotonic()
        return name, _t0
    def _phase_end(name_t0, *, note=""):
        name, t0 = name_t0
        info("[PHASE] %s done in %.3fs%s", name, _time_phase.monotonic() - t0,
             (" -- " + note) if note else "")

    # Decide up front whether the converter is allowed to run at all.
    # --no-convert (default True) is a hard kill switch.
    show_prompts = bool(getattr(args, "show_prompts", True))
    no_convert = bool(getattr(args, "no_convert", True))
    converter_allowed = show_prompts and not no_convert
    # wanna_convert() may read stdin for an interactive prompt; only
    # call it when both prompts are enabled AND --no-convert is OFF
    # (a pass that reads stdin from closed FD EOFErrors in CI/headless).
    if converter_allowed:
        converter_forced_run = bool(wanna_convert())
    else:
        converter_forced_run = False
    # api_export is the small nyan-API regen, NOT the full converter.
    api_export_required_now = api_export_required(asset_path)
    info("CONVERSION_DECISION: api_export=%s ; converter_full=%s",
         "run" if api_export_required_now else "skip",
         "run" if converter_forced_run else "skip")
    info("CONVERSION_REASON: --no-prompts=%s, --no-convert=%s (wanna_convert "
         "not invoked because converter is suppressed)", show_prompts,
         no_convert)

    # ensure that the openage API is present
    api_phase = _phase("api_export")
    if api_export_required_now:
        if not converter_allowed:
            # The user passed --no-convert; engine nyan API is missing or
            # outdated. We do NOT auto-regenerate; we fail fast and clearly.
            from ..error import Error as _Error
            from ..log import err as _err
            _err("openage nyan API modpack is missing or outdated but "
                 "--no-convert was passed; refusing to auto-export. "
                 "Run `openage convert-export-api %s` then retry.",
                 asset_path)
            raise _Error(_Error("openage nyan API modpack missing or outdated"))
        info("Regenerating openage nyan API modpack (api_export)")
        converted_path = asset_path / "converted"
        converted_path.mkdirs()
        export_api(converted_path)
    _phase_end(api_phase)

    # full converter: only via interactive prompt (--no-prompts blocks it).
    convert_phase = _phase("converter_full")
    if converter_forced_run:
        info("Running full converter (interactive prompt active)")
        convert_assets(asset_path, args)
    else:
        info("[SKIP] converter_full: --no-prompts=%s, --no-convert=%s",
             bool(getattr(args, "show_prompts", True)),
             bool(getattr(args, "no_convert", True)))
    _phase_end(convert_phase)

    enumerate_phase = _phase("modpack_discovery")
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
    _phase_end(enumerate_phase)

    check_updates_phase = _phase("check_updates")
    check_updates(playable_modpacks, args.cfg_dir / "converter" / "games")
    _phase_end(check_updates_phase)

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
