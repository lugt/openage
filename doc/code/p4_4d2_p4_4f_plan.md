# Plan: P4.4D.2 Selection Overlay + Player Color & P4.4F Multi-unit Formation + Dynamic Collision

Project: openage (Age of Empires II engine reimplementation). Branch: `mvp/william-wallace`.
Four concrete problems this plan solves:
1. Selected entities show a highlight outline (green friendly / red hostile / yellow neutral); unselected show none.
2. Villagers must not overlap or pass through each other or enemies.
3. Multi-select + right-click ground produces a formation with N distinct slot destinations.
4. The same sprite shows different player colors per owner.

## Global Constraints (bind every task)

**openage coordinate system (critical — past bugs):**
- 1 world unit = 1 tile = 65536 phys. `coord::tile{x,y}` are tile ints.
- `coord::tile{x,y}.to_phys3()` shifts correctly (raw = tile * 65536). NEVER construct `phys_t{static_cast<int64_t>(x*65536)}` or `phys_t{double}` — that double-shifts / overflows. Always go through `coord::tile{...}.to_phys3()` or `to_phys3_center()`.
- `to_phys3()` = cell CORNER origin; `to_phys3_center()` = cell CENTER. `move.cpp` and A* use `to_phys3_center()`. GroupMoveHandler currently uses `to_phys3()` (corner) — a bug to fix in Task 6.
- `FootprintBounds` are in TILE units (center_x/center_y are tile coords, e.g. 5.0). `coord::phys3.ne.to_double()` returns PHYS; to compare with footprint tile units divide by 65536.0 (or convert via coord).
- Half-open AABB `[min, max)`; a tile whose CENTER is inside the bounds is covered.

**Namespace shadowing:** files in `openage::gamestate::system` / `openage::gamestate::scenario` can shadow `openage::scenario`. Use absolute paths `::openage::scenario::swept_aabb(...)`, `::openage::scenario::DynamicOccupancy`, `::openage::scenario::trigger::TriggerRuntime` when calling from those namespaces.

**EventLoop/EventEntity:** `Commander` and `Spawner` are `EventEntity` but `create_event` wants `shared_ptr<EventEntity>`; cast via `std::static_pointer_cast<openage::event::EventEntity, ...>` if needed. `GameSimulation` is NOT `enable_shared_from_this` — pass a null sim shared_ptr to handlers that need it; use the event's `target` inside the handler.

**param_map:** has no `set()`. Build via `EventHandler::param_map::map_t raw{{"k", v}, ...}; param_map params{raw};`.

**Build/run:**
- Build: `ninja -C bin libopenage` (bin is a symlink to `.bin/clang++-debug-Oauto-sanitize-none`). If CMakeLists changed, run `cmake -S . -B bin` first (or the build regenerates automatically).
- Run a demo: `./bin/run test -d gamestate.tests.simulation_demo N`
- `WW_MISSION_PACK_DIR` default: `assets/converted/ww_mvp/campaign/william_wallace`.
- Existing passing demos: 7,9-20,22-32,35-38,42,46-49. Do not break them.

**Layering:** `input/` must not depend on `presenter/`. The Controller stays the selection master; SelectionModel is a presentation mirror wired via a `std::function` callback (already done in Task 1). Renderer stages must not depend on `gamestate/` — pass data via plain structs/callbacks from the presenter.

**Spec prohibitions (this session):** do NOT write Villager into StaticOccupancyGrid; do NOT give all units the same destination; do NOT use pointer addresses for movement priority; do NOT compute formation in GUI/QML; do NOT per-unit re-simulate a right-click; do NOT claim player-color done via full-image tint; do NOT implement ORCA/RVO / complex formation editors / auto-targeting / full Shift command queue; do NOT modify damage/range/cooldown; do NOT rewrite A*; do NOT delete flow-field; do NOT start DDS terrain or full media converter; do NOT start P4.4E polish.

**Reporting honesty:** do not declare a marker complete unless its acceptance is met. Use `PLAYER_COLOR_METADATA_CONVERSION_BLOCKED` if player-color mask is not fully wired to correct palette colors.

---

## Task 1: presenter — instantiate SelectionModel/SelectionOverlay + unify selection state

**Status: implemented pre-skill (edits done, builds, NOT yet committed/reviewed).**

Goal: make SelectionModel the single presentation-layer view of selection; Controller mirrors into it via callback.

Files: `libopenage/input/controller/game/controller.h`, `controller.cpp`, `libopenage/presenter/presenter.h`, `presenter.cpp`.

