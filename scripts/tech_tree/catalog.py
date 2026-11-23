"""Read all civilizations and the complete technology graph from a local DE2 DAT.

The raw export preserves every parsed DAT field. The preview is a projection of
that catalog. It does not replace the game's nyan runtime modpack.
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import asdict, fields, is_dataclass
import gzip
import hashlib
import html
import io
import json
from pathlib import Path
import re
import shutil


AGE_TECHS = {104: 1, 101: 2, 102: 3, 103: 4}


def plain(text):
    return html.unescape(re.sub(r"<[^>]*>", "", text)).replace("\\n", "\n").strip()


def strings_at(root, language):
    result = {}
    directory = root / "resources" / language / "strings/key-value"
    # DLC string files can add and override strings in the base table.
    files = sorted(directory.glob("*.txt"), key=lambda p: (p.name != "key-value-strings-utf8.txt", p.name))
    for source in files:
        for line in source.read_text(encoding="utf-8-sig", errors="replace").splitlines():
            match = re.match(r'^\s*(\d+)\s+"(.*)"\s*$', line)
            if match:
                result[int(match[1])] = plain(match[2].replace(r'\"', '"'))
    return result


def encode_dataclass(value):
    """Expose one record's fields without recursively copying unit arrays."""
    if is_dataclass(value):
        return {field.name: getattr(value, field.name) for field in fields(value)}
    raise TypeError(f"Cannot encode {type(value).__name__}")


def write_json(path, data):
    """Atomic, deterministic JSON, optionally gzip-compressed."""
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    with temporary.open("wb") as target:
        if path.suffix == ".gz":
            stream = gzip.GzipFile(filename="", mode="wb", fileobj=target, mtime=0)
        else:
            stream = target
        with io.TextIOWrapper(stream, encoding="utf-8") as writer:
            json.dump(data, writer, ensure_ascii=False, separators=(",", ":"), allow_nan=False, default=encode_dataclass)
    temporary.replace(path)
    with path.open("rb") as source:
        return hashlib.file_digest(source, "sha256").hexdigest()


def costs(values):
    return {str(c.type): c.amount for c in values if 0 <= c.type <= 3 and c.flag and c.amount}


def eligible_technologies(techs, civ_id, disabled, seeds=()):
    """Resolve at-least-N prerequisites while allowing future research.

    This is civilization eligibility, not the player's current research state.
    Automatic building-presence records and all original prerequisites remain
    in the raw catalog and in the dependency graph.
    """
    possible = set(seeds) - set(disabled)
    candidates = {i for i, t in enumerate(techs) if t.civ in (-1, civ_id) and i not in disabled and t.required_tech_count >= 0}
    changed = True
    while changed:
        changed = False
        for i in sorted(candidates - possible):
            t = techs[i]
            required = {r for r in t.required_techs if r >= 0}
            if len(required & possible) >= t.required_tech_count:
                possible.add(i)
                changed = True
    return possible


def base_age(tech_id, techs, visiting=None, civ_id=None):
    """Earliest age satisfying the original at-least-N prerequisite gate."""
    if tech_id in AGE_TECHS:
        return AGE_TECHS[tech_id]
    visiting = set() if visiting is None else visiting
    if tech_id in visiting or not 0 <= tech_id < len(techs):
        return 1
    visiting.add(tech_id)
    tech = techs[tech_id]
    ages = sorted(base_age(i, techs, visiting, civ_id) for i in set(tech.required_techs)
                  if i >= 0 and (civ_id is None or techs[i].civ in (-1, civ_id)))
    visiting.remove(tech_id)
    count = tech.required_tech_count
    return ages[count - 1] if 0 < count <= len(ages) else 1


def visible_requirements(tech_id, techs, visible, visiting=None):
    """Collapse automatic records only for the overview's connector lines."""
    visiting = set() if visiting is None else visiting
    if tech_id in visiting or not 0 <= tech_id < len(techs):
        return set()
    if tech_id in visible:
        return {f"t{tech_id}"}
    visiting.add(tech_id)
    found = set()
    for previous in set(techs[tech_id].required_techs):
        if previous >= 0:
            found.update(visible_requirements(previous, techs, visible, visiting))
    visiting.remove(tech_id)
    return found


