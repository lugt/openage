# P3 — Gameplay Lifecycle Handoff

**Phase:** P3 complete (P3.2 + P3.3 + P3.4)  
**Branch:** `mvp/william-wallace`  
**Date:** 2026-07-29  
**Companion spec:** `docs/mvp/specs/WW_MISSION1_SCENARIO_RUNTIME_SPEC.md`  
**Companion plan:** `docs/mvp/specs/WW_MISSION1_DEVELOPMENT_PLAN.md`

P3 refactors the William Wallace Mission 1 combat lifecycle into the
event-driven chain

```
AttackCommand → game.attack → game.apply_discrete_effect → Health Curve
              → game.death → game.removal
```

with strict separation: each step is owned by exactly one handler, and
the GameState entity container is mutated only inside
`RemovalEventHandler::invoke`. No combat handler mutates the container
mid-iteration.

---

## 1. 修改前后完整 lifecycle 图

### Before (P3.1 baseline)

```
[User/AI send_command]
  → SendCommandHandler (game.send_command)
      → enqueue AttackCommand
      → create_event("game.attack")
  → AttackHandler (game.attack)
      → pop AttackCommand
      → range check; not in range → Move::move_default + follow-up game.attack
      → in range + cooldown elapsed:
          → target_live->set_attribute(time, health_fqon, current - damage)   [DIRECT HP WRITE]
          → if new_value <= 0: gstate->remove_game_entity(target_id)           [INLINE DEATH/REMOVAL]
          → create_event("game.attack", t+cooldown)
```

### After (P3.4)

```
[User/AI send_command]
  → SendCommandHandler (game.send_command)
      → enqueue AttackCommand
      → create_event("game.attack")
  → AttackHandler (game.attack)                                  [CONTAINER-CLEAN]
      → pop AttackCommand
      → range check; not in range → Move::move_default + follow-up game.attack
      → in range + cooldown elapsed:
          → create_event("game.apply_discrete_effect", t, {target_id, amount, attribute_fqon})
          → create_event("game.attack", t+cooldown)
  → ApplyDiscreteEffectHandler (game.apply_discrete_effect)      [CONTAINER-CLEAN]
      → stale-event safety: target gone? → no-op
      → Live::set_attribute(time, attribute_fqon, current - amount)
      → if new_value <= 0:
          → create_event("game.death", t, {target_id})
  → DeathEventHandler (game.death)                               [CONTAINER-CLEAN]
      → idempotency gate: entity->mark_dead() — one-shot
      → stop_attackers(target_id) — iterates Attack components
      → create_event("game.removal", t, {target_id})
  → RemovalEventHandler (game.removal)                           [SOLE OWNER of remove_game_entity]
      → has_game_entity idempotency
      → gstate->remove_game_entity(target_id)
```

---

## 2. 每个 demo 的结果

| Demo | 描述 | 结果 | 关键输出 |
|---|---|---|---|
| 7 | WW Mission 1 headless regression | PASS | last_dispatched_t=21.28976, 6 event_batches |
| 8 | EventLoop fractional / batch | PASS | peek hits 1.5 exactly, 3 same-time events batched |
| 9 | Mission Pack native integration | PASS | 3 entities spawned, 2 players, 4 chunks |
| 10 | Mission 1 lifecycle characterization | PASS | PlayerVillager=0, PlayerTownCenter=1, EnemyVillager=2 |
| 11 | Damage routed through effect handler (P3.2) | PASS | 5 HP changes, values {19,13,7,1,-5}, stale_event_safe=1 |
| 12 | Generic death event (P3.3) | PASS | mark_dead idempotent, attacker stopped, stale safe |
| 13 | Deferred dead-entity removal (P3.4) | PASS | duplicate removal safe, stale attack+effect safe |

---

## 3. Sanitizer 结果

