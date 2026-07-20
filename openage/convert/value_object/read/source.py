# Copyright 2026 the openage authors. See copying.md for legal info.
"""Lazy source for a Genie .dat: mmap'd raw bytes + per-record offset tables.

This is the build/storage layer of the streaming converter. It walks the .dat
structurally (reusing the same ``GenieStructure`` schema the real reader uses)
but, unlike ``load_gamespec``, it builds **no** ValueMember objects. Instead it
records, for every top-level record (units, graphics, sounds, techs, terrains,
civs, effect bundles, connections, ...), the exact byte offset at which that
record's bytes begin -- plus the dispatched struct class and any ``passed_args``
the record needs to re-read in isolation.

Those offset tables are the *handles* the lazy stores (T3) key on:
``genie_units[id]`` -> ``cls(**args).read(raw, offset, gv)`` rebuilds just that
one record's ValueMember subtree on demand. ``raw`` is an mmap over a tmpfile,
so the ~84 MiB of decompressed bytes live in the OS page cache, not the Python
heap.

Per-struct re-read is self-contained: dynamic lengths resolve from the struct's
own already-read fields, and ``passed_args`` (e.g. ``terrain_count`` for
``TerrainRestriction``) are captured here and replayed. The dat itself already
stores ``Civ.unit_offsets`` (per-civ unit file offsets) but the legacy reader
ignores them for seeking; we record the real running offset instead, which is
what ``read()`` consumes.

The walker mirrors ``openage/convert/tool/schema_probe.py`` (which is proven to
walk the BfG DE2 dat to completion at ~30 MiB). Keep them in sync; the probe is a
diagnostic counter, this is the production handle source.
"""
from __future__ import annotations

import mmap
import os
import re
import struct
import zlib
from dataclasses import dataclass, field
from typing import Any

from openage.convert.value_object.read.genie_structure import GenieStructure
from openage.convert.value_object.read.read_members import (
    ContinueReadMember,
    ContinueReadMemberResult,
    IncludeMembers,
    MultisubtypeMember,
    ReadMember,
    SubdataMember,
)
from openage.convert.value_object.read.member_access import READ_UNKNOWN, SKIP
from openage.convert.value_object.init.game_version import (
    GameEdition,
    GameExpansion,
    GameVersion,
)
from openage.log import info, warn

VARARRAY_MATCH = re.compile(r"([a-zA-Z0-9_]+) *\[([a-zA-Z0-9_]+)\]")
INTEGER_MATCH = re.compile(r"\d+")


# --------------------------------------------------------------------------- #
# raw loading + caching (mmap)
# --------------------------------------------------------------------------- #
def load_raw_mmap(dat_path: str) -> tuple[mmap.mmap, str, object]:
    """
    zlib-decompress the .dat to a tmpfile and mmap it read-only, so the
    decompressed bytes live in the OS page cache rather than the Python heap.

    The cache file is reused across runs (same scheme as schema_probe). The
    returned file object must be kept alive for the mmap to remain valid; the
    caller stores it on the source.
    """
    name = os.path.basename(dat_path)
    cache_env = os.environ.get("OPENAGE_DAT_RAW_CACHE")
    if cache_env and cache_env != "0":
        cache_path = cache_env
    else:
        cache_path = f"/tmp/openage_dat_raw.{name}.bin"

    need_write = True
    if cache_env != "0" and os.path.isfile(cache_path):
        try:
            if os.path.getmtime(cache_path) >= os.path.getmtime(dat_path):
                need_write = False
            else:
                info("genie_source: cache stale, re-decompressing")
        except OSError:
            pass

    if need_write:
        with open(dat_path, "rb") as f:
            compressed = f.read()
        info("genie_source: read %d compressed bytes from %s", len(compressed), dat_path)
        raw = zlib.decompress(compressed, -15)
        del compressed
        _check_versionstr(raw, name)
        if cache_env != "0":
            try:
                with open(cache_path, "wb") as f:
                    f.write(raw)
                info("genie_source: cached raw to %s", cache_path)
            except OSError as exc:
                warn("genie_source: could not write raw cache %s: %s", cache_path, exc)
    else:
        info("genie_source: reusing cached raw at %s", cache_path)

    # mmap the cache file (read-only). Keep the fd open.
    fd = open(cache_path, "rb")
    try:
        mm = mmap.mmap(fd.fileno(), 0, access=mmap.ACCESS_READ)
    except ValueError:
        # empty file shouldn't happen, but fall back to a full read+map
        fd.seek(0)
        data = fd.read()
        fd.close()
        mm = mmap.mmap(-1, len(data))
        mm.write(data)
        mm.seek(0)
        fd = None
    _check_versionstr(mm, name)
    return mm, cache_path, fd


