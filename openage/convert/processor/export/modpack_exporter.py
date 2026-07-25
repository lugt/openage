# Copyright 2020-2022 the openage authors. See copying.md for legal info.
#
# pylint: disable=too-few-public-methods
"""
Export data from a modpack to files.
"""
from __future__ import annotations
import typing


from ....log import info
from ...tool.memprobe import snapshot
from .data_exporter import DataExporter
from .generate_manifest_hashes import generate_hashes
from .media_exporter import MediaExporter
from .output_checks import has_all_media_outputs, has_outputs

if typing.TYPE_CHECKING:
    from argparse import Namespace

    from openage.convert.entity_object.conversion.modpack import Modpack


class ModpackExporter:
    """
    Writes the contents of a created modpack into a targetdir.
    """

    @staticmethod
    def export(modpack: Modpack, args: Namespace) -> None:
        """
        Export a modpack to a directory.

        :param modpack: Modpack that is going to be exported.
        :param args: Converter arguments.
        :type modpack: ..dataformats.modpack.Modpack
        :type args: Namespace
        """
        sourcedir = args.srcdir
        exportdir = args.targetdir

        modpack_dir = exportdir.joinpath(f"{modpack.info.packagename}")

        info("Starting export...")
        snapshot("modpack-export:start")
        info("Dumping info file...")

        # Modpack info file
        DataExporter.export([modpack.info], modpack_dir)

        info("Dumping data files...")
        snapshot("modpack-export:after-data-files")

        # Data files (nyan)
        # --if-missing-nyan is an explicit opt-in skip that takes precedence
        # over --force: --force only enables non-interactive startup and
        # per-file overwrite within categories that ARE being exported, but an
        # explicit "skip if already done" must still be honored (otherwise the
        # flag is dead code in non-interactive mode, which requires --force).
        if args.flag("if_missing_nyan") and has_outputs(modpack_dir, ".nyan"):
            info("Skipping nyan export: existing .nyan files in %s", modpack_dir)
        else:
            DataExporter.export(modpack.get_data_files(), modpack_dir)

        if args.flag("no_media"):
            info("Skipping media file export...")
            return

        if args.flag("if_missing_media") and has_all_media_outputs(modpack_dir):
            info("Skipping media export: all media output categories already exist")
        else:
            info("Exporting media files...")
            snapshot("modpack-export:before-media")

            # Media files
            MediaExporter.export(modpack.get_media_files(), sourcedir, modpack_dir, args)
            snapshot("modpack-export:after-media")

        info("Dumping metadata files...")

        # Metadata files
        DataExporter.export(modpack.get_metadata_files(), modpack_dir)

        # Manifest file
        generate_hashes(modpack, modpack_dir)
        DataExporter.export([modpack.manifest], modpack_dir)
        snapshot("modpack-export:done")
