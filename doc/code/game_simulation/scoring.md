# Player scores

`GameState::player_scores()` publishes a sorted, Qt-free snapshot alongside the player economy snapshot.
The presenter reads it through `HudQmlAdapter`; hovering a score shows the breakdown.
Gaia is excluded and equal scores are ordered by runtime player ID.

The [original Age of Empires II manual, printed page 18](https://manuals.plus/m/bede02f48ca7b2aae252168379555520636b871dcd6c6fdba8ba2766ea0186e2.pdf) defines the resource-value formula used here:

- Current food, wood, gold and stone contribute one point per ten resources.
- Surviving units and completed buildings contribute one point per five resources of production cost.
- Destroying or converting an enemy awards one point per five resources of the target's value.
- Completed paid research contributes one point per five resources spent.
- Exploration contributes up to 1,000 points in proportion to explored map tiles.

Integer arithmetic truncates each published component; no floating-point values enter scoring.
The economy component combines stockpile and live assets, and the technology component combines paid research and exploration.
Castle and Wonder asset values are included in the economy component rather than shown as a separate society column.
Entity values resolve converted production costs through the owner's current nyan view, including civilization and technology patches.
Objects without a production definition have zero asset value.
Completed construction enters the asset total; damaged living units retain their value and dead units leave it.
Scenario-granted technologies do not count as paid research.
Shared vision and reveal-map cheats affect the explored tiles used by the current visibility subsystem.
Tribute has no playable command yet and is not synthesized into score.

Damage events carry the attacker's player ID through death dispatch.
The death handler's idempotency gate prevents duplicate awards, and conversion credits the previous owner's value before changing ownership.
Military resource value and paid research expenditure live in `Player::score_ledger`; current resources, live assets and exploration are derived from world state.
The simulation refreshes published scores at its existing economy update cadence, without presenter access to mutable simulation counters.

`bin/run test testing::scoring` checks spending, losses, research, exploration and rounding.
With converted campaign assets, `bin/run test testing::campaign_scoring` verifies real production values, duplicate-death accounting and leaderboard ordering.

## Live game saves

The existing `OPENAGE_SAVE_V1` header accepts additive `SCORE|player|destroyed_resource_value|paid_research_value` records.
`EXPLORED|player|width|height|bits` preserves explored history with one `0` or `1` per tile in row-major order.
Current visibility is recomputed after loading.
The loader rejects duplicate, negative or unknown-player score records and invalid exploration dimensions or bits before changing the world.
Old saves without these records remain readable; missing cumulative statistics start at zero because their history cannot be recovered.
Loading replaces the previous game's cumulative statistics and exploration rather than retaining future information.
Default save files live under `cfg/saves` in the mounted configuration directory; resolving this writable path does not require the folder to exist.
The first actual save creates the directory.
This extension preserves scoring data but does not make the existing live codec a complete simulation/event checkpoint.
