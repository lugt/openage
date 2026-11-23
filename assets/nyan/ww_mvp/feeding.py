"""Playable terrain and starting positions for Feeding the Army."""

from dataclasses import replace

from openage.scenario.ir import EntityPlacement, MapDefinition, VictoryDefinition


def feeding_the_army(template):
    """Build the resource tutorial from original DE2 entity definitions."""
    terrain = "de2_base.data.terrain.grass.grass.Grass"
    entities = (
        ("GatheringTownCenter", "town_center.town_center.TownCenter", 0, (8, 8)),
        ("FoodGatherer", "villager.villager.Villager", 0, (4, 5)),
        ("WoodGatherer", "villager.villager.Villager", 0, (12, 6)),
        ("GoldGatherer", "villager.villager.Villager", 0, (10, 12)),
        ("ForageBush", "berry_bush.berry_bush.BerryBush", 1, (3, 5)),
        ("Oak", "oak_tree.oak_tree.OakTree", 1, (12, 4)),
        ("GoldDeposit", "gold_mine.gold_mine.GoldMine", 1, (10, 13)),
        ("FarGoldDeposit", "gold_mine.gold_mine.GoldMine", 1, (13, 10)),
        ("Sheep", "sheep.sheep.Sheep", 1, (3, 9)),
        ("Boar", "boar.boar.Boar", 1, (4, 12)),
    )
    return replace(
        template,
        diplomacy=((0, 1, "neutral"), (1, 0, "neutral")),
        fog_of_war=True,
        id="ww_mission2_feeding_the_army",
        map=MapDefinition(16, 16, [terrain] * 256, [0] * 256),
        placements=[EntityPlacement(name, f"de2_base.data.game_entity.generic.{unit}", owner, pos)
                    for name, unit, owner, pos in entities],
        players=[replace(player, name=("Scots", "Gaia")[player.player_id])
                 for player in template.players],
        victory=VictoryDefinition(kind="resource-stockpile"),
    )
