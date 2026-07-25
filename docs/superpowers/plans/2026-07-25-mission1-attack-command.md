# Mission 1 — AttackCommand 垂直切片实现计划

日期: 2026-07-25
分支: mvp/tutorial-demo
目标: Villager vs Villager 的 AttackCommand 端到端垂直切片，打通
`dispatch -> target id -> range check -> reuse Move approach -> cooldown ->
damage -> HP reduce -> death -> entity remove -> MissionDirector victory`。

## 前置（已满足）

- 资产: `/tmp/convert_final/converted` 871 nyan 在位
- C++ 编译: `bin/openage/*.cpython-314*.so` 齐全，`python -m openage game --help` exit 0
- 基线: DBG 清理已提交 (`ce9009ad`)
- Villager 真实 nyan 具备 `Attack(ability.ApplyDiscreteEffect)` + `Live.attributes={Health}` + `Activity`

## 数据基础（Villager 真实 nyan，非虚假能力）

- cooldown: `ApplyDiscreteEffect.reload_time = 2.0`
- damage: `Melee(FlatAttributeChangeDecrease).change_value.AttributeAmount.amount = 3` (type=Health)
- HP: `Live.attributes = {Health}`，起始值由 entity_factory 从 nyan `AttributeSetting.starting_value` 加载
- range: Attack ability **无 range 字段**（ApplyDiscreteEffect 只有 reload_time/application_delay）。
  AoE2 近战本就近身 -> MVP 用简化 melee range 常量（1 tile），不算虚假能力。

## 已识别的 gap（需从零构建）

1. **实体移除机制不存在**: GameState/GameEntity/Manager 无 remove/destroy/despawn API（grep 全空）。
   需新建 `GameState::remove_game_entity(id)` + Live despawn 回调。
2. **AttackCommand 类不存在**: `command_t::ATTACK` 枚举已定义 (types.h:24) 但无对应 Command 类。
3. **system_id_t::ATTACK 不存在**: system/types.h 枚举无 ATTACK。
4. **Mission 1 trigger 当前是 gather_wood** (trigger.cpp:207)，与 M1=combat pivot 冲突，需改 defeat_enemy。

## 关键架构发现（决定实现方式）

- **Activity graph 在 `entity_factory.cpp:68 create_test_activity` 硬编码构建**（非 nyan）:
  `Start->Idle->Cond(moveable)->Cond(command_in_queue)->Move->Wait_for_move->loop`。
  command 入队 -> `command_in_queue` condition 真 -> 跳 Move node -> `Move::move_command` pop MoveCommand。
- **AttackCommand 接入**: 仿 Move 加 Attack TaskSystemNode + `attack_command_in_queue` condition
  在 `Cond(command)` 处按队首命令类型分流（AttackCommand -> Attack node，MoveCommand -> Move node）。
- **`command_in_queue` condition** (activity/condition/command_in_queue.cpp) 只判 `get_queue().empty(time)`，
  仿写 `attack_command_in_queue` peek 队首类型。
- **`Live` 组件** (component/api/live.h) 已为 combat 设计: `set_attribute` 减 HP，
  `on_attribute_change(fqon, cb)` 注释明写 "if new_value <= 0 despawn the entity"。
- **`Move::move_default(entity, state, destination, start_time)`** (system/move.cpp:102) 可直接复用接近目标。

## 分阶段实现（每阶段: 行为测试 + 编译 + 无头冒烟 + 原子提交）

### 阶段 1 — AttackCommand 类 + ATTACK 入队分派
- 新建 `libopenage/gamestate/component/internal/commands/attack.h/.cpp`:
  `AttackCommand(entity_id_t target)`, `get_target()`, `get_type()=command_t::ATTACK`。仿 MoveCommand。
- `libopenage/gamestate/component/internal/commands/CMakeLists.txt`: 加 attack.cpp。
- `libopenage/gamestate/event/send_command.cpp`: ATTACK case 从 params 取 `target_id`，
  `make_shared<AttackCommand>(target_id)` 入队（替换当前 default log-warn 分支，行 88）。
- 测试: AttackCommand 构造/get_type；send_command ATTACK 入队（仿 move_demo 测试模式）。
- 提交: `combat: add AttackCommand class + ATTACK dispatch in SendCommandHandler`

### 阶段 2 — system_id_t::ATTACK + AttackSystem 骨架 + activity graph 接入
- `libopenage/gamestate/system/types.h`: 加 `ATTACK` 枚举。
- 新建 `libopenage/gamestate/system/attack.h/.cpp`: `Attack::attack_command(entity, state, start_time)`
  骨架: pop AttackCommand, log, return 0。
- `libopenage/gamestate/system/CMakeLists.txt`: 加 attack.cpp。
- `libopenage/gamestate/system/activity.cpp` handle_subsystem: 加 `case ATTACK: return Attack::attack_command(...)`。
- 新建 `libopenage/gamestate/activity/condition/attack_command_in_queue.{h,cpp}`: peek 队首是否 AttackCommand。
- 新建 `libopenage/gamestate/activity/event/attack_command_in_queue.{h,cpp}`: primer（仿 command_in_queue event）。
- `libopenage/gamestate/api/definitions.h`: 注册新 condition/primer。
- `libopenage/gamestate/entity_factory.cpp` create_test_activity: 加 Attack node + wait_for_attack +
  在 condition_command 处加 attack 分流分支（attack_command_in_queue -> attack node）。
