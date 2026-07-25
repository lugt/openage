# Copyright 2015-2024 the openage authors. See copying.md for legal info.
#
# pylint: disable=too-many-branches
"""
Entry point for all of the asset conversion.
"""
from __future__ import annotations

from datetime import datetime
import os
import shutil
import subprocess
import typing

from ..log import info, warn

from ..util.fslike.directory import CaseIgnoringDirectory
from ..util.fslike.wrapper import (DirectoryCreator,
                                   Synchronizer as AccessSynchronizer)
from .service.debug_info import debug_cli_args, debug_game_version, debug_mounts
from .service.init.changelog import check_updates
from .service.init.modpack_search import enumerate_modpacks
from .service.init.mount_asset_dirs import mount_asset_dirs
from .service.init.version_detect import create_version_objects
from .tool.interactive import interactive_browser
from .tool.subtool.acquire_sourcedir import acquire_conversion_source_dir, wanna_convert, \
    wanna_check_updates
from .tool.subtool.version_select import get_game_version

if typing.TYPE_CHECKING:
    from argparse import ArgumentParser, Namespace
    from openage.util.fslike.directory import Directory
    from openage.util.fslike.union import UnionPath
    from openage.util.fslike.path import Path


def convert_assets(
    assets: UnionPath,
    args: Namespace,
    srcdir: Directory = None
) -> None:
    """
    Perform asset conversion.

    Requires original assets and stores them in usable and free formats.

    assets must be a filesystem-like object pointing at the game's asset dir.
    srcdir must be None, or point at some source directory.

    This method prepares srcdir and targetdir to allow a pleasant, unified
    conversion experience, then passes them to .driver.convert().
    """
    converted_path = assets / "converted"
    converted_path.mkdirs()
    targetdir = DirectoryCreator(converted_path).root

    # Set compression level for media output if it was not set
    if "compression_level" not in vars(args):
        args.compression_level = 1

    # Set worker count for multi-threading if it was not set
    if "jobs" not in vars(args):
        args.jobs = None

    # Set verbosity for debug output
    if "debug_info" not in vars(args) or not args.debug_info:
        if args.devmode:
            args.debug_info = 3

        else:
            args.debug_info = 0

    # add a dir for debug info
    debug_log_path = converted_path / "debug" / datetime.now().strftime("%Y-%m-%d-%H-%M-%S")
    debugdir = DirectoryCreator(debug_log_path).root
    args.debugdir = debugdir

    # Create CLI args info
    debug_cli_args(args.debugdir, args.debug_info, args)

    # Initialize game versions data
    auxiliary_files_dir = args.cfg_dir / "converter" / "games"
    args.avail_game_eds, args.avail_game_exps = create_version_objects(auxiliary_files_dir)

    # try to get previously used source dir
    asset_locations_path = assets / "converted" / "asset_locations.cache"
    prev_srcdirs = get_prev_srcdir_paths(asset_locations_path)

    # acquire conversion source directory
    if srcdir is None:
        srcdir = acquire_conversion_source_dir(args.avail_game_eds, prev_srcdirs)

    # Acquire game version info
    args.game_version = get_game_version(srcdir, args.avail_game_eds, args.avail_game_exps)
    debug_game_version(args.debugdir, args.debug_info, args)

    if not args.game_version.edition:
        return None

    # Mount assets into conversion folder
    data_dir = mount_asset_dirs(srcdir, args.game_version)
    if not data_dir:
        return None

    args.srcdir = AccessSynchronizer(data_dir).root
    args.targetdir = targetdir

    # Create mountpoint info
    debug_mounts(args.debugdir, args.debug_info, args)

    def flag(name):
        """
        Convenience function for accessing boolean flags in args.
        Flags default to False if they don't exist.
        """
        return getattr(args, name, False)

    args.flag = flag

    # import here so codegen.py doesn't depend on it.
    from .tool.driver import convert

    # --memory-budget-test: force the lazy backend and instrument peak RSS
    # across the whole pipeline. The BudgetGate samples RSS in a background
    # thread (catches transients between stage marks) and writes a report.
    if getattr(args, "memory_budget_test", False):
        os.environ["OPENAGE_CONVERTER_BACKEND"] = "lazy"
        from .tool.budget_gate import BudgetGate
        args.budget_gate = BudgetGate()
        args.budget_gate.start()
    else:
        args.budget_gate = None

    # Run the conversion process
    convert(args)

    if getattr(args, "budget_gate", None) is not None:
        args.budget_gate.finish(notes=[
            "Peak sampled every %.2fs via /proc/self/status VmRSS."
            % 0.25,
            "Compare against legacy backend by unsetting "
            "OPENAGE_CONVERTER_BACKEND and rerunning without "
            "--memory-budget-test (legacy read phase OOMs on DE2).",
        ])

    # clean args
    del args.srcdir
    del args.targetdir

    # Remember the asset location if it is not already in the cache
    if prev_srcdirs is None:
        asset_locations_path.touch()
        prev_srcdirs = set()

    used_asset_path = data_dir.resolve_native_path().decode('utf-8')
    if used_asset_path not in prev_srcdirs:
        try:
            with asset_locations_path.open("a") as file_obj:
                if len(prev_srcdirs) > 0:
                    file_obj.write("\n")

                file_obj.write(used_asset_path)

        except IOError:
            # cache file cannot be accessed, skip writing
            warn(f"Cannot access asset location cache file {asset_locations_path}")
            info("Skipped saving asset location")