- 构建:`ninja -j1 -C .bin/g++-debug-Oauto-sanitize-none`(memory-safe parallelism)
- exit 0,无新增 warning
- Targeted demos 11/12/13 全部 PASS
- Full ASan/UBSan re-run **deferred to P6**(per plan §P6 要求 ×20 Debug / ×50 normal / ×5 ASan / ×5 UBSan)

---

## 4. Stale event 处理语义(P3.4 final)

| Handler | Stale-event 检查 | 行为 |
|---|---|---|
| `ApplyDiscreteEffectHandler` | `has_game_entity(target_id)` | log "stale event, dropping (no-op)" + return |
| `DeathEventHandler` | `has_game_entity(target_id)` | log "stale event, dropping (no-op)" + return |
| `RemovalEventHandler` | `has_game_entity(target_id)` | silent no-op(避免日志噪音) |
| `AttackHandler` | `has_game_entity(target_id)` | log "target gone, stopping attack" + `Attack::stop_attack()` |

**`target_id` 生命周期保证:** 由 `EntityFactory::next_entity_id()` 分配 32-bit 整数,session 内不复用。`has_game_entity(target_id)` 是 "id 是否仍 alive" 的权威检查。

**Idempotency 测试覆盖:**
- ✓ 同一时间戳多个 effect → 1 death(P3.3 demo 12)
- ✓ Duplicate removal event → silent no-op(P3.4 demo 13)
- ✓ 已死实体收到新 damage → log "stale event"(P3.3 demo 12)
- ✓ 已移除实体收到新 attack → log "target gone"(P3.4 demo 13)
- ✓ 已移除实体收到新 effect → log "stale event"(P3.4 demo 13)
- ✓ 已移除实体收到新 removal → silent no-op(P3.4 demo 13)
- ✓ 已移除实体收到新 death → log "stale event"(P3.3 demo 12)

---

## 5. Remaining P0-F-4 ability warnings

P0 审计记录 14 个 `unhandled ability parent` warning(P3 未新增/未消除):

| Ability | 当前状态 |
|---|---|
| `Visibility` | data-only,留待 P5+ |
| `TerrainRequirement` | data-only,留待 P5+ |
| `Collision` | data-only,无 collision grid |
| `RemoveStorage` | data-only |
| `LineOfSight` | data-only |
| `Named` | data-only |
| `Stop` | data-only |
| `Herd` | data-only |
| `Restock` | data-only |
| `ResourceStorage` | 已挂载 `component::ResourceStorage` 但 `name` 是 `engine.ability.type.ResourceStorage`(其他分支可能匹配的是更具体的子能力)— 重新审计 |
| `ApplyContinuousEffect` | data-only,留待 P5+ |
| `ExitContainer` | data-only |
| `ActiveTransformTo` | data-only |
| `EnterContainer` | data-only |
| `UseContingent` | data-only |

P3 未实现任何新 ability 挂载。这些警告是 informational,**不阻塞 combat**。

---

## 6. Commits(P3 全链)

| Hash | 描述 |
|---|---|
| `666c0e21` | `gameplay: route attack damage through discrete effects` (P3.2) |
| `<p33>` | `gameplay: derive generic death events from health state` (P3.3) |
| `<p34>` | `gameplay: defer dead entity removal through event loop` (P3.4) |

报告 + 临时文件:
| 文件 | 描述 |
|---|---|
| `docs/mvp/reports/p3.2-damage-effect-integration.md` | P3.2 详细报告 |
| `docs/mvp/reports/p3.3-generic-death-integration.md` | P3.3 详细报告 |
| `docs/mvp/reports/p3.4-deferred-removal-integration.md` | P3.4 详细报告 |
| `docs/mvp/reports/p3-handoff.md` | 本文件 |

---

## 7. P3 出口声明(仅证明通用 lifecycle,不可声明 mission 完成)

```text
P3_GAMEPLAY_LIFECYCLE_COMPLETE
```

这仅证明以下链路是健壮的:

