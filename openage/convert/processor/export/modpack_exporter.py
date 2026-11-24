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

        # Data files
        DataExporter.export(modpack.get_data_files(), modpack_dir)

        if args.flag("no_media"):
            info("Skipping media file export...")
            return

        info("Exporting media files...")
        snapshot("modpack-export:before-media")

        # Mission 1 / scoped-export filter: consult the user-provided
        # filter (if any) right after the modpack request dict is
        # materialised and immediately before MediaExporter consumes it.
        # The filter does NOT mutate Reader/Processor/Mapping/MediaExporter;
        # it only narrows the request set handed to MediaExporter. The
        # metadata_files list is also pruned in lock-step so an
        # orphan .texture/.sprite entry never reaches the writer
        # without the .png that produces it. Shared metadata entries
        # (referenced by more than one request) are kept iff at
        # least one referencing request survives; metadata with no
        # request refs (e.g., global blend masks) are kept.
        media_files = modpack.get_media_files()
        metadata_files = modpack.get_metadata_files()
        filter_path = getattr(args, "media_export_filter", None)
        kept_request_ids = None
        if filter_path:
            from ...tool.media_export_filter import (
                apply_media_filter, print_filter_summary)
            media_files, filter_result = apply_media_filter(
                media_files, modpack.info.packagename, filter_path)
            print_filter_summary(
                modpack.info.packagename, filter_path, filter_result)
            kept_request_ids = filter_result.kept_request_ids
            if kept_request_ids is not None:
                # Prune metadata files whose dependent requests are all
                # filtered out. Shared entries survive; orphan entries
                # drop. A metadata file with no refs at all is treated
                # as "pre-FIX2 un-tracked" and dropped when a filter is
                # active, because there is no general-infrastructure
                # class that legitimately belongs in modpack.metadata_files
                # without a request reference (blend masks live in
                # media_exports[BLEND], not in metadata_files).
                pruned_meta = []
                dropped_meta = 0
                dropped_untracked = 0
                for mf in metadata_files:
                    refs = mf.request_refs
                    if not refs:
                        dropped_untracked += 1
                    elif mf.has_live_request(kept_request_ids):
                        pruned_meta.append(mf)
                    else:
                        dropped_meta += 1
                metadata_files = pruned_meta
                info(
                    "[MEDIA-FILTER] modpack=%s metadata_kept=%d "
                    "metadata_dropped=%d metadata_untracked_dropped=%d",
                    modpack.info.packagename,
                    len(metadata_files),
                    dropped_meta,
                    dropped_untracked,
                )

        # Media files
        MediaExporter.export(media_files, sourcedir, modpack_dir, args)
        snapshot("modpack-export:after-media")

        info("Dumping metadata files...")

        # Metadata files (filtered if applicable; metadata_files is the
        # var that may have been pruned above).
        DataExporter.export(metadata_files, modpack_dir)

        # Manifest file
        generate_hashes(modpack, modpack_dir)
        DataExporter.export([modpack.manifest], modpack_dir)
        snapshot("modpack-export:done")