def _arg_flag(args, name: str) -> bool:
    """Safe boolean accessor used before args.flag() is installed."""
    return bool(getattr(args, name, False))


def _find_project_root() -> "NativePath":
    """Find the openage project root regardless of which tree is running.

    Walks up from this file looking for a directory that contains a
    ``bin/wwise/`` subdirectory (the wwise data marker). Works whether
    we're loaded from the source tree (``openage/convert/main.py``)
    or from a build copy (``.bin/g++-.../openage/convert/main.py``),
    both of which have the same depth below their respective roots.
    """
    from pathlib import Path as NativePath

    cur = NativePath(__file__).resolve().parent
    for _ in range(6):  # bounded; max 6 levels up
        candidate = cur.parent
        if (candidate / "bin" / "wwise").is_dir():
            return candidate
        cur = candidate
    # Fallback to parents[2] (works only for the source tree).
    return NativePath(__file__).resolve().parents[2]


def _ffmpeg_has_libopus(ffmpeg_path: str) -> bool:
    """Check that ffmpeg advertises the libopus encoder."""
    try:
        result = subprocess.run(
            [ffmpeg_path, "-hide_banner", "-encoders"],
            check=False,
            capture_output=True,
            text=True,
            timeout=10,
        )
    except (OSError, subprocess.TimeoutExpired):
        return False
    if result.returncode != 0:
        return False
    return "libopus" in (result.stdout + result.stderr)


