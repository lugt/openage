# Copyright 2024 the openage authors. See copying.md for legal info.
"""
Helpers for `--if-missing-*` incremental export checks.

Centralized so modpack_exporter and media_exporter share one
recursive "any matching file exists" predicate.

All functions work with openage's FS-like Path objects (which
expose .iterdir(), .is_dir(), .is_file(), .suffix), not stdlib
pathlib -- the exporter pipeline passes FS-like roots.
"""
from __future__ import annotations

import typing

if typing.TYPE_CHECKING:
    from openage.util.fslike.path import Path


def has_outputs(root: "Path", suffix: str | None = None) -> bool:
    """Return True if `root` recursively contains a matching regular file.

    A missing directory, an empty directory, or a directory tree with
    only subdirectories (and no regular files matching `suffix`) returns
    False. Iteration stops at the first match for efficiency.

    :param root: openage FS-like Path to search.
    :param suffix: file extension to match (case-insensitive, with leading
                   dot, e.g. ".nyan"). If None, any regular file counts.
    """
    if not root.exists() or not root.is_dir():
        return False
    return _walk(root, suffix)


def _walk(root: "Path", suffix: str | None) -> bool:
    suffix_lower = suffix.lower() if suffix else None
    for entry in root.iterdir():
        if entry.is_dir():
            if _walk(entry, suffix_lower):
                return True
        elif entry.is_file():
            if suffix_lower is None:
                return True
            if entry.suffix.lower() == suffix_lower:
                return True
    return False


def has_all_media_outputs(modpack_dir: "Path") -> bool:
    """Conjunction check used by `--if-missing-media`.

    Returns True when graphics + sounds + terrain + blend are all
    present (i.e. at least one qualifying file in each of the four
    expected subtrees). Used as the coarse "media export is complete"
    gate.
    """
    return all((
        has_outputs(modpack_dir / "data" / "game_entity" / "shared" / "graphics", ".png"),
        has_outputs(modpack_dir / "data" / "game_entity" / "shared" / "sounds", ".opus"),
        has_outputs(modpack_dir / "data" / "terrain", ".png"),
        has_outputs(modpack_dir / "data" / "blend"),
    ))