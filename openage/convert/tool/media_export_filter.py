# Copyright 2026 the openage authors. See copying.md for legal info.

"""
Mission 1 / scoped media export filter.

The filter is consulted by ``ModpackExporter.export()`` right after the
modpack media request dict is materialised and before it is handed to
``MediaExporter``. It does NOT touch Reader, Processor, Mapping, or the
original ``MediaExportRequest`` generator -- only the request dict that
is about to be consumed by the existing exporter. The filter therefore
satisfies the constraint:

    lazy Reader → preprocessing → grouping/linking → mapping →
    media request generation → filter →
    original MediaExporter.export()

Filter file format (UTF-8, plain text):

    # Lines starting with '#' are comments.
    # Empty lines are ignored.
    # Each match line is four optional fields separated by ':':
    #
    #   modpack  : media_type : targetdir_glob : target_filename_glob
    #
    # An empty field, '*', or absence matches any value.

Examples:

    # Mission 1 closure - Villager graphics
    de2_base:graphics:*/villager/:*

    # Mission 1 closure - TownCenter graphics
    de2_base:graphics:*/town_center/:*

    # Real DE2 terrain (grass only)
    de2_base:terrain:*/grass/:*

A non-matching filter file (no match lines, or a match line with all '*')
is treated as a pass-through (no requests are dropped). A request matches
if any entry line matches on all four fields (logical OR between entries,
AND between fields within one entry).
"""

from __future__ import annotations
import fnmatch
import os
import re
import typing
from dataclasses import dataclass

if typing.TYPE_CHECKING:
    from openage.convert.value_object.read.media_types import MediaType


@dataclass
class FilterResult:
    """Counts and per-request verdicts returned by apply_media_filter()."""

    kept_requests: int = 0
    dropped_requests: int = 0
    original_total: int = 0
    # Set of int(id(MediaExportRequest)) for kept requests so the caller
    # can use the same set when pruning dependent metadata files.
    # ``None`` means "no filter was applied" (pass-through) so the caller
    # should not prune metadata either.
    kept_request_ids: typing.Optional[set[int]] = None


@dataclass
class _Entry:
    modpack: str
    media_type: str
    targetdir_glob: str
    target_filename_glob: str

    def matches(
        self,
        modpack_name: str,
        media_type_name: str,
        targetdir: str,
        target_filename: str,
    ) -> bool:
        return (
            _eq_or_wild(self.modpack, modpack_name)
            and _eq_or_wild(self.media_type, media_type_name.lower())
            and _path_glob_match(self.targetdir_glob, targetdir)
            and _path_glob_match(self.target_filename_glob, target_filename)
        )


def _eq_or_wild(pattern: str, value: str) -> bool:
    """String-equality match with ``*`` and ``?`` as glob wildcards.

    Modpack and media_type are short identifiers, so fnmatch is fine here.
    No ``**`` translation needed.

    The match is case-insensitive because ``MediaType.name`` returns
    uppercase (e.g. 'GRAPHICS', 'TERRAIN') while user-written filter
    rules tend to be lowercase ('graphics'). We lower-case the value
    before matching so the rule syntax stays intuitive.
    """
    if not pattern or pattern == "*":
        return True
    return fnmatch.fnmatch(value.lower(), pattern.lower())


def _path_glob_match(pattern: str, value: str) -> bool:
    """Path-friendly glob: ``*`` matches across path separators.

    In filter paths like ``*/villager/`` users expect the leading ``*`` to
    consume the data/*/graphic_entity/generic prefix, including slashes.
    We therefore translate the pattern so that:

    - ``*``  -> ``[^/]*`` AND ALSO the final segment boundary handled by
      making it match zero-or-more path segments in addition.

    The simplest interpretation: ``*`` matches any character including ``/``.
    This is the most intuitive semantics for path filters. ``**`` is just
    sugar for ``*``.

    Other glob metas from ``fnmatch`` (``?``, ``[``, ``!``) are also
    translated faithfully.
    """
    if not pattern or pattern == "*":
        return True
    placeholder = "\x00ANY\x00"
    translated = pattern.replace("*", placeholder)
    regex = re.escape(translated).replace(re.escape(placeholder), ".*")
    return re.match("^" + regex + "$", value) is not None


def _parse_filter_file(filter_path: str) -> list[_Entry]:
    """Read a filter file into a list of entries.

    Fails fast on malformed lines so a typo in the manifest does not
    silently pass every request through.
    """
    if not os.path.isfile(filter_path):
        raise FileNotFoundError(
            f"media-export-filter file not found: {filter_path}")

    entries: list[_Entry] = []
    with open(filter_path, "r", encoding="utf-8") as fh:
        for raw_lineno, raw_line in enumerate(fh, start=1):
            line = raw_line.rstrip("\n").rstrip("\r")
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            parts = stripped.split(":")
            # Reject clearly-malformed lines: too many ':' separators.
            # Target filenames legitimately never contain ':', so 3
            # colons max (yielding exactly 4 fields).
            if len(parts) > 4:
                raise ValueError(
                    f"{filter_path}:{raw_lineno}: too many ':'-separated "
                    f"fields (got {len(parts)}, expected at most 4): {line!r}")
            while len(parts) < 4:
                parts.append("")
            entries.append(_Entry(*parts))
    return entries


def apply_media_filter(
    export_requests: dict,
    modpack_name: str,
    filter_path: str,
) -> tuple[dict, FilterResult]:
    """Filter a {MediaType: [MediaExportRequest, ...]} dict in place.

    Returns the (possibly reduced) export_requests dict and a FilterResult
    summarising how many requests were kept vs dropped.

    The input dict is not mutated; a new dict is returned. MediaType keys
    that become empty after filtering are dropped entirely.
    """
    entries = _parse_filter_file(filter_path)

    # No filter lines at all means "no filtering" (an empty filter file
    # is a pass-through so the user doesn't silently get an empty
    # export if the file got truncated).
    if not entries:
        original_total = sum(len(reqs) for reqs in export_requests.values())
        result = FilterResult(
            kept_requests=original_total,
            dropped_requests=0,
            original_total=original_total,
            kept_request_ids=None,  # caller must not prune metadata
        )
        return export_requests, result

    # Compute original totals before filtering.
    original_total = sum(len(reqs) for reqs in export_requests.values())

    filtered: dict = {}
    kept_request_ids: set[int] = set()
    for media_type, requests in export_requests.items():
        kept = []
        for req in requests:
            matched = any(
                entry.matches(
                    modpack_name,
                    media_type.name,
                    req.targetdir,
                    req.target_filename,
                )
                for entry in entries
            )
            if matched:
                kept.append(req)
                kept_request_ids.add(id(req))
        if kept:
            filtered[media_type] = kept

    kept_total = sum(len(v) for v in filtered.values())
    result = FilterResult(
        kept_requests=kept_total,
        dropped_requests=original_total - kept_total,
        original_total=original_total,
        kept_request_ids=kept_request_ids,
    )
    return filtered, result


def print_filter_summary(modpack_name: str, filter_path: str,
                         result: FilterResult) -> None:
    """Emit a one-line human summary via the openage log."""
    if result.original_total == 0:
        return
    from openage.log import info
    pct = 100.0 * result.kept_requests / max(1, result.original_total)
    info(
        "[MEDIA-FILTER] modpack=%s file=%s kept=%d/%d (%.1f%%) dropped=%d",
        modpack_name,
        filter_path,
        result.kept_requests,
        result.original_total,
        pct,
        result.dropped_requests,
    )