def preflight_check(args) -> None:
    """Validate that the requested conversion can actually start.

    Runs AFTER argparse but BEFORE the .dat scan so users see failures fast.
    Aggregates all errors and prints them together, then raises
    SystemExit(1) -- never silently skips a missing prerequisite.
    """
    from pathlib import Path as NativePath

    errors: list[str] = []
    project_root = _find_project_root()

    # --- Wwise prerequisites (DE2 audio path) ---------------------------
    pck_dir_value = os.environ.get("OPENAGE_WWISE_PCK_DIR", "")
    if pck_dir_value:
        pck_dir = NativePath(pck_dir_value).expanduser()
        if not pck_dir.is_dir():
            errors.append(
                f"OPENAGE_WWISE_PCK_DIR is not a directory: {pck_dir}")
        else:
            has_pck = any(
                path.is_file() and path.suffix.lower() == ".pck"
                for path in pck_dir.iterdir()
            )
            if not has_pck:
                errors.append(
                    f"OPENAGE_WWISE_PCK_DIR contains no .pck files: {pck_dir}")

        mapping_value = os.environ.get("OPENAGE_WWISE_MAP_PATH", "")
        mapping_path = (
            NativePath(mapping_value).expanduser()
            if mapping_value
            else project_root / "bin" / "wwise" / "openage_wwise_mapping.json"
        )
        if not mapping_path.is_file():
            errors.append(
                f"Wwise mapping is missing: {mapping_path}. "
                "Build it via `python -m openage.convert.tool.wwise.mapping "
                "--pck-dir <DE2>/wwise --out <mapping.json>`.")

        vgmstream = project_root / "bin" / "wwise" / "vgmstream-cli"
        if not vgmstream.is_file():
            errors.append(
                f"vgmstream-cli is missing: {vgmstream}. "
                "Download from https://github.com/vgmstream/vgmstream/releases "
                "and place it there.")
        elif not os.access(vgmstream, os.X_OK):
            errors.append(
                f"vgmstream-cli is not executable: {vgmstream}. "
                "Run `chmod +x` on it.")

        ffmpeg = shutil.which("ffmpeg")
        if ffmpeg is None:
            errors.append(
                "ffmpeg was not found on PATH. "
                "Install it (`apt install ffmpeg`); the libopus encoder is required.")
        elif not _ffmpeg_has_libopus(ffmpeg):
            errors.append(
                f"ffmpeg at {ffmpeg} does not advertise the libopus encoder. "
                "Install an ffmpeg build with libopus support.")

        try:
            from .tool.wwise.mapping import _get_wwiser_parser
            _get_wwiser_parser()
        except (ImportError, RuntimeError) as exc:
            errors.append(
                f"wwiser is not importable: {exc}. "
                "Extract https://github.com/bnnm/wwiser into "
                "openage/.bin/wwiser/wwiser-master/.")

    # --- "Nothing to do" check ------------------------------------------
    all_media_disabled = all(
        _arg_flag(args, name)
        for name in ("no_media", "no_graphics", "no_sounds", "no_blend",
                     "no_terrain")
    )
    any_if_missing = any(
        _arg_flag(args, name)
        for name in ("if_missing_media", "if_missing_nyan",
                     "if_missing_graphics", "if_missing_sounds",
                     "if_missing_terrain", "if_missing_blend")
    )
    if all_media_disabled and not any_if_missing:
        errors.append(
            "Conversion has nothing to do: all media types are disabled "
            "(--no-media/--no-graphics/--no-sounds/--no-terrain/--no-blend) "
            "and no --if-missing-* flag is set.")

    if errors:
        for err in errors:
            warn("Pre-flight check failed: %s", err)
        raise SystemExit(1)


def get_prev_srcdir_paths(asset_location_path: Path) -> set[str] | None:
    """
    Get previously used source directories from a cache file.

    :param asset_location_path: Path to the cache file.
    :type asset_location_path: Path
    :return: Previously used source directories.
    :rtype: set[str] | None
    """
    prev_source_dirs: set[str] = set()
    try:
        with asset_location_path.open("r") as file_obj:
            prev_source_dirs.update(file_obj.read().split("\n"))

    except FileNotFoundError:
        prev_source_dirs = None

    return prev_source_dirs


