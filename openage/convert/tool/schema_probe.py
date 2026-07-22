# Copyright 2026 the openage authors. See copying.md for legal info.

"""
Schema-driven lightweight probe of a Genie .dat file.

Goal: count "how many of each thing" (civs, units, terrains, sounds, graphics,
research, effect bundles, ...) WITHOUT materializing the millions of
ValueMember objects the real reader builds -- that materialization is what
blows RSS past 10 GiB on the BfG DE2 dat.

This probe reuses the exact same struct schema
(``GenieStructure.get_data_format_members``) the real reader uses, but it walks
the raw bytes itself, keeping only:

  * one copy of the decompressed ``raw`` bytes (~40 MiB for DE2),
  * a tiny per-struct dict of the scalar values read so far (needed to resolve
    dynamic ``length="some_count"`` lookups and ``offset_to`` pointer arrays),
  * one global dict of instance counts keyed by struct class name.

So the working set stays in the tens of MiB regardless of how big the dat is.

The decompressed raw is cached to a file (default
``/tmp/openage_dat_raw.<basename>.bin``) so repeated probing does not re-run
zlib. Set ``OPENAGE_DAT_RAW_CACHE`` to override the cache path, or
``OPENAGE_DAT_RAW_CACHE=0`` to disable caching.

Run it directly:

    .venv/bin/python -m openage.convert.tool.schema_probe \\
        /home/gtx/dev/games/aoe2de/resources/_common/dat/empires2_x2_p1.dat

(it imports openage normally, so OPENAGE_BUILD_DIR / the bootstrap applies)
"""
from __future__ import annotations

import os
import re
import struct
import sys
import zlib
from collections import defaultdict