Done in this task:
- `Controller::set_selection_callback(std::function<void(const std::vector<entity_id_t>&)>)` added; `set_selected` fires it after updating `this->selected`.
- `Presenter` members `game_controller`, `selection_model`, `selection_overlay` added; `setup_selection_viewmodel()` creates model+overlay (idempotent), attaches callback, called from `init_input` (after controller creation) and `set_simulation` (late sim attach).

Acceptance: builds clean (`ninja -C bin libopenage`); drag-select still drives command dispatch via `Controller::get_selected()`; SelectionModel mirrors (subscribes via SelectionOverlay). No `input/` -> `presenter/` dependency.

---

## Task 2: renderer — world-space selection overlay (footprint outline) in HUD stage

Goal: draw a world-space outline quad per selected entity, colored by SelectionOverlayColor, following entity position each frame. NOT a QML/screen rectangle.

Files: `libopenage/renderer/stages/hud/render_stage.h`, `render_stage.cpp`, new `assets/shaders/hud_selection_overlay.vert.glsl`, `assets/shaders/hud_selection_overlay.frag.glsl`. Plus a presenter-side per-frame update hook (`presenter.cpp::render()` computes quads from SelectionOverlay + GameState positions/footprints and pushes to hud_renderer).

Design (layering-safe):
- Define in `renderer/stages/hud/` a plain struct `SelectionOverlayQuad { Eigen::Vector3f world_center; float half_w; float half_h; Eigen::Vector4f color; }` (tile/world units; z = terrain 0).
- `HudRenderStage::set_selection_overlays(std::vector<SelectionOverlayQuad>)` stores them; `update()` rebuilds renderables each frame (clear + add one quad renderable each). Use a dedicated shader `hud_selection_overlay` that takes `obj_world_position`-like uniform + half-extents + color, projects via the `camera` UBO (mirror `world2d.vert`'s camera block usage). Draw as a flat quad on the terrain plane (z=0), slightly above to avoid z-fight, with alpha ~0.35 fill + could be outline-only; MVP: filled translucent quad is acceptable as the "highlight" as long as it is world-space and follows the entity.
- The PRESENTER computes quads each frame: for each `SelectionOverlay::entries()`, look up entity via `simulation->get_game()->get_state()->get_game_entity(id)`, read Position curve at current clock time (convert phys3 -> tile/world units for center) and `entity->footprint().bounds()` (already tile units) for half-extents. Map color: FRIENDLY=(0,1,0,0.35), HOSTILE=(1,0,0,0.35), NEUTRAL=(1,1,0,0.35). Call `hud_renderer->set_selection_overlays(quads)` in `Presenter::render()` before `hud_renderer->update()`.
- Overlay does NOT participate in picking and does NOT mutate GameState.

Acceptance: builds; selected friendly villager shows a green world-space quad following it; clicking empty ground clears it (SelectionModel mirror fires); entity removal clears it. Verify via demo 40 (Task 7) and windowed smoke.

---

## Task 3: gamestate — register DynamicOccupancy at spawn + independent removal chain

Goal: every entity (villager AND enemy, static AND dynamic) is registered in `Map::get_dynamic_occupancy()` at spawn time, not only at Move entry. Removal clears it independent of SelectionModel (so headless works).

Files: `libopenage/gamestate/event/spawn_entity.cpp`, `libopenage/scenario/mission_pack_loader.cpp`, `libopenage/gamestate/game_state.{h,cpp}` or `simulation.cpp` (wherever the removal observer chain lives), `libopenage/gamestate/map.h` (accessor already exists).

Context (audit): `spawn_entity.cpp:152-215` and `mission_pack_loader.cpp:260-401` never call `register_entity`. `remove_entity` is only called by `selection_model.cpp` removal observer (presenter-gated) — headless leaves dead entities in occupancy.

Implementation:
- Add a helper `register_entity_in_dynamic_occupancy(GameState/Map&, entity, center_x, center_y)` that builds `FootprintBounds` from the entity's footprint (`fp.radius_x()/radius_y()`, center from position) and calls `map->get_dynamic_occupancy()->register_entity(entity->get_id(), bounds)`. If no footprint, use a small default (radius 0.4) so units still block.
- Call it at the end of `SpawnEntityHandler::invoke` (after `gstate->add_game_entity`) and in `mission_pack_loader.cpp` after `state->add_game_entity(entity)` (line ~372), using the placement center (ep.pos_x/pos_y) or the Position curve value.
- For removal: register a removal observer on the simulation (in `GameSimulation::init` / wherever removal observers are added) that calls `map->get_dynamic_occupancy()->remove_entity(id)` — INDEPENDENT of SelectionModel. Find the existing removal-observer registration site (search `add_removal_observer`). If none exists at the simulation level, add one. Keep the SelectionModel observer too (it does its own selection cleanup + its own remove_entity call; double remove is safe — make remove_entity idempotent).
- Ensure `DynamicOccupancy::remove_entity` is idempotent (no-op if not registered) — check `dynamic_occupancy.cpp:92`.

Acceptance: builds; demo 42 (existing) still passes; a new assertion that a stationary enemy is visible to `DynamicOccupancy::query` before any Move. Verify in Task 7 demos.

---

## Task 4: gameplay — Move runtime blocks on swept AABB + occupancy/curve sync + minimal reservation

Goal: `move.cpp` must not let a segment commit when a dynamic blocker overlaps the swept AABB (currently only logs). Occupancy update must track the position curve, not jump to the segment endpoint at commit.

Files: `libopenage/gamestate/system/move.cpp` (and possibly `movement_reservation.{h,cpp}` for minimal wiring).

Context (audit): `move.cpp:300-313` swept-AABB check only `log::log(MSG(warn)...)` then continues. `move.cpp:384` `update_entity(prev_b, cur_b)` jumps occupancy to `cur_b` (segment end) at commit, but the Position curve (`move.cpp:364` sets only the endpoint keyframe) interpolates across the segment — occupancy leads visible position.

Implementation (MVP, deterministic — no ORCA):
- When the swept-AABB precheck finds a blocker (excluding self), do NOT commit the segment. Options: (a) shorten the segment to just before the blocker (clamp to a safe fraction), or (b) wait one tick and retry. MVP: clamp the segment to a safe sub-target that does not overlap the blocker (compute the max fraction along prev->cur that stays collision-free, conservative via the blocker's footprint), commit that sub-segment, and leave the entity to re-evaluate next tick. If no safe fraction (>0), wait (commit zero-length, reschedule). Log at DBG. Do NOT silently pass through.
- Occupancy sync: instead of `update_entity(prev_b, cur_b)` jumping to `cur_b` at commit, update occupancy to the position the curve will actually render at during this segment. Simplest correct approach: keep occupancy at `prev_b` (the segment START) while the curve interpolates toward `cur_b`, and only advance occupancy to `cur_b` when the entity actually reaches the segment end (i.e., move the `update_entity` call to fire at the segment-end time, or update to `prev_b` at commit and let the NEXT segment's entry update to the prior `cur_b`). Concretely: at segment commit, set occupancy to `prev_b` (where the entity currently is); when the next segment begins (or on arrival), advance to `cur_b`. This keeps occupancy aligned with the visible interpolated position rather than ahead of it. Document the chosen timing in a comment.
- Minimal reservation: optional/deferred — if low-risk, have GroupMoveHandler (Task 6) open reservations per slot and Move close on arrival; otherwise leave MovementReservation as demo-only and note it. Do NOT block Task 4 on reservation.

Acceptance: builds; demo 49 (real multi-unit no-overlap) still passes; a mover approaching a stationary registered enemy (Task 3) stops at the enemy footprint edge instead of overlapping. Verify in Task 7.

---

## Task 5: path — GroupMoveHandler uses shared DynamicOccupancy + correct coords

Goal: GroupMoveHandler must query the Map's shared `get_dynamic_occupancy()` (not a stack-local throwaway) so formation slots avoid live entities, and use `to_phys3_center()` for slot targets (matching move.cpp).

Files: `libopenage/pathfinding/group_move_handler.cpp` (and `.h` if signature changes).

Context (audit): `group_move_handler.cpp:123` builds a local `openage::scenario::DynamicOccupancy occ;` only from movable selection members, then discards it — never touches `map->get_dynamic_occupancy()`. `group_move_handler.cpp:188-193` uses `coord::tile{...}.to_phys3()` (corner + truncates the double slot coord), inconsistent with move.cpp's `to_phys3_center()`.

Implementation:
- Replace the local `occ` with `map->get_dynamic_occupancy()` (the shared runtime instance that now contains ALL entities thanks to Task 3). Pass it to `plan_formation`. Do not register entities into it from the handler (Task 3 already did at spawn). The handler may still need a read-only view; ensure `plan_formation` only queries (does not mutate) — if it mutates, pass a const view or a snapshot copy.
- Fix slot target: convert `a.slot_center_x/y` (doubles in tile units) to phys3 via center, not corner. Since slot coords are fractional tiles, do: `coord::phys3{coord::phys_t::from_double(a.slot_center_x * 65536.0), ...}` OR better, check for a `coord::tile`-to-center helper that accepts fractional. Verify the exact safe construction against the Global Constraints (no double-shift). Likely: build `coord::phys3` directly from phys values computed as `slot_center_x * 65536.0` using `coord::phys_t::from_double(...)` if available, else `static_cast<int64_t>(round(slot_center_x * 65536.0))` passed to the phys_t raw constructor (which takes raw phys, NOT tiles). Confirm by reading `coord/phys.h` for the constructor semantics before writing.
- Keep dispatching one `game.send_command` MOVE per assignment.

Acceptance: builds; demo 46, 48 still pass; slot targets land at cell centers (no half-tile offset). Verify in Task 7.

---

## Task 6: input — wire RMB ground -> group_move (formation); RMB enemy -> attack (resolver)

Goal: the production RMB binding currently sends one MOVE to the same mouse target for ALL selected units (no formation, no attack). Replace it with: build `input::SelectionSnapshot` + `PickResult`, call `resolve_context_action`, then dispatch `game.group_move` (MOVE) or `game.send_command` ATTACK.

Files: `libopenage/input/controller/game/controller.cpp` (`setup_defaults` RMB binding).

Context (audit): the RMB `move_entity` binding (controller.cpp:193-218) sends `game.send_command` MOVE with `controller->get_selected()` and mouse_pos. `game.group_move` is never fired in production (only in demo). `resolve_context_action` exists but is never called. AttackCommand/AttackHandler exist but ATTACK is never sent from input.

Implementation:
- In the RMB binding, build `input::SelectionSnapshot` from `controller->get_selected()` + GameState: for each id, owner from Ownership curve, `has_move_ability` from MOVE component, `has_attack_ability` from ATTACK component.
- Build `PickResult`: `world_x/world_y` from `args.mouse.to_phys3(camera)` converted to tile units (phys / 65536.0); `has_entity` by iterating `gstate->get_game_entities()` and checking `entity->footprint().bounds().contains(world_x, world_y)` (first match); `entity_owner` from that entity's Ownership.
- `self_player = static_cast<player_id_t>(controller->get_controlled())`.
- Call `resolve_context_action(state, selection, pick, self_player)`.
- MOVE -> create `game.group_move` event with `entity_ids` (the movable subset), `center_x/center_y` = pick world coords, `generation` (a per-command counter; use a static or controller field incremented each RMB — stable, not wall-clock).
- ATTACK -> create `game.send_command` event with `type=ATTACK`, `target_id=pick.entity`, `entity_ids` (attack-capable subset).
- else (NO_OP/REJECTED/UNSUPPORTED) -> log at info, no event.
- The `game.group_move` event is handled by GroupMoveHandler (Task 5). Keep the Ctrl+LMB spawn and drag-select bindings unchanged.

Acceptance: builds; RMB ground with 2+ selected villagers issues a `game.group_move` (visible in logs: "GroupMoveHandler::invoke called") and they go to distinct slots; RMB enemy issues ATTACK (logs: "SendCommandHandler: ATTACK"); a single selected villager RMB ground still moves. Verify in Task 7 + windowed smoke.

---

## Task 7: test — demos 40/43/44/45 + regression script update

Goal: add the missing demos and update regression coverage.

Files: new `libopenage/gamestate/demo/selection_overlay_demo.cpp` (demo 40), `destination_reservation_demo.cpp` already exists as 47 — so 43 = `destination_reservation_demo` if not present (audit says 43 missing, 47 exists; create 43 as a distinct reservation-stress demo or reuse), `formation_generation_demo.cpp` (demo 44 — pure formation slot generation, 1/2/4/6/9 units, no movement), `stable_assignment_demo.cpp` (demo 45 — repeated-20x-stable assignment). Register each in `libopenage/gamestate/demo/tests.cpp` switch and `CMakeLists.txt`. Update `scripts/p4_4d_regression.sh` regression set to include 37,38,42,46-49 and the new 40,43,44,45.

Demo specs:
- 40: assert SelectionOverlay entries match selection (friendly green/hostile red/neutral); select -> overlay has entry; clear -> empty; removal -> entry gone; overlay does not affect picking (no API to assert picking; assert overlay non-participation by confirming SelectionOverlay has no GameState mutation API).
- 44: for N in {1,2,4,6,9}, `plan_formation` returns N slots, no slot overlap, slot count == unit count, spacing derived from footprint; near-map-edge translates; TC-near filters invalid slots; no-space returns FORMATION_NO_SPACE.
- 45: run `plan_formation` 20x on the same input (shuffled selection order each time) — assert identical assignments (stable).
- 43: reservation — two units cannot reserve the same area; stable priority (generation<slot<entity); low-priority waits; removal releases; 20x identical.

Acceptance: builds; `./bin/run test -d gamestate.tests.simulation_demo 40|43|44|45` each prints DEMO PASS; existing 7,9-20,22-32,35-38,42,46-49 still pass; `scripts/p4_4d_regression.sh --regression` (or `/tmp/check_demos.sh`) all green.

---

## Task 8: renderer — player color mask wiring + docs

Goal: wire the existing alpha=254 player-color mask (already embedded in converted PNGs) through to the shader so same-sprite entities show different player colors per owner. NOT full-image tint.

Files: `assets/shaders/world2d.frag.glsl`, `libopenage/renderer/stages/world/render_entity.h`, `object.h`, `object.cpp`, `render_stage.cpp`, `libopenage/gamestate/player.h` (add color_id), the gamestate->render entity update path (where `WorldRenderEntity::update` is called — search for it), and a renderer-side minimal player color palette.

Context (audit): mask present as alpha=254, green channel = player color shade index. Player palette NOT exported (converter `driver.py:180` commented out — do NOT modify converter). `Player` has no color field. `WorldRenderEntity::update()` takes no color. `world2d.frag.glsl:29-30` renders alpha=254 as pure red.

Implementation:
- Add a player color palette as a renderer-side constant (a small table mapping player_id -> base RGB, with shade variation by green-channel index). Standard AoE2: P1=blue, P2=red, P3=green, P4=yellow, P5=cyan, P6=pink, P7=grey, P8=orange. For shade index `s` (0..N), modulate brightness. This is a renderer constant, NOT converter work. If exact shade mapping is approximate, mark as `temporary diagnostic fallback` per spec §7 and output `PLAYER_COLOR_METADATA_CONVERSION_BLOCKED`.
- `world2d.frag.glsl`: when texture alpha == 254, replace the pixel color with the player color (look up via a `player_colors` uniform array indexed by a `player_id` uniform, brightness modulated by the green channel). Keep alpha=252/250 debug handling. Normal pixels (alpha=255) unchanged. Do NOT tint the whole image.
- `WorldRenderEntity::update`: add a `player_id` (or `player_color_id`) parameter; thread it from the gamestate Ownership curve at the point render entities are updated (search where `WorldRenderEntity::update(ref_id, position, angle, animation_path, time)` is called — likely in a render factory / game entity sync).
- `WorldObject`: add a `player_id` uniform; set it in `update_uniforms`. Register the uniform id in `render_stage.cpp::init_uniform_ids` and pass from `render_stage.cpp::update` (`obj->get_id()`-style).
- `Player`: add `player_color_id_t color_id` (derive from player_id for MVP: color_id = player_id).

Acceptance: builds; same villager sprite owned by P1 vs P2 renders different player-color regions (mask pixels recolored, non-mask pixels unchanged). If using an approximate palette, clearly mark diagnostic fallback. Demo 41 (player color) asserts the render param (player_id uniform set per owner) since headless can't assert pixels. Output `PLAYER_COLOR_PIPELINE_COMPLETE` only if mask is correctly recolored with a real palette; else `PLAYER_COLOR_METADATA_CONVERSION_BLOCKED`.

Docs: update `doc/code/p4_4d2_p4_4f_handoff.md` with final status markers:
`P4.4D.2_SELECTION_OVERLAY_COMPLETE`, `P4.4F_MULTI_UNIT_FORMATION_COMPLETE`, `P4.4F_DYNAMIC_COLLISION_COMPLETE`, and `PLAYER_COLOR_PIPELINE_COMPLETE` or `PLAYER_COLOR_METADATA_CONVERSION_BLOCKED`, plus `WINDOWED_MULTI_UNIT_MANUAL_ACCEPTANCE_PENDING` / `PLAYER_COLOR_MANUAL_ACCEPTANCE_PENDING`.
