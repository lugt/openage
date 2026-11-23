"""Authored Marching and Fighting scenario for the Mission Pack installer."""

from dataclasses import replace

from openage.scenario.ir import (
    AreaDefinition, EntityPlacement, MapDefinition, TriggerCondition,
    TriggerDefinition, TriggerEffect,
)


def marching_and_fighting(template):
    """Build the map and starting positions from the same tile grid."""
    grass = "de2_base.data.terrain.grass.grass.Grass"
    road = "de2_base.data.terrain.road.road.Road"
    entities = {
        "S": ("ScottishScout", "militia.militia.Militia", 0),
        "A": ("ScottishSoldier", "militia.militia.Militia", 2),
        "B": ("ScottishReinforcement", "militia.militia.Militia", 2),
        "C": ("ScottishConstruction", "outpost.outpost.Outpost", 0),
        "O": ("EnglishOutpost", "outpost.outpost.Outpost", 1),
        "V": ("ScottishVillage", "town_center.town_center.TownCenter", 0),
        "E": ("EnglishSoldier", "militia.militia.Militia", 1),
        "F": ("EnglishReinforcement", "militia.militia.Militia", 1),
    }
    rows = (
        "................",
        "................",
        "................",
        "...S............",
        "...###..C.......",
        ".....#..........",
        "....A#..........",
        ".....B####......",
        ".......O.#......",
        ".........####...",
        "............#...",
        "..........V.#...",
        "............#E..",
        "..............F.",
        "................",
        "................",
    )
    placements = []
    for y, row in enumerate(rows):
        for x, tile in enumerate(row):
            if tile in entities:
                name, unit, player = entities[tile]
                placements.append(EntityPlacement(
                    name, f"de2_base.data.game_entity.generic.{unit}", player, (x, y),
                    overrides={"construction_seconds": 12} if tile == "C" else {},
                ))

    terrain = [road if tile == "#" else grass for row in rows for tile in row]
    road_checkpoint = TriggerCondition("EntityVisitedArea", "ScottishScout", "RoadCheckpoint")
    village_checkpoint = TriggerCondition("EntityVisitedArea", "ScottishScout", "VillageCheckpoint")
    outpost = TriggerCondition("EntityDestroyed", "EnglishOutpost")
    areas = [
        AreaDefinition("RoadCheckpoint", "rectangle", {
            "left": "4", "top": "4", "right": "5", "bottom": "4",
        }),
        AreaDefinition("VillageCheckpoint", "rectangle", {
            "left": "8", "top": "8", "right": "12", "bottom": "8",
        }),
    ]
    triggers = [
        TriggerDefinition("mission_01.road_checkpoint", True, False, 0,
                          (road_checkpoint,), (
                              TriggerEffect("ChangeOwnership", "ScottishSoldier", target_player_id=0),
                              TriggerEffect("ChangeOwnership", "ScottishReinforcement", target_player_id=0),
                              TriggerEffect("CompleteObjective", "mission_01.march"),
                          )),
        TriggerDefinition("mission_01.victory", True, False, 0, (
            road_checkpoint, outpost, village_checkpoint,
            TriggerCondition("EntityDestroyed", "EnglishSoldier"),
            TriggerCondition("EntityDestroyed", "EnglishReinforcement"),
        ), (
            TriggerEffect("CompleteObjective", "mission_01.defeat_enemy"),
            TriggerEffect("DeclareVictory", "enemy_removed"),
        )),
    ]
    return replace(
        template,
        diplomacy=((0, 2, "ally"), (2, 0, "ally"), (0, 1, "enemy"), (1, 0, "enemy")),
        fog_of_war=True,
        id="ww_mission1_marching_and_fighting",
        map=MapDefinition(len(rows[0]), len(rows), terrain, [0] * len(terrain)),
        placements=placements,
        players=[replace(player, name=("Scots", "English")[player.player_id])
                 for player in template.players] + [replace(template.players[0], player_id=2, name="Allies")],
        areas=areas,
        triggers=triggers,
        victory=replace(template.victory, required_removals=()),
    )
