# Copyright 2026 the openage authors. See copying.md for legal info.
"""Extract the original scene's gameplay records using AoE2ScenarioParser."""

from enum import Enum


def _record(obj):
    if isinstance(obj, Enum):
        return obj.value
    if obj is None or isinstance(obj, (str, int, float, bool)):
        return obj
    if isinstance(obj, bytes):
        return obj.hex()
    if isinstance(obj, (list, tuple, set, frozenset)):
        return [_record(value) for value in obj]
    if isinstance(obj, dict):
        return {str(key): _record(value) for key, value in obj.items()}
    if hasattr(obj, '__dict__'):
        return {key: _record(value) for key, value in vars(obj).items()
                if not key.startswith('_') and key not in ('new_effect', 'new_condition')}
    return str(obj)


def read_scenario(path, strings=None):
    """Keep source IDs, order arrays, positions and original condition fields."""
    from AoE2ScenarioParser.scenarios.aoe2_de_scenario import AoE2DEScenario
    from AoE2ScenarioParser.scenarios.scenario_store import store

    scenario = AoE2DEScenario.from_file(str(path))
    strings = strings or {}
    try:
        players = []
        for player in scenario.player_manager.players:
            record = _record(player)
            record.update(player_id=int(player.player_id), civilization=_record(player.civilization),
                          active=bool(player.active),
                          camera_x=getattr(player, '_initial_camera_x', None),
                          camera_y=getattr(player, '_initial_camera_y', None))
            players.append(record)
        triggers = []
        for trigger in scenario.trigger_manager.triggers:
            record = _record(trigger)
            record.update(conditions=_record(trigger.conditions),
                          effects=[dict(_record(effect), effect_type=effect.effect_type,
                                        selected_object_ids=_record(effect.selected_object_ids), quantity=effect.quantity)
                                   for effect in trigger.effects],
                          condition_order=list(trigger.condition_order), effect_order=list(trigger.effect_order))
            record['description'] = strings.get(trigger.description_stid) or trigger.description
            for effect in record['effects']:
                if effect.get('string_id') in strings:
                    effect['message'] = strings[effect['string_id']]
            triggers.append(record)
        return {
            'version': scenario.scenario_version, 'width': scenario.map_manager.map_size,
            'height': scenario.map_manager.map_size,
            'terrain': _record(scenario.map_manager.terrain), 'players': players,
            'units': [dict(_record(unit), player=int(unit.player)) for unit in scenario.unit_manager.get_all_units()],
            'triggers': triggers, 'variables': _record(scenario.trigger_manager.variables),
            'options': _record(scenario.option_manager),
            'messages': _record(scenario.message_manager),
        }
    finally:
        store.remove_scenario(scenario.uuid)
