"""Convert original Wallace scenes to the existing Mission Pack and nyan assets."""

from dataclasses import replace
import gzip
import json
from pathlib import Path

from openage.convert.value_object.conversion.aoc import internal_nyan_names as names
from openage.scenario.ir import EntityPlacement, PlayerDefinition, TriggerCondition, TriggerDefinition, TriggerEffect, VictoryDefinition
from original_map import convert_terrain


PREFIX = 'ww_mvp.campaign.william_wallace.original_entities'
RESOURCE_NAMES = ('Food', 'Wood', 'Stone', 'Gold')


def _sprite(original, graphic, destination):
    """Convert one original atlas at a time, then release all decoded frames."""
    from openage.convert.entity_object.export.formats.sprite_metadata import LayerMode, SpriteMetadata
    from openage.convert.entity_object.export.formats.texture_metadata import TextureMetadata
    from openage.convert.entity_object.export.texture import Texture
    from openage.convert.processor.export.texture_merge import merge_frames
    from openage.convert.value_object.read.media.sld import SLD
    from openage.util.export_cache import ExportCache
    from inspect import getfile, getmodule

    stem = f"graphic_{graphic['id']}"
    output = destination / f'{stem}.sprite'
    source = original / 'resources/_common/drs/graphics' / (graphic['file_name'] + '.sld')
    data = source.read_bytes()
    version = Path(__file__).read_bytes()
    for implementation in (getfile(SpriteMetadata), getfile(TextureMetadata),
                           getmodule(merge_frames).__file__, getmodule(SLD).__file__):
        version += Path(implementation).read_bytes()
    # Frame counts and angle metadata can change independently of the SLD.
    version += json.dumps(graphic, sort_keys=True).encode('utf-8')
    cache = ExportCache(data, [output, output.with_suffix('.png'), output.with_suffix('.texture')], version)
    if cache.valid():
        return f'./original_entity_graphics/{stem}.sprite'
    texture = Texture(SLD(data))
    frames = max(1, graphic['frame_count'])
    angles = max(1, graphic['angle_count'])
    expected = frames * angles
    if len(texture.frames) < expected:
        available = len(texture.frames)
        if available < 1:
            raise ValueError(f"Invalid source frame layout for graphic {graphic['id']}")
        if available < angles:
            angles = available
            frames = 1
        else:
            if available % angles == 1 and angles > 1:
                available -= 1
            frames = max(1, available // angles)
        expected = frames * angles
    del texture.frames[expected:]
    merge_frames(texture)
    metadata = texture.get_metadata()
    texture.image_data.get_pil_image().save(destination / f'{stem}.png')
    descriptor = TextureMetadata(str(destination), f'{stem}.texture')
    descriptor.set_imagefile(f'{stem}.png')
    descriptor.set_size(*metadata['size'])
    descriptor.set_pxformat()
    for frame in metadata['subtex_metadata']:
        descriptor.add_subtex(frame['x'], frame['y'], frame['w'], frame['h'], frame['cx'], frame['cy'])
    (destination / f'{stem}.texture').write_text(descriptor.dump())
    sprite = SpriteMetadata(str(destination), f'{stem}.sprite')
    sprite.add_texture(0, f'{stem}.texture')
    sprite.add_layer(0, LayerMode.LOOP if frames > 1 else LayerMode.OFF,
                     position=graphic['layer'], time_per_frame=graphic['frame_duration'] or .1)
    for direction in range(angles):
        angle = int((270 + direction * 360 / angles) % 360)
        sprite.add_angle(angle)
        for index in range(frames):
            sprite.add_frame(index, angle, 0, 0, direction * frames + index)
    output.write_text(sprite.dump())
    cache.commit()
    return f'./original_entity_graphics/{stem}.sprite'


def _definitions(scene, original, destination, catalog, de2_base_dir=None, media_dir=None):
    units = json.loads(gzip.decompress((catalog / 'raw/civilizations/00-gaia.json.gz').read_bytes()))['units']
    graphics = json.loads(gzip.decompress((catalog / 'raw/graphics.json.gz').read_bytes()))
    lookup = names.UNIT_LINE_LOOKUPS | names.BUILDING_LINE_LOOKUPS | names.AMBIENT_GROUP_LOOKUPS
    for identifier, (name, folder, variants, _) in names.VARIANT_GROUP_LOOKUPS.items():
        for variant in variants:
            lookup[variant] = (name, folder)
    lookup[83] = lookup[293] = ('Villager', 'villager')
    lookup[53] = ('Shorefish', 'shore_fish')
    required = {unit['unit_const'] for unit in scene['units']}
    required.update(effect['object_list_unit_id'] for trigger in scene['triggers'] for effect in trigger['effects']
                    if effect['effect_type'] == 11 and effect.get('object_list_unit_id', -1) >= 0)
    required.update((38, 74, 83, 293, 600))
    lines = ['!version 0.2.0',
             'import de2_base.data.game_entity.generic.cliff.cliff as cliff',
             'import de2_base.data.game_entity.generic.forest_tree.forest_tree as forest',
             'import engine.util.game_entity as entity', 'import engine.ability.type as ability',
             'import engine.ability.property.type as property', 'import engine.util.graphics as graphics',
             'import engine.util.patch as patch',
             'import de2_base.data.tech.generic.loom.loom as base_tech',
             'import de2_base.data.game_entity.generic.militia.militia as militia', '',
             '# Tracking remains in the source scenario but has no standalone DE2 export.',
             'Tracking(base_tech.Loom):', '    Tech.updates = o{Update}', '',
             '    Update(patch.Patch):', '        Patch.patch = InfantrySight', '',
             '        InfantrySight<militia.Militia.LineOfSight>(patch.NyanPatch):',
             '            LineOfSight.range += 2.0']
    media = media_dir or (destination / 'original_entity_graphics')
    media.mkdir(parents=True, exist_ok=True)
    de2_base = de2_base_dir or (destination.parents[2] / 'de2_base')
    result = {}
    for identifier in sorted(required):
        unit = units[identifier]
        graphic_id = unit['standing_graphic'][0]
        has_sld = graphic_id >= 0 and (
            original / 'resources/_common/drs/graphics' / (graphics[graphic_id]['file_name'] + '.sld')
        ).is_file()
        if identifier in lookup and (
            not has_sld
            or (de2_base / f'data/game_entity/generic/{lookup[identifier][1]}/graphics/idle_{lookup[identifier][1]}.sprite').is_file()
        ):
            name, folder = lookup[identifier]
            fqon = f'de2_base.data.game_entity.generic.{folder}.{folder}.{name}'
        else:
            fqon = f'{PREFIX}.Unit{identifier}'
            if not has_sld:
                lines.extend(('', f'Unit{identifier}(entity.GameEntity):',
                              '    GameEntity.types = {}', '    GameEntity.abilities = {}',
                              '    GameEntity.modifiers = {}', '    GameEntity.variants = {}'))
            else:
                tree = unit['class_'] == 15
                parent = 'forest.ForestTree' if tree else 'cliff.Cliff'
                sprite = _sprite(original, graphics[graphic_id], media)
                lines.extend(('', f'Unit{identifier}({parent}):',
                              '    GameEntity.abilities = {' + (', '.join('forest.ForestTree.' + name for name in ('Death', 'Collision', 'Live', 'Named', 'Resistance', 'TerrainRequirement', 'Visibility', 'Selectable', 'Pathable', 'Harvestable')) + ', Idle' if tree else 'Idle, cliff.Cliff.Visibility') + '}',
                              '    GameEntity.variants = {}', '',
                              '    Idle(ability.Idle):', '        Ability.properties = {property.Animated: Animated}', '',
                              '        Animated(property.Animated):', '            Animated.animations = {Animation}', '',
                              '            Animation(graphics.Animation):', f'                Animation.sprite = "{sprite}"'))
        object_type = 2 if unit['type'] == 80 else 3 if unit['class_'] == 4 else 4 if unit['type'] == 70 and unit['class_'] not in (9, 10, 11, 38) else 1
        result[str(identifier)] = {'fqon': fqon, 'class': unit['class_'], 'object_type': object_type,
                                   'unit_type': unit['type'], 'sight': unit['line_of_sight'],
                                   'capturable': unit['type'] == 80 or object_type in (3, 4),
                                   'radius_x': unit['collision_size_x'], 'radius_y': unit['collision_size_y']}
    (destination / 'original_entities.nyan').write_text('\n'.join(lines) + '\n')
    return result


def _hud_icons(original: Path, destination: Path, catalog: Path, definitions: dict[str, dict], hud_dir: Path | None = None) -> None:
    """Prepare original portraits and action subjects without a runtime DAT reader."""
    from scripts.tech_tree.catalog import IconExporter

    exporter = IconExporter(original, hud_dir or (destination / "hud"))
    actions = original / "widgetui/textures/ingame/actions"
    for command, identifier in (("stop", 3), ("cancel", 0), ("pack", 16), ("unpack", 17)):
        source = actions / f"{identifier:03d}_.png"
        target = exporter.output / f"command.{command}.png"
        if source.is_file():
            exporter.export_image(source, target)
        else:
            target.unlink(missing_ok=True)
    units = json.loads(gzip.decompress((catalog / "raw/civilizations/00-gaia.json.gz").read_bytes()))["units"]
    technologies = json.loads(gzip.decompress((catalog / "raw/technologies.json.gz").read_bytes()))
    subjects = {}
    for identifier, (name, folder) in (names.UNIT_LINE_LOOKUPS | names.BUILDING_LINE_LOOKUPS).items():
        subjects[f"de2_base.data.game_entity.generic.{folder}.{folder}.{name}"] = (
            units[identifier]["icon_id"],
            "building" if units[identifier]["type"] == 80 else "unit",
        )
    subjects["de2_base.data.game_entity.generic.villager.villager.Villager"] = (units[83]["icon_id"], "unit")
    for identifier, definition in definitions.items():
        unit = units[int(identifier)]
        subjects[definition["fqon"]] = (unit["icon_id"], "building" if unit["type"] == 80 else "unit")
    for identifier, (name, folder) in names.TECH_GROUP_LOOKUPS.items():
        subjects[f"de2_base.data.tech.generic.{folder}.{folder}.{name}"] = (
            technologies[identifier]["icon_id"],
            "technology",
        )
    for subject, (icon_id, kind) in sorted(subjects.items()):
        target = exporter.output / f"{subject}.png"
        if icon_id < 0:
            target.unlink(missing_ok=True)
            continue
        if icon := exporter.icon(icon_id, kind):
            image = (exporter.output / icon).read_bytes()
            if not target.is_file() or target.read_bytes() != image:
                target.write_bytes(image)
        else:
            target.unlink(missing_ok=True)


def _civilization_actions(catalog, civilization):
    metadata = json.loads(gzip.decompress((catalog / 'raw/civilization_metadata.json.gz').read_bytes()))
    record = next((item for item in metadata if item['data_name'] == civilization), None)
    if record is None:
        raise ValueError(f'Unknown source civilization: {civilization}')
    slug = record['internal_name'].lower().replace(' ', '-')
    matches = list((catalog / 'civilizations').glob(f'*-{slug}.json'))
    if len(matches) != 1:
        raise ValueError(f'Missing exported civilization availability: {civilization}')
    nodes = json.loads(matches[0].read_text())['nodes']
    units = ({83: ('Villager', 'villager'), 293: ('Villager', 'villager')}
             | names.UNIT_LINE_LOOKUPS | names.BUILDING_LINE_LOOKUPS)
    allowed = set()
    for node in nodes:
        if not node['available']:
            continue
        technology = node['kind'] == 'technology'
        definition = (names.TECH_GROUP_LOOKUPS if technology else units).get(node['genie_id'])
        if definition:
            name, folder = definition
            namespace = 'tech' if technology else 'game_entity'
            allowed.add(f'de2_base.data.{namespace}.generic.{folder}.{folder}.{name}')
    return sorted(allowed)


def original_scene(template, mission, original, destination, catalog, de2_base_dir=None, shared_media_dir=None):
    scene = json.loads(mission.read_text()) if isinstance(mission, Path) else mission
    media_dir = (shared_media_dir / 'original_entity_graphics') if shared_media_dir else None
    terrain_dir = (shared_media_dir / 'original_terrain_graphics') if shared_media_dir else None
    hud_dir = (shared_media_dir / 'hud') if shared_media_dir else None
    definitions = _definitions(scene, original, destination, catalog, de2_base_dir=de2_base_dir, media_dir=media_dir)
    _hud_icons(original, destination, catalog, definitions, hud_dir=hud_dir)
    # Reserve player 2 for Gaia, preserving other slots if a later scene uses them.
    remap = {0: 2, 1: 0, 2: 1, **{index: index for index in range(3, 9)}}
    active = {0, 1} | {unit['player'] for unit in scene['units']}
    active |= {index for index, player in enumerate(scene['players']) if player['active']}
    players = []
    source_players = []
    for index in sorted(active):
        player = scene['players'][index]
        owner = remap[index]
        values = tuple((f'de2_base.data.util.resource.types.{resource}', player[resource.lower()]) for resource in RESOURCE_NAMES)
        players.append(PlayerDefinition(owner, player['tribe_name'] or ('Gaia' if index == 0 else f'Player {index}'), values))
        disabled = []
        for identifier in (player.get('disabled_techs') or []):
            if identifier in names.TECH_GROUP_LOOKUPS:
                name, folder = names.TECH_GROUP_LOOKUPS[identifier]
                disabled.append(f'de2_base.data.tech.generic.{folder}.{folder}.{name}')
        for identifier in (player.get('disabled_units') or []) + (player.get('disabled_buildings') or []):
            entry = ({83: ('Villager', 'villager'), 293: ('Villager', 'villager')}
                     | names.UNIT_LINE_LOOKUPS | names.BUILDING_LINE_LOOKUPS).get(identifier)
            if entry:
                name, folder = entry
                disabled.append(f'de2_base.data.game_entity.generic.{folder}.{folder}.{name}')
        record = dict(player, runtime_id=owner, disabled_subjects=disabled)
        if index != 0:
            record['allowed_subjects'] = _civilization_actions(catalog, player['civilization'])
        source_players.append(record)
    placements = [EntityPlacement(f"object_{unit['reference_id']}", definitions[str(unit['unit_const'])]['fqon'],
                                 remap[unit['player']], (unit['x'], unit['y']), unit['rotation'],
                                 {'genie_id': unit['unit_const'], 'height': unit['z'], 'source_id': unit['reference_id']})
                  for unit in scene['units']]
    triggers = []
    for trigger in scene['triggers']:
        conditions = tuple(TriggerCondition('AoE2Condition', args=dict(condition, parent=trigger['trigger_id']))
                           for condition in trigger['conditions'])
        order = trigger['effect_order'] or list(range(len(trigger['effects'])))
        effects = tuple(TriggerEffect('AoE2Effect', args=dict(trigger['effects'][index], parent=trigger['trigger_id']))
                        for index in order)
        triggers.append(TriggerDefinition(f"original.{trigger['trigger_id']}", bool(trigger['enabled']),
                                          bool(trigger['looping']), 0, conditions, effects))
    terrain = {'width': scene['width'], 'height': scene['height'],
               'terrain_ids': [tile['terrain_id'] for tile in scene['terrain']],
               'elevation': [tile['elevation'] for tile in scene['terrain']]}
    diplomacy = []
    for a in sorted(active):
        for b in sorted(active):
            if a == b:
                continue
            row = scene['players'][a].get('diplomacy') or []
            raw_stance = row[b - 1] if 0 <= b - 1 < len(row) else 1
            stance = 'neutral' if 0 in (a, b) else {0: 'ally', 1: 'neutral', 3: 'enemy'}.get(raw_stance, 'neutral')
            diplomacy.append((remap[a], remap[b], stance))
    match = __import__('re').search(r'_(\d+)\.', scene['source_file'])
    number = int(match.group(1)) if match else 1
    metadata = {'mission': number, 'source_sha256': scene['source_sha256'], 'players': source_players,
                'ai_test_variant': scene.get('ai_test_variant', 'original'),
                'definitions': definitions,
                'technologies': {str(identifier): f'de2_base.data.tech.generic.{folder}.{folder}.{name}'
                                 for identifier, (name, folder) in names.TECH_GROUP_LOOKUPS.items()},
                'player_map': {str(key): value for key, value in remap.items()},
                'variables': scene.get('variables', []),
                'triggers': [{key: value for key, value in trigger.items() if key not in ('conditions', 'effects', 'new_effect', 'new_condition')} for trigger in scene['triggers']],
                'objects': {str(unit['reference_id']): unit['unit_const'] for unit in scene['units']}}
    metadata['technologies']['90'] = f'{PREFIX}.Tracking'
    raw_terrain = catalog / 'raw/globals.json.gz'
    terrain_definitions = json.loads(gzip.decompress(raw_terrain.read_bytes()))['terrain_block']['terrains']
    return replace(template, id=f'ww_mission{number}_original', source='original-aoe2scenario', source_file=scene['source_file'],
                   map=convert_terrain(terrain, original / 'resources/_common/terrain/textures/2x',
                                       destination, terrain_definitions, graphics_dir=terrain_dir),
                   players=players, placements=placements, areas=[], triggers=triggers,
                   victory=VictoryDefinition('original-triggers'), diplomacy=tuple(diplomacy), fog_of_war=True,
                   original_scenario=metadata)