- 相应 CMakeLists 注册新源文件。
- 测试: issue AttackCommand -> AttackSystem 被调用（log 或 spy 验证）。
- 提交: `combat: wire AttackSystem into activity graph + ATTACK system_id`

### 阶段 3 — range check + reuse Move approach
- `system/attack.cpp`: 取 target = `state->get_game_entity(target_id)`；取双方 Position 组件 current_pos；
  计算距离；MVP melee range 常量比较。
  超出 range -> `Move::move_default(entity, state, target_pos, start_time)` 接近，返回 move time（下 tick 再评估）。
  在 range -> 继续 attack。
- 测试: 远距离 attack -> villager 朝目标移动；近距离 -> 不移动直接进入 attack。
- 提交: `combat: range check + reuse Move::move_default for approach`

### 阶段 4 — attack cooldown + damage + HP reduce
- `system/attack.cpp`: cooldown（记录 last_attack_time，< reload_time 则等待）；
  damage 应用: 取 target Live 组件，`set_attribute(time, Health_fqon, current_hp - damage)`。
- damage/reload_time 来源: 从 nyan Attack ability 读取（MVP 简化解析: 取 `ApplyDiscreteEffect.reload_time`
  + 第一个 FlatAttributeChange 的 `AttributeAmount.amount`）。若解析复杂，阶段 4 先用常量 3/2.0
  并标注 TODO，阶段 4b 接 nyan 解析。
- entity_factory: 挂 AttackComponent（或 AttackSystem 通过 entity 的 Attack ability 直接读 nyan）。
- 测试: 在 range 内 attack -> target HP 减少；cooldown 内重复 attack 不重复扣血。
- 提交: `combat: apply damage via Live::set_attribute with cooldown`

### 阶段 5 — death (HP<=0) + entity remove
- `libopenage/gamestate/game_state.{h,cpp}`: 加 `remove_game_entity(entity_id_t)`（从 game_entities map erase）。
- `libopenage/gamestate/entity_factory.cpp` Live 分支(行 250-268): 注册
  `live->on_attribute_change(Health_fqon, [state,entity](new_val){ if (new_val <= 0) state->remove_game_entity(id); })`。
- 可能需 GameEntityManager 配合取消 in-flight events（评估）。
- 测试: 持续 attack -> target HP 归零 -> entity 从 `get_game_entities()` 移除。
- 提交: `combat: despawn entity on HP<=0 via Live callback + GameState::remove_game_entity`

### 阶段 6 — MissionDirector victory (Mission 1 trigger 改 combat)
- `libopenage/gamestate/tutorial/trigger.cpp` create_mission_1_trigger: 把 gather_wood step
  改为 defeat_enemy step（敌人 owner!=0 Villager 死亡 -> 胜利）。
  predicate: 遍历 `get_game_entities()`，无 owner!=0 且 alive 的 Villager 即胜。
  （或 despawn 回调里 `trigger->mark_completed(1)`。）
- 测试: 杀敌 -> Mission 1 trigger `is_finished()==true`。
- 提交: `combat: Mission 1 trigger victory on enemy villager death`

### 阶段 7 — 端到端无头冒烟 + 教程冒烟
- 无头集成测试: spawn 2 villagers (player 0 + player 1 enemy)，issue AttackCommand，
  推进 SimLoop，verify target 死亡 + entity 移除 + trigger 胜利。
- 教程冒烟: `python -m openage game --tutorial --asset-dir /tmp/convert_final --modpacks engine --modpacks de2_base --no-prompts --headless` 不崩。
- 提交: `combat: end-to-end AttackCommand smoke test (villager vs villager)`

## 关键设计决策

1. **damage 数据源**: 从真实 nyan Attack ability 读取 reload_time=2.0 + Melee damage=3。
   MVP 简化解析（不实现完整 effect bundle / Resistance / 多 batch 系统），取首个 FlatAttributeChange amount。
2. **range**: Attack 无 range 字段 -> MVP melee range 常量（1 tile）。
3. **实体移除**: 新建 `GameState::remove_game_entity` + Live `on_attribute_change` despawn 回调。
4. **activity graph**: 加 Attack node + `attack_command_in_queue` condition 按队首类型分流。
5. **Mission 1 trigger**: gather_wood -> defeat_enemy（对齐 M1=combat pivot，不实现 Gather）。
6. **限制遵守**: Villager vs Villager；不实现 Gather/Build/Train/Research/AI/正式 GUI。
   command 通过测试 API（send_command event）issue，不做 GUI 右键 A 键。

## 风险

- activity graph 改动可能破坏现有 Move 流程 -> 阶段 2 后必须跑现有 move 冒烟回归。
- 实体移除可能遗漏 in-flight events（move keyframe / wait events）-> 阶段 5 评估 GameEntityManager 清理。
- nyan Attack ability 解析深度 -> 阶段 4 可先用常量解锁，4b 接真实数据。
