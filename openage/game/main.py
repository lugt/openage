# Copyright 2015-2024 the openage authors. See copying.md for legal info.
#
# pylint: disable=too-many-locals

"""
Holds the game entry point for openage.
"""

from __future__ import annotations

import typing

from ..log import info

if typing.TYPE_CHECKING:
    from argparse import ArgumentParser


def init_subparser(cli: ArgumentParser) -> None:
    """Initializes the parser for game-specific args."""
    cli.set_defaults(entrypoint=main)

    cli.add_argument(
        "--gl-debug", action="store_true", help="throw exceptions directly from the OpenGL calls"
    )

    cli.add_argument("--headless", action="store_true", help="run without displaying graphics")
    cli.add_argument(
        "--ignore-user-input",
        action="store_true",
        help="ignore game-window mouse/keyboard input during automation; Escape still exits",
    )

    cli.add_argument(
        "--no-convert",
        action="store_true",
        dest="no_convert",
        default=True,
        help="Disable automatic converter / api-export runs. openage will "
        "only LOAD existing converted assets; missing or outdated "
        "assets cause a fast, readable failure. Production / "
        "tutorial should pass this. Use the explicit `openage convert` "
        "subcommand (or omit --no-convert) to run the converter.",
    )

    cli.add_argument("--modpacks", nargs="+", required=True, type=str, help="list of modpacks to load")

    cli.add_argument("--check-updates", action="store_true", help="Check if the assets are up to date")

    cli.add_argument(
        "--window-size",
        nargs=2,
        type=int,
        default=[1024, 768],
        metavar=("WIDTH", "HEIGHT"),
        help="Initial window size in pixels",
    )

    cli.add_argument("--vsync", action="store_true", help="Enable vertical synchronization")

    cli.add_argument(
        "--window-mode",
        choices=["fullscreen", "borderless", "windowed"],
        default="windowed",
        help="Set the window mode",
    )


def main(args, error):
    """
    Makes sure that the assets have been converted,
    and jumps into the C++ main method.
    """
    del error  # unused

    # we have to import stuff inside the function
    # as it depends on generated/compiled code
    from .. import config
    from ..assets import get_asset_path
    from ..convert.service.init.api_export_required import api_export_required
    from ..convert.service.init.changelog import check_updates
    from ..convert.service.init.modpack_search import enumerate_modpacks
    from ..convert.tool.api_export import export_api
    from ..cppinterface.setup import setup as cpp_interface_setup
    from ..cvar.location import get_config_path
    from ..util.fslike.union import Union
    from .main_cpp import run_game

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

    # PHASE: converted-asset check + conversion decision (game command)
    import time as _time_phase
    def _phase_end(name, t0, *, note=""):
        info("[PHASE] %s done in %.3fs%s", name, _time_phase.monotonic() - t0,
             (" -- " + note) if note else "")

    # Decision: never run api_export or converter without explicit user opt-in.
    api_required_now = api_export_required(asset_path)
    no_convert = bool(getattr(args, "no_convert", True))
    info("CONVERSION_DECISION: api_export=%s",
         "run" if api_required_now else "skip")
    info("CONVERSION_REASON: api_required=%s, --no-convert=%s (game has NO "
         "full-converter path; only api-export can auto-run for the engine "
         "modpack and is suppressed when --no-convert)", api_required_now,
         no_convert)

    api_t0 = _time_phase.monotonic()
    if api_required_now:
        if no_convert:
            from ..error import Error as _Error
            from ..log import err as _err
            _err("openage nyan API modpack is missing or outdated but "
                 "--no-convert was passed; refusing to auto-export. "
                 "Run `openage convert-export-api %s` then retry.",
                 asset_path)
            raise _Error(_Error("openage nyan API modpack missing or "
                                "outdated"))
        info("Regenerating openage nyan API modpack (api_export)")
        converted_path = asset_path / "converted"
        converted_path.mkdirs()
        export_api(converted_path)
    _phase_end("api_export", api_t0)

    enum_t0 = _time_phase.monotonic()
    modpack_dir = asset_path / "converted"
    available_modpacks = enumerate_modpacks(modpack_dir, exclude={"engine"})
    missing = [m for m in args.modpacks if m not in available_modpacks]
    if missing:
        raise FileNotFoundError(
            f"Modpack(s) {missing} not found in {modpack_dir}. "
            f"Run `openage convert` first, or omit --no-convert to let "
            f"the engine nyan API regenerate.")
    _phase_end("modpack_discovery", enum_t0)

    if args.check_updates:
        chk_t0 = _time_phase.monotonic()
        check_updates(available_modpacks, args.cfg_dir / "converter" / "games")
        _phase_end("check_updates", chk_t0)
    else:
        info("[PHASE] check_updates skipped (only runs with --check-updates)")

    # encode modpacks as bytes for the C++ interface
    args.modpacks = [modpack.encode("utf-8") for modpack in args.modpacks]

    # Pass window parameters to engine
    args.window_args = {
        "width": args.window_size[0],
        "height": args.window_size[1],
        "vsync": args.vsync,
        "window_mode": args.window_mode,
    }

    # start the game, continue in main_cpp.pyx!
    return run_game(args, root)
