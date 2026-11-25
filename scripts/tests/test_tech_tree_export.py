"""Regressions for technology gates, civilization isolation and bounded DAT reads."""

from collections import defaultdict
from dataclasses import dataclass
import gzip
import io
import json
from pathlib import Path
import sys
from types import SimpleNamespace as Record
import weakref
import zlib

import pytest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parents[2]))
from tech_tree.catalog import Catalog, base_age, eligible_technologies, visible_requirements, write_json
from tech_tree.reader import CHUNK_SIZE, inflate, read_dat


def technology(required=(), minimum=0, civ=-1, effect_id=-1):
    return Record(required_techs=required, required_tech_count=minimum, civ=civ,
                  effect_id=effect_id, research_locations=[])


def test_at_least_n_gate_and_building_presence():
    techs = [technology(), technology(minimum=1), technology([0, 1], 2), technology([0, 1], 1)]
    assert eligible_technologies(techs, 13, set()) == {0, 3}
    assert eligible_technologies(techs, 13, set(), seeds={1}) == {0, 1, 2, 3}


def test_disabled_tech_and_foreign_civilization_do_not_leak():
    techs = [technology(), technology([0], 1), technology(civ=44), technology([2], 1)]
    assert eligible_technologies(techs, 13, {1}) == {0}
    assert eligible_technologies(techs, 44, {1}) == {0, 2, 3}


def test_cycles_and_repeated_slots_do_not_satisfy_gates():
    techs = [technology([1], 1), technology([0], 1), technology(), technology([2, 2], 2)]
    assert eligible_technologies(techs, 13, set()) == {2}


def test_age_uses_own_civilization_optional_prerequisites():
    techs = [technology() for _ in range(105)]
    techs[10] = technology([101], 1, civ=44)
    techs[11] = technology([101], 1)
    techs[12] = technology([102, 11, 10], 2)
    assert base_age(12, techs, civ_id=13) == 3
    assert base_age(12, techs, civ_id=44) == 2


def test_hidden_prerequisites_expand_into_overview_edges():
    techs = [technology(), technology([0], 1), technology([1], 1)]
    assert visible_requirements(1, techs, {0, 2}) == {"t0"}


def test_presence_unlocks_and_regional_setup_enable():
    catalog = Catalog.__new__(Catalog)
    catalog.data = Record(techs=[technology(), technology(minimum=1), technology([1], 1), technology([0], 1)],
                          effects=[Record(effect_commands=[Record(type=2, a=1, b=1)])])
    catalog.data.techs[2].effect_id = 0
    catalog.enable_techs = defaultdict(set, {1: {2}})
    catalog.upgrade_techs = defaultdict(set)
    building = Record(enabled=1, disabled=0, building=Record(tech_id=1, stack_unit_id=-1))
    unit = Record(enabled=0, disabled=0, building=None)
    civ = Record(units=[building, unit])
    catalog.data.techs[0].effect_id = 1
    catalog.data.effects.append(Record(effect_commands=[Record(type=102, d=3)]))
    possible, active, disabled = catalog.availability(13, civ, [])
    assert possible == {0, 1, 2}
    assert active == {0, 1}
    assert disabled == {3}
    possible, _, disabled = catalog.availability(13, civ, [Record(type=8, a=3, b=12, d=1)])
    assert 3 in possible and 3 not in disabled


def test_upgrade_source_must_belong_to_same_reachable_effect():
    catalog = Catalog.__new__(Catalog)
    catalog.data = Record(techs=[technology(civ=13, effect_id=0), technology(civ=44, effect_id=1)],
                          effects=[Record(effect_commands=[Record(type=3, a=1, b=3)]),
                                   Record(effect_commands=[Record(type=3, a=2, b=3)])])
    catalog.enable_techs = defaultdict(set)
    catalog.upgrade_techs = defaultdict(set, {3: {0, 1}})
    unit = lambda enabled: Record(enabled=enabled, disabled=0, building=None)
    _, active, _ = catalog.availability(13, Record(units=[None, unit(0), unit(1), unit(0)]), [])
    assert 3 not in active


def test_streaming_json_is_lossless_and_deterministic(tmp_path):
    @dataclass
    class Example:
        values: list
    target = tmp_path / "record.json.gz"
    first = write_json(target, Example([1, None, {"中文": "原始值"}]))
    assert json.loads(gzip.decompress(target.read_bytes())) == {"values": [1, None, {"中文": "原始值"}]}
    assert write_json(target, Example([1, None, {"中文": "原始值"}])) == first
    assert not target.with_suffix(".gz.tmp").exists()


