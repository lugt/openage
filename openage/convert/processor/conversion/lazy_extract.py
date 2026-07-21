# Copyright 2014-2026 the openage authors. See copying.md for legal info.

"""
Lazy extraction: populate the dataset stores from ``GenieSource`` offset tables
without materializing the genie tree.

Each ``extract_*_lazy`` mirrors the legacy ``extract_*`` in
``processor/conversion/{aoc,de2}/processor.py`` but, instead of iterating the
fully-materialized ``gamespec`` ValueMember tree (the OOM root cause), it wraps
each record's byte-offset handle in a ``DynamicLoader`` and stores a lightweight
``GenieObject``. The ValueMember subtree is built on first access (cached,
LRU-bounded in T5); ``post_build`` hooks encapsulate the per-category mutations
the legacy extract performed eagerly (turn ``attacks``/``armors`` into
containers, graft ``unit_commands``, swap a civ's eager ``units`` array for a
lazy container, pop ``effects`` into a separate lazy store).

Consumer code (``obj["m"].value``, ``a.diff(b)``, ``__getitem__``) is unchanged.
"""
from __future__ import annotations
import typing
from collections.abc import Mapping

from openage.convert.value_object.read.dynamic_loader import DynamicLoader
from openage.convert.value_object.read.value_members import (
    ArrayMember, ContainerMember, StorageType,
)

if typing.TYPE_CHECKING:
    from openage.convert.entity_object.conversion.aoc.genie_object_container \
        import GenieObjectContainer
    from openage.convert.value_object.read.source import GenieSource


# --------------------------------------------------------------------------- #
# civ "units" member: a lazy id0 -> unit-subtree mapping
# --------------------------------------------------------------------------- #
class _LazyUnitContainer(Mapping):
    """
    Mapping ``{id0_value: unit ValueMember}`` that builds each civ-unit
    override on first access. Drop-in for the dict that
    ``ArrayMember.get_container("id0")`` produces, but never materializes all
    overrides at once -- ``civ["units"][id0]`` builds that one record; ``id0 in
    civ["units"]`` / ``.keys()`` are cheap handle lookups.

    A ``collections.abc.Mapping`` so ``ContainerMember`` accepts it directly as
    its backing dict.
    """

    __slots__ = ('_src', '_civ_idx', '_handles', '_cache')

    def __init__(self, src: "GenieSource", civ_idx: int,
                 handles: dict[int, "object"]):
        self._src = src
        self._civ_idx = civ_idx
        self._handles = handles          # {id0: RecordHandle}
        self._cache: dict = {}

    def _build_one(self, id0):
        cached = self._cache.get(id0)
        if cached is not None:
            return cached
        h = self._handles[id0]
        from openage.convert.value_object.read.value_members import IDMember
        extra = _unit_type_extra(h, IDMember)
        dl = DynamicLoader(h.offset, h.cls, h.args, self._src.raw, self._src.gv,
                           extra=extra)
        # ContainerMember wraps the lazy member dict, mirroring a unit struct:
        # ``civ["units"][id0]["member"].value`` resolves through the loader.
        unit = ContainerMember(f"unit_{id0}", dl)
        self._cache[id0] = unit
        return unit

    def __getitem__(self, id0):
        if id0 not in self._handles:
            raise KeyError(id0)
        return self._build_one(id0)

    def __iter__(self):
        return iter(self._handles)

    def __len__(self) -> int:
        return len(self._handles)


def _civ_units_by_civ(src: "GenieSource") -> dict[int, dict[int, "object"]]:
    """Index civ_units once: {civ_idx: {id0: RecordHandle}} (O(n), not O(n*civs))."""
    by_civ: dict[int, dict[int, "object"]] = {}
    for (c, id0), h in src.civ_units.items():
        by_civ.setdefault(c, {})[id0] = h
    return by_civ


def _unit_type_extra(h, id_member_cls):
    """Build the ``{"unit_type": IDMember(...)}`` extra for a unit handle.

    The civ.units multisubtype tag (unit_type) is read at the array level and
    stashed on the record handle (``h.tag``); legacy injects it as a member on
    each unit object, so consumers doing ``unit["unit_type"].value`` find it.
    For base units (civs[0]) the tag is always present; the struct itself does
    not carry the member.
    """
    tag = getattr(h, "tag", None)
    if tag is None:
        return None
    return {"unit_type": id_member_cls("unit_type", tag)}