# Reuse the project's own schema + constants. Importing these does NOT read
# the dat file -- the schema is pure class definitions (with @cache).
from openage.convert.value_object.read.genie_structure import (
    STRUCT_TYPE_LOOKUP,
    GenieStructure,
)
from openage.convert.value_object.read.read_members import (
    ContinueReadMember,
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
# raw loading + caching
# --------------------------------------------------------------------------- #
def load_raw(dat_path: str) -> tuple[bytes, str]:
    """
    Read + zlib-decompress the .dat. Cache the decompressed bytes to a file
    so repeated probes skip the decompress (and so the raw is available for
    hexdump / binwalk / other tools).
    """
    name = os.path.basename(dat_path)
    cache_env = os.environ.get("OPENAGE_DAT_RAW_CACHE")
    if cache_env and cache_env != "0":
        cache_path = cache_env
    else:
        cache_path = f"/tmp/openage_dat_raw.{name}.bin"

    # If a usable cache exists AND it's newer than the source .dat, reuse it.
    if cache_env != "0" and os.path.isfile(cache_path):
        try:
            if os.path.getmtime(cache_path) >= os.path.getmtime(dat_path):
                with open(cache_path, "rb") as f:
                    raw = f.read()
                info("schema_probe: loaded cached raw (%d bytes) from %s",
                     len(raw), cache_path)
                # sanity: real dat starts with "VER x.y\0"
                _check_versionstr(raw, name)
                return raw, cache_path
            else:
                info("schema_probe: cache stale, re-decompressing")
        except OSError:
            pass

    with open(dat_path, "rb") as f:
        compressed = f.read()
    info("schema_probe: read %d compressed bytes from %s", len(compressed), dat_path)

    raw = zlib.decompress(compressed, -15)
    del compressed
    info("schema_probe: decompressed -> %d bytes", len(raw))

    _check_versionstr(raw, name)

    if cache_env != "0":
        try:
            with open(cache_path, "wb") as f:
                f.write(raw)
            info("schema_probe: cached raw to %s", cache_path)
        except OSError as exc:
            warn("schema_probe: could not write raw cache %s: %s", cache_path, exc)

    return raw, cache_path


def _check_versionstr(raw: bytes, name: str) -> None:
    vs = raw[:8].rstrip(b"\x00").decode("ascii", errors="ignore")
    info("schema_probe: %s versionstr = %r", name, vs)


# --------------------------------------------------------------------------- #
# game version
# --------------------------------------------------------------------------- #
def make_game_version(versionstr: str) -> GameVersion:
    """
    Build a GameVersion for AOE2DE, detecting the 'Battle for Greece' (VER 8.4)
    variant the same way gamedata.py does. We do NOT run the full version_detect
    pipeline (which hashes files, walks media_paths, loads modpack defs, etc.)
    because the schema only ever branches on:

        game_version.edition.game_id   (e.g. "AOE2DE")
        game_version.expansions        (tuple of GameExpansion)
        game_version.update_number     ("BfG" for the VER 8.4 variant)

    So we build the minimum object that satisfies those accesses, bypassing
    GameBase.__init__ (which wants media_paths / modpacks / version hashes
    we do not need here).
    """
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
    info("schema_probe: game_id=AOE2DE update=%s (bfg=%s)",
         gv.update_number, bfg)
    return gv


# --------------------------------------------------------------------------- #
# the lightweight walker
# --------------------------------------------------------------------------- #
class _NS:
    """
    Tiny attribute namespace. SubdataMember.get_length() does
    ``getattr(obj, length_def)`` to resolve dynamic lengths, so the values we
    accumulate while walking a struct must support attribute access (a plain
    dict does not). We set/read attributes via setattr/getattr and keep a
    __dict__ so external code that pokes it works too.
    """

    __slots__ = ("__dict__",)

    def __init__(self, **kw):
        self.__dict__.update(kw)

    def __contains__(self, key):
        return key in self.__dict__

    def get(self, key, default=None):
        return self.__dict__.get(key, default)


class Probe:
    """
    Walks raw bytes following the struct schema, counting instances per class
    without building ValueMember objects.

    State per active struct read: a plain dict `values` holding the scalar
    fields read so far (so dynamic lengths + offset_to lookups resolve). This
    dict is tiny (a struct has a few dozen scalars at most) and discarded once
    its enclosing read returns.
    """

    def __init__(self, raw: bytes, game_version: GameVersion):
        self.raw = raw
        self.gv = game_version
        # class name -> number of instances whose bytes we walked past
        self.counts: dict[str, int] = defaultdict(int)
        self.depth = 0

    # --- public entry ------------------------------------------------------- #
    def run(self, root_cls: type[GenieStructure]) -> None:
        self._walk_struct(root_cls, 0, _NS(), label="root")

    # --- core: walk one struct's members ------------------------------------ #
    def _walk_struct(
        self,
        cls: type[GenieStructure],
        offset: int,
        values: _NS,
        label: str,
    ) -> int:
        self.counts[cls.__name__] += 1
        self.depth += 1
        prefix = "  " * min(self.depth, 30)

        members = cls.get_data_format(self.gv, allowed_modes=None,
                                      flatten_includes=True)

        stop = False
        for entry in members:
            # entry is (is_parent, export, var_name, storage_type, var_type)
            _, export, var_name, storage_type, var_type = entry

            if stop:
                # remaining members absent in source data; consume nothing
                continue

            if isinstance(var_type, IncludeMembers):
                # flatten=True above should have inlined these; skip defensively
                continue

            if isinstance(var_type, (SubdataMember, MultisubtypeMember)):
                offset, stop = self._walk_array(
                    var_type, export, var_name, offset, values, cls,
                )
                continue

            # primitive (str c-type OR a ReadMember instance like EnumLookup)
            offset, stop, val = self._read_primitive(
                var_type, export, var_name, offset, values,
            )
            if val is not None and var_name:
                setattr(values, var_name, val)

        self.depth -= 1
        if self.depth <= 1:
            info("schema_probe: %-28s @0x%08x (now 0x%08x)  %s",
                 cls.__name__, 0, offset, prefix)
        return offset

    # --- primitive read ----------------------------------------------------- #
    def _read_primitive(
        self,
        var_type,
        export,
        var_name: str,
        offset: int,
        values: _NS,
    ) -> tuple[int, bool, object]:
        """Return (new_offset, stop_reading, value_or_None)."""
        if isinstance(var_type, ReadMember):
            struct_type = var_type.raw_type
            try:
                data_count = var_type.get_length(values)
            except (ValueError, AttributeError):
                data_count = 1
            is_custom = True
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
            is_custom = False
        else:
            raise TypeError(f"unknown member definition {var_type!r} for "
                            f"{var_name}")

        # Skip members: still consume bytes if they have a known size,
        # but for unknown-size members we cannot safely skip, so we just
        # leave offset alone (matches real reader's SKIP for primitives
        # where size is static -- SKIP still reads nothing because the real
        # reader only unpacks when export != SKIP). To keep alignment correct
        # we must advance by the static size even when skipping.
        if data_count == "any_length":
            # Can't determine size without an external length; bail with a clear
            # message rather than silently misaligning.
            raise RuntimeError(
                f"member {var_name!r} has any_length; probe needs a concrete "
                "length source -- this struct is not probe-friendly as-is."
            )

        symbol = STRUCT_TYPE_LOOKUP[struct_type]
        fmt = f"< {data_count}{symbol}"
        size = struct.calcsize(fmt)

        if export == SKIP:
            # static-size skip: advance only if size is knowable & positive
            if size >= 0:
                return offset + size, False, None
            return offset, False, None

        chunk = self.raw[offset:offset + size]
        if len(chunk) < size:
            raise RuntimeError(
                f"ran off end of raw at {var_name!r}: need {size} bytes at "
                f"0x{offset:x}, have {len(chunk)}"
            )
        result = struct.unpack_from(fmt, self.raw, offset)
        offset += size

        # Collapse to a single scalar for storage (arrays keep as tuple).
        val = result[0] if (not isinstance(result, tuple) or len(result) == 1) else result
        # char[] string -> decode
        if symbol == "s":
            raw_str = result[0]
            try:
                val = raw_str.split(b"\x00", 1)[0].decode("utf-8", errors="replace")
            except Exception:
                val = repr(raw_str)

        stop = False
        if is_custom and isinstance(var_type, ContinueReadMember):
            # entry_hook returns ABORT when value==0 -> stop reading further
            # members of this struct (e.g. UnitHeader.exists == 0 -> no
            # unit_commands follow). unpack_from always returns a tuple, so
            # extract the scalar first.
            raw_val = result[0] if isinstance(result, tuple) else result
            hooked = var_type.entry_hook(raw_val)
            from openage.convert.value_object.read.read_members import (
                ContinueReadMemberResult,
            )
            if hooked == ContinueReadMemberResult.ABORT:
                stop = True

        return offset, stop, val

    # --- array walk (Subdata / Multisubtype) -------------------------------- #
    def _walk_array(
        self,
        var_type,
        export,
        var_name: str,
        offset: int,
        parent_values: _NS,
        parent_cls,
    ) -> tuple[int, bool]:
        # resolve length
        try:
            list_len = var_type.get_length(parent_values)
        except (ValueError, AttributeError):
            list_len = 0
        if list_len == "any_length":
            raise RuntimeError(f"array {var_name!r} is any_length; cannot probe")

        # passed_args: copy named fields from parent into child values
        base_child_values: _NS = _NS()
        if getattr(var_type, "passed_args", None):
            args = var_type.passed_args
            if isinstance(args, str):
                args = {args}
            for a in args:
                if hasattr(parent_values, a):
                    setattr(base_child_values, a, getattr(parent_values, a))

        # offset_to: an array of pointers read earlier; filter entries whose
        # predicate is false (e.g. pointer==0 -> absent). Real reader still
        # reads sequentially; offset_to only gates, it does not seek. Mirror that.
        offset_lookup = None
        if var_type.offset_to:
            ptr_name = var_type.offset_to[0]
            offset_lookup = getattr(parent_values, ptr_name, None)
            pred = var_type.offset_to[1]

        # SubdataMember is a SUBCLASS of MultisubtypeMember (it calls super()
        # with subtype_definition=None and class_lookup={None: ref_type}), so
        # isinstance() lies: a SubdataMember would be treated as multisubtype.
        # Distinguish by checking for an actual subtype_definition.
        is_multi = isinstance(var_type, MultisubtypeMember) \
            and getattr(var_type, "subtype_definition", None) is not None

        for i in range(list_len):
            if offset_lookup is not None:
                ptr = offset_lookup[i] if i < len(offset_lookup) else 0
                if not pred(ptr):
                    continue

            child_values = _NS(**base_child_values.__dict__)

            if is_multi:
                # read the subtype tag, then dispatch on its looked-up name.
                # subtype_definition is (export, name, storage_type, ReadMember)
                sub_export, sub_name, sub_st, sub_var_type = var_type.subtype_definition
                off2, _, tag_val = self._read_primitive(
                    sub_var_type, sub_export, sub_name, offset, child_values,
                )
                offset = off2
                if sub_name:
                    setattr(child_values, sub_name, tag_val)
                # EnumLookupMember.entry_hook(raw_int) -> lookup name string
                raw_tag = tag_val[0] if isinstance(tag_val, tuple) else tag_val
                tag_name = sub_var_type.entry_hook(raw_tag)
                child_cls = var_type.class_lookup.get(tag_name)
                if child_cls is None:
                    raise RuntimeError(
                        f"multisubtype {var_name!r}: no class for tag "
                        f"{raw_tag!r} ({tag_name!r})"
                    )
            else:
                child_cls = var_type.class_lookup[None]

            offset = self._walk_struct(
                child_cls, offset, child_values,
                label=f"{var_name}[{i}]",
            )

        return offset, False


# --------------------------------------------------------------------------- #
# entry point
# --------------------------------------------------------------------------- #
def probe(dat_path: str) -> dict:
    raw, cache_path = load_raw(dat_path)
    versionstr = raw[:8].rstrip(b"\x00").decode("ascii", errors="ignore")
    gv = make_game_version(versionstr)

    info("schema_probe: raw size = %.1f MiB", len(raw) / 1048576.0)
    probe = Probe(raw, gv)

    # Top level is EmpiresDatWrapper(empiresdat=[EmpiresDat]).
    from openage.convert.value_object.read.media.datfile.empiresdat import (
        EmpiresDatWrapper,
    )
    probe.run(EmpiresDatWrapper)

    return dict(probe.counts)


def print_report(counts: dict) -> None:
    print("\n" + "=" * 64)
    print(" Genie .dat schema probe -- instance counts by struct class")
    print("=" * 64)
    # sort by count desc
    rows = sorted(counts.items(), key=lambda kv: (-kv[1], kv[0]))
    total = 0
    for name, cnt in rows:
        print(f"  {name:<40} {cnt:>10,}")
        total += cnt
    print("-" * 64)
    print(f"  {'TOTAL struct instances walked':<40} {total:>10,}")
    print("=" * 64 + "\n")


def main(argv: list[str]) -> int:
    if len(argv) < 2:
        print(__doc__)
        return 2
    dat_path = argv[1]
    if not os.path.isfile(dat_path):
        print(f"error: dat file not found: {dat_path}", file=sys.stderr)
        return 1
    counts = probe(dat_path)
    print_report(counts)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