def init_subparser(cli: ArgumentParser):
    """ Initializes the parser for convert-specific args. """
    cli.set_defaults(entrypoint=main)

    cli.add_argument(
        "--source-dir", default=None,
        help="source data directory")

    cli.add_argument(
        "--output-dir", default=None,
        help="destination data output directory")

    cli.add_argument(
        "--force", action='store_true',
        help="force conversion, even if up-to-date assets already exist.")

    cli.add_argument(
        "--gen-extra-files", action='store_true',
        help="generate some extra files, useful for debugging the converter.")

    cli.add_argument(
        "--no-media", action='store_true',
        help="do not convert any media files (slp, wav, ...)")

    cli.add_argument(
        "--no-metadata", action='store_true',
        help=("do not store any metadata "
              "(except for those associated with media files)"))

    cli.add_argument(
        "--no-sounds", action='store_true',
        help="do not convert any sound files")

    cli.add_argument(
        "--no-graphics", action='store_true',
        help="do not convert game graphics")

    cli.add_argument(
        "--no-interface", action='store_true',
        help="do not convert interface graphics")

    cli.add_argument(
        "--no-scripts", action='store_true',
        help="do not convert scripts (AI and Random Maps)")

    cli.add_argument(
        "--no-terrain", action='store_true',
        help="do not convert terrain graphics")

    cli.add_argument(
        "--no-blend", action='store_true',
        help="do not convert blending masks")

    cli.add_argument(
        "--if-missing-media", action='store_true',
        help="skip whole media export when graphics+sounds+terrain+blend outputs all exist")

    cli.add_argument(
        "--if-missing-nyan", action='store_true',
        help="skip nyan export when modpack already contains .nyan files")

    cli.add_argument(
        "--if-missing-graphics", action='store_true',
        help="skip graphics conversion when PNG outputs exist")

    cli.add_argument(
        "--if-missing-sounds", action='store_true',
        help="skip sound conversion when .opus outputs exist")

    cli.add_argument(
        "--if-missing-terrain", action='store_true',
        help="skip terrain conversion when terrain PNG outputs exist")

    cli.add_argument(
        "--if-missing-blend", action='store_true',
        help="skip blend conversion when blend outputs exist")

    cli.add_argument(
        "--no-pickle-cache", action='store_true',
        help="don't use a pickle file to skip the dat file reading.")

    cli.add_argument(
        "--jobs", "-j", type=int, default=None)

    cli.add_argument(
        "--interactive", "-i", action='store_true',
        help="browse the files interactively")

    cli.add_argument(
        "--id", type=int, default=None,
        help="only convert files with this id (used for debugging..)")

    cli.add_argument(
        "--compression-level", type=int, default=2, choices=[0, 1, 2, 3, 4],
        help="set PNG compression level")

    cli.add_argument(
        "--debug-info", type=int, choices=[0, 1, 2, 3, 4, 5, 6],
        help="create debug output for the converter run; verbosity levels 0-6")

    cli.add_argument(
        "--low-memory", action='store_true',
        help="Activate low memory mode")

    cli.add_argument(
        "--memory-budget-test", action='store_true',
        help=("Force the lazy converter backend, sample peak RSS through the "
              "full pipeline, and write a budget report to "
              "docs/superpowers/reports/phase1.txt. Acceptance: peak RSS "
              "< 500 MiB (P2); < 300 MiB is stretch (P3). Implies "
              "OPENAGE_CONVERTER_BACKEND=lazy."))

    cli.add_argument(
        "--export-api", action='store_true',
        help="Export the openage nyan API definition as a modpack")

    cli.add_argument(
        "--check-updates", action='store_true',
        help="Check if the assets are up to date"
    )

    cli.add_argument(
        "--no-prompts", action='store_false', dest='show_prompts',
        help="Disable user prompts"
    )


def main(args, error):
    """ CLI entry point """
    del error  # unused

    # initialize libopenage
    from ..cppinterface.setup import setup
    setup(args)

    # conversion source
    if args.source_dir is not None:
        srcdir = CaseIgnoringDirectory(args.source_dir).root
    else:
        srcdir = None

    # mount the config folder at "cfg/"
    from ..cvar.location import get_config_path
    from ..util.fslike.union import Union
    root = Union().root
    root["cfg"].mount(get_config_path())
    args.cfg_dir = root["cfg"]

    if args.interactive:
        interactive_browser(root["cfg"], srcdir)
        return 0

    # conversion target
    from ..assets import get_asset_path
    outdir = get_asset_path(args.output_dir)

    if args.force or (args.show_prompts and wanna_convert()):
        preflight_check(args)
        convert_assets(outdir, args, srcdir)

    if args.check_updates or (args.show_prompts and wanna_check_updates()):
        # check if the assets are up to date
        modpack_dir = outdir / "converted"
        available_modpacks = enumerate_modpacks(modpack_dir, exclude={"engine"})

        game_info_dir = args.cfg_dir / "converter" / "games"

        check_updates(available_modpacks, game_info_dir)

    return 0