def modified_research(tech_id, tech, effects):
    """Apply direct civilization cost and research-time commands."""
    price = costs(tech.resource_costs)
    duration = tech.research_locations[0].research_time if tech.research_locations else 0
    for effect in effects:
        if effect.a != tech_id:
            continue
        if effect.type == 101 and 0 <= effect.b <= 3:
            key = str(effect.b)
            price[key] = int(effect.d if effect.c == 0 else price.get(key, 0) + effect.d)
        elif effect.type == 103:
            duration = effect.d if effect.c == 0 else duration + effect.d
    return price, duration


class Catalog:
    def __init__(self, data, root, output):
        self.data = data
        self.root = root
        self.output = output
        self.zh = strings_at(root, "zh")
        self.en = strings_at(root, "en")
        source = root / "resources/_common/dat/civilizations.json"
        self.metadata = json.loads(source.read_text(encoding="utf-8"))["civilization_list"] if source.is_file() else []
        self.icon_root = root / "resources/_common/wpfg/resources"
        self.icons = {}
        for directory in (self.icon_root / "uniticons", self.icon_root / "Icons"):
            if directory.is_dir():
                for icon in directory.rglob("*.png"):
                    self.icons.setdefault(icon.name, icon)
        self.dds_icons = {}
        for kind, folder in (("technology", "tech"), ("building", "buildings"), ("unit", "units")):
            for icon in (root / "widgetui/textures/ingame" / folder).glob("*"):
                match = re.match(r"^(\d+)_", icon.name)
                if match and icon.suffix.lower() == ".dds":
                    self.dds_icons.setdefault((kind, int(match[1])), icon)
        self.copied_icons = set()
        self.public = {
            i for i, t in enumerate(data.techs)
            if any(l.location_id > 0 and l.button_id > 0 for l in t.research_locations)
        }
        self.unit_connections = {c.id: c for c in data.tech_tree.unit_connections}
        self.building_connections = {c.id: c for c in data.tech_tree.building_connections}
        self.upgrade_techs = defaultdict(set)
        self.enable_techs = defaultdict(set)
        self.previous_units = defaultdict(set)
        for i, tech in enumerate(data.techs):
            for effect in self.effect(tech.effect_id):
                if effect.type == 2 and effect.b == 1 and effect.a >= 0:
                    self.enable_techs[effect.a].add(i)
                if effect.type == 3 and effect.a >= 0 and effect.b >= 0:
                    self.upgrade_techs[effect.b].add(i)
                    self.previous_units[effect.b].add(effect.a)
        self.diagnostics = {"missing_icons": set(), "missing_names": set()}

    def effect(self, identifier):
        return self.data.effects[identifier].effect_commands if 0 <= identifier < len(self.data.effects) else []

    def label(self, identifier, fallback):
        return self.zh.get(identifier) or self.en.get(identifier) or fallback

    def icon(self, icon_id, kind):
        # Prefer available UI PNGs, then decode the original numbered DDS icons.
        suffix = "50729" if kind == "technology" else "50730"
        options = [f"{icon_id:03d}_{suffix}.png"]
        if kind == "building":
            options.insert(0, f"{icon_id:03d}_50731.png")
        for name in options:
            source = self.icons.get(name)
            if source:
                destination = self.output / "icons" / name
                if name not in self.copied_icons:
                    destination.parent.mkdir(exist_ok=True)
                    shutil.copyfile(source, destination)
                    self.copied_icons.add(name)
                return f"icons/{name}"
        original = self.dds_icons.get((kind, icon_id))
        if original:
            from PIL import Image
            name = f"{kind}-{icon_id:03d}.png"
            if name not in self.copied_icons:
                destination = self.output / "icons" / name
                destination.parent.mkdir(exist_ok=True)
                with Image.open(original) as decoded:
                    decoded.save(destination)
                self.copied_icons.add(name)
            return f"icons/{name}"
        self.diagnostics["missing_icons"].add(f"{kind}:{icon_id}")
        return None

    def producer(self, identifier, civ):
        if 0 <= identifier < len(civ.units):
            unit = civ.units[identifier]
            if unit:
                return self.label(unit.language_dll_name, unit.name)
        return "其他"

    def technology_node(self, identifier, civ_id, tree_effects, possible, disabled):
        tech = self.data.techs[identifier]
        location = next(l for l in tech.research_locations if l.location_id > 0 and l.button_id > 0)
        age = base_age(identifier, self.data.techs, civ_id=civ_id)
        # Age advances are researched in the preceding age.
        if identifier in (101, 102, 103):
            age = AGE_TECHS[identifier] - 1
        price, duration = modified_research(identifier, tech, tree_effects)
        effects = [asdict(e) for e in self.effect(tech.effect_id)]
        return {
            "id": f"t{identifier}", "genie_id": identifier,
            "kind": "technology", "name": self.label(tech.language_dll_name, tech.name),
            "english": self.en.get(tech.language_dll_name, tech.name), "internal_name": tech.name,
            "age": age, "producer": location.location_id,
            "cost": price, "seconds": duration, "icon": self.icon(tech.icon_id, "technology"),
            "available": identifier in possible,
            "status": "disabled" if identifier in disabled else "available" if identifier in possible else "prerequisite",
            "requirements": [i for i in tech.required_techs if i >= 0],
            "required_count": tech.required_tech_count,
            "dependencies": sorted(set().union(*(
                visible_requirements(previous, self.data.techs, self.public)
                for previous in tech.required_techs if previous >= 0
            )) - {f"t{identifier}"}),
            "effect_id": tech.effect_id, "effects": effects,
            "unique": tech.civ == civ_id,
            "locations": [asdict(l) for l in tech.research_locations],
        }

    def unit_node(self, identifier, civ_id, civ, available):
        unit = civ.units[identifier]
        connection = self.unit_connections.get(identifier) or self.building_connections.get(identifier)
        kind = "building" if unit.type == 80 else "unit"
        locations = [l for l in unit.creatable.train_locations if l.unit_id >= 0 and l.button_id > 0]
        producer = locations[0].unit_id if locations else -1
        # For buildings, the overview is grouped into an explicit construction lane.
        if kind == "building":
            producer = -1
        requirements = sorted(t for t in self.enable_techs[identifier] | self.upgrade_techs[identifier]
                              if self.data.techs[t].civ in (-1, civ_id))
        age = 1 if unit.enabled else min((base_age(i, self.data.techs, civ_id=civ_id) for i in requirements), default=1)
        # location_in_age is the vertical display position. Age IDs live in
        # common's mode-0 slots in the original tree, and also in tech gates.
        if connection and not requirements:
            common = connection.common
            age = next((value for value, mode in zip(common.unit_research[:common.slots_used], common.mode)
                        if mode == 0 and value in (1, 2, 3, 4)), age)
        deps = set(f"u{i}" for i in self.previous_units[identifier])
        for tech in requirements:
            deps.update(visible_requirements(tech, self.data.techs, self.public))
        attack = asdict(unit.type_50) if unit.type_50 else {}
        return {
            "id": f"u{identifier}", "genie_id": identifier, "kind": kind,
            "name": self.label(unit.language_dll_name, unit.name),
            "english": self.en.get(unit.language_dll_name, unit.name), "internal_name": unit.name,
            "age": age, "producer": producer, "cost": costs(unit.creatable.resource_costs),
            "seconds": locations[0].train_time if locations else 0, "icon": self.icon(unit.icon_id, kind),
            "available": available, "status": "available" if available else "disabled",
            "requirements": requirements, "required_count": 1 if requirements else 0,
            "dependencies": sorted(deps), "hp": unit.hit_points,
            "speed": unit.speed, "sight": unit.line_of_sight,
            "combat": attack, "locations": [asdict(l) for l in locations],
            "graphics": {
                "standing": list(unit.standing_graphic), "dying": unit.dying_graphic,
                "walking": unit.dead_fish.walking_graphic if unit.dead_fish else -1,
                "attacking": unit.type_50.attack_graphic if unit.type_50 else -1,
            },
        }

    def availability(self, identifier, civ, tree_effects):
        disabled = set()
        forced = set()
        # Global automatic setup disables regional lines. Civ setup can
        # subsequently re-enable them with DE2 Modify Tech commands (type 8).
        automatic_setup = [effect for tech in self.data.techs
                           if tech.civ in (-1, identifier) and tech.required_tech_count == 0
                           and not any(location.location_id > 0 and location.button_id > 0
                                       for location in tech.research_locations)
                           for effect in self.effect(tech.effect_id)]
        for effect in automatic_setup + list(tree_effects):
            if effect.type == 102 and effect.d >= 0:
                disabled.add(int(effect.d))
            elif effect.type == 8 and effect.b == 12:
                if effect.d == 0:
                    disabled.add(effect.a)
                else:
                    disabled.discard(effect.a)
                    if effect.d in (2, 3):
                        forced.add(effect.a)
        active = {i for i, u in enumerate(civ.units) if u and u.enabled and not u.disabled}
        blocked_units = {e.a for e in tree_effects if e.type == 2 and e.b == 0}
        active.update(e.a for e in tree_effects if e.type == 2 and e.b == 1 and e.a >= 0)
        possible = set()
        while True:
            previous = (len(active), len(possible))
            # Constructing an available building completes its presence tech.
            # Several age gates require N of these shadow records.
            present = {civ.units[i].building.tech_id for i in active
                       if 0 <= i < len(civ.units) and civ.units[i] and civ.units[i].building
                       and civ.units[i].building.tech_id >= 0}
            possible = eligible_technologies(self.data.techs, identifier, disabled, present | forced)
            for unit_id, technologies in self.enable_techs.items():
                if technologies & possible:
                    active.add(unit_id)
            for target, technologies in self.upgrade_techs.items():
                # Match each upgrade's source to that same reachable effect.
                if any(e.type == 3 and e.b == target and e.a in active
                       for t in technologies & possible for e in self.effect(self.data.techs[t].effect_id)):
                    active.add(target)
            # Building foundations point at their completed/stacked entity.
            for i in list(active):
                unit = civ.units[i] if 0 <= i < len(civ.units) else None
                if unit and unit.building and unit.building.stack_unit_id >= 0:
                    active.add(unit.building.stack_unit_id)
            active.difference_update(blocked_units)
            if previous == (len(active), len(possible)):
                break
        return possible, active, disabled

    def civilization(self, identifier, civ):
        metadata = self.metadata[identifier] if identifier < len(self.metadata) else {}
        tree_effects = self.effect(civ.tech_tree_id)
        possible, active, disabled = self.availability(identifier, civ, tree_effects)
        nodes = [
            self.technology_node(i, identifier, tree_effects, possible, disabled)
            for i in sorted(self.public) if self.data.techs[i].civ in (-1, identifier)
        ]
        candidates = {
            i for i, u in enumerate(civ.units) if u and u.type in (70, 80) and u.creatable
            and any(l.unit_id >= 0 and l.button_id > 0 for l in u.creatable.train_locations)
        }
        # Other civilizations' unique units stay in the raw export. The preview
        # includes a civ's available units and unavailable shared tech-tree lines.
        for i in sorted(candidates):
            unique_owners = {self.data.techs[t].civ for t in self.enable_techs[i] | self.upgrade_techs[i] if self.data.techs[t].civ > 0}
            if i in active or (not unique_owners and (i in self.unit_connections or i in self.building_connections)):
                nodes.append(self.unit_node(i, identifier, civ, i in active))
        # A producer must be potentially available. Map alternate Town Centers
        # etc. by their original displayed name, preserving their IDs in data.
        available_buildings = {self.producer(i, civ) for i in active if i < len(civ.units) and civ.units[i] and civ.units[i].type == 80}
        for node in nodes:
            node["group"] = "建筑建造" if node["kind"] == "building" else self.producer(node["producer"], civ)
            if node["producer"] > 0 and node["group"] not in available_buildings:
                node["available"] = False
                node["status"] = "producer_unavailable"
        internal = metadata.get("internal_name", civ.name)
        slug = re.sub(r"[^a-z0-9-]+", "-", internal.lower()).strip("-")
        emblem = self.icon_root / "civ_emblems" / f"{slug}.png"
        emblem_path = None
        if emblem.is_file():
            (self.output / "emblems").mkdir(exist_ok=True)
            shutil.copyfile(emblem, self.output / "emblems" / emblem.name)
            emblem_path = f"emblems/{emblem.name}"
        return {
            "emblem": emblem_path,
            "id": identifier, "slug": slug, "name": self.label(metadata.get("name_string_id", -1), internal),
            "english": internal, "era": metadata.get("era", "unknown"),
            "raw_name": civ.name, "team_bonus_id": civ.team_bonus_id, "tech_tree_effect_id": civ.tech_tree_id,
            "description": self.zh.get(metadata.get("tech_tree_description_string_id"), ""),
            "disabled_technologies": sorted(disabled), "eligible_technologies": sorted(possible),
            "setup_effects": [asdict(e) for e in tree_effects],
            "team_bonus_effects": [asdict(e) for e in self.effect(civ.team_bonus_id)],
            "nodes": nodes,
        }

    def export(self, provenance):
        output = self.output
        archive = output / "raw"
        write_json(archive / "technologies.json.gz", self.data.techs)
        write_json(archive / "effects.json.gz", self.data.effects)
        write_json(archive / "tech_tree.json.gz", self.data.tech_tree)
        write_json(archive / "graphics.json.gz", self.data.graphics)
        write_json(archive / "unit_headers.json.gz", self.data.unit_headers)
        write_json(archive / "civilization_metadata.json.gz", self.metadata)
        separate = {"civs", "techs", "effects", "tech_tree", "graphics", "unit_headers"}
        write_json(archive / "globals.json.gz", {field.name: getattr(self.data, field.name)
                                                for field in fields(self.data) if field.name not in separate})
        civs = []
        manifest = []
        for identifier, civ in enumerate(self.data.civs):
            metadata = self.metadata[identifier] if identifier < len(self.metadata) else {}
            slug = re.sub(r"[^a-z0-9-]+", "-", metadata.get("internal_name", civ.name).lower()).strip("-")
            raw_path = Path("raw/civilizations") / f"{identifier:02d}-{slug}.json.gz"
            checksum = write_json(output / raw_path, civ)
            if identifier:
                preview = self.civilization(identifier, civ)
                normalized_path = Path("civilizations") / f"{identifier:02d}-{slug}.json"
                write_json(output / normalized_path, preview)
                script_path = normalized_path.with_suffix(".js")
                encoded = json.dumps(preview, ensure_ascii=False, separators=(",", ":"))
                (output / script_path).write_text("window.openageTreeLoaded(" + encoded.replace("<", "\\u003c") + ");", encoding="utf-8")
                civs.append({key: value for key, value in preview.items() if key not in ("nodes", "setup_effects", "team_bonus_effects", "eligible_technologies")})
                civs[-1].update({"data": str(normalized_path), "script": str(script_path),
                                 "node_count": len(preview["nodes"]),
                                 "available_count": sum(n["available"] for n in preview["nodes"])})
                manifest.append({
                    "id": identifier, "name": preview["name"], "english": preview["english"],
                    "raw": str(raw_path), "raw_sha256": checksum, "data": str(normalized_path),
                    "unit_records": sum(u is not None for u in civ.units),
                    "preview_nodes": len(preview["nodes"]),
                    "available_nodes": sum(n["available"] for n in preview["nodes"]),
                })
            print(f"Exported {identifier:02d} {metadata.get('internal_name', civ.name)}", flush=True)

        tech_records = [{
            "id": i, "name": self.label(t.language_dll_name, t.name), "internal_name": t.name,
            "required": [r for r in t.required_techs if r >= 0], "minimum": t.required_tech_count,
            "civ": t.civ, "effect_id": t.effect_id, "visible": i in self.public,
        } for i, t in enumerate(self.data.techs)]
        result = {
            "schema": "openage-genie-tech-tree-v1", "source": provenance,
            "counts": {"civilizations": len(civs), "civilization_records": len(self.data.civs),
                       "technologies": len(self.data.techs), "effects": len(self.data.effects),
                       "graphics": sum(graphic is not None for graphic in self.data.graphics),
                       "graphic_slots": len(self.data.graphics), "public_research_records": len(self.public)},
            "technologies": tech_records, "civilizations": civs,
        }
        write_json(output / "tech-trees.json", result)
        write_json(output / "manifest.json", {
            "schema": result["schema"], "source": provenance, "counts": result["counts"],
            "civilizations": manifest,
            "diagnostics": {key: sorted(value) for key, value in self.diagnostics.items()},
            "runtime_note": "Original DAT catalogs are exported here. Runtime nyan integration is a separate conversion step.",
        })
        return result
