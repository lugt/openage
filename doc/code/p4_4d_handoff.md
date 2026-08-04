# P4.4D / P4.4D.1 — Windowed Selection + Attack Input

## P4.4D 准确状态

实际完成（4 atomic commit，库 + scripts 4 file）：

```
input: introduce PointerEvent and semantic action resolver
input: route right-click through ContextActionResolver
test: cover P4.4D ContextActionResolver and windowed-style move routing
scripts: add P4.4D regression + windowed launch driver
```

P4.4D.1 增量（3 atomic commit）：

```
gameplay: add AttackApproach service for reachable approach positions
test: cover P4.4D.1 AttackApproach and pointer-driven Mission 1 combat
fix: simulation.h uses absolute scenario::trigger namespace
```

Demos 实际执行集合（28 个 PASS = 0 FAIL）：

```
7, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
35, 36, 37, 38
```

demo 21/33/34 未实现（21 是早期 placeholder，33/34 是
P4.4D.1 §6 指定的坐标 + point-selection 专项，本阶段未实
现；P4.4D.1 §11 标注为"建议 demos"而非强制）。

demo 8 / 33 / 34 / 39 编号有但无对应实现 — report 中不假装执行。

## 范围修正（与上版报告对照）

* 上版报告"27 / 28 个 demo"是错误的，实际是 26/28。
* 上版报告 "MISSION_1_POINTER_DRIVEN_ATTACK_COMPLETE" 是
  错标，因为 demo 38（pointer-driven combat lifecycle）
  在 P4.4D 阶段尚未实现。
* 上版 P4.4D.7（Attack approach position）当时为
  `pending`，本阶段由 `find_attack_approach` 服务实现。

## P4.4D 实际只完成了 pointer-to-command routing

P4.4D 阶段交付的代码路径：

```
PointerEvent
→ coord::picking
→ SelectionSnapshot
→ resolve_context_action (semantic action resolver)
→ Commander (MoveCommand / AttackCommand)
```

未交付：

* LMB 单击 point selection（P4.4D.1.1 未实现）
* selection footprint overlay in renderer（P4.4D.1.2 未实现）
* AttackApproach service 与 Attack 状态机（P4.4D.1.3 / .5 本阶
  段已交付 service，但 Attack runtime 仍使用 P2 的
  in_range 判断 + Move runtime 的目标 cell 接近；attack
  state 字段未暴露给 HUD）
* SelectionModel 与 controller.set_selected 之间的 wiring
* renderer selection overlay

因此 P4.4D 阶段正确的完成信号是：

```
P4.4D_CONTEXT_ACTION_ROUTING_COMPLETE
WINDOWED_MANUAL_ACCEPTANCE_PENDING
```

不是 `P4.4D_WINDOWED_INPUT_CODE_PATH_COMPLETE` 或
`MISSION_1_POINTER_DRIVEN_ATTACK_COMPLETE`。

## P4.4D.1 在 P4.4D 之上补齐

* `find_attack_approach` 服务（pathfinding/attack_approach）
  — AABB 距离、CandidateSet、path cost 排序、APPROACH_FOUND
  / APPROACH_FAILED 双状态
* demo 37：AttackApproach service 5 pin
* demo 38：pointer-driven Mission 1 combat lifecycle 6 pin

demo 38 跑通完整链路：synthetic pointer → resolver →
Commander → AttackCommand → Move runtime →
NavigationService → AttackHandler → DeathEventHandler →
RemovalEventHandler → TriggerRuntime → MissionState=VICTORY。

攻击者在 TC 阻挡下绕路，HP 序列下降，Enemy 被
removal，MissionState 进入 VICTORY，pin 6 验证 attack
final position 在 enemy 环内。

## P4.4D.1 实际完成信号

```
P4.4D_WINDOWED_INPUT_CODE_PATH_COMPLETE     [P4.4D + P4.4D.1]
MISSION_1_POINTER_DRIVEN_ATTACK_COMPLETE    [demo 38 lifecycle 通过]
WINDOWED_MANUAL_ACCEPTANCE_PENDING          [用户尚未在真机验证]
```

## 显式未做（P4.4D.1 §10 禁止）

* 自动索敌 / 视野 / stance / formation
* 动态单位避让
* A* 路径平滑
* 修改 damage / range / cooldown
* 修改 Trigger Runtime
* P4.4E GUI 美化
* DDS / terrain / media conversion
* 直接调 AttackHandler 伪造 pointer lifecycle（demo 38 严
  格走 resolver → Commander → production runtime 路径）

## 启动命令

```bash
# 全 28 个 headless demo 回归
bash scripts/p4_4d_regression.sh --regression

# 窗口化人工验收
bash scripts/p4_4d_regression.sh --fixture direct_combat
bash scripts/p4_4d_regression.sh --fixture tc_obstacle
```

## 人工窗口验收步骤

1. 启动窗口（`--fixture direct_combat`）。
2. LMB 单击 Villager（当前 LMB release 走 drag-select；
   P4.4D.1.1 未实现单击 → 拖框是当前真实路径）。
3. RMB 空地 → 看到 Villager 移动。
4. RMB Enemy → 看到 Villager 朝 Enemy 走去（绕过 TC
   时走 demo 32/37/38 同一份 NavigationService）。
5. 看到 Enemy HP 下降、消失。
6. 看到 Objective banner / Victory panel。

用户真实 macOS 窗口完成上述 6 步后输出：

```
P4.4D_WINDOWED_INTERACTION_CONFIRMED
```