# --------------------------------------------------------------------------- #
# post_build hooks
# --------------------------------------------------------------------------- #
def _unit_post_build(src: "GenieSource", unit_id: int):
    """
    Returns a post_build hook for a base unit that mirrors DE2
    ``extract_genie_units``: turn ``attacks``/``armors`` into containers keyed
    by ``type_id`` (merging duplicate ``amount``), and graft ``unit_commands``
    from the unit header (ActionUnits with type>=40) or an empty array.
    """
    def hook(built: dict, loader: "DynamicLoader") -> None:
        # attacks / armors -> containers (cached in extra so they survive evict)
        for name in ("attacks", "armors"):
            if name in built and name not in loader.extra:
                raw = built.get(name)
                if raw is not None:
                    loader.extra[name] = raw.get_container("type_id",
                                                            merge_field="amount")
        # unit_commands: grafted from unit_headers (not present on the struct).
        # unit_type lives in loader.extra (injected by extract), not built yet
        # (post_build runs before the extra merge).
        if "unit_commands" not in built and "unit_commands" not in loader.extra:
            ut_member = loader.extra.get("unit_type")
            unit_type = ut_member.value if ut_member is not None else 0
            if unit_type >= 40 and unit_id in src.unit_headers:
                uh = src.unit_headers[unit_id]
                uh_dl = DynamicLoader(uh.offset, uh.cls, uh.args,
                                      src.raw, src.gv)
                loader.extra["unit_commands"] = uh_dl["unit_commands"]
            else:
                loader.extra["unit_commands"] = ArrayMember(
                    "unit_commands", StorageType.CONTAINER_MEMBER, members=[])

    return hook


def _civ_post_build(src: "GenieSource", civ_idx: int, by_civ: dict):
    """
    Returns a post_build hook for a civ that mirrors ``extract_genie_civs``:
    pop the eager ``units`` array (freeing ~2300 built overrides) and replace
    it with a lazy container built from ``src.civ_units``. The civ's small
    members (team_bonus_id, tech_tree_id, resources, ...) stay cached.
    """
    def hook(built: dict, loader: "DynamicLoader") -> None:
        if "units" not in loader.extra:
            handles = by_civ.get(civ_idx, {})
            loader.extra["units"] = ContainerMember(
                "units", _LazyUnitContainer(src, civ_idx, handles))
        # drop the eager units array if still present (rebuilt on access)
        built.pop("units", None)

    return hook


def _effect_bundle_post_build(src: "GenieSource", bundle_id: int):
    """
    Returns a post_build hook for an effect bundle that mirrors
    ``extract_genie_effect_bundles``: pop ``effects`` (held separately as
    GenieEffectObjects) so they are not duplicated in the bundle members.
    Effects are built lazily into the bundle's ``effects`` store on access.
    """
    def hook(built: dict, loader: "DynamicLoader") -> None:
        built.pop("effects", None)

    return hook


def _graphic_post_build(src: "GenieSource", graphic_id: int):
    """
    Returns a post_build hook for a graphic mirroring ``extract_genie_graphics``
    DE2: mark ``exists=False`` when the slp is not in ``existing_graphics``.
    (Filename filtering happens at extract time, not here, since it decides
    whether the graphic is stored at all.)
    """
    def hook(built: dict, loader: "DynamicLoader") -> None:
        # exists flag is set on the GenieGraphic object after construction;
        # nothing to mutate in members here. Hook retained for symmetry.
        return None

    return hook


# --------------------------------------------------------------------------- #
# extract functions
# --------------------------------------------------------------------------- #
def extract_genie_units_lazy(src: "GenieSource",
                             full_data_set: "GenieObjectContainer") -> None:
    from openage.convert.entity_object.conversion.aoc.genie_unit \
        import GenieUnitObject
    from openage.convert.value_object.read.value_members import IDMember

    for unit_id, h in src.units.items():
        extra = _unit_type_extra(h, IDMember)
        dl = DynamicLoader(h.offset, h.cls, h.args, src.raw, src.gv,
                           extra=extra,
                           post_build=_unit_post_build(src, unit_id))
        unit = GenieUnitObject(unit_id, full_data_set, members=dl)
        full_data_set.genie_units[unit.get_id()] = unit


def extract_genie_techs_lazy(src: "GenieSource",
                             full_data_set: "GenieObjectContainer") -> None:
    from openage.convert.entity_object.conversion.aoc.genie_tech \
        import GenieTechObject

    for tech_id, h in src.techs.items():
        dl = DynamicLoader(h.offset, h.cls, h.args, src.raw, src.gv)
        tech = GenieTechObject(tech_id, full_data_set, members=dl)
        full_data_set.genie_techs[tech.get_id()] = tech


def extract_genie_effect_bundles_lazy(
    src: "GenieSource",
    full_data_set: "GenieObjectContainer",
) -> None:
    from openage.convert.entity_object.conversion.aoc.genie_effect \
        import GenieEffectBundle, GenieEffectObject

    for bundle_id, h in src.effect_bundles.items():
        # Eager-build this one bundle to extract its effects (bounded: one
        # bundle at a time; the built dict is released after the loop body).
        # The bundle's *members* stay lazy via a DynamicLoader whose post_build
        # pops ``effects`` so they are not duplicated in the member dict.
        inst = h.cls(**h.args)
        _, em = inst.read(src.raw, h.offset, src.gv)
        built_once: dict = {}
        for member in em:
            built_once[member.name] = member

        effects = {}
        raw_effects = built_once.get("effects")
        if raw_effects is not None:
            for idx, raw_effect in enumerate(raw_effects.value):
                effect = GenieEffectObject(idx, bundle_id, full_data_set,
                                           members=raw_effect.value)
                effects[idx] = effect

        dl = DynamicLoader(h.offset, h.cls, h.args, src.raw, src.gv,
                           post_build=_effect_bundle_post_build(src, bundle_id))
        bundle = GenieEffectBundle(bundle_id, effects, full_data_set, members=dl)
        full_data_set.genie_effect_bundles[bundle.get_id()] = bundle