def _check_versionstr(raw, name: str) -> None:
    vs = bytes(raw[:8]).rstrip(b"\x00").decode("ascii", errors="ignore")
    info("genie_source: %s versionstr = %r", name, vs)


# --------------------------------------------------------------------------- #
# game version (minimal, bypasses GameBase.__init__ -- mirrors schema_probe)
# --------------------------------------------------------------------------- #
def make_game_version(versionstr: str) -> GameVersion:
    bfg = versionstr == "VER 8.4"

    edition = object.__new__(GameEdition)
    edition.game_id = "AOE2DE"
    edition.support = None
    edition.flags = {"media_cache": "AOE2DE_media_cache.json"}
    edition.media_cache = "AOE2DE_media_cache.json"
    edition.target_modpacks = {}
    edition.game_file_versions = []
    edition.media_paths = {}

    if bfg:
        exp = object.__new__(GameExpansion)
        exp.game_id = "BfG"
        exp.support = None
        exp.flags = {}
        exp.target_modpacks = []
        exp.game_file_versions = []
        exp.media_paths = {}
        expansions = (exp,)
    else:
        expansions = ()

    gv = GameVersion(
        edition=edition,
        expansions=expansions,
        update_number="BfG" if bfg else "base",
    )
    info("genie_source: game_id=AOE2DE update=%s (bfg=%s)", gv.update_number, bfg)
    return gv


# --------------------------------------------------------------------------- #
# handle + namespace
# --------------------------------------------------------------------------- #
@dataclass
class RecordHandle:
    """Persistent handle to one top-level record's bytes.

    ``cls(**args).read(raw, offset, gv)`` rebuilds this record's ValueMember
    subtree on demand. ``args`` carries ``passed_args`` captured from the parent
    (e.g. terrain_count for TerrainRestriction).
    """
    offset: int
    cls: type
    args: dict = field(default_factory=dict)


class _NS:
    """Attribute namespace so SubdataMember.get_length()'s getattr(obj, name)
    resolves while we walk. Discarded once its enclosing read returns."""
    __slots__ = ("__dict__",)

    def __init__(self, **kw):
        self.__dict__.update(kw)

    def __contains__(self, key):
        return key in self.__dict__

    def get(self, key, default=None):
        return self.__dict__.get(key, default)


# path element categories -> table attr name on GenieSource
_PATH_CATEGORIES = {
    "unit_headers": "unit_headers",
    "graphics": "graphics",
    "sounds": "sounds",
    "researches": "techs",
    "terrains": "terrains",
    "effect_bundles": "effect_bundles",
    "terrain_restrictions": "terrain_restrictions",
    "player_colors": "player_colors",
    "civs": "civs",
    "age_connections": "age_connections",
    "building_connections": "building_connections",
    "unit_connections": "unit_connections",
    "tech_connections": "tech_connections",
}