```text
AttackCommand
→ game.attack
→ game.apply_discrete_effect
→ Health Curve
→ game.death (idempotent)
→ game.removal (deferred)
```

**未声明:**
- ❌ `MISSION_1_SCENARIO_IMPORTABLE`(P1 仍 Path D BLOCKED)
- ❌ `MISSION_1_HEADLESS_SEMANTIC`(demo 7 PASS,但含 `objective_poll_count=6` H0.5 TEMPORARY DEBT)
- ❌ `MISSION_1_WINDOWED_PLAYABLE`(`ENVIRONMENT_BLOCKED`)

---

## 8. P4.1 最小可执行入口

### 任务
**P4.1 — 引入 `MissionCompletedEvent` lifecycle hook**,由 `WilliamWallaceMission01ObjectiveController` 订阅,**移除 H0.5 TEMPORARY DEBT**(`tick_objective_controller` polling API + `on_update` per-batch 轮询 + `objective_poll_count` instrumentation)。

### 目标

```text
game.removal (target=EnemyVillager)
  → objective_controller handler (subscribed to game.removal)
      → if (target_id == EnemyVillager instance FQON):
          → mark MissionCompleted
          → dispatch game.mission_completed
      → else: ignore
```

### 入口点(P3 已提供)

1. **`game.removal` event**(P3.4) — objective controller 订阅此事件,代替 `tick_objective_controller` 轮询。
2. **`GameState::get_named_entities()`**(P2.7) — 控制器已用此查询 EnemyVillager runtime id;P4.1 不需改动。
3. **`Live::on_attribute_change` / `on_threshold_drop`**(P3.3) — 可用于 HP HUD 更新(P4.1 范围外)。

### 移除项(H0.5 TEMPORARY DEBT)

- `GameSimulation::tick_objective_controller(time)` method
- `WilliamWallaceMission01ObjectiveController::on_update(now)` polling
- `objective_poll_count` instrumentation in demo 7
- p3.1 report + `ww_mission1_lifecycle.cpp` log line 的 "TEMPORARY DEBT (H0.5)" 标注

### 保留 invariant

- demo 7 仍然 PASS(last_dispatched_t=21.28976,event_batches=6 — 现在应是 6,**objective_poll_count 不再需要**)
- demo 10 的 objective polling 标注应改写为 "MissionCompletedEvent lifecycle hook (P4.1)"

### 建议提交(3 个 atomic commit)

```
objective: subscribe to game.removal lifecycle hook
objective: remove H0.5 tick_objective_controller polling
docs: record P4.1 mission-completed lifecycle integration
```

### 不在 P4.1 范围

- ❌ Mission 1 原版 trigger(触发条件+效果)— P4.2+
- ❌ 复杂 AreaWatcher / Diplomacy / Project 体系 — P4.3+
- ❌ owner/faction 系统 — 不在 P4 范围
- ❌ media/terrain 真实资源 — P5

---

## 9. 验证清单(P3 完成)

- ✅ demo 7 / 8 / 9 / 10 / 11 / 12 / 13 全部 PASS
- ✅ Scenario pytest 103/103 + validate_nyan pytest 3/3
- ✅ Production smoke reaches Initialize OK
- ✅ ninja -j1 exit 0(memory-safe parallelism)
- ✅ 每个 demo 都在 RED → GREEN 流程中实现
- ✅ 每阶段 atomic commit + 独立报告
- ✅ AttackHandler / ApplyDiscreteEffectHandler / DeathEventHandler 均 container-clean
- ✅ RemovalEventHandler 是 `remove_game_entity` 唯一调用者
- ✅ GameEntity::mark_dead 提供 death 幂等
- ✅ Live::on_threshold_drop 提供通用阈值监听(供 P5+ scripted effects 使用)

---

## 10. 不应自动进入 P4

按 spec 严格顺序,P4.1 入口已记录,但**本 handoff 后停止**,等待用户授权再启动 P4。