# Copyright 2026 the openage authors. See copying.md for legal info.
"""Wwise audio extraction + decoding for AoE2 DE."""
from .extract import (
    extract_wem,
    extract_wem_to_opus_bytes,
    find_wem_bytes,
)
from .mapping import build_mapping, DEFAULT_MAP_PATH

__all__ = [
    "extract_wem",
    "extract_wem_to_opus_bytes",
    "find_wem_bytes",
    "build_mapping",
    "DEFAULT_MAP_PATH",
]