class GenieSource:
    """mmap'd raw .dat + per-record offset tables. The lazy stores (T3) read
    handles from here and rebuild ValueMember subtrees on demand."""

    def __init__(self, raw: mmap.mmap, game_version: GameVersion, raw_file):
        self.raw = raw
        self.gv = game_version
        self._raw_file = raw_file  # keep fd alive for the mmap

        # category -> {key: RecordHandle}
        self.unit_headers: dict[int, RecordHandle] = {}
        self.units: dict[int, RecordHandle] = {}            # base units (civs[0]), key=id0
        self.civ_units: dict[tuple, RecordHandle] = {}      # (civ_idx, id0), civ_idx>0 overrides
        self.graphics: dict[int, RecordHandle] = {}
        self.sounds: dict[int, RecordHandle] = {}
        self.techs: dict[int, RecordHandle] = {}
        self.terrains: dict[int, RecordHandle] = {}
        self.effect_bundles: dict[int, RecordHandle] = {}
        self.terrain_restrictions: dict[int, RecordHandle] = {}
        self.player_colors: dict[int, RecordHandle] = {}
        self.civs: dict[int, RecordHandle] = {}
        self.age_connections: dict[int, RecordHandle] = {}
        self.building_connections: dict[int, RecordHandle] = {}
        self.unit_connections: dict[int, RecordHandle] = {}
        self.tech_connections: dict[int, RecordHandle] = {}

    @classmethod
    def from_dat(cls, dat_path: str, game_version=None) -> "GenieSource":
        raw, _cache, fd = load_raw_mmap(dat_path)
        versionstr = bytes(raw[:8]).rstrip(b"\x00").decode("ascii", errors="ignore")
        gv = game_version if game_version is not None else make_game_version(versionstr)
        info("genie_source: raw size = %.1f MiB", len(raw) / 1048576.0)
        src = cls(raw, gv, fd)
        src._scan()
        src._summary()
        return src

    @classmethod
    def from_compressed(cls, compressed: bytes, game_version,
                        name: str = "empires.dat") -> "GenieSource":
        """
        Build a source from already-read compressed .dat bytes (so callers
        that hold a fslike file object -- e.g. ``get_gamespec`` -- need not
        resolve a real filesystem path). The decompressed bytes are written to
        the same tmpfile cache as ``from_dat`` and mmap'd read-only.
        """
        cache_env = os.environ.get("OPENAGE_DAT_RAW_CACHE")
        if cache_env and cache_env != "0":
            cache_path = cache_env
        else:
            cache_path = f"/tmp/openage_dat_raw.{name}.bin"

        reuse = cache_env != "0" and os.path.isfile(cache_path)
        if not reuse:
            raw = zlib.decompress(compressed, -15)
            _check_versionstr(raw, name)
            if cache_env != "0":
                try:
                    with open(cache_path, "wb") as f:
                        f.write(raw)
                    info("genie_source: cached raw to %s", cache_path)
                except OSError as exc:
                    warn("genie_source: could not write raw cache %s: %s",
                         cache_path, exc)
            del raw
        else:
            info("genie_source: reusing cached raw at %s", cache_path)
        del compressed

        fd = open(cache_path, "rb")
        try:
            mm = mmap.mmap(fd.fileno(), 0, access=mmap.ACCESS_READ)
        except ValueError:
            fd.seek(0)
            data = fd.read()
            fd.close()
            mm = mmap.mmap(-1, len(data))
            mm.write(data)
            mm.seek(0)
            fd = None
        _check_versionstr(mm, name)

        gv = game_version
        info("genie_source: raw size = %.1f MiB", len(mm) / 1048576.0)
        src = cls(mm, gv, fd)
        src._scan()
        src._summary()
        return src


    # ------------------------------------------------------------------ #
    # the offset-recording walk (mirrors schema_probe.Probe, no ValueMembers)
    # ------------------------------------------------------------------ #
    def _scan(self) -> None:
        from openage.convert.value_object.read.media.datfile.empiresdat import (
            EmpiresDatWrapper,
        )
        self._walk_struct(EmpiresDatWrapper, 0, _NS(), [])

    def _walk_struct(self, cls, offset, values, path):
        start_offset = offset
        members = cls.get_data_format(self.gv, allowed_modes=None,
                                      flatten_includes=True)

        stop = False
        for entry in members:
            _, export, var_name, storage_type, var_type = entry
            if stop:
                continue
            if isinstance(var_type, IncludeMembers):
                continue
            if isinstance(var_type, (SubdataMember, MultisubtypeMember)):
                offset, stop = self._walk_array(
                    var_type, export, var_name, offset, values, cls, path)
                continue
            offset, stop, val = self._read_primitive(
                var_type, export, var_name, offset, values)
            if val is not None and var_name:
                setattr(values, var_name, val)

        self._maybe_record(cls, start_offset, values, path)
        return offset

    def _maybe_record(self, cls, offset, values, path) -> None:
        if not path:
            return
        var_name, idx = path[-1]

        if var_name == "units":
            # base (civs[0]) vs per-civ override; key by id0
            civ_idx = None
            for pn, pi in path:
                if pn == "civs":
                    civ_idx = pi
            id0 = getattr(values, "id0", None)
            if id0 is None:
                return
            if civ_idx == 0:
                self.units[id0] = RecordHandle(offset, cls)
            elif civ_idx is not None and civ_idx > 0:
                self.civ_units[(civ_idx, id0)] = RecordHandle(offset, cls)
            return

        cat = _PATH_CATEGORIES.get(var_name)
        if cat is None:
            return
        table: dict = getattr(self, cat)
        table[idx] = RecordHandle(offset, cls)

    def _read_primitive(self, var_type, export, var_name, offset, values):
        if isinstance(var_type, ReadMember):
            struct_type = var_type.raw_type
            try:
                data_count = var_type.get_length(values)
            except (ValueError, AttributeError):
                data_count = 1
        elif isinstance(var_type, str):
            m = VARARRAY_MATCH.match(var_type)
            if m:
                struct_type = m.group(1)
                if struct_type == "char":
                    struct_type = "char[]"
                data_count = m.group(2)
                if INTEGER_MATCH.match(data_count):
                    data_count = int(data_count)
                else:
                    data_count = getattr(values, data_count, 0)
            else:
                struct_type = var_type
                data_count = 1
        else:
            raise TypeError(f"unknown member definition {var_type!r} for {var_name}")

        if data_count == "any_length":
            raise RuntimeError(
                f"member {var_name!r} has any_length; source needs a concrete length")

        # local copy to avoid importing STRUCT_TYPE_LOOKUP from genie_structure
        from openage.convert.value_object.read.genie_structure import STRUCT_TYPE_LOOKUP
        symbol = STRUCT_TYPE_LOOKUP[struct_type]
        fmt = f"< {data_count}{symbol}"
        size = struct.calcsize(fmt)

        if export == SKIP:
            if size >= 0:
                return offset + size, False, None
            return offset, False, None

        chunk = self.raw[offset:offset + size]
        if len(chunk) < size:
            raise RuntimeError(
                f"ran off end of raw at {var_name!r}: need {size} bytes at "
                f"0x{offset:x}, have {len(chunk)}")
        result = struct.unpack_from(fmt, self.raw, offset)
        offset += size

        val = result[0] if (not isinstance(result, tuple) or len(result) == 1) else result
        if symbol == "s":
            raw_str = result[0]
            try:
                val = raw_str.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
            except Exception:
                val = repr(raw_str)

        stop = False
        if isinstance(var_type, ContinueReadMember):
            raw_val = result[0] if isinstance(result, tuple) else result
            if var_type.entry_hook(raw_val) == ContinueReadMemberResult.ABORT:
                stop = True

        return offset, stop, val

    def _walk_array(self, var_type, export, var_name, offset, parent_values,
                    parent_cls, path):
        try:
            list_len = var_type.get_length(parent_values)
        except (ValueError, AttributeError):
            list_len = 0
        if list_len == "any_length":
            raise RuntimeError(f"array {var_name!r} is any_length; cannot scan")

        # capture passed_args from the parent so a lazily-rebuilt record has them
        args_snapshot: dict[str, Any] = {}
        passed = getattr(var_type, "passed_args", None)
        if passed:
            keys = {passed} if isinstance(passed, str) else set(passed)
            for a in keys:
                if hasattr(parent_values, a):
                    args_snapshot[a] = getattr(parent_values, a)

        base_child_values = _NS()
        if passed:
            for a in args_snapshot:
                setattr(base_child_values, a, args_snapshot[a])

        offset_lookup = None
        if var_type.offset_to:
            ptr_name = var_type.offset_to[0]
            offset_lookup = getattr(parent_values, ptr_name, None)
            pred = var_type.offset_to[1]

        is_multi = isinstance(var_type, MultisubtypeMember) \
            and getattr(var_type, "subtype_definition", None) is not None

        for i in range(list_len):
            if offset_lookup is not None:
                ptr = offset_lookup[i] if i < len(offset_lookup) else 0
                if not pred(ptr):
                    continue

            child_values = _NS(**base_child_values.__dict__)

            if is_multi:
                sub_export, sub_name, sub_st, sub_var_type = var_type.subtype_definition
                off2, _, tag_val = self._read_primitive(
                    sub_var_type, sub_export, sub_name, offset, child_values)
                offset = off2
                if sub_name:
                    setattr(child_values, sub_name, tag_val)
                raw_tag = tag_val[0] if isinstance(tag_val, tuple) else tag_val
                tag_name = sub_var_type.entry_hook(raw_tag)
                child_cls = var_type.class_lookup.get(tag_name)
                if child_cls is None:
                    raise RuntimeError(
                        f"multisubtype {var_name!r}: no class for tag "
                        f"{raw_tag!r} ({tag_name!r})")
            else:
                child_cls = var_type.class_lookup[None]

            child_path = path + [(var_name, i)]
            offset = self._walk_struct(
                child_cls, offset, child_values, child_path)

        return offset, False

    # ------------------------------------------------------------------ #
    def _summary(self) -> None:
        info("genie_source: offset tables -> "
             "units=%d unit_headers=%d graphics=%d sounds=%d techs=%d "
             "terrains=%d effect_bundles=%d terrain_restrictions=%d "
             "player_colors=%d civs=%d civ_units=%d "
             "age_conn=%d bldg_conn=%d unit_conn=%d tech_conn=%d",
             len(self.units), len(self.unit_headers), len(self.graphics),
             len(self.sounds), len(self.techs), len(self.terrains),
             len(self.effect_bundles), len(self.terrain_restrictions),
             len(self.player_colors), len(self.civs), len(self.civ_units),
             len(self.age_connections), len(self.building_connections),
             len(self.unit_connections), len(self.tech_connections))


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    dat_path = argv[1]
    if not os.path.isfile(dat_path):
        print(f"error: dat file not found: {dat_path}", file=__import__("sys").stderr)
        return 1
    src = GenieSource.from_dat(dat_path)
    print("\n" + "=" * 64)
    print(" GenieSource offset tables")
    print("=" * 64)
    rows = [
        ("units (base, civs[0])", len(src.units)),
        ("unit_headers", len(src.unit_headers)),
        ("civ_units (overrides)", len(src.civ_units)),
        ("graphics", len(src.graphics)),
        ("sounds", len(src.sounds)),
        ("techs (researches)", len(src.techs)),
        ("terrains", len(src.terrains)),
        ("effect_bundles", len(src.effect_bundles)),
        ("terrain_restrictions", len(src.terrain_restrictions)),
        ("player_colors", len(src.player_colors)),
        ("civs", len(src.civs)),
        ("age_connections", len(src.age_connections)),
        ("building_connections", len(src.building_connections)),
        ("unit_connections", len(src.unit_connections)),
        ("tech_connections", len(src.tech_connections)),
    ]
    for name, cnt in rows:
        print(f"  {name:<28} {cnt:>10,}")
    print("=" * 64)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main(sys.argv))