def extract_genie_civs_lazy(src: "GenieSource",
                            full_data_set: "GenieObjectContainer") -> None:
    from openage.convert.entity_object.conversion.aoc.genie_civ \
        import GenieCivilizationObject

    by_civ = _civ_units_by_civ(src)
    for civ_id, h in src.civs.items():
        # Pinned: evicting a civ forces re-reading its ~2300 unit overrides
        # (read() is whole-struct). The post-build civ dict is small (members
        # + lazy units container), so 54 pinned civs are cheap.
        dl = DynamicLoader(h.offset, h.cls, h.args, src.raw, src.gv,
                           post_build=_civ_post_build(src, civ_id, by_civ),
                           pinned=True)
        civ = GenieCivilizationObject(civ_id, full_data_set, members=dl)
        full_data_set.genie_civs[civ.get_id()] = civ


def extract_genie_graphics_lazy(src: "GenieSource",
                                full_data_set: "GenieObjectContainer") -> None:
    from openage.convert.entity_object.conversion.aoc.genie_graphic \
        import GenieGraphic

    for graphic_id, h in src.graphics.items():
        dl = DynamicLoader(h.offset, h.cls, h.args, src.raw, src.gv)
        # filename filter (DE2): skip graphics with no filename. Build only the
        # members needed, then evict so the rest stays lazy.
        filename = dl["filename"].value.lower() if "filename" in dl else ""
        if not filename:
            dl.evict()
            continue
        graphic = GenieGraphic(graphic_id, full_data_set, members=dl)
        slp_id = dl["slp_id"].value if "slp_id" in dl else None
        if str(slp_id) not in full_data_set.existing_graphics:
            graphic.exists = False
        dl.evict()
        full_data_set.genie_graphics[graphic.get_id()] = graphic

    # Detect subgraphics: iterate all graphics, build each only for the
    # graphic_deltas read, then evict so we don't hold 13k built subtrees.
    for genie_graphic in full_data_set.genie_graphics.values():
        genie_graphic.detect_subgraphics()
        members = genie_graphic.members
        if isinstance(members, DynamicLoader):
            members.evict()


def extract_genie_sounds_lazy(src: "GenieSource",
                              full_data_set: "GenieObjectContainer") -> None:
    from openage.convert.entity_object.conversion.aoc.genie_sound \
        import GenieSound

    for sound_id, h in src.sounds.items():
        dl = DynamicLoader(h.offset, h.cls, h.args, src.raw, src.gv)
        sound = GenieSound(sound_id, full_data_set, members=dl)
        full_data_set.genie_sounds[sound.get_id()] = sound


def extract_genie_terrains_lazy(src: "GenieSource",
                                full_data_set: "GenieObjectContainer") -> None:
    from openage.convert.entity_object.conversion.aoc.genie_terrain \
        import GenieTerrainObject

    for terrain_id, h in src.terrains.items():
        dl = DynamicLoader(h.offset, h.cls, h.args, src.raw, src.gv)
        terrain = GenieTerrainObject(terrain_id, full_data_set, members=dl)
        full_data_set.genie_terrains[terrain.get_id()] = terrain


def extract_genie_restrictions_lazy(
    src: "GenieSource",
    full_data_set: "GenieObjectContainer",
) -> None:
    from openage.convert.entity_object.conversion.aoc.genie_terrain \
        import GenieTerrainRestriction

    for restriction_id, h in src.terrain_restrictions.items():
        dl = DynamicLoader(h.offset, h.cls, h.args, src.raw, src.gv)
        restriction = GenieTerrainRestriction(restriction_id, full_data_set,
                                              members=dl)
        full_data_set.genie_terrain_restrictions[restriction.get_id()] = restriction


def extract_connections_lazy(src: "GenieSource",
                             full_data_set: "GenieObjectContainer") -> None:
    from openage.convert.entity_object.conversion.aoc.genie_connection \
        import (GenieAgeConnection, GenieBuildingConnection,
                GenieUnitConnection, GenieTechConnection)

    stores_and_classes = (
        (src.age_connections, GenieAgeConnection, full_data_set.age_connections),
        (src.building_connections, GenieBuildingConnection,
         full_data_set.building_connections),
        (src.unit_connections, GenieUnitConnection,
         full_data_set.unit_connections),
        (src.tech_connections, GenieTechConnection,
         full_data_set.tech_connections),
    )

    for handle_table, cls, store in stores_and_classes:
        for conn_id, h in handle_table.items():
            dl = DynamicLoader(h.offset, h.cls, h.args, src.raw, src.gv)
            # Key by the connection's real ``id`` field (read from the lazy
            # loader), NOT the array index. Consumers (e.g. create_unit_lines)
            # look up connections by the unit/age id stored in *other*
            # connections' ``other_connected_ids``, so an index key would miss.
            real_id = dl["id"].value
            connection = cls(real_id, full_data_set, members=dl)
            store[connection.get_id()] = connection
