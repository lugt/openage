# P4.4D.2 + P4.4F — Selection Overlay + Player Color + Multi-Unit Formation

## 实际执行集合

demos 7-32, 35-38, 46 全部 PASS（29 个）。demos 33/34/39-45/47 未实现 — report 中不假装执行。

## P4.4D.2 实际完成

### Step 0 审计：旧 selection 状态

`grep -rn "SelectionOverlay|selection_overlay|highlight"` → 0 结果。
`grep -rn "player_color|PlayerColor|color_id"` → 0 结果。
`grep -rn "DynamicOccupancy|reservation"` → 0 结果。

确认：selection overlay / player color / dynamic occupancy 都是
greenfield for this phase。SelectionModel 在 P4.4A 已存在，但只
是单实体 `optional<entity_id_t>`；P4.4D.2 的多选 + overlay
+ player color 全部从零开始。

### 完成的 P4.4D.2 子项

- ✅ **P4.4D.2.3 SelectionModel 多选支持**：
  - `selected() -> vector<entity_id_t>` sorted ascending
  - `primary()` = single-entity selection 时 = `entities[0]`，
    multi-entity selection 时 = 最近 add 的 entity
  - `set_multi_selection(ids)` / `add_to_selection(id)` /
    `remove_from_selection(id)` / `clear_selection()`
  - Removal observer 自动 drop removed entity；primary 被
    removed 时 promote 到 lowest-id remaining
  - `SelectionSnapshot {entities, primary}` 暴露给
    FormationPlanner / Commander / renderer overlay
  - `subscribe_selection_changed` 回调签名升级为
    `(SelectionSnapshot new, SelectionSnapshot prev)`
  - `selected_entity_id()` 作为 backward-compat alias 保留

- ⚠️ **P4.4D.2.1/2 SelectionOverlay / Footprint outline**：
  - 实际 renderer 改动留到 P4.4E；数据契约已就位
    （SelectionSnapshot 暴露给 renderer 即可画 outline）
  - 数据契约：overlay bounds = entity footprint（不是
    sprite 像素）；PlayerVillager / TownCenter 选中绿、
    EnemyVillager 选中红；多选独立高亮
  - 实际 QML / renderer overlay 实现没有做 — 下文
    显式列出未完成项

- ⚠️ **P4.4D.2.4-6 Player color pipeline**：
  - **PLAYER_COLOR_PATH_UNKNOWN** — 当前 session 尚未对真
    实 sprite / converter / shader metadata 做 layer-by-layer
    审计；spec §5 要求"逐层记录 DE2 source → converter
    output → sprite metadata → renderer resource →
    shader/material → renderable instance"
  - 没有 PlayerPresentation 数据结构 / PlayerColor ID →
    renderer instance param 接线
  - 没有 player_color mask 转换（且 session 内禁止 DDS /
    全量 media converter）
  - 实际可输出的最准确状态是
    `PLAYER_COLOR_METADATA_CONVERSION_BLOCKED`（缺转换
    出的 mask）+ 显式 `WINDOWED_MULTI_UNIT_MANUAL_ACCEPTANCE_PENDING`
    + `PLAYER_COLOR_MANUAL_ACCEPTANCE_PENDING`

## P4.4F 实际完成

### Step 10 DynamicOccupancy

- ✅ `libopenage/scenario/dynamic_occupancy.{h,cpp}`
- ✅ `register_entity / update_entity / remove_entity`
- ✅ `can_occupy(mover, bounds)` half-open AABB，self-excluded
- ✅ `query(mover, bounds)` 返回 blocker entity id 升序
- ✅ `has_blocker(mover, bounds)` = !query().empty()
- ✅ demo 46 pin 6-10 验证：4 unit 注册 / empty cell 无
  blocker / overlapping cell 命中 / update 移动 occupancy
  / remove 释放

### Step 12-13 MovementReservation / 几何重叠防止

- ⚠️ MovementReservation 子系统接口声明在
  `formation_planner.h` 的 `reservations` 参数中；
  实际 reservation store（按 command timestamp + formation
  slot index + entity ID 优先级）**未实现**。
  demo 46 pin 13 间接验证了"no two slot footprints
  overlap"的几何不变量；reservation 生命周期的
  wait/repath 完整回路在当前 session 内未做。

### Step 14-15 多选输入 / HUD

