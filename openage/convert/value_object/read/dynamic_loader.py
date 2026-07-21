# Copyright 2014-2026 the openage authors. See copying.md for legal info.

# TODO pylint: disable=C,R

"""
Lazy mapping of a struct's ValueMembers.

A ``DynamicLoader`` is a drop-in replacement for the ``dict[str, ValueMember]``
that ``ConverterObject`` normally holds as ``self.members``: it supports the
full mapping protocol (``__getitem__``, ``__contains__``, ``keys``/``items``/
``values``, ``update``, ``get``, ``__len__``, ``__iter__``) so every consumer
of a ``ConverterObject`` -- ``get_member``, ``has_member``, ``diff``,
``add_member`` -- works unchanged. The difference: it does not materialize the
struct's ValueMember subtree until a consumer first touches it, and ``evict()``
drops the built dict so an LRU (the lazy stores in T3) can bound peak RSS.

The subtree is rebuilt on demand from the record's byte offset via
``cls(**args).read(raw, offset, game_version)`` -- which is self-contained per
struct (dynamic lengths and ``passed_args`` resolve within the single read).
``raw`` is the mmap'd decompressed .dat held by ``GenieSource``; ``args`` carries
``passed_args`` captured during the offset-table pre-pass (e.g. ``terrain_count``
for ``TerrainRestriction``).

Extra members added via ``update()`` (e.g. ``unit_commands`` grafted onto a unit
by ``extract_genie_units``) are kept separate from the dat-built dict so they
survive ``evict()`` and are re-merged on the next build.
"""
from __future__ import annotations
import os
import typing
from collections import OrderedDict

if typing.TYPE_CHECKING:
    from openage.convert.value_object.read.genie_structure import GenieStructure
    from openage.convert.value_object.init.game_version import GameVersion
    from openage.convert.value_object.read.value_members import ValueMember


# --------------------------------------------------------------------------- #
# global LRU over built DynamicLoaders (T5). Without this, every record a
# consumer touches stays built for the whole run -- the processing/export
# phases would re-materialize the full tree and OOM again. The registry caps
# how many subtrees are alive at once; evicted loaders rebuild on next access.
# --------------------------------------------------------------------------- #
def _lru_cap() -> int:
    """Max built loaders kept alive.

    Env ``OPENAGE_LAZY_CACHE_SIZE`` overrides; default 256 keeps DE2/BfG convert
    peak RSS under the 500 MiB P2 budget (validated: 256 -> 459 MiB peak;
    2048 -> 536 MiB, which fails P2). Lower trades memory for rebuild churn;
    raise only on machines with headroom.
    """
    try:
        return max(8, int(os.environ.get("OPENAGE_LAZY_CACHE_SIZE", "256")))
    except ValueError:
        return 256


_LRU: "OrderedDict[int, DynamicLoader]" = OrderedDict()


def lru_stats() -> tuple[int, int]:
    """Return (built_count, cap) for instrumentation."""
    return len(_LRU), _lru_cap()


def lru_clear() -> None:
    """Evict every built loader (release all cached subtrees)."""
    for loader in list(_LRU.values()):
        loader.evict()
    _LRU.clear()


class DynamicLoader:
    """
    Lazy member mapping backed by a (offset, cls, args) handle into ``raw``.

    Builds the ValueMember subtree on first access, caches it, and supports
    ``evict()`` so a bounded cache can free it without losing ``update()``-ed
    extras.
    """

    __slots__ = ('offset', 'cls', 'args', 'raw', 'game_version',
                 'extra', 'post_build', 'pinned', '_built')

    def __init__(
        self,
        offset: int,
        cls: type,
        args: dict,
        raw,
        game_version: "GameVersion",
        extra: dict = None,
        post_build: typing.Callable[[dict, "DynamicLoader"], None] = None,
        pinned: bool = False,
    ):
        self.offset = offset
        self.cls = cls
        self.args = args
        self.raw = raw
        self.game_version = game_version

        # members added via update() after construction (e.g. unit_commands).
        # kept across evictions; re-merged on build.
        self.extra = dict(extra) if extra else {}

        # optional hook run after read() builds the dict, before the extra
        # merge. Encapsulates per-category extract mutations (e.g. pop the
        # civ's eager ``units`` array and replace it with a lazy container,
        # turn ``attacks``/``armors`` into containers, graft ``unit_commands``).
        # It may mutate ``built`` in place and/or stash persistent results in
        # ``loader.extra`` (which survive evict and are re-merged below).
        self.post_build = post_build

        # Pinned loaders are never registered with the global LRU and thus
        # never evicted. Use for small-but-expensive-to-rebuild records (e.g.
        # civs: evicting forces re-reading ~2300 unit overrides).
        self.pinned = pinned

        # cached built dict[str, ValueMember]; None until first access or after
        # evict().
        self._built: dict[str, "ValueMember"] | None = None

    # ------------------------------------------------------------------ #
    def _build(self) -> dict[str, "ValueMember"]:
        """Materialize the ValueMember subtree from the offset, cache it."""
        if self._built is not None:
            return self._built

        inst = self.cls(**self.args)
        _, members = inst.read(self.raw, self.offset, self.game_version)

        built: dict[str, "ValueMember"] = {}
        for member in members:
            built[member.name] = member

        if self.post_build is not None:
            self.post_build(built, self)

        if self.extra:
            built.update(self.extra)

        self._built = built

        if not self.pinned:
            _LRU[id(self)] = self
            _enforce_lru_cap()

        return built

    def evict(self) -> None:
        """
        Drop the built member dict so its memory can be reclaimed. ``extra``
        members are preserved; the subtree is rebuilt on next access.
        """
        self._built = None
        _LRU.pop(id(self), None)

    @property
    def is_built(self) -> bool:
        return self._built is not None

    # ------------------------------------------------------------------ #
    # mapping protocol -- every access goes through _build() (cached)
    # ------------------------------------------------------------------ #
    def __getitem__(self, key) -> "ValueMember":
        return self._build()[key]

    def __contains__(self, key) -> bool:
        return key in self._build()

    def __iter__(self):
        return iter(self._build())

    def __len__(self) -> int:
        return len(self._built) if self._built is not None else \
            len(self.extra) + self._member_count_estimate()

    def keys(self):
        return self._build().keys()

    def values(self):
        return self._build().values()

    def items(self):
        return self._build().items()

    def get(self, key, default=None) -> "ValueMember":
        return self._build().get(key, default)

    def update(self, mapping) -> None:
        """Add/override members. Survives evict() (stored in ``extra``)."""
        if hasattr(mapping, "items"):
            entries = mapping.items()
        else:
            entries = mapping
        for key, value in entries:
            self.extra[key] = value
            if self._built is not None:
                self._built[key] = value

    def _member_count_estimate(self) -> int:
        """Cheap upper-bound member count from the schema, used only for
        ``__len__`` before a build is forced (e.g. ``len(obj.members)``)."""
        try:
            members = self.cls.get_data_format(
                self.game_version, allowed_modes=None, flatten_includes=True)
        except Exception:
            return 0
        return sum(1 for entry in members if entry[2] is not None)

    def __repr__(self) -> str:
        return f"DynamicLoader<{self.cls.__name__}@0x{self.offset:x} " \
               f"{'built' if self._built is not None else 'lazy'}>"


def _enforce_lru_cap() -> None:
    """Evict oldest built loaders until the registry is within the cap."""
    cap = _lru_cap()
    while len(_LRU) > cap:
        _key, loader = _LRU.popitem(last=False)
        # loader.evict() would re-touch _LRU; drop its cache directly.
        loader._built = None  # noqa: SLF001