def test_inflater_bounds_expansion_chunks():
    class Destination(io.BytesIO):
        def write(self, chunk):
            assert len(chunk) <= CHUNK_SIZE
            return super().write(chunk)
    raw = b"x" * (5 * CHUNK_SIZE)
    compressed = zlib.compress(raw, wbits=-15)
    destination = Destination()
    inflate(io.BytesIO(compressed), destination)
    assert destination.getvalue() == raw


@pytest.mark.parametrize("tail", [b"", b"trailing"])
def test_inflater_rejects_corruption(tail):
    compressed = zlib.compress(b"data", wbits=-15)
    source = compressed[:-1] if not tail else compressed + tail
    with pytest.raises(ValueError):
        inflate(io.BytesIO(source), io.BytesIO())


def test_reader_indexes_without_retaining_civilizations(tmp_path, monkeypatch):
    pytest.importorskip("genieutils")
    from genieutils.civ import Civ
    from genieutils.datfile import DatFile
    alive = weakref.WeakSet()
    peak = 0
    class Civilization:
        def __init__(self, value):
            nonlocal peak
            self.value = value
            alive.add(self)
            peak = max(peak, len(alive))
    def civ_from_bytes(cls, reader):
        return Civilization(reader.read_int_8())
    def dat_from_bytes(cls, reader):
        return Record(civs=reader.read_class_array(Civ, 10), version="test")
    monkeypatch.setattr(Civ, "from_bytes", classmethod(civ_from_bytes))
    monkeypatch.setattr(DatFile, "from_bytes", classmethod(dat_from_bytes))
    source = tmp_path / "example.dat"
    source.write_bytes(zlib.compress(bytes(range(10)), wbits=-15))
    with read_dat(source) as (data, provenance):
        assert not alive
        assert peak == 1
        assert len(data.civs) == 10
        assert data.civs[3].value == 3
        assert not alive
        assert provenance["parsed_bytes"] == 10


def test_building_icon_does_not_use_unrelated_unit_icon(tmp_path):
    from PIL import Image
    from tech_tree.catalog import IconExporter

    root = tmp_path / "original"
    icons = root / "resources/_common/wpfg/resources/uniticons"
    icons.mkdir(parents=True)
    Image.new("RGBA", (4, 4), "red").save(icons / "001_50730.png")
    exporter = IconExporter(root, tmp_path / "exported")
    assert exporter.icon(1, "building") is None
    assert exporter.icon(1, "unit") == "icons/001_50730.png"


def test_original_dds_icon_cache_tracks_source_and_output(tmp_path):
    from PIL import Image
    from tech_tree.catalog import IconExporter

    root = tmp_path / "original"
    source = root / "widgetui/textures/ingame/buildings/001_house.DDS"
    source.parent.mkdir(parents=True)
    Image.new("RGBA", (4, 4), "red").save(source, format="DDS")
    output = tmp_path / "exported"
    name = IconExporter(root, output).icon(1, "building")
    target = output / name
    version = target.stat().st_mtime_ns
    assert IconExporter(root, output).icon(1, "building") == name
    assert target.stat().st_mtime_ns == version
    Image.new("RGBA", (4, 4), "blue").save(source, format="DDS")
    IconExporter(root, output).icon(1, "building")
    with Image.open(target) as decoded:
        assert decoded.getpixel((0, 0)) == (0, 0, 255, 255)
    target.write_bytes(b"damaged")
    IconExporter(root, output).icon(1, "building")
    with Image.open(target) as decoded:
        assert decoded.getpixel((0, 0)) == (0, 0, 255, 255)


def test_catalog_uses_shared_icon_export_without_calling_catalog_export(tmp_path):
    from PIL import Image
    from tech_tree.catalog import IconExporter

    root = tmp_path / "original"
    source = root / "widgetui/textures/ingame/units/001_villager.dds"
    source.parent.mkdir(parents=True)
    Image.new("RGBA", (4, 4), "red").save(source, format="DDS")
    catalog = Catalog.__new__(Catalog)
    IconExporter.__init__(catalog, root, tmp_path / "exported")
    assert catalog.icon(1, "unit") == "icons/unit-001.png"