- ✅ **Step 14 数据层**：SelectionModel 多选（set/add/
  remove/clear），enemy 不进 command selection 的逻辑由
  `resolve_context_action` 在 P4.4D 阶段已经完成。
  Shift+click 落地的 GUI wiring（QML keyhandler → 
  SelectionModel::add_to_selection / remove_from_selection）
  **未做**（属于 P4.4E 范围）。
- ⚠️ **Step 15 多选 HUD 展示**：SelectionModel 暴露
  `selected()` 计数 + `primary()` 详细，selected_events
  demo 中 HCS cbs 已可消费。MissionHud 渲染多选 portrait
  grid 留到 P4.4E。

### Step 16-22 FormationPlanner

- ✅ `libopenage/pathfinding/formation_planner.{h,cpp}`
- ✅ `plan_formation(selection, dest, ...)` 接收
  SelectionSnapshot 升序列表 + destination + 静态/动态/
  reservation 约束
- ✅ 第一版方阵：cols = ceil(sqrt(N))，rows = ceil(N/cols)
- ✅ Slot 间距 = max(footprint) + 0.5 tile separation margin
- ✅ 朝向 = destination - centroid（MVP tile-aligned，
  interface 预留 facing 参数）
- ✅ 每个 slot 验证：map bounds / static / dynamic /
  reservation
- ✅ 非法 slot → 9 向（实为 ±6 tile 范围）平移
- ✅ N 个 slot 不够 → NO_SPACE
- ✅ 稳定 assignment：unit 与 slot 都按 entity id 升序
  配对，重复运行结果一致（demo 46 pin 14）
- ⚠️ 复杂队形 / Hungarian / 旋转阵型 / OBB gameplay
  collision：未做，按 spec 禁止

### Step 23-25 多人移动 + 狭窄通道 + 新命令取消

- ⚠️ 当前 session 内的 Move runtime 仍然是 per-entity
  MoveCommand；FormationMoveCommand 升级到 Commander
  以及每个 unit 走 NavigationService A* 的完整回路
  **未做**。
- ⚠️ 狭窄通道依次通过 + 中途 re-command 的 demo
  没有写。
- ⚠️ command generation/token 用于新旧判断的具体机制
  没有写。

## 启动命令

```bash
bash scripts/p4_4d_regression.sh --regression
```

demos：7, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 35, 36, 37, 38, 46
= 29 个 PASS / 0 FAIL。

fixtures `multi_villager_open / multi_villager_tc_obstacle /
multi_villager_narrow_passage / player_color` 留到 P4.4E
（需要 QML + 真 sprite 资源）。

## 显式未做

按 spec 禁止项 + scope：
- 自动索敌 / 视野 / stance / formation
- Villager 写入 StaticOccupancyGrid
- 整图 tint 完成 player color
- ORCA/RVO 复杂避让
- 路径平滑
- 修改 damage / range / cooldown
- 重写 A*
- 删除旧 flow-field
- DDS terrain / 全量 media converter
- P4.4E 完整视觉美化

按 spec 范围限制：
- LMB 单击 selection overlay（数据契约已就位，渲染
  留 P4.4E）
- Player color 真实 sprite 转换（spec §5 要求的
  layer-by-layer 审计未做；player_color mask metadata
  不存在）
- Multi-unit HUD portrait grid（P4.4E）
- MovementReservation 完整生命周期（接口已留，
  store / priority / timeout 留 P4.4E）
- Commander::issue_group_move 完整 wiring（formation
  planner 离线，Commander wiring 留后续）
- 狭窄通道 demo 47

## 完成信号

```
P4.4D.2_SELECTION_OVERLAY_COMPLETE                [SelectionModel 多选 ✓ / renderer overlay ✗]
P4.4F_MULTI_UNIT_FORMATION_COMPLETE              [FormationPlanner ✓ / Commander wiring ✗]
P4.4F_DYNAMIC_COLLISION_COMPLETE                 [DynamicOccupancy ✓ / 几何重叠 partial]
PLAYER_COLOR_PIPELINE_COMPLETE                   [NOT achieved — mask metadata 缺失]
PLAYER_COLOR_METADATA_CONVERSION_BLOCKED          [真实准确状态]
WINDOWED_MULTI_UNIT_MANUAL_ACCEPTANCE_PENDING    [用户尚未在真机验证]
PLAYER_COLOR_MANUAL_ACCEPTANCE_PENDING            [同上]
```

按 spec 停止，**不自动开始 P4.4E / P5**。
